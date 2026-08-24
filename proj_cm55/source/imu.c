/******************************************************************************
 * File Name:   imu.c
 *
 * Description: This file contains the task that initializes and configures the
 *              BMI270 Motion Sensor and executes the Fall detection model 
 *              inference.
 *
 * Related Document: See README.md
 *
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

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "imu.h"
#include "retarget_io_init.h"

#ifdef COMPONENT_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#endif


/******************************************************************************
 * Macros
 ******************************************************************************/
 /* Required data rate is 50 Hz. So, data sample to be sent once in 20 msec */
 #define IMU_SAMPLES_RATE               20
/* Task priority and stack size for the Motion sensor task */
#define TASK_MOTION_SENSOR_PRIORITY     (configMAX_PRIORITIES - 1)
#define TASK_MOTION_SENSOR_STACK_SIZE   (1024U)
/* I2C Clock frequency in Hz */
#define I2C_CLK_FREQ_HZ                 (400000U)

#define DETECTCOUNT                     10
#define LED_STOP_COUNT                  10000

#define TIMER_INT_PRIORITY              (3U)
#define BIT_MASK_CHECK                  (0U)


/*******************************************************************************
 * Global Variables
 ********************************************************************************/

static mtb_hal_i2c_t CYBSP_I2C_CONTROLLER_hal_obj;
cy_stc_scb_i2c_context_t CYBSP_I2C_CONTROLLER_context;

volatile long tick1 = 0;

volatile uint8_t send_data = 0;

/* Motion sensor task handle */
static TaskHandle_t motion_sensor_task_handle;

/* Instance of BMI270 sensor structure */
mtb_bmi270_data_t bmi270_data;
mtb_bmi270_t bmi270;

static const char* LABELS[IMAI_DATA_OUT_COUNT] = IMAI_SYMBOL_MAP;

cy_stc_sysint_t timer_irq_cfg =
{
    .intrSrc = CYBSP_GENERAL_PURPOSE_TIMER_IRQ,
    .intrPriority = TIMER_INT_PRIORITY
};

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
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    tick1++;
    send_data++;
    
    if (send_data == IMU_SAMPLES_RATE)
    {
        send_data = 0;
        /* Send a task notification to the task */
        vTaskNotifyGiveFromISR(motion_sensor_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/*******************************************************************************
* Function Name: get_time_from_millisec
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
void get_time_from_millisec(unsigned long milliseconds, char* timeString) {
  unsigned int seconds = (milliseconds / 1000) % 60;
  unsigned int minutes = (milliseconds / (1000 * 60)) % 60;
  unsigned int hours = (milliseconds / (1000 * 60 * 60));
  sprintf(timeString, "%02u:%02u:%02u", hours, minutes, seconds);
}

/*******************************************************************************
 * Function Name: motion_sensor_init
 ********************************************************************************
 * Summary:
 *  Function that configures the I2C master interface and then initializes
 *  the motion sensor.
 *
 * Parameters:
 *  None
 *
 * Return:
 * result
 *
 *******************************************************************************/
static cy_rslt_t motion_sensor_init(void)
{
    cy_rslt_t result;

    mtb_bmi270_sens_config_t config = {0};
    uint8_t sens_list[2] = {BMI2_ACCEL, BMI2_GYRO};

    /* Initialize the I2C master interface for BMI270 motion sensor */
     result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW,
             &CYBSP_I2C_CONTROLLER_config,
             &CYBSP_I2C_CONTROLLER_context);

     if(CY_RSLT_SUCCESS != result)
     {
         printf(" Error : I2C initialization failed !!\r\n");
         handle_app_error();
     }
     Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

     /* Configure the I2C master interface with the desired clock frequency */
     result = mtb_hal_i2c_setup(&CYBSP_I2C_CONTROLLER_hal_obj,
             &CYBSP_I2C_CONTROLLER_hal_config,
             &CYBSP_I2C_CONTROLLER_context,
             NULL);

     if(CY_RSLT_SUCCESS != result)
     {
         printf(" Error : I2C setup failed !!\r\n");
         handle_app_error();
     }

     /* Initialize the BMI270 motion sensor */
     result = mtb_bmi270_init_i2c(&bmi270, &CYBSP_I2C_CONTROLLER_hal_obj, MTB_BMI270_ADDRESS_DEFAULT);
     if(CY_RSLT_SUCCESS != result)
     {
         printf(" Error : IMU sensor init failed !!\r\n");
         handle_app_error();
     }
     
     mtb_bmi270_config_default(&bmi270);

     result = mtb_bmi270_get_sensor_config(&config, 1, &bmi270);
     if(CY_RSLT_SUCCESS != result)
     {
         printf(" Error : IMU sensor config failed !!\r\n");
         handle_app_error();
     }

     /* Disable both Accelormeter and Gyroscope */
     mtb_bmi270_sensor_disable(sens_list, 2, &bmi270);
     mtb_bmi270_sensor_disable(sens_list, 1, &bmi270);

    /* Set the output data rate and range of the accelerometer *
     * Fall detection model requires IMU data at 50 Hz data rate *
     * IMU data rate = ODR / Sampling average (Bandwidth parameter) */
    config.sensor_config.type = BMI2_ACCEL;
    config.sensor_config.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    config.sensor_config.cfg.acc.range = BMI2_ACC_RANGE_8G;
    config.sensor_config.cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
    config.sensor_config.cfg.acc.filter_perf = BMI2_POWER_OPT_MODE;
    

    result = mtb_bmi270_set_sensor_config(&config, 1, &bmi270);

    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : IMU sensor config failed !!\r\n");
        CY_ASSERT(0);
    }

    /* Enable accelerometer */
    result = mtb_bmi270_sensor_enable(sens_list, 1, &bmi270);
    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : IMU sensor enable failed !!\r\n");
        CY_ASSERT(0);
    }

   
    /*timer set up*/
    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_IMO , (8000000/1000)-1);
    Cy_SysTick_SetCallback(0, systick_isr1);        /* point to SysTick ISR to increment the 1ms count */
    
    return result;
}

