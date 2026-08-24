/****************************************************************************
* File Name        : audio.c
*
* Description      : This file implements the interface with the PDM, as
*                    well as the PDM ISR to feed the pre-processor.
*
* Related Document : See README.md
*
*****************************************************************************
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
*****************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/

#include "cy_pdl.h"
#include "cybsp.h"
#include "audio.h"
#include "cy_syslib.h"
#include <time.h>

#ifdef DIRECTIONOFARRIVAL_MODEL
#include "ready_models/audio_data.h"
#endif

#ifdef COMPONENT_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#endif


/*****************************************************************************
 * Macros
 *****************************************************************************/
#define SYSTICK_MAX_CNT                         (0xFFFFFF)

#define PDM_CHANNEL                             (3u)
/* Define how many samples in a frame */
#define FRAME_SIZE                              (1024)

/* Desired sample rate. Typical values: 8/16/22.05/32/44.1/48kHz */
#define SAMPLE_RATE_HZ                          16000u

/* Decimation Rate of the PDM/PCM block. Typical value is 64 */
#define DECIMATION_RATE                         64u

#define DETECTCOUNT                             10
#define LED_STOP_COUNT                          500

/* ธง baby_crying ค้างกี่ ms หลังตรวจเจอครั้งสุดท้าย
 * ต้องนานพอให้จอ (รีเฟรช 500 ms) และ MQTT เห็นทัน */
#define CRY_FLAG_HOLD_MS                        5000u

/* PDM PCM hardware FIFO size */
#define HW_FIFO_SIZE                            (64u)

/* Rx FIFO trigger level/threshold configured by user */
#define RX_FIFO_TRIG_LEVEL                      (HW_FIFO_SIZE/2)

/* Total number of interrupts to get the FRAME_SIZE number of samples*/
#define NUMBER_INTERRUPTS_FOR_FRAME             (FRAME_SIZE/RX_FIFO_TRIG_LEVEL)

/* Multiplication factor of the input signal.
 * This should ideally be 1. Higher values will have a negative impact on
 * the sampling dynamic range. However, it can be used as a last resort
 * when MICROPHONE_GAIN is already at maximum and the ML model was trained
 * with data at a higher amplitude than the microphone captures.
 * Note: If you use the same board for recording training data and
 * deployment of your own ML model set this to 1.0. */
#define DIGITAL_BOOST_FACTOR                    1.0f

/* Specifies the dynamic range in bits.
 * PCM word length, see the A/D specific documentation for valid ranges. */
 #define AUIDO_BITS_PER_SAMPLE                  16

/* Converts given audio sample into range [-1,1] */
#define SAMPLE_NORMALIZE(sample)                (((float) (sample)) / (float) (1 << (AUIDO_BITS_PER_SAMPLE - 1)))

/* PDM PCM interrupt configuration parameters */
const cy_stc_sysint_t PDM_IRQ_cfg =
{
    .intrSrc = (IRQn_Type)CYBSP_PDM_CHANNEL_3_IRQ,
    .intrPriority = 2
};

/* RTOS tasks */
#define AUDIO_TASK_NAME                      "audio_task"
#define AUDIO_TASK_STACK_SIZE                (configMINIMAL_STACK_SIZE * 10)
#define AUDIO_TASK_PRIORITY                  (configMAX_PRIORITIES - 1)

/*****************************************************************************
 * Function Prototypes
 *****************************************************************************/
