#include "env_sensor.h"
#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <math.h>

// นำเข้า Library ของ IMU (BMI270)
#include "mtb_bmi270.h"
#include "babymate_shared.h"


#define SHT40_ADDR  0x44

// ตัวแปรเก็บค่าล่าสุด 
static env_data_t current_env_data = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

// ตัวแปรสำหรับ I2C และ IMU
static cy_stc_scb_i2c_context_t env_i2c_context;
static mtb_hal_i2c_t env_hal_i2c_obj;
static mtb_bmi270_t bmi270;
static mtb_bmi270_data_t bmi270_data;

// สถานะที่ส่งขึ้น cloud
static uint8_t s_fallen_over = 0u;
static float   s_tilt_deg    = 0.0f;
static float   s_vibration_g = 0.0f;   /* peak-to-peak ของ |a| ในหน้าต่าง 0.5 วิ */

// ========================================================
// ฟังก์ชันอ่านค่าอุณหภูมิ (SHT40)
// ========================================================
static void read_sht40(void) {
    uint8_t cmd_sht40 = 0xFD; 
    uint8_t sht_data[6] = {0};
    uint32_t timeout = 100;

    if (Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW, SHT40_ADDR, CY_SCB_I2C_WRITE_XFER, timeout, &env_i2c_context) == CY_SCB_I2C_SUCCESS) {
        Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, cmd_sht40, timeout, &env_i2c_context);
        Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, timeout, &env_i2c_context);
    } else {
        Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, timeout, &env_i2c_context);
        printf("[SHT40] Error: Sensor not responding.\n");
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(15)); 
    
    if (Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW, SHT40_ADDR, CY_SCB_I2C_READ_XFER, timeout, &env_i2c_context) == CY_SCB_I2C_SUCCESS) {
        for(int i=0; i<5; i++) {
            Cy_SCB_I2C_MasterReadByte(CYBSP_I2C_CONTROLLER_HW, CY_SCB_I2C_ACK, &sht_data[i], timeout, &env_i2c_context);
        }
        Cy_SCB_I2C_MasterReadByte(CYBSP_I2C_CONTROLLER_HW, CY_SCB_I2C_NAK, &sht_data[5], timeout, &env_i2c_context);
        Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, timeout, &env_i2c_context);
        
        uint16_t t_ticks = (sht_data[0] << 8) | sht_data[1];
        uint16_t rh_ticks = (sht_data[3] << 8) | sht_data[4];
        
        float raw_temp = -45.0f + (175.0f * ((float)t_ticks / 65535.0f));
        /* ชดเชยความร้อนที่บอร์ดคายออกมาเอง (self-heating)
         * SHT40 อยู่บนบอร์ดเดียวกับ WiFi/เรดาร์/จอ ซึ่งอุ่นตลอดเวลา
         * ค่าที่วัดได้จึงสูงกว่าอุณหภูมิห้องจริง
         *
         * เดิมตั้งไว้ -7.0 แล้วยังสูงเกิน จึงปรับเป็น -15.0 ตามที่วัดเทียบจริง
         * ถ้าย้ายเซนเซอร์ออกนอกบอร์ดเมื่อไหร่ ต้องลดค่านี้ลงตาม */
        current_env_data.temperature = raw_temp - 15.0f;
        current_env_data.humidity = -6.0f + (125.0f * ((float)rh_ticks / 65535.0f));
        
        printf("[SHT40] Temp: %d C | Humidity: %d %%\n", (int)current_env_data.temperature, (int)current_env_data.humidity);
    }
}

