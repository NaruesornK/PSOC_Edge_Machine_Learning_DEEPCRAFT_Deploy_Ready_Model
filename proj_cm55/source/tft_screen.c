/*
 * tft_screen.c — ปลุกจอ Waveshare 4.3" DSI + LVGL แล้วรัน UI
 *
 * ลำดับการปลุกยกมาจาก mtb-example-psoc-edge-gfx-lvgl-demo ของ Infineon
 * (ตัวอย่างที่ทดสอบกับ KIT_PSE84_AI + จอรุ่นนี้มาแล้ว) ตัดส่วน demo ออก
 * แล้วใส่ UI ของ BabyMate แทน
 *
 * ลำดับสำคัญมาก ห้ามสลับ:
 *   1. Cy_GFXSS_Init          ตั้ง DC + MIPI-DSI + GPU
 *   2. ต่อ interrupt DC/GPU
 *   3. เปิด I2C (SCB5)        จอกับทัชอยู่บัสนี้ทั้งคู่
 *   4. mtb_disp_waveshare_4p3_init  เขียน reg 0x81/0x85 ที่ addr 0x45 = ปลุก panel
 *   5. vg_lite_init           GPU 2D
 *   6. lv_init / disp / indev
 *   7. babymate_ui_create     สร้างหน้าจอ
 */
#include "tft_screen.h"

#include "cybsp.h"
#include "cy_pdl.h"
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "vg_lite.h"
#include "vg_lite_platform.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "display_i2c_config.h"
#include "mtb_disp_dsi_waveshare_4p3.h"

#include "babymate_ui.h"
#include "camera_ai.h"

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define TFT_TASK_NAME               ("TFT")
#define TFT_TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 16)
#define TFT_TASK_PRIORITY           (configMAX_PRIORITIES - 3)

#define GPU_INT_PRIORITY            (3U)
#define DC_INT_PRIORITY             (3U)
#define I2C_CONTROLLER_IRQ_PRIORITY (2UL)

#define APP_BUFFER_COUNT            (2U)
#define DEFAULT_GPU_CMD_BUFFER_SIZE ((64U) * (1024U))
#define GPU_TESSELLATION_BUFFER_SIZE ((MY_DISP_VER_RES) * 128U)

/* หน่วยความจำที่ VGLite ใช้ทำ command buffer + tessellation buffer
 * อยู่ใน .cy_gpu_buf ซึ่ง linker map ไปที่ region gfx_mem (SOCMEM 0x2633C000)
 * ก้อนนี้บวกกับ framebuffer 2 ใบใน lv_port_disp.c ต้องไม่เกิน 0x1C4000 */
#define VGLITE_HEAP_SIZE            (((DEFAULT_GPU_CMD_BUFFER_SIZE) * (APP_BUFFER_COUNT)) + \
                                     ((GPU_TESSELLATION_BUFFER_SIZE) * (APP_BUFFER_COUNT)))

#define GPU_MEM_BASE                (0x0U)
#define VG_PARAMS_POS               (0UL)

/* จอรีเฟรชทุก 500 ms ก็พอ เซนเซอร์อัปเดตช้ากว่านั้นอยู่แล้ว
 * ตั้งถี่กว่านี้ = กิน CPU ฟรี ๆ แย่งเวลา AI จับเสียงร้อง */
#define UI_REFRESH_MS               (500U)

/* ---- การกลับภาพกล้อง OV7675 ----
 * register 0x1E (MVFP):  bit5 (0x20) = mirror ซ้าย-ขวา · bit4 (0x10) = vflip บน-ล่าง
 *
 * ไดรเวอร์ของ Infineon ตั้ง mirror (0x20) ไว้ตายตัวตอน init
 * ตัวนี้เขียนทับทีหลังเพื่อเพิ่ม vflip เข้าไปด้วย = ภาพกลับบน-ล่าง
 *
 * ถ้าทิศทางยังไม่ถูก เปลี่ยนเลขนี้ค่าเดียวแล้ว build ใหม่:
 *   0x00 ไม่กลับ · 0x10 บน-ล่าง · 0x20 ซ้าย-ขวา · 0x30 กลับทั้งสองแกน */
#define OV7675_MVFP_REG_ADDR        (0x1Eu)
#define CAM_MVFP_VALUE              (0x30u)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
/* VGLite heap — ต้องอยู่ใน gfx_mem ไม่ใช่ SRAM ปกติ GPU เข้าถึงไม่ได้ */
CY_SECTION(".cy_gpu_buf") static uint8_t contiguous_mem[VGLITE_HEAP_SIZE] = {0xFFu};
static volatile void *vglite_heap_base = &contiguous_mem;

static TaskHandle_t tft_task_handle = NULL;

