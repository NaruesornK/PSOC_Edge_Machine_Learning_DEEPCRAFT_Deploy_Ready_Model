/*
 * babymate_shared.h — บล็อกข้อมูลร่วมระหว่าง CM55 (เซนเซอร์) กับ CM33-NS (WiFi/MQTT)
 *
 * ทำไมต้องมี: เซนเซอร์ทุกตัวอยู่บน CM55 แต่ WiFi/MQTT ต้องรันบน CM33-NS
 * (ชิป WiFi ต่อผ่าน SDIO และ connectivity stack ของ Infineon ออกแบบมาแบบนั้น)
 * ⇒ ข้อมูลต้องข้ามคอร์
 *
 * วิธี: ใช้ SOCMEM ก้อน m55_allocatable_shared ซึ่ง "ทั้งสองคอร์เห็นที่อยู่เดียวกัน"
 * ยืนยันจาก BSP:
 *   cymem_CM55_0.h : CYMEM_CM55_0_m55_allocatable_shared_START = 0x240FF000
 *   cymem_CM33_0.h : CYMEM_CM33_0_m55_allocatable_shared_START = 0x240FF000
 * ขนาด 4 KB ซึ่งเหลือเฟือสำหรับ struct นี้
 *
 * หมายเหตุเรื่อง cache: ใช้ที่อยู่ 0x24xxxxxx (ไม่ใช่ alias 0x04xxxxxx) และประกาศ
 * ทุกฟิลด์เป็น volatile ถ้าเจออาการค่าไม่อัปเดตข้ามคอร์ ให้สงสัยเรื่อง cache
 * เป็นอันดับแรก แล้วเพิ่ม cache clean/invalidate ที่ฝั่ง CM55
 *
 * การกันข้อมูลฉีก (torn read): ใช้ seqlock
 *   ผู้เขียน  : seq++ (เป็นเลขคี่) -> เขียนข้อมูล -> seq++ (กลับเป็นเลขคู่)
 *   ผู้อ่าน   : อ่าน seq -> อ่านข้อมูล -> อ่าน seq ซ้ำ ถ้าไม่ตรงกันหรือเป็นคี่ = อ่านใหม่
 */
#ifndef BABYMATE_SHARED_H
#define BABYMATE_SHARED_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ⚠️ ห้ามย้ายกลับไป 0x240FF000 (m55_allocatable_shared)
 *
 * ที่อยู่เดิมชนกับ section .cy_sharedmem ซึ่ง linker วางลง region นั้น
 * และไดรเวอร์กล้อง OV7675 ประกาศ line_buffer (0x500 ไบต์) ไว้ที่ offset 0 พอดี:
 *     .cy_sharedmem  0x240ff000  0x500  mtb_dvp_camera_ov7675.o
 *     0x240ff000                        line_buffer
 * ผลคือพอ mtb_dvp_cam_ov7675_init() ทำงาน มันล้าง magic ทิ้ง
 * -> bm_snapshot คืน false ตลอด -> จอขึ้น "--" ทุกช่องทั้งที่เซนเซอร์ทำงานปกติ
 *
 * ย้ายมาใช้ m33_m55_shared แทน ซึ่งเป็น region ที่ออกแบบมาให้สองคอร์ใช้ร่วมกัน
 * โดยเฉพาะ ขนาด 256 KB และไม่มี section ไหนของ toolchain มาวางทับ
 * ยืนยันว่าทั้งสองคอร์เห็นที่อยู่เดียวกัน:
 *   cymem_gnu_CM55_0.ld : CYMEM_CM55_0_m33_m55_shared_START = 0x262A0000
 *   cymem_gnu_CM33_0.ld : CYMEM_CM33_0_m33_m55_shared_START = 0x262A0000 */
#define BM_SHARED_ADDR   0x262A0000u
#define BM_SHARED_MAGIC  0xBABE0007u   /* เพิ่มเลขทุกครั้งที่ struct เปลี่ยนรูป */

/* แหล่งอ้างอิงของโหมด auto — เลือกได้ว่าจะให้ตัดสินใจจากอะไร */
#define BM_AUTO_SRC_TEMP  0u   /* ตามอุณหภูมิ */
#define BM_AUTO_SRC_PM25  1u   /* ตามค่าฝุ่น PM2.5 */

/* สถานะการหายใจ ต้องตรงกับ breath_state_t ใน breathing.h */
#define BM_BREATH_WARMUP       0u
#define BM_BREATH_NO_TARGET    1u
#define BM_BREATH_MOVING       2u
#define BM_BREATH_MEASURING    3u
#define BM_BREATH_BREATHING    4u
#define BM_BREATH_APNEA_ALARM  5u