/*******************************************************************************
 * Function Name: task_motion
 ********************************************************************************
 * Summary:
 *  This task is used to configure the motion sensor, passing IMU data is passed to 
 * fall detection model for inferencing and model output is displayed
 *
 * Parameters:
 *  void *pvParameters : Task parameter defined during task creation (unused)
 *
 * Return:
 *  None
 *
 *******************************************************************************/
static void task_motion(void* pvParameters)
{
    cy_rslt_t result;
    /* LED variables */
    static int led_off = 0;
    static int led_on = 0;
    static int16_t success_flag = 0;
    
    int label_scores[IMAI_DATA_OUT_COUNT];
    static int prediction_count = 0;

    /* Initialize DEEPCRAFT pre-processing library */
    IMAI_FED_init();

    /* Initialize BMI270 motion sensor and suspend the task upon failure */
    result = motion_sensor_init();
    if(CY_RSLT_SUCCESS != result)
    {
        CY_ASSERT(0);
    }

    unsigned long start_t = tick1;

    for(;;)
    {
        /* Wait here until ISR notifies us */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Get IMU data */        
        /* Read x, y, z components of acceleration */
        result =  mtb_bmi270_read(&bmi270, &bmi270_data);
        if (CY_RSLT_SUCCESS != result)
        {
             CY_ASSERT(0);
        }

        if ((result == BMI2_OK) && (bmi270_data.sensor_data.status & BMI2_DRDY_ACC))
        {
#ifdef USE_SENSOR_REMAPPING
        /* Remapping the accelerometer data is done as per the model */
        imu_remap_sensor_orientation(&(bmi270_data.sensor_data.acc));
#endif
            float data_in[IMAI_DATA_IN_COUNT] =
            {
                (float) (bmi270_data.sensor_data.acc.y / 4096.0f),
                (float) (bmi270_data.sensor_data.acc.x / 4096.0f),
                (float) (-bmi270_data.sensor_data.acc.z / 4096.0f),
            };
            
            /* pass IMU data to model's enqueue function */
            result = IMAI_FED_enqueue(data_in);

            /* Check model predictions using dequeue function */
            switch(IMAI_FED_dequeue(label_scores))
            {
                case IMAI_RET_SUCCESS:
                    success_flag = 0;
                    prediction_count += 1;
                    if (label_scores[1] == 1)
                    {
                        /* New line when LED from off to on */
                        if ((led_off - CYBSP_LED_STATE_ON) > 0)
                        {
                            printf("\r\n");
                        }

                        /* Print triggered class and the triggered time since IMAI init.*/
                        unsigned long t = tick1 - start_t;
                        char timeString[9];
                        get_time_from_millisec(t, timeString);
                        printf("%s %s\r\n",LABELS[1],timeString);                        

                        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_ON);
                        led_off = 0;
                        led_on = tick1;
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


                                                
                        /* Turn off LED after the LED is on for 10 secs */
                        if((tick1 - led_on) > LED_STOP_COUNT)
                        {
                            Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_OFF);
                        }
                        led_off = 1;
                    }
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
    }
}

#ifdef USE_SENSOR_REMAPPING
/*******************************************************************************
* Function Name: imu_remap_sensor_orientation
********************************************************************************
* Summary:
* Remapping the sensor data to match with CY8CKIT-062S2-AI orientation.
*
* Parameters:
* data: pointer IMU Sensors data
*
* Return:
* None
*
*******************************************************************************/
void imu_remap_sensor_orientation(struct bmi2_sens_axes_data *data)
{
    /* remapping the data */ 
    data->x = -(data->x);
    data->y = -(data->y);
}
#endif

/*******************************************************************************
 * Function Name: create_motion_sensor_task
 ********************************************************************************
 * Summary:
 *  Function that creates the motion sensor task.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  CY_RSLT_SUCCESS upon successful creation of the motion sensor task, else a
 *  non-zero value that indicates the error.
 *
 *******************************************************************************/
cy_rslt_t create_motion_sensor_task(void)
{
    BaseType_t status;
    printf("****************** DEEPCRAFT Ready Model: FALL DETECTION ****************** \r\n\n");

    status = xTaskCreate(task_motion, "Motion Sensor Task", TASK_MOTION_SENSOR_STACK_SIZE,
            NULL, TASK_MOTION_SENSOR_PRIORITY, &motion_sensor_task_handle);

    return (pdPASS == status) ? CY_RSLT_SUCCESS : (cy_rslt_t) status;
}

/* [] END OF FILE */

