#include "radar_task.h"
#include "FreeRTOS.h"
#include "task.h"
/******************************************************************************
* File Name:   main.c
*
* Description: This is the main file for mtb-example-psoc-edge-ml-deepcraft-deploy-radar
* Code Example.
*
* Related Document: See README.md
*
*******************************************************************************
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

#include <stdlib.h>
#include "cybsp.h"
#include "retarget_io_init.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include "mtb_hal.h"
#include "cybsp.h"
#include "retarget_io_init.h"
#include "xensiv_bgt60trxx_mtb.h"
#include "xensiv_bgt60trxx_platform.h"
#define XENSIV_BGT60TRXX_CONF_IMPL
#include "radar_settings.h"
#include "cy_scb_spi.h"

#include "breathing.h"
#include "babymate_shared.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* SPI controller */
#ifdef USE_KIT_PSE84_HMI
#define RADAR_SPI_CONTROLLER_HW     CYBSP_SPI_RADAR_CONTROLLER_HW
#define RADAR_SPI_CONTROLLER_IRQ    CYBSP_SPI_RADAR_CONTROLLER_IRQ
#define RADAR_SPI_CONTROLLER_config CYBSP_SPI_RADAR_CONTROLLER_config
#define RADAR_SPI_SLAVE_SELECT      CY_SCB_SPI_SLAVE_SELECT0
#else
#define RADAR_SPI_CONTROLLER_HW     CYBSP_SPI_CONTROLLER_HW
#define RADAR_SPI_CONTROLLER_IRQ    CYBSP_SPI_CONTROLLER_IRQ
#define RADAR_SPI_CONTROLLER_config CYBSP_SPI_CONTROLLER_config
#define RADAR_SPI_SLAVE_SELECT      CY_SCB_SPI_SLAVE_SELECT1
#endif

/* ทุกกี่มิลลิวินาทีถึงจะพิมพ์ผลเรดาร์ออก UART หนึ่งครั้ง
 * ไม่เกี่ยวกับ frame rate ของเรดาร์ ไม่เกี่ยวกับ DSP - ลดแค่ความถี่ที่พิมพ์ */
#define RADAR_PRINT_PERIOD_MS   5000u

/* These sizes and masks must be aligned. */
#define DEFAULT_FIFO_SETTING                            6144

#define RING_BUFFER_MASK                                0x0000FFFF
#define RING_BUFFER_MASK32                              0x00007FFF /* Mask for 32-bits */
#define XENSIV_BGT60TRXX_SPI_BURST_MODE_CMD             (0xFF000000UL)  /* Write addr 7f<<1 | 0x01 */
#define XENSIV_BGT60TRXX_SPI_BURST_MODE_SADR_POS        (17U)
#define XENSIV_BGT60TRXX_SPI_BURST_MODE_RWB_POS         (16U)
#define XENSIV_BGT60TRXX_SPI_BURST_MODE_LEN_POS         (9U)
#define RING_BUFFER_SIZE                                0x00010000 /* 64k samples */

#define XENSIV_BGT60TRXX_IRQ_PRIORITY                   (1U) 
#define SPI_INTR_NUM                                    ((IRQn_Type) RADAR_SPI_CONTROLLER_IRQ)
#define SPI_INTR_PRIORITY                               (2U)


/*******************************************************************************
* Ringbuffer array
*******************************************************************************/
uint16_t bgt60_tensor_ring[ RING_BUFFER_SIZE ];  /* This is the size of the internal fifo. */
uint32_t* ring32;
int bgt60_ring_next_to_write = 0;
int bgt60_ring_next_to_read = 0;
int bgt60_ring_level=0;


cy_stc_scb_spi_context_t SPI_context;
mtb_hal_spi_t CYBSP_SPI_CONTROLLER_hal_obj;

cy_stc_sysint_t irq_cfg;

static volatile bool data_available = false;
/*******************************************************************************
* Preset configuration variables and structs.
*******************************************************************************/
struct xensiv_bgt60trxx_type
{
    uint32_t fifo_addr;
    uint16_t fifo_size;
    xensiv_bgt60trxx_device_t device;
};