/* ---- ท่านอนที่กล้อง AI ตรวจได้ ----
 * โมเดล YOLOv5n object detection เทรนมาให้แยก 2 ท่า
 * คว่ำหน้าเป็นท่าที่เสี่ยง SIDS สูงสุดในเด็กทารก จึงต้องเตือน */
#define BM_POSE_NONE     0u   /* ไม่เห็นเด็กในเฟรม */
#define BM_POSE_SUPINE   1u   /* หงาย — ท่าที่ปลอดภัย */
#define BM_POSE_PRONE    2u   /* คว่ำ — ท่าเสี่ยง */

/* ---- สาเหตุที่คาดว่าทำให้เด็กร้อง ----
 * ตอน AI ตรวจเจอเสียงร้อง เราจะเก็บค่าเซนเซอร์ทุกตัว ณ วินาทีนั้นไว้
 * แล้วเทียบกับเกณฑ์ข้างล่าง เพื่อเดาว่าน่าจะร้องเพราะอะไร
 *
 * ข้อควรระวังในการตีความ: นี่คือ "ความสัมพันธ์" ไม่ใช่ "สาเหตุ" ที่พิสูจน์แล้ว
 * เด็กร้องตอนห้องร้อน ไม่ได้แปลว่าร้องเพราะร้อนเสมอไป ใช้เป็นตัวช่วยชี้เป้า
 * ให้พ่อแม่ไปดูก่อน ไม่ใช่คำวินิจฉัย */
#define BM_CRY_UNKNOWN     0u   /* ร้องโดยไม่มีเงื่อนไขไหนผิดปกติ */
#define BM_CRY_TOO_HOT     1u
#define BM_CRY_TOO_COLD    2u
#define BM_CRY_DUSTY       3u
#define BM_CRY_TILTED      4u   /* อุปกรณ์/เปลเอียงผิดปกติ */
#define BM_CRY_HUMID       5u
#define BM_CRY_NO_BREATH   6u   /* เรดาร์แจ้งหยุดหายใจร่วมด้วย = เร่งด่วนสุด */

/* เกณฑ์ตัดสิน — วางไว้ที่นี่เพื่อให้ CM55 กับ CM33 ใช้ค่าเดียวกันเสมอ */
#define BM_CRY_TEMP_HOT_C     30.0f
#define BM_CRY_TEMP_COLD_C    24.0f
#define BM_CRY_HUMID_PCT      70.0f
#define BM_CRY_PM25_HIGH      35.0f
#define BM_CRY_TILT_DEG       45.0f