/* lv_port_indev.c ประกาศ extern ตัวนี้ไว้ ต้องนิยามที่นี่ */
cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

static const cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc      = GFXSS_DC_IRQ,
    .intrPriority = DC_INT_PRIORITY
};

static const cy_stc_sysint_t gpu_irq_cfg =
{
    .intrSrc      = GFXSS_GPU_IRQ,
    .intrPriority = GPU_INT_PRIORITY
};

static const cy_stc_sysint_t disp_touch_i2c_irq_cfg =
{
    .intrSrc      = DISPLAY_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY
};

/*******************************************************************************
 * Interrupt handlers
 ******************************************************************************/
static void dc_irq_handler(void)
{
    BaseType_t woken = pdFALSE;

    Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &gfx_context);

    if (NULL != tft_task_handle)
    {
        xTaskNotifyFromISR(tft_task_handle, 1, eSetValueWithOverwrite, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void gpu_irq_handler(void)
{
    Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
    vg_lite_IRQHandler();
}

static void disp_touch_i2c_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(DISPLAY_I2C_CONTROLLER_HW,
                         &disp_touch_i2c_controller_context);
}

/*******************************************************************************
 * Function Name: tft_hw_init
 ********************************************************************************
 * ปลุกฮาร์ดแวร์ทั้งชุด คืน false ถ้าพังขั้นไหนก็ตาม
 * พิมพ์บอกทุกขั้นเพื่อให้ดู log แล้วรู้ทันทีว่าค้างตรงไหน
 *******************************************************************************/
static bool tft_hw_init(void)
{
    cy_en_gfx_status_t     gfx_status;
    cy_en_sysint_status_t  sysint_status;
    cy_en_scb_i2c_status_t i2c_status;
    vg_lite_error_t        vglite_status;

    /* ---- 1. GFXSS (DC + MIPI-DSI + GPU) ---- */
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;

    /* ⚠️ ต้องเปิด DC interrupt เอง — Device Configurator ไม่ได้ generate ให้
     *
     * GFXSS_dc_config ใน cycfg_peripherals.c ไม่มีฟิลด์ .interrupt_mask
     * (personality ไม่เปิดให้ตั้ง) มันเลยเป็น 0 = ไม่มี interrupt ยิงเลย
     *
     * ผลคือ disp_flush() ใน lv_port_disp.c ที่รอ vsync ด้วย
     * ulTaskNotifyTake(portMAX_DELAY) จะค้างตั้งแต่เฟรมแรก
     * -> LVGL วาดเสร็จแต่ไม่มีอะไรขึ้นจอ = จอดำสนิท */
    GFXSS_config.dc_cfg->interrupt_mask = GFXSS_DC_DISP0_INT_MASK;

    GFXSS_config.dc_cfg->gfx_layer_config->width  = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->gfx_layer_config->height = MY_DISP_VER_RES;
    GFXSS_config.dc_cfg->display_width            = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->display_height           = MY_DISP_VER_RES;

    GFXSS_config.dc_cfg->gfx_layer_config->buffer_address    = frame_buffer1;
    GFXSS_config.dc_cfg->gfx_layer_config->uv_buffer_address = frame_buffer1;

    gfx_status = Cy_GFXSS_Init(GFXSS, &GFXSS_config, &gfx_context);
    if (CY_GFX_SUCCESS != gfx_status)
    {
        printf("[TFT] Cy_GFXSS_Init failed (%d)\n", (int)gfx_status);
        return false;
    }
    printf("[TFT] GFXSS ok (%ux%u, DSI 1 lane)\n",
           (unsigned)MY_DISP_HOR_RES, (unsigned)MY_DISP_VER_RES);

    /* ---- 2. interrupt ของ DC และ GPU ---- */
    sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);
    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("[TFT] DC interrupt init failed (%d)\n", (int)sysint_status);
        return false;
    }
    NVIC_EnableIRQ(GFXSS_DC_IRQ);

    sysint_status = Cy_SysInt_Init(&gpu_irq_cfg, gpu_irq_handler);
    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("[TFT] GPU interrupt init failed (%d)\n", (int)sysint_status);
        return false;
    }
    Cy_GFXSS_Enable_GPU_Interrupt(GFXSS);
    NVIC_EnableIRQ(GFXSS_GPU_IRQ);

    /* ---- 3. I2C บัสจอ (SCB5) — ใช้ร่วมกันทั้งตัวคุม panel และทัช ---- */
    i2c_status = Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW,
                                 &DISPLAY_I2C_CONTROLLER_config,
                                 &disp_touch_i2c_controller_context);
    if (CY_SCB_I2C_SUCCESS != i2c_status)
    {
        printf("[TFT] display I2C init failed (%d)\n", (int)i2c_status);
        return false;
    }

    sysint_status = Cy_SysInt_Init(&disp_touch_i2c_irq_cfg, disp_touch_i2c_interrupt);
    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("[TFT] display I2C interrupt init failed (%d)\n", (int)sysint_status);
        return false;
    }
    NVIC_EnableIRQ(disp_touch_i2c_irq_cfg.intrSrc);
    Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

    /* ให้จอตั้งไฟให้นิ่งก่อน ตัวอย่างของ Infineon ก็รอ 500 ms เท่านี้ */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ---- 4. ปลุก panel ผ่าน I2C 0x45 ---- */
    i2c_status = mtb_disp_waveshare_4p3_init(DISPLAY_I2C_CONTROLLER_HW,
                                             &disp_touch_i2c_controller_context);
    if (CY_SCB_I2C_SUCCESS != i2c_status)
    {
        printf("[TFT] panel init failed (%d) - เช็คสาย FPC ที่ J10\n", (int)i2c_status);
        return false;
    }
    printf("[TFT] panel awake (Waveshare 4.3\" @ I2C 0x45)\n");

    /* ตั้งค่ากล้องตรงนี้ ก่อน lv_port_indev_init() จะสร้าง task ทัช
     *
     * ไดรเวอร์กล้องใช้ SCB I2C API แบบ manual ซึ่งใช้ร่วมกับ ISR ไม่ได้
     * camera_ai_hw_init() จึงปิด ISR ชั่วคราว — ช่วงนั้นห้ามมีใครใช้บัส
     * ถ้าย้ายไปทำหลัง task ทัชเกิดแล้ว จะชนกันเพราะทัช poll ทุก 33 ms */
    (void)camera_ai_hw_init();

    /* ---- 5. GPU 2D ---- */
    {
        vg_module_parameters_t vg_params;

        vg_params.register_mem_base                  = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
        vg_params.gpu_mem_base[VG_PARAMS_POS]        = GPU_MEM_BASE;
        vg_params.contiguous_mem_base[VG_PARAMS_POS] = (void *)vglite_heap_base;
        vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;

        vg_lite_init_mem(&vg_params);
    }

    vglite_status = vg_lite_init(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    if (VG_LITE_SUCCESS != vglite_status)
    {
        printf("[TFT] vg_lite_init failed (%d)\n", (int)vglite_status);
        vg_lite_close();
        return false;
    }

    /* ---- 6. LVGL ---- */
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    printf("[TFT] LVGL ready\n");

    return true;
}

