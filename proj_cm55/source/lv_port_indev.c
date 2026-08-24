/*******************************************************************************
* File Name        : lv_port_indev.c
*
* Description      : This file provides implementation of low level input device
*                    driver for LVGL.
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
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_indev_private.h"
#include "cy_utils.h"
#if defined(MTB_CTP_GT911)
#include "mtb_ctp_gt911.h"
#elif defined(MTB_CTP_ILI2511)
#include "mtb_ctp_ili2511.h"
#elif defined(MTB_CTP_FT5406)
#include "mtb_ctp_ft5406.h"
#elif defined(MTB_CTP_FT5446)
#include "mtb_ctp_ft5446.h"
#endif
#include "cybsp.h"
#include "display_i2c_config.h"
#include "FreeRTOS.h"
#include "task.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#if defined(MTB_CTP_ILI2511)
#define CTP_RESET_PORT             GPIO_PRT17
#define CTP_RESET_PIN              (3U)
#define CTP_IRQ_PORT               GPIO_PRT17
#define CTP_IRQ_PIN                (2U)
#elif defined(MTB_CTP_FT5446)
#define CTP_RESET_PORT             GPIO_PRT16
#define CTP_RESET_PIN              (7U)
#define CTP_IRQ_PORT               GPIO_PRT11
#define CTP_IRQ_PIN                (6U)
#endif

/* LVGL indev timer period - can be fast since callback is just a cache read */
#define INDEV_READ_PERIOD_MS       33U

/* Background touch I2C polling interval */
#define TOUCH_POLL_MS              33U
#define TOUCH_TASK_STACK_SIZE      256U
#define TOUCH_TASK_PRIORITY        (tskIDLE_PRIORITY + 1U)

#define RESETVAL                   (0)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Cached touch state - written by background task, read by LVGL callback.
 * Single-writer on Cortex-M, so no lock needed for atomic word-sized fields. */
static volatile int      touch_cache_x     = RESETVAL;
static volatile int      touch_cache_y     = RESETVAL;
static volatile uint8_t  touch_cache_state = LV_INDEV_STATE_REL;

static TaskHandle_t touch_task_handle = NULL;

#if defined(MTB_CTP_ILI2511)
/* ILI2511 touch controller configuration */
mtb_ctp_ili2511_config_t ctp_ili2511_cfg =
{
    .scb_inst        = DISPLAY_I2C_CONTROLLER_HW,
    .i2c_context     = &disp_touch_i2c_controller_context,
    .rst_port        = CTP_RESET_PORT,
    .rst_pin         = CTP_RESET_PIN,
    .irq_port        = CTP_IRQ_PORT,
    .irq_pin         = CTP_IRQ_PIN,
    .irq_num         = ioss_interrupts_gpio_17_IRQn,
    .touch_event     = false,
};
#endif /* MTB_CTP_ILI2511 */

#if defined(MTB_CTP_FT5406)
mtb_ctp_ft5406_config_t ctp_ft5406_cfg =
{
    .i2c_base        = DISPLAY_I2C_CONTROLLER_HW,
    .i2c_context     = &disp_touch_i2c_controller_context,
};
#endif /* MTB_CTP_FT5406 */

#if defined(MTB_CTP_FT5446)
mtb_ctp_ft5446_config_t ctp_ft5446_cfg =
{
  .scb_instance        = DISPLAY_I2C_CONTROLLER_HW,
  .i2c_context         = &disp_touch_i2c_controller_context,
  .rst_port            = CTP_RESET_PORT,
  .rst_pin             = CTP_RESET_PIN,
  .irq_port            = CTP_IRQ_PORT,
  .irq_pin             = CTP_IRQ_PIN,
  .irq_num             = ioss_interrupts_gpio_11_IRQn,
  .touch_event         = false,
};
#endif /* MTB_CTP_FT5446 */

