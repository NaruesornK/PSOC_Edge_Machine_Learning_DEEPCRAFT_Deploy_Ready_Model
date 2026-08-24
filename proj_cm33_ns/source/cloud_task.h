/*
 * cloud_task — WiFi + MQTT (ThingsBoard) บนคอร์ CM33 non-secure
 *
 * ทำไมต้องอยู่คอร์นี้: ชิป WiFi CYW55513 ต่อผ่าน SDIO และ Infineon ออกแบบให้
 * connectivity stack (WHD / lwIP / mbedTLS / secure-sockets) รันบน CM33-NS
 * ส่วนเซนเซอร์ทั้งหมดอยู่บน CM55 ⇒ ข้อมูลจริงต้องส่งข้ามคอร์ด้วย IPC
 * (ยังไม่ได้ทำ ตอนนี้ส่งค่าทดสอบไปก่อนเพื่อพิสูจน์ว่าท่อถึง ThingsBoard)
 */
#ifndef CLOUD_TASK_H
#define CLOUD_TASK_H

#include <stdbool.h>

/* สร้าง task — เรียกจาก main ก่อน vTaskStartScheduler */
void create_cloud_task(void);

/* true เมื่อ MQTT ต่อกับ broker อยู่ */
bool cloud_is_connected(void);

#endif /* CLOUD_TASK_H */