static void pdm_pcm_event_handler(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
volatile long tick1 = 0;

/* Set up one buffer for data collection and one for processing */
int16_t audio_buffer0[FRAME_SIZE] = {0};
int16_t audio_buffer1[FRAME_SIZE] = {0};
int16_t* active_rx_buffer;
int16_t* full_rx_buffer;

/* Model Output variable */
int data_out[IMAI_DATA_OUT_COUNT] = {0};
static const char* LABELS[IMAI_DATA_OUT_COUNT] = IMAI_DATA_OUT_SYMBOLS;

/* Task handler */
static TaskHandle_t audio_task_handler;

/*******************************************************************************
* Function Name: systick_isr1
********************************************************************************
* Summary: This increments every time the SysTick counter decrements to 0.
*
* Parameters:
*   None
*
* Return:
*   None
*
*
*******************************************************************************/
void systick_isr1(void)
{
    tick1++;
}

/*******************************************************************************
* Function Name: get_time_from_millisec_audio
********************************************************************************
* Summary: This function prints the time when a output class is detected.
*
* Parameters:
*   milliseconds : time when a output class is detected.
*   timeString   : time of detected class in hr:m:s format.
*
* Return:
*   None
*
*
*******************************************************************************/
void get_time_from_millisec_audio(unsigned long milliseconds, char* timeString) {
  unsigned int seconds = (milliseconds / 1000) % 60;
  unsigned int minutes = (milliseconds / (1000 * 60)) % 60;
  unsigned int hours = (milliseconds / (1000 * 60 * 60));
  sprintf(timeString, "%02u:%02u:%02u", hours, minutes, seconds);
}


/*******************************************************************************
* Function Name: audio_init
********************************************************************************
* Summary:
*    A function used to initialize and configure the PDM based on the shield
*    selected in the Makefile. Starts an asynchronous read which triggers an
*    interrupt when completed.
*
* Parameters:
*   None
*
* Return:
*     The status of the initialization.
*
*
*******************************************************************************/
cy_rslt_t audio_init(void)
{
    cy_rslt_t result;

    /* Set up pointers to two buffers to implement a ping-pong buffer system.
     * One gets filled by the PDM while the other can be processed. */
    memset(audio_buffer0, 0, FRAME_SIZE*sizeof(int16_t));
    memset(audio_buffer1, 0, FRAME_SIZE*sizeof(int16_t));
    active_rx_buffer = audio_buffer0;
    full_rx_buffer = audio_buffer1;

    /* Initialize PDM PCM block */
    result = Cy_PDM_PCM_Init(CYBSP_PDM_HW, &CYBSP_PDM_config);
    if(CY_PDM_PCM_SUCCESS != result)
    {
        return result;
    }

    Cy_PDM_PCM_Channel_Enable(CYBSP_PDM_HW, PDM_CHANNEL);
    /* Initialize and enable PDM PCM channel 3 -Right */
    Cy_PDM_PCM_Channel_Init(CYBSP_PDM_HW, &channel_3_config, PDM_CHANNEL);

    /* Set the gain as per the model. */
    #if defined(ALARM_MODEL) || defined(SIREN_MODEL)
    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_SEL_GAIN_23DB);
    #else
    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_SEL_GAIN_5DB);
    #endif

    /* An interrupt is registered for right channel, clear and set masks for it. */
    Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    Cy_PDM_PCM_Channel_SetInterruptMask(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);

    /* Register the IRQ handler */
    result = Cy_SysInt_Init(&PDM_IRQ_cfg, &pdm_pcm_event_handler);
    if(CY_SYSINT_SUCCESS != result)
    {
        return result;
    }
    NVIC_ClearPendingIRQ(PDM_IRQ_cfg.intrSrc);
    NVIC_EnableIRQ(PDM_IRQ_cfg.intrSrc);

    /* Set up pointers to two buffers to implement a ping-pong buffer system.
    * One gets filled by the PDM while the other can be processed. */
    active_rx_buffer = audio_buffer0;
    full_rx_buffer = audio_buffer1;


    Cy_PDM_PCM_Activate_Channel(CYBSP_PDM_HW, PDM_CHANNEL);

    /* --- BUG FIX: Do NOT hijack SysTick in FreeRTOS! --- */
    // Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_IMO , (8000000/1000)-1);
    // Cy_SysTick_SetCallback(0, systick_isr1);

    return 0;
}


