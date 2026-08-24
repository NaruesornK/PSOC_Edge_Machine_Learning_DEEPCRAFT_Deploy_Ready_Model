/*
 * tft_screen.h — จอสัมผัส Waveshare 4.3" DSI (800x480) บน KIT_PSE84_AI
 *
 * ต่อที่ J10 (RPi MIPI DSI, FPC 15 pin) ไม่ต้อง rework
 *   วิดีโอ : MIPI-DSI 1 lane @ 810 Mbps ผ่าน GFXSS
 *   คุมจอ  : I2C 0x45  (บน SCB5 = CYBSP_I2C_DISPLAY_CONTROLLER)
 *   ทัช    : I2C 0x38  (FT5406, บัสเดียวกัน)
 *
 * ทั้ง task รันบน CM55 คู่กับเซนเซอร์ตัวอื่น อ่านค่าจาก babymate_shared.h
 * โดยตรง ไม่ต้องผ่าน queue เพราะอยู่คอร์เดียวกัน
 */
#ifndef TFT_SCREEN_H
#define TFT_SCREEN_H

#include "cy_result.h"

/* สร้าง task ของจอ เรียกครั้งเดียวจาก main() ก่อน vTaskStartScheduler()
 * ตัวจอจะ init ตัวเองข้างใน task เพราะ mtb_disp_waveshare_4p3_init()
 * ต้องการ RTOS delay ระหว่างขั้นตอน */
cy_rslt_t create_tft_task(void);

#endif /* TFT_SCREEN_H */
