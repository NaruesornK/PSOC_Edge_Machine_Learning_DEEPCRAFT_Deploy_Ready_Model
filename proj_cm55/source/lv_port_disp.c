/*******************************************************************************
* File Name        : lv_port_disp.c
*
* Description      : This file provides implementation of low level display
*                    device driver for LVGL.
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
#include "lv_draw_sw.h"
#include <stdbool.h>
#include <string.h>
#include "cy_graphics.h"


#if LV_COLOR_DEPTH == 16
    #define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#elif LV_COLOR_DEPTH == 32
    #define BYTE_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_ARGB8888))
#endif


/*******************************************************************************
* Global Variables
*******************************************************************************/
/* A second full-screen buffer is only needed for double-buffer configurations.
 * Single-buffer mode always uses one buffer - including single-buffer DIRECT
 * (partial) mode, where the live frame buffer the display controller is
 * scanning already holds the previous frame, so LVGL can redraw just the dirty
 * areas straight into it without a second buffer.
 *
 * Each full-screen buffer is MY_DISP_HOR_RES x MY_DISP_VER_RES x BYTE_PER_PIXEL.
 * For the default 4.3-inch display that is 832 x 480 x 2 = 798,720 B (~780 KB):
 *   - Double-buffer FULL/DIRECT : 2 buffers -> ~1560 KB
 *   - Single-buffer FULL/DIRECT : 1 buffer  -> ~780 KB  (~50% saving) */

CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN uint8_t disp_buf1[MY_DISP_HOR_RES *
                                               MY_DISP_VER_RES * BYTE_PER_PIXEL];
#if !USE_SINGLE_BUFFER_MODE
CY_SECTION(".cy_gpu_buf") LV_ATTRIBUTE_MEM_ALIGN uint8_t disp_buf2[MY_DISP_HOR_RES *
                                               MY_DISP_VER_RES * BYTE_PER_PIXEL];
#endif
/* Frame buffers used by GFXSS to render UI */
void *frame_buffer1 = &disp_buf1;
#if !USE_SINGLE_BUFFER_MODE
void *frame_buffer2 = &disp_buf2;
#else
void *frame_buffer2 = NULL;  /* Single-buffer mode: no second buffer */
#endif

cy_stc_gfx_context_t gfx_context;


