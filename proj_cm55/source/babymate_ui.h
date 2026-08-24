/*
 * babymate_ui.h — หน้าจอ LVGL ของ BabyMate (800x480 ทัชสกรีน)
 *
 * 4 หน้า สลับด้วยปุ่มแถบล่าง:
 *   HOME   ค่าหลัก 4 ช่อง + แถบเตือน
 *   BABY   ผล AI จับเสียงร้อง + สาเหตุที่วิเคราะห์ได้
 *   VITAL  เรดาร์วัดการหายใจ
 *   SETUP  ปุ่ม/สไลเดอร์ควบคุม เขียนกลับ shared block
 *
 * ข้อความบนจอเป็นภาษาอังกฤษล้วน เพราะ LVGL เรนเดอร์ภาษาไทยไม่ถูก
 * (สระบน-ล่างกับวรรณยุกต์ต้องใช้ shaping engine ซึ่ง LVGL ไม่มี)
 */
#ifndef BABYMATE_UI_H
#define BABYMATE_UI_H

/* สร้าง widget ทั้งหมด เรียกครั้งเดียวหลัง lv_init() + lv_port_disp_init() */
void babymate_ui_create(void);

/* อ่านค่าล่าสุดจาก shared block แล้วอัปเดตข้อความ/สีบนจอ
 * เรียกเป็นรอบจาก tft_task (ทุก 500 ms) */
void babymate_ui_refresh(void);

#endif /* BABYMATE_UI_H */