// ========================================================
// ฟังก์ชันอ่านความเอียง (IMU - BMI270)
// ========================================================
static void read_imu(void) {
    if (mtb_bmi270_read(&bmi270, &bmi270_data) == CY_RSLT_SUCCESS) {
        if (bmi270_data.sensor_data.status & BMI2_DRDY_ACC) {
            // 1. แปลงค่าดิบให้เป็นหน่วย G (แรงโน้มถ่วง)
            float ax = (float)bmi270_data.sensor_data.acc.x / 4096.0f;
            float ay = (float)bmi270_data.sensor_data.acc.y / 4096.0f;
            float az = (float)bmi270_data.sensor_data.acc.z / 4096.0f;
            
            current_env_data.accel_x = ax;
            current_env_data.accel_y = ay;
            current_env_data.accel_z = az;
            
            // 2. คำนวณแรงกระแทก (Impact / Fall)
            float total_g = sqrtf((ax*ax) + (ay*ay) + (az*az));
            
            if (total_g > 2.5f) { 
                printf("[IMU] ! FALL DETECTED ! (Impact: %.1f G)\n", total_g);
            }
            
            // 3. คำนวณความเอียง (Tilt Angle / Fallen Over)
            static int tilt_alert_cooldown = 0;

            if (total_g > 0.5f && total_g < 1.5f) {
                if (ay > -0.4f) {
                    s_fallen_over = 1u;
                    if (tilt_alert_cooldown == 0) {
                        printf("[IMU] ! DEVICE FALLEN OVER ! \n");
                        tilt_alert_cooldown = 150;
                    }
                } else {
                    s_fallen_over = 0u;
                    tilt_alert_cooldown = 0;
                }
            }
            if (tilt_alert_cooldown > 0) tilt_alert_cooldown--;

            /* มุมเอียงจากแนวตั้ง: ปกติแกน Y ชี้ลงพื้น (ay ≈ -1 g เมื่อตั้งตรง)
             * acos ของสัดส่วนนั้นให้องศาที่เบนออกจากแนวตั้ง 0° = ตั้งตรงสนิท */
            if (total_g > 0.1f) {
                float c = -ay / total_g;
                if (c >  1.0f) c =  1.0f;
                if (c < -1.0f) c = -1.0f;
                s_tilt_deg = acosf(c) * (180.0f / 3.14159265f);
            }
            

            /* 3.5 ความสั่นสะเทือน — หน้าต่างสั้น 0.5 วิ (25 รอบ × 20 ms)
             *
             * แยกออกจากตัวนับ 6 วินาทีข้างล่างเพราะคนละงานกัน:
             *   ตัวนี้        = ดูว่า "ตอนนี้" สั่นแค่ไหน เอาไปโชว์บนจอแบบสด
             *   ตัว 6 วินาที  = ดูว่าเด็กขยับตัวไหม ต้องใช้หน้าต่างยาวถึงจะแม่น
             *
             * ใช้ peak-to-peak เพื่อตัดแรงโน้มถ่วง 1 g ที่ค้างอยู่ในสัญญาณออก */
            {
                static float vib_max   = 0.0f;
                static float vib_min   = 999.0f;
                static int   vib_timer = 0;

                if (total_g > vib_max) vib_max = total_g;
                if (total_g < vib_min) vib_min = total_g;

                if (++vib_timer >= 25) {
                    s_vibration_g = vib_max - vib_min;
                    vib_max   = 0.0f;
                    vib_min   = 999.0f;
                    vib_timer = 0;
                }
            }

            // 4. สแกนสัญญาณชีพ (Micro-movement / Breathing)
            static float max_g = 0.0f;
            static float min_g = 999.0f;
            static int move_timer = 0;
            static int no_movement_streak = 0; // นับว่าไม่ขยับมากี่รอบแล้ว
            
            if (total_g > max_g) max_g = total_g;
            if (total_g < min_g) min_g = total_g;
            
            move_timer++;
            if (move_timer >= 300) { // เช็คทุกๆ 6 วินาที (300 * 20ms)
                float delta_g = max_g - min_g; 
                
                if (delta_g > 0.008f) { 
                    // ถ้ามีการขยับแม้แต่ครั้งเดียวใน 1 นาที ให้รีเซ็ตค่ากลับเป็น 0
                    if (no_movement_streak > 0) {
                        printf("[VITAL] Baby moved! (Resetting timer)\n");
                    }
                    no_movement_streak = 0; 
                    current_env_data.is_moving = 1;
                } else {
                    no_movement_streak++; // บวกคะแนนความนิ่ง
                    
                    // ถ้าเด็กหลับลึก ไม่ขยับเลย ก็จะสะสมไปเรื่อยๆ
                    if (no_movement_streak >= 10) { 
                        // สะสมครบ 10 รอบ (10 * 6วิ = 60 วินาที) แปลว่า 1 นาทีเต็มๆ ที่ไม่มีการขยับเลย!
                        current_env_data.is_moving = 0;
                        printf("[VITAL] ! WARNING: NO MOVEMENT FOR 1 MINUTE ! (Check the baby!)\n");
                    } else {
                        // ปริ้นท์บอกเฉยๆ ว่าตอนนี้นิ่งมากี่วินาทีแล้ว
                        printf("[VITAL] Still no movement... (%d0 seconds)\n", no_movement_streak);
                    }
                }
                
                max_g = 0.0f;
                min_g = 999.0f;
                move_timer = 0;
            }
        }
    }
}