/*******************************************************************************
* Function Name: audio_task
********************************************************************************
* Summary:
* This is the main task.
*    1. Initializes the PDM/PCM block.
*    2. Wait for the frame data available for process.
*    3. Runs the model and provides the result.
* Parameters:
*  pvParameters : unused
*
* Return:
*  none
*
*******************************************************************************/
void audio_task(void *pvParameters)
{
    cy_rslt_t result;
    /* LED variables */
    static int led_off = 0;
    static int led_on = 0;
    int label_scores[IMAI_DATA_OUT_COUNT];
    static int prediction_count = 0;
    static int16_t success_flag = 0;
    
    /* Initialize DEEPCRAFT pre-processing library */
    IMAI_AED_init();

    /* ---- ปรับความไวของโมเดล ----
     *
     * ทำไมต้องมี: โมเดลไม่ได้คืนความน่าจะเป็นดิบออกมา แต่คืนธง 0/1 ที่ผ่าน
     * post-processing ภายในไลบรารีมาแล้ว ซึ่งมีเงื่อนไขซ้อนกันหลายชั้น
     * ค่า default ถูกตั้งไว้เข้มเพื่อกัน false positive ในงานจริง
     * ผลคือเสียงร้องที่ไม่ดังพอ/ไม่ยาวพอจะไม่ทำให้ธงขึ้นเลย
     *
     * โปรเจกต์นี้ไม่เคยเรียก IMAI_AED_sensitivity() มาก่อน เลยติดค่าเข้มมาตลอด
     *
     * ความหมายแต่ละตัว (จาก babycry_lib.h):
     *   confidence     เกณฑ์ความมั่นใจ ยิ่งต่ำยิ่งไวแต่หลอนง่ายขึ้น
     *   average        เฉลี่ยกี่ค่าก่อนเทียบเกณฑ์ (1 = ไม่เฉลี่ย ตอบไวสุด)
     *   subsequent     ต้องเกินเกณฑ์ติดกันกี่ครั้งถึงจะยิง
     *   pool/selection ในกลุ่ม pool ค่า ต้องเกินเกณฑ์อย่างน้อยกี่ตัว
     *
     * ตั้งไว้ไวสุดก่อนเพื่อพิสูจน์ว่าโมเดลตรวจเจอได้จริง ถ้าเจอแล้วหลอนบ่อย
     * ค่อยไล่เพิ่ม confidence กับ subsequent กลับขึ้นทีละขั้น */
    {
        /* ไล่ลองหลายชุดเพื่อหาว่าโมเดลรับช่วงไหน แล้วใช้ชุดที่ "ไวที่สุดที่ผ่าน"
         * เรียงจากไวสุดไปเข้มสุด ตัวแรกที่ได้ rc 0 คือตัวที่เอา
         * (rc -4 = IMAI_RET_OUTOFBOUNDS = ชุดนั้นอยู่นอกช่วงที่รองรับ) */
        static const PP_config_t candidates[] = {
            { 0.30f, 1u, 1u, 1u, 1u },
            { 0.50f, 1u, 1u, 1u, 1u },
            { 0.50f, 1u, 1u, 2u, 1u },
            { 0.50f, 1u, 1u, 3u, 1u },
            { 0.50f, 1u, 1u, 5u, 2u },
            { 0.50f, 1u, 2u, 3u, 2u },
            { 0.60f, 1u, 1u, 5u, 3u },
            { 0.70f, 1u, 1u, 1u, 1u },
            { 0.70f, 2u, 2u, 3u, 2u },
            { 0.80f, 1u, 1u, 1u, 1u },
            { 0.90f, 1u, 1u, 1u, 1u },
            { 0.50f, 0u, 0u, 0u, 0u },
        };

        int applied = -1;

        for (uint32_t i = 0u; i < (sizeof(candidates) / sizeof(candidates[0])); i++)
        {
            const int rc = IMAI_AED_sensitivity(candidates[i]);

            printf("[AI] try conf=%.2f avg=%u sub=%u pool=%u sel=%u -> rc %d%s\n",
                   (double)candidates[i].confidence,
                   (unsigned)candidates[i].average,
                   (unsigned)candidates[i].subsequent,
                   (unsigned)candidates[i].pool,
                   (unsigned)candidates[i].pool_selection,
                   rc, (0 == rc) ? "  <== OK" : "");

            if ((0 == rc) && (applied < 0))
            {
                applied = (int)i;
                /* ไม่ break เพื่อให้เห็นภาพรวมว่าอะไรผ่าน/ไม่ผ่านบ้างในรอบเดียว */
            }
        }

        if (applied >= 0)
        {
            /* ชุดหลัง ๆ ทับค่าไปแล้ว ต้องตั้งชุดที่เลือกซ้ำอีกครั้ง */
            (void)IMAI_AED_sensitivity(candidates[applied]);
            printf("[AI] using candidate #%d (conf %.2f)\n",
                   applied, (double)candidates[applied].confidence);
        }
        else
        {
            IMAI_AED_sensitivity_reset();
            printf("[AI] !! no candidate accepted, using library default !!\n");
        }
    }


    result = audio_init();
    if(result != 0)
    {
        CY_ASSERT(0);
    }

    unsigned long led_start_t = xTaskGetTickCount();

    /* ---- มิเตอร์วัดระดับเสียงเข้า (ชั่วคราว เพื่อหาสาเหตุ BabyCry ไม่ติด) ----
     *
     * ทำไมต้องมี: จุด "." ที่พิมพ์อยู่บอกแค่ว่า inference รันสำเร็จ
     * แต่ไม่ได้บอกว่าไมค์ได้ยินอะไรจริงหรือเปล่า ถ้าไมค์ตายหรือเสียงเบามาก
     * โมเดลก็จะรันบนความเงียบไปเรื่อย ๆ และไม่มีวันตัดสินว่า "ร้อง"
     *
     * อ่านค่ายังไง (peak คือค่าสูงสุดของสัญญาณ 0.0-1.0):
     *   < 0.01  ไมค์ไม่ได้ยินอะไรเลย  -> ปัญหาฮาร์ดแวร์/PDM/gain
     *   0.05-0.3 ได้ยินแต่เบา         -> เพิ่ม gain หรือขยับไมค์ใกล้ขึ้น
     *   > 0.5   ได้ยินชัดเจน          -> ปัญหาอยู่ที่โมเดล ไม่ใช่เสียงเข้า
     *   ~1.00 ค้าง                    -> คลิป สัญญาณแรงเกินจนเพี้ยน
     *
     * ลบทิ้งได้เลยเมื่อหาสาเหตุเจอแล้ว */
    float    mic_peak     = 0.0f;
    uint32_t mic_frames   = 0u;

    /* เวลาที่ธง baby_crying จะหมดอายุ ถูกเลื่อนออกไปทุกครั้งที่ตรวจเจอใหม่ */
    uint32_t cry_hold_until = 0u;
    /* FRAME_SIZE ตัวอย่างต่อรอบ ที่ 16 kHz -> พิมพ์ทุก ~1 วินาที */
    const uint32_t MIC_REPORT_FRAMES = (uint32_t)(SAMPLE_RATE_HZ / FRAME_SIZE);

    for(;;)
    {
        /* Wait here until ISR notifies us */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        mic_frames++;

        for (uint32_t index = 0; index < FRAME_SIZE; index++)
        {

            int16_t val_temp = full_rx_buffer[index];

            /*convert int to float*/
            float data_in = SAMPLE_NORMALIZE(val_temp) * DIGITAL_BOOST_FACTOR;

            {
                const float mag = (data_in < 0.0f) ? -data_in : data_in;
                if (mag > mic_peak)
                {
                    mic_peak = mag;
                }
            }

            if (data_in > 1.0)
            {
                data_in = 1.0f;
            }
            else if (data_in < -1.0)
            {
               data_in = -1.0f;
            }

            /*pass audio sample for enqueue*/
            result = IMAI_AED_enqueue(&data_in);
            if (IMAI_RET_SUCCESS != result)
            {
                CY_ASSERT(0);
            }

            switch(IMAI_AED_dequeue(label_scores))
            {
                case IMAI_RET_SUCCESS:
                    success_flag = 1;
                    prediction_count += 1;
                    if (label_scores[1] == 1)
                    {
                        /* New line when LED from off to on */
                        if ((led_off - CYBSP_LED_STATE_ON) > 0)
                        {
                            printf("\r\n");
                        }

                        /* Print triggered class and the triggered time since IMAI init.*/
                        unsigned long t = xTaskGetTickCount() - led_start_t;
                        char timeString[9];
                        get_time_from_millisec_audio(t, timeString);
                        printf("%s %s\r\n",LABELS[1],timeString);

                        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_ON);
                        led_off = 0;
                        led_on = xTaskGetTickCount();

                        /* บอก CM33 ให้ส่งขึ้น ThingsBoard
                         * ฟิลด์ขนาด 1 ไบต์ เขียนทีเดียวจบ ไม่ต้องใช้ seqlock */
                        bm_shared()->baby_crying = 1u;
                        cry_hold_until = xTaskGetTickCount() + pdMS_TO_TICKS(CRY_FLAG_HOLD_MS);
                    }
                    else
                    {
                        /* Only print non-label class very 10 predictions */
                        if (prediction_count>DETECTCOUNT)
                        {
                            printf(".");
                            fflush( stdout );
                            prediction_count = 0;
                        }
                        /* Turn off LED after the LED is on for 500ms */
                        if((xTaskGetTickCount() - led_on) > LED_STOP_COUNT)
                        {
                            Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_OFF);
                        }

                        /* ธง baby_crying ค้างไว้นานกว่า LED มาก
                         *
                         * ของเดิมเคลียร์พร้อม LED คือหลังตรวจไม่เจอแค่ 500 ms
                         * แต่จอรีเฟรชทุก 500 ms และ MQTT ส่งช้ากว่านั้นอีก
                         * ธงจึงกระพริบเร็วจนจอกับ dashboard แทบไม่ทันเห็น
                         * = "ตรวจเจอแล้วแต่ดูเหมือนไม่ทำงาน"
                         *
                         * เสียงร้องจริงมีช่วงหายใจเข้าที่เงียบคั่นเป็นจังหวะด้วย
                         * ถ้าเคลียร์เร็วเกินจะกลายเป็นติด ๆ ดับ ๆ ทั้งที่ยังร้องอยู่ */
                        if ((int32_t)(xTaskGetTickCount() - cry_hold_until) >= 0)
                        {
                            bm_shared()->baby_crying = 0u;
                        }
                        led_off = 1;
                    }
                    break;
                    
                    case IMAI_RET_NOMEM:
                        /* Something went wrong, stop the program */
                        printf("Unable to perform inference. Internal memory error.\r\n");
                        break;
                    case IMAI_RET_TIMEDOUT:
                         if (success_flag == 1)
                         {
                              printf("The evaluation period has ended. Please rerun the evaluation or purchase a license for the ready model.\r\n");
                         }
                         success_flag = 0;
                         break;

            }
        }

        /* รายงานระดับเสียงทุก ~1 วินาที แล้วรีเซ็ต peak เริ่มนับรอบใหม่ */
        if (mic_frames >= MIC_REPORT_FRAMES)
        {
            printf("\r\n[MIC] peak %.3f\r\n", (double)mic_peak);
            fflush(stdout);
            mic_peak   = 0.0f;
            mic_frames = 0u;
        }
    }
}