typedef struct
{
    uint32_t magic;              /* = BM_SHARED_MAGIC เมื่อ CM55 ตั้งค่าเสร็จแล้ว */
    volatile uint32_t seq;       /* seqlock */

    /* ---------- TELEMETRY : CM55 เขียน / CM33 อ่าน ---------- */
    float    temperature;        /* °C  จาก SHT40 */
    float    humidity;           /* %RH จาก SHT40 */
    float    pm25;               /* ug/m3 — BMV080 ถอดออกแล้ว ค่าเป็น 0 */

    float    accel_x;            /* g */
    float    accel_y;
    float    accel_z;
    float    tilt_deg;           /* องศาที่เอียงจากแนวตั้ง 0 = ตั้งตรง */

    /* ความสั่นสะเทือน = (|a| สูงสุด − |a| ต่ำสุด) ในหน้าต่าง 0.5 วินาที หน่วย g
     *
     * ทำไมใช้ช่วงกว้าง (peak-to-peak) ไม่ใช่ค่าเฉลี่ย: แรงโน้มถ่วง 1 g
     * ค้างอยู่ในสัญญาณตลอดเวลา ค่าเฉลี่ยจะเป็น ~1.0 เสมอไม่ว่าจะสั่นหรือไม่
     * การหักสูงสุด−ต่ำสุดทำให้ค่า DC หายไปเอง เหลือแต่ส่วนที่แกว่งจริง
     *
     * อ่านคร่าว ๆ:  < 0.01 นิ่งสนิท · 0.01-0.05 สั่นเบา (หายใจ/ขยับตัว)
     *              > 0.1 สั่นแรง (เขย่าเปล/มีคนจับ) */
    float    vibration_g;

    float    bpm;                /* อัตราหายใจ ครั้ง/นาที จากเรดาร์ */
    float    breath_psr;         /* ความน่าเชื่อถือของ bpm ยิ่งสูงยิ่งชัด */
    float    range_m;            /* ระยะที่เจอเป้า เมตร */
    uint8_t  breath_state;       /* BM_BREATH_* */

    uint8_t  is_moving;          /* 1 = ขยับ/หายใจ · 0 = นิ่งสนิท */
    uint8_t  fallen_over;        /* 1 = อุปกรณ์ล้ม */
    uint8_t  baby_crying;        /* 1 = AI ตรวจพบเสียงเด็กร้อง */

    uint32_t uptime_s;

    /* ---------- LINK STATUS : CM33 เขียน / CM55 อ่านไปโชว์บนจอ ----------
     * ทิศทางกลับกับ telemetry ข้างบน (ที่ CM55 เขียน CM33 อ่าน)
     * เป็นไบต์เดียว การเขียนบน ARM เป็น atomic จึงไม่ต้องใช้ seqlock
     * และไม่ต้องอยู่ในบล็อก bm_write_begin/end ของ CM55 */
    volatile uint8_t wifi_connected;   /* 1 = ต่อ AP ได้ มี IP แล้ว */
    volatile uint8_t cloud_connected;  /* 1 = MQTT ต่อ ThingsBoard ติด */

    /* ---------- CAMERA AI : CM55 เขียน (กล้อง OV7675 + YOLOv5n) ---------- */
    uint8_t  cam_pose;         /* BM_POSE_* — ท่านอนที่ตรวจได้ */
    float    cam_confidence;   /* ความมั่นใจของกล่องที่ดีที่สุด 0..1 */
    uint32_t cam_frames;       /* นับเฟรมที่ประมวลผลไปแล้ว ใช้ดูว่ากล้องยังเดินอยู่ */
    uint32_t cam_infer_ms;     /* เวลา inference ต่อเฟรม (ms) */

    /* ---------- ALERT รวมกล้อง+เรดาร์ ----------
     * เงื่อนไขที่อันตรายที่สุด: กล้องเห็นเด็กอยู่ในเปลชัดเจน
     * แต่เรดาร์จับการหายใจไม่ได้
     *
     * ทำไมต้องใช้สองตัวรวมกัน: เรดาร์อย่างเดียวแยกไม่ออกว่า
     * "ไม่มีการหายใจ" กับ "ไม่มีใครอยู่ในเปล" — สองอย่างนี้ค่าเหมือนกัน
     * แต่ความหมายต่างกันสุดขั้ว กล้องเป็นตัวยืนยันว่ามีเด็กอยู่จริง */
    volatile uint8_t breath_alarm;  /* 1 = เห็นเด็กแต่ไม่มีการหายใจ */

    /* ---------- CRY EVENT : ภาพนิ่งของเซนเซอร์ตอนเด็กร้อง ----------
     * CM55 เติมตอนตรวจเจอเสียงร้องครั้งใหม่ (ขอบขาขึ้นของ baby_crying)
     * CM33 เห็น cry_pending = 1 แล้วส่งขึ้น dashboard จากนั้นเคลียร์เป็น 0 */
    volatile uint8_t cry_pending;      /* 1 = มีเหตุการณ์ใหม่ที่ยังไม่ได้ส่ง */
    uint8_t  cry_reason;               /* BM_CRY_* */
    uint32_t cry_count;                /* ร้องมาแล้วกี่ครั้งตั้งแต่เปิดเครื่อง */
    uint32_t cry_at_uptime_s;          /* ร้องตอนวินาทีที่เท่าไหร่ */
    uint32_t cry_gap_s;                /* ห่างจากครั้งก่อนกี่วินาที (0 = ครั้งแรก) */

    float    cry_temperature;          /* ค่าเซนเซอร์ ณ วินาทีที่ร้อง */
    float    cry_humidity;
    float    cry_pm25;
    float    cry_tilt_deg;
    float    cry_bpm;
    uint8_t  cry_breath_state;

    /* ---------- CONTROL : เขียนได้ทั้งจาก ThingsBoard (CM33) และจอ TFT (CM55) ----------
     * เป็นตัวแปรขนาดคำเดียว การเขียนบน ARM เป็น atomic อยู่แล้ว จึงไม่ต้องใช้ seqlock */
    volatile uint8_t  power_on;    /* สวิตช์หลัก 1 = เปิด */
    volatile uint8_t  auto_mode;   /* 1 = ให้ระบบตัดสินใจเอง */
    volatile uint8_t  output_on;   /* ผลลัพธ์จริงที่ใช้สั่งงาน (CM55 คำนวณให้) */
    volatile uint8_t  auto_source; /* BM_AUTO_SRC_TEMP หรือ BM_AUTO_SRC_PM25 */

    /* จุดตัดของแต่ละแหล่ง แยกชุดกันเพื่อให้สลับแหล่งไปมาแล้วค่าที่ตั้งไว้ไม่หาย
     * ทั้งสองชุดใช้ hysteresis: on ต้องมากกว่า off เสมอ */
    volatile float    temp_on_c;   /* ร้อนเกินค่านี้ -> เปิด */
    volatile float    temp_off_c;  /* เย็นกว่าค่านี้ -> ปิด */
    volatile float    pm25_on;     /* ฝุ่นเกินค่านี้ (ug/m3) -> เปิด */
    volatile float    pm25_off;    /* ฝุ่นต่ำกว่าค่านี้ -> ปิด */

    volatile uint32_t control_seq; /* เพิ่มขึ้นทุกครั้งที่มีคนแก้ค่าควบคุม */
} babymate_shared_t;

