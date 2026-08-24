/*******************************************************************************
* File Name        : lv_port_disp.h
*
* Description      : This file provides constants and function prototypes
*                    for configuring low level display driver in LVGL.
*
* Related Document : See README.md
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
******************************************************************************/

#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#ifdef __cplusplus
extern "C" {
#endif


/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cybsp.h"
#include "cy_pdl.h"
#include "cycfg.h"

#include "lvgl.h"


/*******************************************************************************
* Macros
*******************************************************************************/
/* Set to 1 to use LVGL DIRECT (partial) render mode, where LVGL redraws only
 * the dirty areas of the frame buffer each refresh. Set to 0 to use FULL
 * render mode, where the whole screen is rendered every frame.
 *
 * Buffer usage now follows USE_SINGLE_BUFFER_MODE (below) in both render modes:
 *   - DIRECT + double-buffer : 2 buffers; saves render/blit time, not RAM.
 *   - DIRECT + single-buffer : 1 buffer (~780 KB, ~50% saving). LVGL redraws
 *     only the changed regions straight into the live frame buffer the display
 *     controller is scanning. Because static areas are never rewritten, any
 *     tearing is confined to the small animated regions (e.g. the music-player
 *     spectrum) instead of the whole screen - the recommended single-buffer
 *     setting for mostly-static UIs such as the music-player demo.
 * Each full-screen buffer is MY_DISP_HOR_RES x MY_DISP_VER_RES x 2 bytes; for
 * the default 4.3-inch display that is 832x480x2 = ~780 KB per buffer. */
#define USE_PARTIAL_RENDER_MODE (0U)

/*******************************************************************************
* RAM optimisation mode (each full-screen buffer is MY_DISP_HOR_RES x
* MY_DISP_VER_RES x 2 bytes; for the default 4.3-inch display that is
* 832x480x2 = ~780 KB per buffer).
*
*  USE_SINGLE_BUFFER_MODE 0 (default)
*    Double-buffer. Two full-screen frame buffers (~1560 KB total). LVGL renders
*    into the back buffer while the display controller scans out the front
*    buffer, then the buffers are swapped on vsync. No tearing.
*
*  USE_SINGLE_BUFFER_MODE 1
*    Single-buffer (~780 KB, ~50% RAM saving). LVGL renders into the same buffer
*    the display controller is scanning out and waits for vsync before reusing
*    it. Tearing behaviour depends on the render mode:
*      - With FULL mode (USE_PARTIAL_RENDER_MODE 0) the whole screen is rewritten
*        every frame, so tearing can be visible across the entire panel on any
*        animation.
*      - With DIRECT mode (USE_PARTIAL_RENDER_MODE 1, recommended) only the
*        changed regions are rewritten, so tearing is limited to the small
*        animated areas (e.g. the music-player spectrum) while the static parts
*        of the UI stay rock-solid.
*******************************************************************************/
/* กลับมาใช้ double buffer ได้แล้ว หลังย้าย weight ของ YOLO ไป flash (XIP)
 * ซึ่งคืน SOCMEM ให้ 1,682,000 B — ไม่ต้องแลกกับภาพฉีกอีกต่อไป */
#define USE_SINGLE_BUFFER_MODE    (0U)

#if defined(MTB_DISPLAY_W4P3INCH_RPI)
#define MY_DISP_VER_RES                              (480U)
#define MY_DISP_HOR_RES                              (832U)
#define ACTUAL_DISP_VER_RES                          (480U)
#define ACTUAL_DISP_HOR_RES                          (800U)
#elif defined(MTB_DISPLAY_R4INCH_TFT)
#define MY_DISP_VER_RES                              (480U)
#define MY_DISP_HOR_RES                              (512U)
#define ACTUAL_DISP_VER_RES                          (480U)
#define ACTUAL_DISP_HOR_RES                          (480U)
#else
#define MY_DISP_VER_RES                              (600U)
#define MY_DISP_HOR_RES                              (1024U)
#define ACTUAL_DISP_VER_RES                          (600U)
#define ACTUAL_DISP_HOR_RES                          (1024U)
#endif
extern cy_stc_gfx_context_t gfx_context;
extern void *frame_buffer1;
extern void *frame_buffer2;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
/* Initialize low level display driver */
void lv_port_disp_init(void);


#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* LV_PORT_DISP_H */


/* [] END OF FILE */