struct radar_config {
    uint64_t start_freq;
    uint64_t end_freq;
    uint32_t samples_per_chirp;
    uint32_t chirps_per_frame;
    uint32_t rx_antennas;
    uint32_t tx_antennas;
    uint32_t sample_rate;
    float chirp_repetition_time;
    float frame_repetition_time;
    int frame_rate;
    int num_samples_per_frame;
    uint32_t fifo_int_level;
    uint32_t number_of_regs;
    const uint32_t* register_list;
};
struct radar_config radar_configs;

enum spi_state {
    NONE = 0,
    IDLE,
    BURST_PENDING,
    FIFO_READ_PENDING,
    FIFO_READ_DONE
};

enum spi_state sp_state = IDLE;

static void load_presets(void);
static void spi_set_data_width(CySCB_Type* base, uint32_t data_width);


static void cm55_ml_deepcraft_task(void);

#define RING_BUFFER_SIZE 0x00010000 /* 64k samples */

/*******************************************************************************
* Types
*******************************************************************************/
typedef struct {
    xensiv_bgt60trxx_mtb_t bgt60_obj;
    uint16_t bgt60_buffer0[16384];
    uint16_t bgt60_buffer1[16384];
    float bgt60_send_buffer[RING_BUFFER_SIZE];
    bool have_data;
    int skipped_frames;
} dev_bgt60trxx_t;

__attribute__ ((section(".cy_socmem_data"), used)) dev_bgt60trxx_t radar = {0}; 

/*******************************************************************************
* Function Name: mSPI_Interrupt
********************************************************************************
* Summary:
*   SPI_Interrupt
* Parameters:
*   
* Return:
*   
*******************************************************************************/
void mSPI_Interrupt(void)
{
    Cy_SCB_SPI_Interrupt(RADAR_SPI_CONTROLLER_HW, &SPI_context);
}