/* ตัวชี้ไปยังบล็อกร่วม — ทั้งสองคอร์เรียกอันเดียวกัน */
static inline volatile babymate_shared_t *bm_shared(void)
{
    return (volatile babymate_shared_t *)BM_SHARED_ADDR;
}

/* CM55 เรียกครั้งเดียวตอนบูต ก่อนที่ใครจะอ่าน */
static inline void bm_init(void)
{
    volatile babymate_shared_t *s = bm_shared();

    memset((void *)s, 0, sizeof(*s));

    /* ค่าเริ่มต้นที่ปลอดภัย: เปิดเครื่อง ปิด auto */
    s->power_on    = 1u;
    s->auto_mode   = 0u;
    s->output_on   = 1u;
    s->auto_source = BM_AUTO_SRC_TEMP;

    s->temp_on_c  = 30.0f;   /* ร้อนเกิน 30 °C -> เปิด */
    s->temp_off_c = 28.0f;   /* ต่ำกว่า 28 °C -> ปิด (เว้นช่วง 2 °C กันเปิดปิดถี่) */
    s->pm25_on    = 35.0f;   /* 35 ug/m3 = เกณฑ์ "เริ่มมีผลต่อสุขภาพ" ของ WHO 24 ชม. */
    s->pm25_off   = 25.0f;   /* เว้นช่วง 10 ug/m3 กันเปิดปิดถี่ตอนค่าแกว่ง */

    s->seq   = 0u;
    s->magic = BM_SHARED_MAGIC;

    /* ดันค่าลง SRAM จริงทันที ห้ามค้างอยู่แค่ใน D-cache
     *
     * ทำไมต้องมี: บล็อกนี้อยู่ที่ 0x240FF000 ซึ่งอ่าน/เขียนผ่าน D-cache ของคอร์
     * โค้ดของ GFXSS กับ DMA ของกล้องเรียก invalidate cache เป็นช่วง ๆ
     * ถ้าบรรทัดที่ยัง dirty (มี magic อยู่) โดน invalidate ก่อนถูก clean
     * ค่าจะถูกทิ้งไปเฉย ๆ แล้ว SRAM ยังเป็น 0 จาก memset ข้างบน
     *
     * อาการที่เจอจริง: bm_init พิมพ์ magic ถูก แต่พอ scheduler เริ่ม
     * bm_snapshot อ่านได้ 0 ตลอด -> จอขึ้น "--" ทุกช่อง ทั้งที่ seq เดินปกติ
     * (seq อยู่ cache line เดียวกัน จึงนับต่อจาก 0 ได้เหมือนไม่มีอะไรผิด) */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr((uint32_t *)(void *)s, (int32_t)sizeof(*s));
#endif
}

/* CM55: ครอบทุกครั้งที่จะเขียนค่า telemetry */
static inline void bm_write_begin(void) { bm_shared()->seq++; __asm volatile("dmb"); }
static inline void bm_write_end(void)   { __asm volatile("dmb"); bm_shared()->seq++; }

/* CM33: อ่านสำเนาที่ไม่ฉีก คืน false ถ้า CM55 ยังไม่พร้อม */
static inline bool bm_snapshot(babymate_shared_t *out)
{
    volatile babymate_shared_t *s = bm_shared();

    if (s->magic != BM_SHARED_MAGIC) return false;

    for (uint32_t try = 0u; try < 5u; try++)
    {
        const uint32_t before = s->seq;
        if ((before & 1u) != 0u) continue;          /* กำลังเขียนอยู่ */

        __asm volatile("dmb");
        memcpy(out, (const void *)s, sizeof(*out));
        __asm volatile("dmb");

        if (s->seq == before) return true;          /* ไม่มีใครแทรก = ใช้ได้ */
    }
    return false;
}

#endif /* BABYMATE_SHARED_H */