/*******************************************************************************
 * Function Name: tft_task
 *******************************************************************************/
static void tft_task(void *arg)
{
    (void)arg;

    uint32_t next_ui_refresh = 0u;

    if (!tft_hw_init())
    {
        printf("[TFT] !! display disabled, task idle !!\n");
        vTaskDelete(NULL);
        return;
    }

    babymate_ui_create();
    printf("[TFT] UI up\n");

    /* กล้องต้องเริ่มหลังจอ เพราะใช้ I2C SCB5 ตัวเดียวกันและใช้ VGLite
     * ที่ tft_hw_init() เป็นคนเปิด ถ้าเริ่มก่อนจะ init ซ้ำแล้วบัสพัง */
    if (create_camera_ai_task() != CY_RSLT_SUCCESS)
    {
        printf("[TFT] !! create_camera_ai_task FAILED\n");
    }

    for (;;)
    {
        const uint32_t now = (uint32_t)xTaskGetTickCount();

        /* อัปเดตค่าบนจอเป็นรอบ ๆ แยกจากรอบวาดของ LVGL
         * เพื่อไม่ให้ไปกวน animation กับการตอบสนองของทัช */
        if ((int32_t)(now - next_ui_refresh) >= 0)
        {
            babymate_ui_refresh();
            next_ui_refresh = now + pdMS_TO_TICKS(UI_REFRESH_MS);
        }

        /* lv_timer_handler บอกเองว่าอีกกี่ ms ค่อยเรียกอีกที
         * จำกัดไม่ให้เกิน 30 ms กันไม่ให้ทัชหน่วงจนรู้สึกได้ */
        uint32_t till_next = lv_timer_handler();
        if (till_next > 30u)
        {
            till_next = 30u;
        }
        vTaskDelay(pdMS_TO_TICKS(till_next));
    }
}

/*******************************************************************************
 * Function Name: create_tft_task
 *******************************************************************************/
cy_rslt_t create_tft_task(void)
{
    const BaseType_t rc = xTaskCreate(tft_task, TFT_TASK_NAME,
                                      TFT_TASK_STACK_SIZE, NULL,
                                      TFT_TASK_PRIORITY, &tft_task_handle);

    return (pdPASS == rc) ? CY_RSLT_SUCCESS : (cy_rslt_t)~CY_RSLT_SUCCESS;
}