/*******************************************************************************
 * Function Name: create_audio_task
 ********************************************************************************
 * Summary:
 *  Function that creates the audio task.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  CY_RSLT_SUCCESS upon successful creation of the radar sensor task, else a
 *  non-zero value that indicates the error.
 *
 *******************************************************************************/
cy_rslt_t create_audio_task(void)
{
    BaseType_t status;
    printf("****************** DEEPCRAFT Ready Model: %s ****************** \r\n\n", LABELS[1]);

    /* Create the RTOS task */
    status = xTaskCreate(audio_task, AUDIO_TASK_NAME, AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, &audio_task_handler);


    return (pdPASS == status) ? CY_RSLT_SUCCESS : (cy_rslt_t) status;
}


/*******************************************************************************
* Function Name: pdm_pcm_event_handler
********************************************************************************
* Summary:
*  PDM/PCM ISR handler. A flag is set when the interrupt is generated.
*
* Parameters:
*  arg: not used
*  event: event that occurred
*
*******************************************************************************/
static void pdm_pcm_event_handler(void)
{
    /* Used to track how full the buffer is */
    static uint16_t frame_counter = 0;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* Check the interrupt status */
    uint32_t intr_status = Cy_PDM_PCM_Channel_GetInterruptStatusMasked(CYBSP_PDM_HW, PDM_CHANNEL);
    if(CY_PDM_PCM_INTR_RX_TRIGGER & intr_status)
    {
        /* Move data from the PDM fifo and place it in a buffer */
        for(uint32_t index=0; index < RX_FIFO_TRIG_LEVEL; index++)
        {
            int32_t data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, PDM_CHANNEL);
            active_rx_buffer[frame_counter * RX_FIFO_TRIG_LEVEL + index] = (int16_t)(data);
        }
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_RX_TRIGGER);
        frame_counter++;
    }

    /* Check if the buffer is full */
    if((NUMBER_INTERRUPTS_FOR_FRAME) <= frame_counter)
    {
        /* Flip the active and the next rx buffers */
        int16_t* temp = active_rx_buffer;
        active_rx_buffer = full_rx_buffer;
        full_rx_buffer = temp;

        /* Send a task notification to the task */
        vTaskNotifyGiveFromISR(audio_task_handler, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        frame_counter = 0;

    }

    /* Clear the remaining interrupts */
    if((CY_PDM_PCM_INTR_RX_FIR_OVERFLOW | CY_PDM_PCM_INTR_RX_OVERFLOW |
            CY_PDM_PCM_INTR_RX_IF_OVERFLOW | CY_PDM_PCM_INTR_RX_UNDERFLOW) & intr_status)
    {
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    }

}


/* [] END OF FILE */
