#include "cybsp.h"
#include <stdio.h>
#include "retarget_io_init.h"
#include "FreeRTOS.h"
#include "task.h"

// นำเข้าไฟล์เซนเซอร์
#include "env_sensor.h"
#include "audio.h"
#include "env_sensor.h"
#include "radar_task.h"
#include "tft_screen.h"        // จอสัมผัส Waveshare 4.3" DSI (J10)
#include "babymate_shared.h"   // บล็อกข้อมูลร่วมกับ CM33 (WiFi/MQTT)

/* ===================================================================
 * FreeRTOS failure hooks
 *
 * FreeRTOSConfig.h เปิด configCHECK_FOR_STACK_OVERFLOW=2 กับ
 * configUSE_MALLOC_FAILED_HOOK=1 ไว้ แต่โปรเจกต์ไม่เคยนิยาม hook เอง
 * เลยไปใช้ตัว weak default ของ MTB ซึ่งเป็นแค่ while(1) เงียบ ๆ
 * => stack ล้น หรือ malloc ไม่พอ = บอร์ดค้างสนิท ไม่มี log ออกมาเลย
 *
 * สองตัวข้างล่างทำให้มันบอกก่อนตายว่าใครเป็นคนพัง
 * =================================================================== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("\n\n*** STACK OVERFLOW in task \"%s\" ***\n", pcTaskName);
    for (;;) { __asm volatile ("nop"); }
}

void vApplicationMallocFailedHook(void);
void vApplicationMallocFailedHook(void)
{
    printf("\n\n*** MALLOC FAILED (heap หมด) ***\n");
    for (;;) { __asm volatile ("nop"); }
}

/* PDL เรียกตัวนี้จาก fault handler ทุกชนิด ตัว weak เดิมเป็นแค่ while(1) เงียบ ๆ
 * ทับด้วยของจริงเพื่อให้บอกสาเหตุออกมาทาง UART ก่อนจอด
 *
 * อ่านค่าออกมาแล้วแปลแบบนี้:
 *   CFSR bit 1 (0x02)      = PRECISERR  ไปแตะที่อยู่ที่ไม่มีจริง (พอยน์เตอร์ขยะ)
 *   CFSR bit 7 (0x80)      = BFARVALID  ⇒ BFAR คือที่อยู่ที่ไปแตะจริง ๆ
 *   CFSR bit 16 (0x10000)  = UNDEFINSTR
 *   CFSR bit 24 (0x1000000)= UNALIGNED
 *   HFSR 0x40000000        = FORCED     ยกระดับมาจาก fault ตัวอื่น
 */
void Cy_SysLib_ProcessingFault(void);
void Cy_SysLib_ProcessingFault(void)
{
    printf("\n\n*** CM55 HARD FAULT ***\n");
    printf("  CFSR  = 0x%08lX\n", (unsigned long)SCB->CFSR);
    printf("  HFSR  = 0x%08lX\n", (unsigned long)SCB->HFSR);
    printf("  MMFAR = 0x%08lX\n", (unsigned long)SCB->MMFAR);
    printf("  BFAR  = 0x%08lX\n", (unsigned long)SCB->BFAR);
    for (;;) { __asm volatile ("nop"); }
}

int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq();
    init_retarget_io(); // เปิดการทำงานของ printf

    // ล้างหน้าจอ Terminal
     //Clear screen (removed ANSI sequence for compatibility)
    printf("=== BabyMate Sensor Test ===\n");

    /* ล้างบล็อกข้อมูลร่วมและตั้งค่าเริ่มต้นของสวิตช์ควบคุม
     * ต้องทำก่อนสร้าง task ใด ๆ และก่อนที่ CM33 จะเริ่มอ่าน
     * CM33 เช็ก magic ก่อนอ่านทุกครั้ง ถ้ายังไม่ตั้งมันจะข้ามไปเอง */
    bm_init();
    printf("[SYS] shared block ready at 0x%08lX  magic=0x%08lX size=%u\n",
           (unsigned long)BM_SHARED_ADDR,
           (unsigned long)bm_shared()->magic,
           (unsigned)sizeof(babymate_shared_t));

    // 1. สั่งเปิดเซนเซอร์ SHT40 (มันจะไปแอบทำงานอยู่เบื้องหลังใน env_sensor.c)
    create_env_sensor_task();
    printf("[SYS] Environment Sensor task (SHT40 & BMI270) launched!\n");
    
    // 2. สั่งเปิดไมค์และ AI จับเสียงเด็กร้อง (ทำงานคู่กันเลยครับ)
    if (create_audio_task() != CY_RSLT_SUCCESS) {
        printf("[SYS] !! create_audio_task FAILED\n");
    } else {
        printf("[SYS] Audio + BabyCry AI task launched!\n");
    }

    // 3. เรียก Task ของ Radar (ข้างในยังถูก Comment ปิดไว้อยู่เพื่อความปลอดภัย)
    create_radar_task();
    printf("[SYS] Radar task launched!\n");

    // 4. จอสัมผัส — ปลุกทีหลังสุด เพราะมันจะไปอ่านค่าที่ task ข้างบนเขียนไว้
    if (create_tft_task() != CY_RSLT_SUCCESS) {
        printf("[SYS] !! create_tft_task FAILED\n");
    } else {
        printf("[SYS] TFT touchscreen task launched!\n");
    }

    printf("[SYS] starting scheduler...\n");
    /* Start the RTOS scheduler. This function should never return */
    vTaskStartScheduler();
}