/*******************************************************************************
* Function Name: xensiv_bgt60trxx_interrupt_handler
********************************************************************************
* Summary:
* This is the interrupt handler to react on sensor indicating the availability
* of new data
*    1. Triggers the radar data manager for buffering radar data into software buffer.
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
void xensiv_bgt60trxx_interrupt_handler(void)
{
    data_available = true;
    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);
    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);
}

/*******************************************************************************
* Function Name: radar_data_process
********************************************************************************
* Summary:
* This function processes the radar sensor data and feeds the data to DEEPCRAFT
* pre-processor model and returns the results.
*
* Parameters:
*  void
*
* Return:
*  CY_RSLT_SUCCESS if successful, otherwise an error code indicating failure.
*
*******************************************************************************/
cy_rslt_t radar_data_process(void)
{
    int a = Cy_GPIO_Read(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);
    if (a)
    {
        /* Clear the interrupt */
        Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);

        /* Get radar data directly using Infineon API */
        int32_t status = xensiv_bgt60trxx_get_fifo_data(&radar.bgt60_obj.dev, bgt60_tensor_ring, radar_configs.num_samples_per_frame);

        if (status != XENSIV_BGT60TRXX_STATUS_OK)
        {
            /* จำกัดอัตราการพิมพ์
             *
             * เดิมพิมพ์ทุกเฟรม เมื่อเรดาร์ค้าง (เจอหลัง reset ผ่าน debugger หลายรอบ)
             * มันพ่นออกมา ~450 KB ต่อ 40 วินาที จน UART ตัน และกลบ log ของ
             * WiFi/MQTT บนคอร์ CM33 จนดีบักไม่ได้เลย
             *
             * error แบบนี้ถ้าเกิดก็เกิดรัวอยู่แล้ว พิมพ์เป็นระยะก็พอรู้เรื่อง */
            static uint32_t fifo_err_count = 0u;
            fifo_err_count++;

            if ((fifo_err_count <= 3u) || ((fifo_err_count % 500u) == 0u))
            {
                printf("ERROR: xensiv_bgt60trxx_get_fifo_data failed with status %ld (x%lu)\r\n",
                       status, (unsigned long)fifo_err_count);
            }
        }

        /* Process raw data to float for breathing DSP */
        for (int i = 0; i < radar_configs.num_samples_per_frame; i++)
        {
            radar.bgt60_send_buffer[i] = ((int16_t*)bgt60_tensor_ring)[i] * 1.0f;
        }

        {
            static breath_result_t br;
            static TickType_t br_last_print = 0;

            /* Print only on a fresh result. breathing_process_frame() emits
             * about once a second, including while its window is still
             * filling - printing on every frame just showed a frozen line.
             *
             * throttle อีกชั้นเพื่อลดความถี่ที่พิมพ์ออก UART:
             * DSP ยังประมวลผลทุกเฟรมเหมือนเดิม ค่า bpm ไม่เปลี่ยนเลย
             * แค่พิมพ์ห่างขึ้นเป็นทุก RADAR_PRINT_PERIOD_MS
             * อยากได้ถี่/ห่างกว่านี้ ปรับเลขตัวเดียวที่ define ข้างบน */
            TickType_t now = xTaskGetTickCount();
            bool may_print = (br_last_print == 0) ||
                             ((now - br_last_print) >= pdMS_TO_TICKS(RADAR_PRINT_PERIOD_MS));

            if (breathing_process_frame(radar.bgt60_send_buffer, &br))
            {
                /* ส่งขึ้นบล็อกร่วมทุกครั้งที่ได้ผลใหม่ (ประมาณ 1 ครั้ง/วินาที)
                 * ไม่ผูกกับ throttle ของการพิมพ์ เพราะ cloud ควรได้ค่าล่าสุดเสมอ
                 * แม้เราจะลดความถี่ที่พิมพ์ลง UART ไปแล้วก็ตาม */
                volatile babymate_shared_t *sh = bm_shared();
                bm_write_begin();
                sh->bpm          = br.bpm;
                sh->breath_psr   = br.psr;
                sh->range_m      = br.range_m;
                sh->breath_state = (uint8_t)br.state;
                bm_write_end();
            }

            if (may_print && (br.fill_pct > 0.0f))
            {
                br_last_print = now;

                if (br.fill_pct < 100.0f)
                {
                    printf("[RADAR] %-9s | filling %3.0f%% | bin %2d (%.2f m) | energy %.3e\r\n",
                           breathing_state_name(br.state), br.fill_pct,
                           br.range_bin, br.range_m, br.motion_energy);
                }
                else
                {
                    printf("[RADAR] %-9s | %4.1f bpm | PSR %4.2f | SNR %4.2f | bin %2d (%.2f m) | energy %.3e | pp %5.1f rad\r\n",
                           breathing_state_name(br.state), br.bpm, br.psr, br.snr,
                           br.range_bin, br.range_m, br.motion_energy,
                           br.phase_pp_rad);
                }
            }
        }
    }
    else
    {
        /* Yield to FreeRTOS */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return CY_RSLT_SUCCESS;
}
    


