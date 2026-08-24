/*
 * camera_ai.h — กล้อง OV7675 + YOLOv5n object detection
 *
 * ผลลัพธ์เขียนลง babymate_shared.h:
 *   cam_pose        BM_POSE_NONE / SUPINE (หงาย) / PRONE (คว่ำ)
 *   cam_confidence  ความมั่นใจ 0..1
 *   cam_frames      นับเฟรมที่ประมวลผลแล้ว
 *   cam_infer_ms    เวลา inference ต่อเฟรม
 *   breath_alarm    1 = เห็นเด็กแต่เรดาร์ไม่เจอการหายใจ
 */
#ifndef CAMERA_AI_H
#define CAMERA_AI_H

#include "cy_result.h"

/* ตั้งค่าฮาร์ดแวร์กล้อง (I2C + DMA + XCLK)
 *
 * ⚠️ ต้องเรียกตอนที่ "ไม่มีใครใช้บัส I2C อยู่" เท่านั้น
 * ไดรเวอร์กล้องของ Infineon ใช้ SCB I2C API แบบ manual (SendStart/WriteByte)
 * ซึ่งใช้ร่วมกับ ISR ที่ไดรเวอร์จอ/ทัชต้องการไม่ได้ — ฟังก์ชันนี้จึงปิด ISR
 * ชั่วคราวระหว่างทำงาน จึงต้องเรียกก่อนสร้าง task ทัช
 *
 * ลำดับที่ถูกใน tft_hw_init():
 *   panel init -> camera_ai_hw_init() -> vg_lite_init -> lv_port_indev_init() */
cy_rslt_t camera_ai_hw_init(void);

/* สร้าง task ประมวลผลภาพ เรียกหลัง UI พร้อมแล้ว
 * หลัง init กล้องไม่แตะบัส I2C อีก (เฟรมมาทาง DMA) จึงไม่ชนกับจอ/ทัช */
cy_rslt_t create_camera_ai_task(void);

#endif /* CAMERA_AI_H */
