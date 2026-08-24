/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for non-secure
*                    application in the CM33 CPU
*
* Related Document : See README.md
*
********************************************************************************
* (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/

#include "cybsp.h"
#include "retarget_io_init.h"
#include "cycfg_peripherals.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cy_time.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cloud_task.h"


/*******************************************************************************
* Macros
*******************************************************************************/
/* The timeout value in microseconds used to wait for CM55 core to be booted */
#define CM55_BOOT_WAIT_TIME_USEC    (10U)

/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR          (CYMEM_CM33_0_m55_nvm_START + \
                                        CYBSP_MCUBOOT_HEADER_SIZE)

/*******************************************************************************
* Function Name: handle_app_error
********************************************************************************
* Summary:
* User defined error handling function
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
/* handle_app_error() ย้ายไปนิยามใน retarget_io_init.h แล้ว (เอามาจาก code example ของ Infineon)
 * ถ้าเก็บไว้ทั้งสองที่จะ redefinition error */

/*******************************************************************************
* การตั้งค่าที่ WiFi ต้องใช้ (ถอดมาจาก mtb-example-psoc-edge-wifi-secure-tcp-client)
*
* ⚠️ ตกส่วนนี้ไปครั้งแรก ผลคือ cy_wcm_init() คืน 0x082E000B
*    = CY_RSLT_WCM_BSP_INIT_ERROR เพราะ WHD ต้องมีนาฬิกาสำหรับ tickless idle
*    และ RTC สำหรับ clib ก่อนถึงจะปลุกชิป WiFi ได้
*******************************************************************************/

/* ต้องรอ 2 CLK_LF cycle หลังเปิด/ปิด MCWDT ค่านี้ขึ้นกับความถี่ CLK_LF ที่ BSP ตั้ง */
#define LPTIMER_0_WAIT_TIME_USEC           (62U)

/* 1 = สำคัญสูงสุด */
#define APP_LPTIMER_INTERRUPT_PRIORITY     (1U)

static mtb_hal_lptimer_t lptimer_obj;
static mtb_hal_rtc_t     rtc_obj;

/* RTC — clib support library ใช้สำหรับฟังก์ชันเวลา */
static void setup_clib_support(void)
{
    Cy_RTC_Init(&CYBSP_RTC_config);
    Cy_RTC_SetDateAndTime(&CYBSP_RTC_config);

    /* ⚠️ ตั้งวันที่ให้ใกล้เคียงปัจจุบัน — จำเป็นสำหรับ TLS
     *
     * mbedtls คอมไพล์มาพร้อม MBEDTLS_HAVE_TIME_DATE ⇒ มันจะตรวจว่า certificate
     * ของเซิร์ฟเวอร์ยังไม่หมดอายุและเริ่มใช้แล้ว โดยเทียบกับนาฬิกาของบอร์ด
     * ถ้านาฬิกาเป็นค่า default จากโรงงาน (เช่นปี 2000) certificate ของ
     * thingsboard.cloud จะถูกมองว่า "ยังไม่เริ่มใช้" แล้ว handshake ล้มทันที
     *
     * ยังไม่ได้ทำ SNTP — ตั้งค่าคงที่ไปก่อน ไม่ต้องแม่นระดับวินาที
     * แค่ต้องอยู่ในช่วงอายุ certificate (ปัจจุบันหมดอายุ 28 ต.ค. 2026)
     * ปี = 2 หลักสุดท้าย ตาม API ของ PDL */
    (void)Cy_RTC_SetDateAndTimeDirect(0u, 0u, 12u, 19u, 8u, 26u);

    mtb_clib_support_init(&rtc_obj);
}

static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

/* LPTimer — RTOS abstraction ใช้ทำ tickless idle ซึ่ง WHD พึ่งพาอยู่ */
static void setup_tickless_idle_timer(void)
{
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc      = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&lptimer_intr_cfg, lptimer_interrupt_handler))
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    if (CY_MCWDT_SUCCESS != Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW,
                                          &CYBSP_CM33_LPTIMER_0_config))
    {
        handle_app_error();
    }

    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW, CY_MCWDT_CTR_Msk,
                    LPTIMER_0_WAIT_TIME_USEC);

    if (CY_RSLT_SUCCESS != mtb_hal_lptimer_setup(&lptimer_obj,
                                                 &CYBSP_CM33_LPTIMER_0_hal_config))
    {
        handle_app_error();
    }

    cyabs_rtos_set_lptimer(&lptimer_obj);
}
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function of the CM33 non-secure application. 
* 
* It initializes the device and board peripherals. It also initializes the 
* retarget-io middleware to be used with the debug UART port using which 
* "Hello World!" is printed on the debug UART. The LED pin is initialized with
* default configurations. The CM55 core is enabled and then the programs enters
* an infinite while loop which toggles the LED with a frequency of 1 Hz.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board initialization failed. Stop program execution */
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* นาฬิกาที่ WiFi ต้องใช้ ต้องตั้งก่อน __enable_irq และก่อนสร้าง task */
    setup_tickless_idle_timer();
    setup_clib_support();

    /* Enable global interrupts */
    __enable_irq();

    /* Enable CM55. */
    /* CY_CM55_APP_BOOT_ADDR must be updated if CM55 memory layout is changed.*/
    /* เปิด debug UART ก่อน จะได้เห็น log ของ WiFi/MQTT
     * หมายเหตุ: CM55 เปิด UART ตัวเดียวกันนี้อีกรอบ ⇒ log สองคอร์จะสลับบรรทัดกันบ้าง */
    init_retarget_io();
    printf("\n=== BabyMate 2.0 : CM33-NS (WiFi + MQTT) ===\n");

    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    /* WiFi + MQTT ทำงานบนคอร์นี้ ไม่ใช่ CM55 เพราะชิป WiFi ต่อผ่าน SDIO
     * และ connectivity stack ของ Infineon ออกแบบมาให้รันบน CM33-NS */
    create_cloud_task();

    vTaskStartScheduler();

    /* ไม่ควรมาถึงบรรทัดนี้ ถ้ามาถึง = FreeRTOS heap ไม่พอ */

    for(;;)
    {
//        Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
    }
}

/* [] END OF FILE */