/*******************************************************************************
* Function Name: radar_init
********************************************************************************
* Summary:
* This function configures the SPI interface, initializes radar and interrupt
* service routine to indicate the availability of radar data.
*
* Parameters:
*  void
*
* Return:
*  Success or error
*
*******************************************************************************/
cy_rslt_t radar_init(void)
{
    cy_rslt_t result;

    /* Connect SPI pins to SCB3 hardware */
    Cy_GPIO_SetHSIOM(CYBSP_RSPI_MISO_PORT, CYBSP_RSPI_MISO_PIN, P21_4_SCB3_SPI_MISO);
    Cy_GPIO_SetHSIOM(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, P21_5_SCB3_SPI_MOSI);
    Cy_GPIO_SetHSIOM(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, P21_6_SCB3_SPI_CLK);

    result = Cy_SCB_SPI_Init(RADAR_SPI_CONTROLLER_HW, &RADAR_SPI_CONTROLLER_config, &SPI_context);
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    
    if (result == CY_RSLT_SUCCESS)
    {
        cy_stc_sysint_t spiIntrConfig =
        {
            .intrSrc      = SPI_INTR_NUM,
            .intrPriority = SPI_INTR_PRIORITY,
        };

        Cy_SysInt_Init(&spiIntrConfig, &mSPI_Interrupt);
        NVIC_EnableIRQ(SPI_INTR_NUM);

        /* Set active target select to line 0 */
        Cy_SCB_SPI_SetActiveSlaveSelect(RADAR_SPI_CONTROLLER_HW, RADAR_SPI_SLAVE_SELECT);
        /* Enable SPI Controller block. */
        Cy_SCB_SPI_Enable(RADAR_SPI_CONTROLLER_HW);
    }


    if (CY_RSLT_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    load_presets();


    radar.have_data = false;
    radar.skipped_frames = 0;
    
    /* Enable the RADAR. */
    radar.bgt60_obj.iface.scb_inst = RADAR_SPI_CONTROLLER_HW;
    radar.bgt60_obj.iface.spi = &SPI_context;
    radar.bgt60_obj.iface.sel_port = CYBSP_RSPI_CS_PORT;
    radar.bgt60_obj.iface.sel_pin = CYBSP_RSPI_CS_PIN;
    radar.bgt60_obj.iface.rst_port = CYBSP_RADAR_RESET_PORT;
    radar.bgt60_obj.iface.rst_pin = CYBSP_RADAR_RESET_PIN;
    radar.bgt60_obj.iface.irq_port = CYBSP_RADAR_INT_PORT;
    radar.bgt60_obj.iface.irq_pin = CYBSP_RADAR_INT_PIN;
    radar.bgt60_obj.iface.irq_num = CYBSP_RADAR_INT_IRQ;


    /* Reduce drive strength to improve EMI */
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_MOSI_PORT, CYBSP_RSPI_MOSI_PIN, CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYBSP_RSPI_CLK_PORT, CYBSP_RSPI_CLK_PIN, CY_GPIO_DRIVE_1_8);

    irq_cfg.intrSrc = (IRQn_Type)radar.bgt60_obj.iface.irq_num;
    irq_cfg.intrPriority = XENSIV_BGT60TRXX_IRQ_PRIORITY;

    result = xensiv_bgt60trxx_mtb_init(&radar.bgt60_obj, radar_configs.register_list, radar_configs.number_of_regs);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    /*Initialization uses preset 0*/
    result = xensiv_bgt60trxx_mtb_interrupt_init(&radar.bgt60_obj, radar_configs.num_samples_per_frame);
    CY_ASSERT(result == CY_RSLT_SUCCESS);

    Cy_SysInt_Init(&irq_cfg, xensiv_bgt60trxx_interrupt_handler);

    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);
    NVIC_EnableIRQ(irq_cfg.intrSrc);

    Cy_GPIO_ClearInterrupt(CYBSP_RADAR_INT_PORT, CYBSP_RADAR_INT_NUM);
    NVIC_ClearPendingIRQ(irq_cfg.intrSrc);

    Cy_SysLib_Delay(1000);

    if (result != CY_RSLT_SUCCESS)
    {
        printf("ERROR: xensiv_bgt60trxx_mtb_init failed\n");
        return false;
    }
  
    if (result == CY_RSLT_SUCCESS)
    {
        xensiv_bgt60trxx_set_fifo_limit(&radar.bgt60_obj.dev, radar_configs.fifo_int_level);
    }
    printf("Radar: Initialized device.\r\n");

    if (xensiv_bgt60trxx_soft_reset(&radar.bgt60_obj.dev, XENSIV_BGT60TRXX_RESET_FIFO) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        printf("Fifo reset error error.\r\n");
    }

    /* Reset the local fifo. */
    bgt60_ring_next_to_write = 0;
    bgt60_ring_next_to_read = 0;
    bgt60_ring_level=0;

    radar.have_data = false;
    if (xensiv_bgt60trxx_start_frame(&radar.bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        printf("Start frame error.\r\n");
    }

    return CY_RSLT_SUCCESS;

}
/*******************************************************************************
 * Platform functions implementation
 ******************************************************************************/
static void spi_set_data_width(CySCB_Type* base, uint32_t data_width)
{
    CY_ASSERT(CY_SCB_SPI_IS_DATA_WIDTH_VALID(data_width));

    CY_REG32_CLR_SET(SCB_TX_CTRL(base),
                     SCB_TX_CTRL_DATA_WIDTH,
                     (uint32_t)data_width - 1U);
    CY_REG32_CLR_SET(SCB_RX_CTRL(base),
                     SCB_RX_CTRL_DATA_WIDTH,
                     (uint32_t)data_width - 1U);
}