/*******************************************************************************
* Function Name: touchpad_init
********************************************************************************
* Summary:
*  Initialization function for touchpad supported by LVGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void touchpad_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

#if defined(MTB_CTP_GT911)
    result = mtb_gt911_init(DISPLAY_I2C_CONTROLLER_HW,
                                &disp_touch_i2c_controller_context);

#elif defined(MTB_CTP_ILI2511)
    result = mtb_ctp_ili2511_init(&ctp_ili2511_cfg);
#elif defined(MTB_CTP_FT5406)
    result = (cy_rslt_t)mtb_ctp_ft5406_init(&ctp_ft5406_cfg);
#elif defined(MTB_CTP_FT5446)
    result = mtb_ctp_ft5446_init(&ctp_ft5446_cfg);
#endif

    if (CY_RSLT_SUCCESS != result)
    {
        CY_ASSERT(0);
    }
}


/*******************************************************************************
* Function Name: touch_poll_task
********************************************************************************
* Summary:
*  Background FreeRTOS task that polls the touch controller via I2C at a fixed
*  interval. Results are stored in volatile cache variables so the LVGL indev
*  callback can read them without any I2C overhead (zero blocking of the
*  rendering thread).
*
* Parameters:
*  void *arg: Pointer to the argument passed to the task (not used)
*
* Return:
*  void
*
*******************************************************************************/
static void touch_poll_task(void *arg)
{
    (void)arg;
    int tx = RESETVAL, ty = RESETVAL;

    for (;;)
    {
        uint8_t st = LV_INDEV_STATE_REL;

#if defined(MTB_CTP_GT911)
        cy_rslt_t res = mtb_gt911_get_single_touch(DISPLAY_I2C_CONTROLLER_HW,
                                                    &disp_touch_i2c_controller_context,
                                                    &tx, &ty);
        if (CY_RSLT_SUCCESS == res)
        {
            st = LV_INDEV_STATE_PR;
        }
#elif defined(MTB_CTP_ILI2511)
        cy_rslt_t res = mtb_ctp_ili2511_get_single_touch(&tx, &ty);
        if (CY_RSLT_SUCCESS == res)
        {
            st = LV_INDEV_STATE_PR;
        }
#elif defined(MTB_CTP_FT5406)
        mtb_ctp_touch_event_t ev;
        cy_rslt_t res = (cy_rslt_t)mtb_ctp_ft5406_get_single_touch(&ev, &tx, &ty);
        if ((CY_RSLT_SUCCESS == res) &&
            ((MTB_CTP_TOUCH_DOWN == ev) || (MTB_CTP_TOUCH_CONTACT == ev)))
        {
            st = LV_INDEV_STATE_PR;
        }
#elif defined(MTB_CTP_FT5446)
        cy_rslt_t res = mtb_ctp_ft5446_get_single_touch(&tx, &ty);
        if (CY_RSLT_SUCCESS == res)
        {
            st = LV_INDEV_STATE_PR;
        }
#endif

        /* Update cache - single writer, word-sized stores are atomic on Cortex-M */
        touch_cache_x     = tx;
        touch_cache_y     = ty;
        touch_cache_state = st;

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}


/*******************************************************************************
* Function Name: touchpad_read
********************************************************************************
* Summary:
*  Touchpad read function called periodically by the LVGL library. It returns
*  the latest touch state and coordinates from the cache populated by the
*  background touch polling task, so it performs no I2C access and never blocks
*  the rendering thread.
*
* Parameters:
*  *indev_drv: Pointer to the input driver structure to be registered by LVGL.
*  *data: Pointer to the data buffer holding touch coordinates.
*
* Return:
*  void
*
*******************************************************************************/
LV_ATTRIBUTE_FAST_MEM void touchpad_read(lv_indev_t *indev_drv,
                                         lv_indev_data_t *data)
{
    (void)indev_drv;

    /* Read from cache - no I2C, no blocking, no context switch */
    data->state = (lv_indev_state_t)touch_cache_state;

#if defined(MTB_CTP_FT5406)
    data->point.x = ACTUAL_DISP_HOR_RES - touch_cache_x;
    data->point.y = ACTUAL_DISP_VER_RES - touch_cache_y;
#elif defined(MTB_CTP_ILI2511) || defined(MTB_CTP_GT911) || defined(MTB_CTP_FT5446)
    data->point.x = touch_cache_x;
    data->point.y = touch_cache_y;
#endif
}


/*******************************************************************************
* Function Name: lv_port_indev_init
********************************************************************************
* Summary:
*  Initialization function for input devices supported by LVGL.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_indev_init(void)
{
    /* Initialize the touch controller hardware */
    touchpad_init();

    /* Start background touch polling task - does I2C reads independently
     * of the LVGL rendering thread so touch never blocks rendering.
     * Guard against creating a duplicate task if this is ever called twice. */
    if (touch_task_handle == NULL)
    {
        xTaskCreate(touch_poll_task, "TouchPoll",
                    TOUCH_TASK_STACK_SIZE, NULL,
                    TOUCH_TASK_PRIORITY, &touch_task_handle);
    }

    /* Register a touchpad input device - callback just reads cache (instant) */
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_timer_set_period(indev->read_timer, INDEV_READ_PERIOD_MS);
}


/* [] END OF FILE */
