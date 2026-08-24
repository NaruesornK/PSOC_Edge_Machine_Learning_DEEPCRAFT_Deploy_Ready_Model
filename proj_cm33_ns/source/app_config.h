/*
 * ค่าตั้งของ WiFi และ ThingsBoard
 *
 * ⚠️ ไฟล์นี้มีรหัสผ่านและ access token อยู่ ถ้าเอาโปรเจกต์ขึ้น git
 *    ให้ใส่ชื่อไฟล์นี้ใน .gitignore ก่อน
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------------- WiFi ----------------
 * บอร์ดนี้รับ 2.4 GHz เท่านั้น ถ้าใช้ hotspot มือถือต้องตั้งเป็น 2.4 GHz */
#define WIFI_SSID                   "KONG"
#define WIFI_PASSWORD               "22222222"
#define WIFI_SECURITY               CY_WCM_SECURITY_WPA2_AES_PSK

/* ลองต่อ WiFi กี่ครั้งก่อนยอมแพ้แล้ววนใหม่ */
#define WIFI_MAX_RETRIES            8u
#define WIFI_RETRY_DELAY_MS         5000u

/* ---------------- ThingsBoard ----------------
 * ยืนยันจาก https://thingsboard.io/docs/reference/mqtt-api/
 *   username = device access token
 *   password = เว้นว่าง
 *   topic    = v1/devices/me/telemetry
 *   payload  = JSON เช่น {"temperature":25}
 */
#define TB_HOSTNAME                 "thingsboard.cloud"

/* 1883 = ธรรมดา · 8883 = TLS (ยังไม่เปิด ดู MQTT_USE_TLS) */
#define TB_PORT                     1883

#define TB_ACCESS_TOKEN             "VcYaZlahSC57UjVWWcEY"

#define TB_TELEMETRY_TOPIC          "v1/devices/me/telemetry"

/* ---- topic สำหรับรับคำสั่งจาก ThingsBoard ----
 *
 * ThingsBoard มีสองช่องทางสั่งงาน ใช้ทั้งคู่เพื่อให้ยืดหยุ่นตอนทำ dashboard:
 *
 * 1. RPC — ปุ่ม/สวิตช์บน dashboard ที่ตั้งเป็น RPC widget
 *    ขาเข้า : v1/devices/me/rpc/request/{requestId}
 *             payload = {"method":"setPower","params":true}
 *    ขาตอบ  : v1/devices/me/rpc/response/{requestId}
 *
 * 2. Shared attributes — แก้ได้จากหน้า Attributes ของ device
 *    ขาเข้า : v1/devices/me/attributes   payload = {"power":true}
 *
 * เราส่งสถานะปัจจุบันกลับขึ้น v1/devices/me/attributes ด้วย เพื่อให้ปุ่มบน
 * dashboard แสดงสถานะจริงของเครื่อง ไม่ใช่ค้างตามที่กดครั้งสุดท้าย */
#define TB_ATTR_TOPIC               "v1/devices/me/attributes"
#define TB_RPC_REQUEST_SUB          "v1/devices/me/rpc/request/+"
#define TB_RPC_RESPONSE_PREFIX      "v1/devices/me/rpc/response/"

/* ตั้ง 1 เมื่อพร้อมเปิด TLS (ต้องเปลี่ยน TB_PORT เป็น 8883 และฝัง root CA ด้วย)
 * ตอนนี้ปิดไว้ก่อนเพื่อให้แยกออกว่าถ้าต่อไม่ติดเป็นเพราะ WiFi, MQTT หรือ certificate */
#define MQTT_USE_TLS                0

/* ชื่อ client ที่ broker เห็น จะซ้ำกับใครไม่ได้ */
#define MQTT_CLIENT_ID              "babymate2"

#define MQTT_KEEPALIVE_SEC          60u

/* ส่ง telemetry ทุกกี่มิลลิวินาที */
#define TELEMETRY_PERIOD_MS         20000u

#endif /* APP_CONFIG_H */