// ========================================================
// ตรรกะสวิตช์: รวมคำสั่ง manual กับ auto ให้เหลือผลลัพธ์เดียว
//
// auto_mode = 0 -> output ตาม power_on ตรง ๆ (สั่งจาก TFT หรือ ThingsBoard)
// auto_mode = 1 -> ระบบตัดสินเอง โดยเลือกได้ว่าจะอิงอะไรผ่าน auto_source
//                    BM_AUTO_SRC_TEMP -> ใช้อุณหภูมิ เทียบกับ temp_on_c / temp_off_c
//                    BM_AUTO_SRC_PM25 -> ใช้ค่าฝุ่น  เทียบกับ pm25_on / pm25_off
//
// ทั้งสองแหล่งใช้ hysteresis เหมือนกัน:
//   เกินค่า on   -> เปิด
//   ต่ำกว่าค่า off -> ปิด
//   อยู่ระหว่างกลาง -> คงสถานะเดิม ไม่แตะ
//
// ทำไมต้อง hysteresis: ถ้าใช้จุดตัดเดียว พอค่าจริงแกว่งรอบ ๆ จุดนั้น
// เอาต์พุตจะกระพริบเปิดปิดรัว ซึ่งกินไฟและทำให้รีเลย์พังเร็ว
// ========================================================
static void apply_control_logic(void) {
    volatile babymate_shared_t *s = bm_shared();

    if (!s->auto_mode) {
        s->output_on = s->power_on;
        return;
    }

    float value, on_at, off_at;

    if (s->auto_source == BM_AUTO_SRC_PM25) {
        value  = current_env_data.pm25;
        on_at  = s->pm25_on;
        off_at = s->pm25_off;
    } else {
        value  = current_env_data.temperature;
        on_at  = s->temp_on_c;
        off_at = s->temp_off_c;
    }

    if (value >= on_at) {
        s->output_on = 1u;
    } else if (value <= off_at) {
        s->output_on = 0u;
    }
    /* ระหว่างสองค่า = ไม่แตะ ปล่อยให้ค้างสถานะเดิม */
}

// ========================================================
// จับเหตุการณ์ "เด็กร้อง" แล้วเก็บบริบทของเซนเซอร์ ณ วินาทีนั้น
//
// audio task แค่ตั้งธง baby_crying = 1 เพราะมันไม่มีข้อมูลเซนเซอร์อื่นเลย
// ส่วนนี้อยู่ใน env task ซึ่งเห็นทั้งอุณหภูมิ ความชื้น ฝุ่น ความเอียง และ
// อัตราหายใจ ⇒ เป็นที่เดียวที่ประกอบภาพรวมได้ครบ
//
// จับเฉพาะ "ขอบขาขึ้น" (0 -> 1) ไม่ใช่ตอนที่ธงยังค้างเป็น 1
// ไม่งั้นเสียงร้องยาวครั้งเดียวจะถูกนับเป็นหลายสิบเหตุการณ์
//
// การจัดลำดับเงื่อนไข: เรียงจากเร่งด่วนที่สุดลงมา เพราะถ้าเข้าหลายข้อพร้อมกัน
// เราอยากให้พ่อแม่เห็นข้อที่อันตรายที่สุดก่อน
// ========================================================
static void check_cry_event(void) {
    volatile babymate_shared_t *s = bm_shared();
    static uint8_t  prev_crying   = 0u;
    static uint32_t last_cry_time = 0u;

    const uint8_t now_crying = s->baby_crying;

    if ((now_crying == 1u) && (prev_crying == 0u)) {
        const uint32_t now_s = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        const float t    = current_env_data.temperature;
        const float rh   = current_env_data.humidity;
        const float dust = current_env_data.pm25;

        uint8_t reason;

        if (s->breath_state == BM_BREATH_APNEA_ALARM) {
            reason = BM_CRY_NO_BREATH;          /* เร่งด่วนสุด มาก่อนเสมอ */
        } else if (s_tilt_deg >= BM_CRY_TILT_DEG) {
            reason = BM_CRY_TILTED;
        } else if (t >= BM_CRY_TEMP_HOT_C) {
            reason = BM_CRY_TOO_HOT;
        } else if (t <= BM_CRY_TEMP_COLD_C) {
            reason = BM_CRY_TOO_COLD;
        } else if (dust >= BM_CRY_PM25_HIGH) {
            reason = BM_CRY_DUSTY;
        } else if (rh >= BM_CRY_HUMID_PCT) {
            reason = BM_CRY_HUMID;
        } else {
            reason = BM_CRY_UNKNOWN;            /* ทุกอย่างปกติ แต่ยังร้อง */
        }

        bm_write_begin();
        s->cry_count++;
        s->cry_at_uptime_s  = now_s;
        s->cry_gap_s        = (last_cry_time == 0u) ? 0u : (now_s - last_cry_time);
        s->cry_reason       = reason;
        s->cry_temperature  = t;
        s->cry_humidity     = rh;
        s->cry_pm25         = dust;
        s->cry_tilt_deg     = s_tilt_deg;
        s->cry_bpm          = s->bpm;
        s->cry_breath_state = s->breath_state;
        s->cry_pending      = 1u;               /* บอก CM33 ว่ามีของใหม่ */
        bm_write_end();

        last_cry_time = now_s;

        printf("[CRY] #%lu reason=%u | temp %d C | rh %d %% | tilt %d deg | gap %lu s\n",
               (unsigned long)s->cry_count, (unsigned)reason,
               (int)t, (int)rh, (int)s_tilt_deg, (unsigned long)s->cry_gap_s);
    }

    prev_crying = now_crying;
}

