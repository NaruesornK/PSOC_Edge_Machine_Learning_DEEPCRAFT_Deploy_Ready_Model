#ifndef ENV_SENSOR_H
#define ENV_SENSOR_H

#include "cy_result.h"

// โครงสร้างข้อมูลที่ใช้เก็บค่าอากาศและการเคลื่อนไหว
typedef struct {
    float temperature;
    float humidity;
    float pm25;
    
    // ค่าความเอียงจาก IMU (แกน X, Y, Z)
    float accel_x;
    float accel_y;
    float accel_z;
    
    // สถานะสัญญาณชีพ (Micro-movement)
    uint8_t is_moving; // 1 = ขยับตัว/หายใจ, 0 = นิ่งสนิท (อันตราย)
    
} env_data_t;

// ฟังก์ชันสำหรับให้ main.c สั่งเริ่มทำงานเซนเซอร์ SHT40 (สร้าง Task)
void create_env_sensor_task(void);

// ฟังก์ชันสำหรับดึงค่าล่าสุดไปใช้งานที่อื่น (เช่น เอาไปโชว์ขึ้นจอ TFT)
env_data_t get_env_data(void);

#endif // ENV_SENSOR_H
