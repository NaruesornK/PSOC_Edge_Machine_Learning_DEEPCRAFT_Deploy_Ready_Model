/*
 * babymate_control — API สั่งเปิด/ปิด สำหรับเรียกจากฝั่ง CM55 (เช่นปุ่มบนจอ TFT)
 *
 * ตัวแปรควบคุมตัวจริงอยู่ในบล็อกร่วม (babymate_shared.h) ซึ่ง ThingsBoard
 * ก็เขียนได้เหมือนกันผ่าน CM33 ⇒ กดจากจอกับสั่งจาก dashboard คุมตัวเดียวกัน
 * ใครสั่งทีหลังชนะ และ control_seq จะเพิ่มขึ้นทุกครั้งเพื่อให้ CM33 รู้ว่ามีการ
 * เปลี่ยนแปลง แล้วรีบส่งสถานะล่าสุดกลับขึ้น dashboard
 *
 * หมายเหตุ: ตอนนี้ tft_screen.c ยังเป็น stub (ไม่มีไดรเวอร์จอและ touch จริง)
 * ไฟล์นี้เตรียมไว้ให้เรียกได้ทันทีเมื่อทำจอเสร็จ
 */
#ifndef BABYMATE_CONTROL_H
#define BABYMATE_CONTROL_H

#include <stdbool.h>
#include "babymate_shared.h"

/* สั่งเปิด/ปิดด้วยมือ — การสั่งมือจะปิดโหมด auto ให้อัตโนมัติ
 * เพราะถ้าไม่ปิด ผู้ใช้จะกดแล้วเห็นมันเด้งกลับเองภายในไม่กี่วินาที */
static inline void babymate_set_power(bool on)
{
    volatile babymate_shared_t *s = bm_shared();
    s->power_on  = on ? 1u : 0u;
    s->auto_mode = 0u;
    s->output_on = s->power_on;
    s->control_seq++;
}

/* สลับสถานะ — เหมาะกับปุ่มกดบนจอ */
static inline void babymate_toggle_power(void)
{
    babymate_set_power(bm_shared()->power_on ? false : true);
}

/* เปิด/ปิดโหมด auto ตามอุณหภูมิ */
static inline void babymate_set_auto(bool on)
{
    volatile babymate_shared_t *s = bm_shared();
    s->auto_mode = on ? 1u : 0u;
    s->control_seq++;
}

/* ตั้งอุณหภูมิจุดตัด — on ต้องมากกว่า off เสมอ ไม่งั้นจะเปิดปิดรัวจนพัง */
static inline void babymate_set_temp_thresholds(float on_c, float off_c)
{
    volatile babymate_shared_t *s = bm_shared();
    if (on_c <= off_c) off_c = on_c - 1.0f;   /* กันตั้งค่ากลับด้าน */
    s->temp_on_c  = on_c;
    s->temp_off_c = off_c;
    s->control_seq++;
}

/* ตั้งจุดตัดค่าฝุ่น PM2.5 (ug/m3) กติกาเดียวกับอุณหภูมิ */
static inline void babymate_set_pm25_thresholds(float on_v, float off_v)
{
    volatile babymate_shared_t *s = bm_shared();
    if (on_v <= off_v) off_v = on_v - 1.0f;
    s->pm25_on  = on_v;
    s->pm25_off = off_v;
    s->control_seq++;
}

/* เลือกว่าโหมด auto จะอิงอะไร — อุณหภูมิ หรือ ค่าฝุ่น PM2.5
 * จุดตัดของสองแหล่งเก็บแยกกัน สลับไปมาแล้วค่าที่ตั้งไว้ไม่หาย */
static inline void babymate_set_auto_source(uint8_t src)
{
    volatile babymate_shared_t *s = bm_shared();
    s->auto_source = (src == BM_AUTO_SRC_PM25) ? BM_AUTO_SRC_PM25 : BM_AUTO_SRC_TEMP;
    s->control_seq++;
}

/* อ่านสถานะไปแสดงบนจอ */
static inline bool    babymate_is_on(void)           { return bm_shared()->output_on != 0u; }
static inline bool    babymate_is_auto(void)         { return bm_shared()->auto_mode != 0u; }
static inline uint8_t babymate_get_auto_source(void) { return bm_shared()->auto_source; }

/* ชื่อแหล่งไว้พิมพ์บนจอ */
static inline const char *babymate_auto_source_name(void)
{
    return (bm_shared()->auto_source == BM_AUTO_SRC_PM25) ? "PM2.5" : "Temp";
}

#endif /* BABYMATE_CONTROL_H */