/*******************************************************************************
* Summary:
*   This just loads preset from header file constants to enable selection of
*   various radar presets.
* See radar_settings.h for information of the sources of radar settings.
*
*******************************************************************************/
static void load_presets()
{
    radar_configs.start_freq =            XENSIV_BGT60TRXX_CONF_START_FREQ_HZ;
    radar_configs.end_freq =              XENSIV_BGT60TRXX_CONF_END_FREQ_HZ;
    radar_configs.samples_per_chirp =     XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP;
    radar_configs.chirps_per_frame =      XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME;
    radar_configs.rx_antennas =           XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS;
    radar_configs.tx_antennas =           XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS;
    radar_configs.sample_rate =           XENSIV_BGT60TRXX_CONF_SAMPLE_RATE;
    radar_configs.chirp_repetition_time = XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S;
    radar_configs.frame_repetition_time = XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S;

    radar_configs.frame_rate = (int)(0.5 + 1.0/radar_configs.frame_repetition_time);
    radar_configs.num_samples_per_frame =
            radar_configs.samples_per_chirp *
            radar_configs.chirps_per_frame *
            radar_configs.rx_antennas;

    radar_configs.fifo_int_level = DEFAULT_FIFO_SETTING; /* This will equal 75% of the fifo. 12288 samples */

    radar_configs.number_of_regs = XENSIV_BGT60TRXX_CONF_NUM_REGS;
    radar_configs.register_list = register_list;

}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM55 CPU. It initializes BSP, radar, SPI and the
* ML model. It reads data from radar sensor continuously, processes it within
* the model and displays the output.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
void radar_task(void* param)
{
    // cybsp_init() ถูกเรียกจาก main.c แล้ว ไม่ต้องเรียกซ้ำ
    cm55_ml_deepcraft_task();
}



/*******************************************************************************
* Function Name: system_init
********************************************************************************
* Summary:
*  Initializes the neural network based on the DEEPCRAFT model and the
*  DEEPCRAFT pre-processor and initializes the Radar sensor.
*
* Parameters:
*  None
*
* Returns:
*  The status of the initialization.
*
*******************************************************************************/
static cy_rslt_t system_init(void)
{
    cy_rslt_t result;

    /* IMAI_init() ถูกเรียกจาก audio_task แล้ว ห้ามเรียกซ้ำ! */
    // IMAI_init();

    /* BabyMate: breathing-rate DSP, shares the same raw chirp buffer */
    breathing_init();

    result = radar_init();

    /* breathing.c hard-codes the geometry of the preset in radar_settings.h
     * (64 samples x 32 chirps x 3 rx, 33.284 fps, 0.0375 m bins). A preset
     * change would otherwise be silent and yield a plausible but wrong bpm. */
    if (radar_configs.num_samples_per_frame != 6144)
    {
        printf("WARNING: frame is %d samples, breathing.c expects 6144.\r\n"
               "         Update the geometry block at the top of breathing.c.\r\n",
               radar_configs.num_samples_per_frame);
    }

    /* ANSI ESC sequence for clear screen */
    printf("==========================================\r\n");
    
    return result;
}

/*******************************************************************************
* Function Name: cm55_ml_deepcraft_task
********************************************************************************
* Summary:
*  Contains the main loop for the application. It sets up the UART for
*  logs and initialises the system (DEEPCRAFT pre-processor and Radar sensor
*  for gesture input). It then invokes the Radar Data Processing function that
*  sends the data for pre-processing, inferencing, and prints in the results
*  when enough data is received.
*
* Parameters:
*  None
*
* Returns:
*  None
*
*******************************************************************************/

static void cm55_ml_deepcraft_task(void)
{
    cy_rslt_t result;
    /* Initializes retarget-io middleware. */
    // init_retarget_io();

    /* Clear screen disabled */
    printf("\r\n");
    
    /* Initialize inference engine and sensors */
    result = system_init();

    /* Initialization failed */
    if(CY_RSLT_SUCCESS != result)
    {
        /* Failed to initialize properly */
        printf("System initialization fail\r\n");
        while(1);
    }

    for (;;)
    {
        /* Invoke the PDM Data Processing function that sends the data for
         * pre-processing, inferencing, and print the results when enough data
         * is received.
         */
        radar_data_process();
    }
}

void create_radar_task(void) { xTaskCreate(radar_task, "radar_task", 4096, NULL, 2, NULL); }