/*******************************************************************************
* Function Name: disp_flush
********************************************************************************
* Summary:
*  Flush the content of the internal buffer to the specific area on the display.
*  You can use DMA or any hardware acceleration to do this operation in the
*  background but 'lv_display_flush_ready()' has to be called when finished.
*
* Parameters:
*  *disp_drv: Pointer to the display driver structure to be registered by HAL.
*  *area: Pointer to the area of the screen (not used).
*  *color_p: Pointer to the frame buffer address.
*
* Return:
*  void
*
*******************************************************************************/
static void LV_ATTRIBUTE_FAST_MEM disp_flush(lv_display_t *disp_drv,
                                             const lv_area_t *area,
                                             uint8_t *color_p)
{
    CY_UNUSED_PARAMETER(area);

#if USE_PARTIAL_RENDER_MODE
    /* DIRECT mode: LVGL calls disp_flush once per dirty area.
     * Only swap framebuffers on the LAST area - otherwise we would wait
     * for vsync N times per frame (one per dirty rectangle), killing FPS.
     * For intermediate areas, just signal ready immediately. */
    if(lv_display_flush_is_last(disp_drv))
    {
        /* Drain any stale vsync notification that arrived while LVGL was rendering.
         * Without this, the wait below can consume an OLD vsync that fired before
         * Set_FrameBuffer — meaning LVGL starts writing to the previous front buffer
         * while the DC is still reading it, causing visible flicker. */
        ulTaskNotifyTake(pdTRUE, 0);

        Cy_GFXSS_Set_FrameBuffer((GFXSS_Type*) GFXSS, (uint32_t*) color_p,
                &gfx_context);

        /* แก้จาก portMAX_DELAY: ถ้า DC interrupt ไม่ยิงด้วยเหตุใดก็ตาม
     * การรอแบบไม่มี timeout จะทำให้จอดำสนิทถาวรและหาสาเหตุยากมาก
     * ใส่เพดาน 100 ms ไว้ ภาพจะกระตุกแทนที่จะดับสนิท */
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
    /* Inform the graphics library that you are ready with the flushing */
    lv_display_flush_ready(disp_drv);
#else
    /* Drain any stale vsync notification that arrived while LVGL was rendering.
     * Without this, the wait below can consume an OLD vsync that fired before
     * Set_FrameBuffer - meaning LVGL starts writing to the previous front buffer
     * while the DC is still reading it, causing visible flicker. */
    ulTaskNotifyTake(pdTRUE, 0);

    Cy_GFXSS_Set_FrameBuffer((GFXSS_Type*) GFXSS, (uint32_t*) color_p,
            &gfx_context);

    /* รอ vsync แบบมีเพดาน 100 ms แต่ "ต้อง" เรียก flush_ready เสมอ
     * ไม่ว่าจะได้ notify หรือ timeout — ถ้าไม่เรียก LVGL จะถือว่ายังวาดไม่เสร็จ
     * แล้วค้างถาวร (ของเดิมครอบด้วย if ซึ่งปลอดภัยเฉพาะตอนใช้ portMAX_DELAY) */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    /* Inform the graphics library that you are ready with the flushing */
    lv_display_flush_ready(disp_drv);
#endif
}


/*******************************************************************************
* Function Name: lv_port_disp_init
********************************************************************************
* Summary:
*  Initialization function for display devices supported by LVGL.
*   LVGL requires a buffer where it internally draws the widgets.
*   Later this buffer will be passed to your display driver's `flush_cb` to copy
*   its content to your display.
*   The buffer has to be greater than 1 display row
*
*   There are 3 buffering configurations:
*   1. Create ONE buffer:
*      LVGL will draw the display's content here and writes it to your display
*
*   2. Create TWO buffer:
*      LVGL will draw the display's content to a buffer and writes it your
*      display.
*      You should use DMA to write the buffer's content to the display.
*      It will enable LVGL to draw the next part of the screen to the other
*      buffer while the data is being sent from the first buffer.
*      It makes rendering and flushing parallel.
*
*   3. Double buffering
*      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
*      This way LVGL will always provide the whole rendered screen in `flush_cb`
*      and you only need to change the frame buffer's address.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void lv_port_disp_init(void)
{
    memset(disp_buf1, 0, sizeof(disp_buf1));
#if !USE_SINGLE_BUFFER_MODE
    memset(disp_buf2, 0, sizeof(disp_buf2));
#endif

    lv_display_t * disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);

    lv_display_set_flush_cb(disp, disp_flush);

    lv_tick_set_cb(xTaskGetTickCount);

    /* Use explicit stride to prevent stride_is_auto recalculation.
     * Buffer is MY_DISP_HOR_RES(832) wide for VG-Lite 16px alignment,
     * but logical display is ACTUAL_DISP_HOR_RES(800).
     * Without explicit stride, lv_refr.c recalculates stride from buf_area width (800)
     * instead of buffer width (832), causing garbled display output. */
    uint32_t buf_stride = MY_DISP_HOR_RES * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_NATIVE);
#if USE_PARTIAL_RENDER_MODE
#if USE_SINGLE_BUFFER_MODE
    /* Single-buffer DIRECT mode: LVGL redraws only the invalidated (animated)
     * areas directly into the one frame buffer the display controller scans.
     * Static regions are never rewritten, so tearing is confined to the small
     * areas that actually change (e.g. the music-player spectrum/album art)
     * instead of the whole screen, while still using a single ~780 KB buffer. */
    lv_display_set_buffers_with_stride(disp, disp_buf1, NULL, sizeof(disp_buf1),
                                       buf_stride, LV_DISPLAY_RENDER_MODE_DIRECT);
#else
    lv_display_set_buffers_with_stride(disp, disp_buf1, disp_buf2, sizeof(disp_buf1),
                                       buf_stride, LV_DISPLAY_RENDER_MODE_DIRECT);
#endif
#elif USE_SINGLE_BUFFER_MODE
    /* Single-buffer FULL mode: pass NULL as the second buffer so LVGL renders
     * into the single frame buffer that the display controller scans out. */
    lv_display_set_buffers_with_stride(disp, disp_buf1, NULL, sizeof(disp_buf1),
                                       buf_stride, LV_DISPLAY_RENDER_MODE_FULL);
#else
    lv_display_set_buffers_with_stride(disp, disp_buf1, disp_buf2, sizeof(disp_buf1),
                                       buf_stride, LV_DISPLAY_RENDER_MODE_FULL);
#endif

    /* Set the display resolution to match the physical resolution so that
     * LVGL renders the UI within the limits of the actual display resolution.
     */
    lv_display_set_resolution(disp, ACTUAL_DISP_HOR_RES, ACTUAL_DISP_VER_RES);

    Cy_GFXSS_Clear_DC_Interrupt((GFXSS_Type*) GFXSS, &gfx_context);
}


/* [] END OF FILE */