// ส่งค่าล่าสุดทั้งหมดลงบล็อกร่วมให้ CM33 เอาไปส่งขึ้น ThingsBoard
static void publish_to_shared(void) {
    volatile babymate_shared_t *s = bm_shared();

    bm_write_begin();
    s->temperature = current_env_data.temperature;
    s->humidity    = current_env_data.humidity;
    s->pm25        = current_env_data.pm25;      /* BMV080 ถอดออกแล้ว = 0 */
    s->accel_x     = current_env_data.accel_x;
    s->accel_y     = current_env_data.accel_y;
    s->accel_z     = current_env_data.accel_z;
    s->tilt_deg    = s_tilt_deg;
    s->vibration_g = s_vibration_g;
    s->is_moving   = current_env_data.is_moving;
    s->fallen_over = s_fallen_over;
    s->uptime_s    = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    bm_write_end();
}

// ========================================================
// พนักงาน (Task) ที่ทำงานเบื้องหลัง
// ========================================================
static void env_sensor_task(void *arg) {
    // 1. เปิดพอร์ต I2C ให้ Hardware
    Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW, &CYBSP_I2C_CONTROLLER_config, &env_i2c_context);
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);


    // 2. ผูก I2C เข้ากับ HAL Library ของ Infineon (เพื่อให้เซนเซอร์ IMU ใช้ได้)
    mtb_hal_i2c_setup(&env_hal_i2c_obj, &CYBSP_I2C_CONTROLLER_hal_config, &env_i2c_context, NULL);
    
    // 3. เริ่มต้นเซนเซอร์ IMU (BMI270)
    if (mtb_bmi270_init_i2c(&bmi270, &env_hal_i2c_obj, MTB_BMI270_ADDRESS_DEFAULT) == CY_RSLT_SUCCESS) {
        mtb_bmi270_config_default(&bmi270);
        uint8_t sens_list[1] = {BMI2_ACCEL};
        mtb_bmi270_sens_config_t config = {0};
        
        // ตั้งค่าให้จับความเคลื่อนไหว (Acceleration)
        mtb_bmi270_get_sensor_config(&config, 1, &bmi270);
        config.sensor_config.type = BMI2_ACCEL;
        config.sensor_config.cfg.acc.odr = BMI2_ACC_ODR_100HZ; // สแกนไว 100 ครั้งต่อวิ
        config.sensor_config.cfg.acc.range = BMI2_ACC_RANGE_8G; // รองรับแรงกระแทกถึง 8G
        mtb_bmi270_set_sensor_config(&config, 1, &bmi270);
        
        mtb_bmi270_sensor_enable(sens_list, 1, &bmi270);
        printf("[ENV SENSOR] IMU (BMI270) Started.\n");
    }
    
    int counter_15s = 750; // บังคับให้อ่าน SHT40 ทันทีในรอบแรก
    int counter_1s  = 0;   // ตัวนับสำหรับอัปเดตบล็อกร่วมทุก 1 วินาที

    // 4. วนลูปอ่านค่าไปเรื่อยๆ
    for(;;) {
        // อ่านค่าเอียงตลอดเวลาเพื่อจับการล้ม (ทุกๆ 20ms)
        read_imu();
        check_cry_event();   // ตรวจทุก 20 ms เพื่อไม่ให้พลาดเสียงร้องสั้น ๆ
        
        // ถ้าครบ 750 รอบ (750 * 20ms = 15000ms หรือ 15 วิ) ค่อยอ่านอุณหภูมิ 1 ครั้ง
        if (counter_15s >= 750) {
            read_sht40();

            counter_15s = 0;
        }
        counter_15s++;

        /* อัปเดตบล็อกร่วมทุก ๆ 1 วินาที (50 รอบ x 20 ms)
         * ถี่กว่านี้ไม่มีประโยชน์ เพราะ CM33 ส่งขึ้น cloud ช้ากว่านั้นมาก */
        if (++counter_1s >= 50) {
            counter_1s = 0;
            apply_control_logic();
            publish_to_shared();
        }
        
        // หลับทีละนิด (20ms) เพื่อเปิดทางให้ AI Baby Cry ทำงานได้ลื่นๆ
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}

// ========================================================
void create_env_sensor_task(void) {
    // เพิ่ม RAM ให้ Task นี้เป็น 2048 เพราะคำสั่ง mtb_bmi270_init ใช้แรมเยอะ
    xTaskCreate(env_sensor_task, "EnvTask", 2048, NULL, (configMAX_PRIORITIES - 1), NULL);
}

env_data_t get_env_data(void) {
    return current_env_data;
}
