/*
 * camera_ai.c — กล้อง OV7675 + YOLOv5n object detection (DEEPCRAFT/ImagiNet)
 *
 * หน้าที่:
 *   1. รับภาพจากกล้อง DVP 320x240 RGB565 (double buffer)
 *   2. กลับภาพตามที่ตั้งใน CAM_MVFP_VALUE (register 0x1E)
 *   3. แปลงเป็น RGB888 320x320 (เติมขอบบน-ล่าง) ป้อนโมเดล
 *   4. อ่านผลเป็นท่านอน คว่ำ/หงาย แล้วเขียนลง shared block
 *   5. รวมกับเรดาร์: เห็นเด็กแต่ไม่มีการหายใจ = ปลุกเตือน
 *
 * ข้อสำคัญเรื่อง I2C: กล้อง (0x21) อยู่บัส SCB5 เดียวกับจอ (0x45) และทัช (0x38)
 * จึงใช้ context เดียวกับที่ tft_screen.c เปิดไว้แล้ว ห้าม init ซ้ำ
 * และ task นี้ต้องถูกสร้าง "หลัง" จอ init เสร็จเท่านั้น
 */
#include "camera_ai.h"

#include "cybsp.h"
#include "cy_pdl.h"
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "vg_lite.h"
#include "mtb_dvp_camera_ov7675.h"

#include "display_i2c_config.h"
#include "lv_port_indev.h"      /* extern disp_touch_i2c_controller_context */
#include "babymate_shared.h"

#include "yolo_object.h"        /* YOLO_IMAI_init / YOLO_IMAI_compute */

/*******************************************************************************
 * Macros
 ******************************************************************************/
#define CAM_TASK_NAME           ("CAM")
#define CAM_TASK_STACK_SIZE     (configMINIMAL_STACK_SIZE * 8)
/* ต่ำกว่าจอ (MAX-3) และต่ำกว่าเสียง/เซนเซอร์ (MAX-1) เพราะ inference กินยาว
 * ห้ามไปแย่งเวลา AI จับเสียงร้องซึ่งต้องตามสตรีมเสียงแบบเรียลไทม์ */
#define CAM_TASK_PRIORITY       (tskIDLE_PRIORITY + 2)

/* ---- การกลับภาพ OV7675 ----
 * register 0x1E (MVFP): bit5 (0x20) = mirror ซ้าย-ขวา · bit4 (0x10) = vflip บน-ล่าง
 * ไดรเวอร์ Infineon ตั้ง mirror (0x20) ไว้ตายตัวตอน init ตัวนี้เขียนทับทีหลัง
 *
 * เปลี่ยนเลขนี้ค่าเดียวถ้าทิศยังไม่ถูก:
 *   0x00 ไม่กลับ · 0x10 บน-ล่าง · 0x20 ซ้าย-ขวา · 0x30 กลับทั้งสองแกน */
#define OV7675_MVFP_REG         (0x1Eu)
#define CAM_MVFP_VALUE          (0x30u)

/* ขนาดภาพ */
#define CAM_W                   (OV7675_FRAME_WIDTH)    /* 320 */
#define CAM_H                   (OV7675_FRAME_HEIGHT)   /* 240 */
#define AI_W                    (320u)
#define AI_H                    (320u)
/* โมเดลรับ 320x320 แต่กล้องให้ 320x240 จึงเติมแถบดำบน-ล่างอย่างละ 40 แถว
 * (letterbox) แทนการยืดภาพ เพราะการยืดทำให้สัดส่วนตัวเด็กเพี้ยน
 * ซึ่งกระทบความแม่นของโมเดลที่เทรนด้วยภาพสัดส่วนจริง */
#define AI_PAD_TOP              ((AI_H - CAM_H) / 2u)   /* 40 */

/* ผลลัพธ์โมเดล: float[6][5] = 6 กล่อง x 5 ค่า */
#define DET_MAX                 (6u)
#define DET_STRIDE              (5u)

/* ต้องมั่นใจกี่เปอร์เซ็นต์ถึงจะนับว่าเห็นเด็กจริง */
#define DET_CONF_MIN            (0.40f)

/* เห็นเด็กแต่เรดาร์ไม่เจอการหายใจนานเกินกี่ ms ถึงจะปลุกเตือน
 * ตั้งไว้ 20 วิ เพราะเรดาร์ต้องใช้เวลาสะสมสัญญาณกว่าจะสรุปได้
 * ถ้าสั้นกว่านี้จะเตือนหลอนทุกครั้งที่เด็กพลิกตัว */
#define BREATH_ALARM_DELAY_MS   (20000u)

/*******************************************************************************
 * Global Variables
 ******************************************************************************/
/* บัฟเฟอร์กล้อง 2 ใบ ต้องอยู่ใน gfx_mem เพราะ DMA ของ GFXSS เข้าถึง */
CY_SECTION(".cy_gpu_buf") CY_ALIGN(64)
static uint8_t cam_frame_mem[2][CAM_W * CAM_H * OV7675_BYTES_PER_PIXEL];

static vg_lite_buffer_t cam_buffer[2];

/* context แยกสำหรับช่วงที่บัสอยู่ในโหมด manual (ตอน init กล้อง)
 * ห้ามใช้ตัวเดียวกับจอ เพราะสองโหมดเก็บ state คนละแบบ */
static cy_stc_scb_i2c_context_t cam_i2c_context;

static volatile bool cam_frame_ready  = false;
static volatile bool cam_active_frame = false;   /* false=ใบ0 · true=ใบ1 */

/* ภาพที่แปลงแล้วสำหรับป้อนโมเดล — 320*320*3 = 307,200 B
 * ใหญ่เกินกว่าจะวางบน stack หรือ DTCM จึงไปอยู่ SOCMEM */
CY_SECTION(".cy_socmem_data") CY_ALIGN(16)
static uint8_t ai_input[AI_W * AI_H * 3u];

static float ai_output[DET_MAX * DET_STRIDE];

static TaskHandle_t cam_task_handle = NULL;

/*******************************************************************************
 * Function Name: cam_set_flip
 ********************************************************************************
 * เขียน register 0x1E ของ OV7675 ผ่าน I2C เพื่อกำหนดทิศการกลับภาพ
 * ไดรเวอร์ไม่เปิด API ให้ตั้งค่านี้ จึงต้องเขียนตรง
 *******************************************************************************/
static bool cam_set_flip(uint8_t mvfp_value)
{
    const uint8_t payload[2] = { OV7675_MVFP_REG, mvfp_value };
    cy_en_scb_i2c_status_t st;

    /* ใช้ cam_i2c_context เพราะฟังก์ชันนี้ถูกเรียกตอนบัสอยู่ในโหมด manual
     * (ภายใน camera_ai_hw_init หลัง re-init แล้ว) */
    st = Cy_SCB_I2C_MasterSendStart(DISPLAY_I2C_CONTROLLER_HW, OV7675_I2C_ADDR,
                                    CY_SCB_I2C_WRITE_XFER, 100u,
                                    &cam_i2c_context);
    if (CY_SCB_I2C_SUCCESS != st)
    {
        Cy_SCB_I2C_MasterSendStop(DISPLAY_I2C_CONTROLLER_HW, 100u,
                                  &cam_i2c_context);
        return false;
    }

    for (uint32_t i = 0u; i < sizeof(payload); i++)
    {
        st = Cy_SCB_I2C_MasterWriteByte(DISPLAY_I2C_CONTROLLER_HW, payload[i], 100u,
                                        &cam_i2c_context);
        if (CY_SCB_I2C_SUCCESS != st)
        {
            break;
        }
    }

    Cy_SCB_I2C_MasterSendStop(DISPLAY_I2C_CONTROLLER_HW, 100u,
                              &cam_i2c_context);

    return (CY_SCB_I2C_SUCCESS == st);
}

/*******************************************************************************
 * Function Name: frame_to_ai_input
 ********************************************************************************
 * RGB565 320x240 -> RGB888 320x320 (เติมขอบบน-ล่างเป็นสีดำ)
 *
 * RGB565 บิตเรียงเป็น RRRRRGGG GGGBBBBB (little endian: ไบต์ต่ำมาก่อน)
 * การขยาย 5->8 บิตใช้วิธีเลื่อนซ้ายแล้วเติมบิตสูงซ้ำลงมา (r<<3 | r>>2)
 * ซึ่งให้ค่าเต็ม 0..255 พอดี ต่างจากการเลื่อนเฉย ๆ ที่จะได้สูงสุดแค่ 248
 *******************************************************************************/
static void frame_to_ai_input(const uint8_t *src)
{
    /* ขอบบนและล่างเป็นศูนย์ ทำครั้งเดียวก็พอเพราะไม่มีอะไรมาเขียนทับ */
    static bool padded = false;
    if (!padded)
    {
        memset(ai_input, 0, sizeof(ai_input));
        padded = true;
    }

    uint8_t *dst = &ai_input[AI_PAD_TOP * AI_W * 3u];

    for (uint32_t i = 0u; i < (CAM_W * CAM_H); i++)
    {
        const uint16_t px = (uint16_t)((uint16_t)src[(i * 2u) + 1u] << 8) | src[i * 2u];

        const uint8_t r5 = (uint8_t)((px >> 11) & 0x1Fu);
        const uint8_t g6 = (uint8_t)((px >>  5) & 0x3Fu);
        const uint8_t b5 = (uint8_t)( px        & 0x1Fu);

        dst[(i * 3u) + 0u] = (uint8_t)((r5 << 3) | (r5 >> 2));
        dst[(i * 3u) + 1u] = (uint8_t)((g6 << 2) | (g6 >> 4));
        dst[(i * 3u) + 2u] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
}

/*******************************************************************************
 * Function Name: publish_result
 ********************************************************************************
 * แปลงผลดิบจากโมเดลเป็นท่านอน แล้วเขียนลง shared block
 *
 * ⚠️ ข้อสมมติเรื่องรูปแบบผลลัพธ์:
 * โมเดลคืน float[6][5] แต่ header ที่ ImagiNet gen มาไม่ได้ระบุว่า 5 ค่าคืออะไร
 * (ต่างจากโมเดลเสียงที่มี IMAI_DATA_OUT_SYMBOLS บอกชื่อคลาสไว้ชัด)
 *
 * ตอนนี้จึงตีความแบบมาตรฐานของ YOLO ไว้ก่อน: [x, y, w, h, class_or_score]
 * และ "พิมพ์ค่าดิบทั้ง 30 ตัวออก log" เพื่อให้ยืนยันจากของจริงได้
 * ถ้ารูปแบบจริงต่างจากนี้ ให้แก้เฉพาะฟังก์ชันนี้ ที่เหลือไม่ต้องแตะ
 *******************************************************************************/
static void publish_result(uint32_t infer_ms)
{
    volatile babymate_shared_t *s = bm_shared();

    float   best_conf = 0.0f;
    uint8_t best_pose = BM_POSE_NONE;

    for (uint32_t d = 0u; d < DET_MAX; d++)
    {
        const float *box  = &ai_output[d * DET_STRIDE];
        const float  val5 = box[4];

        /* ค่าที่ 5 ตีความได้สองแบบ รองรับทั้งคู่:
         *   ถ้าอยู่ในช่วง 0..1  -> เป็น confidence (คลาสอยู่ที่อื่นหรือมีคลาสเดียว)
         *   ถ้าเป็นจำนวนเต็ม >=1 -> เป็น class id */
        float   conf;
        uint8_t pose;

        if ((val5 > 0.0f) && (val5 <= 1.0f))
        {
            conf = val5;
            /* คลาสเดียวจากโมเดล: ใช้สัดส่วนกล่องแยกท่าแทน
             * เด็กนอนหงายเห็นลำตัวเต็มกว้าง / คว่ำจะเห็นแคบและสั้นกว่า
             * เป็นการเดาจากรูปทรง ควรแทนที่ด้วย class id จริงเมื่อรู้รูปแบบ */
            pose = (box[2] >= box[3]) ? BM_POSE_SUPINE : BM_POSE_PRONE;
        }
        else
        {
            conf = 1.0f;
            pose = ((uint8_t)val5 == 0u) ? BM_POSE_SUPINE : BM_POSE_PRONE;
        }

        if ((conf > best_conf) &&
            ((box[2] > 0.0f) || (box[3] > 0.0f)))   /* กล่องต้องมีขนาดจริง */
        {
            best_conf = conf;
            best_pose = pose;
        }
    }

    if (best_conf < DET_CONF_MIN)
    {
        best_pose = BM_POSE_NONE;
        best_conf = 0.0f;
    }

    s->cam_pose       = best_pose;
    s->cam_confidence = best_conf;
    s->cam_infer_ms   = infer_ms;
    s->cam_frames++;

    /* ---- รวมกล้อง + เรดาร์ ----
     * ปลุกเตือนเมื่อ "เห็นเด็กในเฟรม" แต่ "เรดาร์ไม่เจอการหายใจ" ต่อเนื่องนานพอ
     * กล้องเป็นตัวยืนยันว่ามีเด็กอยู่จริง ตัดกรณีเปลว่างที่เรดาร์ให้ค่าเหมือนกัน */
    {
        static uint32_t no_breath_since = 0u;

        const bool baby_seen  = (BM_POSE_NONE != best_pose);
        const bool breathing  = (BM_BREATH_BREATHING == s->breath_state) ||
                                (BM_BREATH_MEASURING == s->breath_state) ||
                                (BM_BREATH_MOVING    == s->breath_state);

        if (baby_seen && !breathing)
        {
            const uint32_t now = (uint32_t)xTaskGetTickCount();

            if (0u == no_breath_since)
            {
                no_breath_since = now;
            }
            else if ((now - no_breath_since) >= pdMS_TO_TICKS(BREATH_ALARM_DELAY_MS))
            {
                if (0u == s->breath_alarm)
                {
                    printf("[CAM] !! ALARM: baby visible (%s) but no breathing !!\n",
                           (BM_POSE_PRONE == best_pose) ? "PRONE" : "SUPINE");
                }
                s->breath_alarm = 1u;
            }
        }
        else
        {
            no_breath_since = 0u;
            s->breath_alarm = 0u;
        }
    }
}

/*******************************************************************************
 * Function Name: camera_ai_task
 *******************************************************************************/
cy_rslt_t camera_ai_hw_init(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* ผูกบัฟเฟอร์กล้องเข้ากับโครงสร้างที่ไดรเวอร์ต้องการ */
    for (uint32_t i = 0u; i < 2u; i++)
    {
        memset(&cam_buffer[i], 0, sizeof(cam_buffer[i]));
        cam_buffer[i].width  = CAM_W;
        cam_buffer[i].height = CAM_H;
        cam_buffer[i].format = VG_LITE_RGB565;
        cam_buffer[i].stride = CAM_W * OV7675_BYTES_PER_PIXEL;
        cam_buffer[i].memory = cam_frame_mem[i];
        cam_buffer[i].address = (uint32_t)(uintptr_t)cam_frame_mem[i];
    }

    /* ---- สลับบัส I2C ไปโหมด manual ชั่วคราว ----
     *
     * บัส SCB5 ถูกใช้ด้วย API สองแบบที่เข้ากันไม่ได้:
     *   จอ + ทัช : Cy_SCB_I2C_MasterWrite/Read  -> ต้องมี ISR ขับ state machine
     *   กล้อง    : Cy_SCB_I2C_MasterSendStart/WriteByte -> ต้องปั่น state เอง
     *
     * แค่ปิด NVIC ไม่พอ เพราะ context ยังค้างสภาพจากการใช้งานแบบ interrupt
     * ของไดรเวอร์จอ ทำให้ API แบบ manual ไม่ยอมเริ่มทรานแซกชัน
     * (พิสูจน์แล้ว: สแกนไม่เจอแม้แต่ 0x45 ทั้งที่จอเพิ่ง init ผ่าน)
     *
     * ตัวอย่าง vision ของ Infineon ไม่ register ISR เลย แล้วใช้ manual ล้วน
     * เราจึง re-init SCB ด้วย context สะอาดก่อนใช้ แล้วคืนสภาพให้จอทีหลัง */
    NVIC_DisableIRQ(DISPLAY_I2C_CONTROLLER_IRQ);

    Cy_SCB_I2C_Disable(DISPLAY_I2C_CONTROLLER_HW, &disp_touch_i2c_controller_context);
    (void)Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW, &DISPLAY_I2C_CONTROLLER_config,
                          &cam_i2c_context);
    Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

    if (CY_RSLT_SUCCESS != mtb_dvp_cam_ov7675_init(cam_buffer,
                                                   &cam_i2c_context,
                                                   (bool *)&cam_frame_ready,
                                                   (bool *)&cam_active_frame))
    {
        printf("[CAM] camera init failed\n");
        result = (cy_rslt_t)~CY_RSLT_SUCCESS;
    }
    else
    {
        printf("[CAM] OV7675 ok (%ux%u RGB565 @ I2C 0x%02X)\n",
               (unsigned)CAM_W, (unsigned)CAM_H, (unsigned)OV7675_I2C_ADDR);

        if (cam_set_flip(CAM_MVFP_VALUE))
        {
            printf("[CAM] flip set MVFP=0x%02X\n", (unsigned)CAM_MVFP_VALUE);
        }
        else
        {
            printf("[CAM] !! flip register write failed\n");
        }
    }

    /* คืนบัสให้จอกับทัช: re-init ด้วย context เดิมเพื่อล้างสภาพที่กล้องทิ้งไว้
     * แล้วค่อยเปิด ISR กลับ ลำดับนี้สลับไม่ได้ ไม่งั้น ISR จะยิงตอน context
     * ยังไม่พร้อม */
    Cy_SCB_I2C_Disable(DISPLAY_I2C_CONTROLLER_HW, &cam_i2c_context);
    (void)Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW, &DISPLAY_I2C_CONTROLLER_config,
                          &disp_touch_i2c_controller_context);
    Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

    NVIC_EnableIRQ(DISPLAY_I2C_CONTROLLER_IRQ);

    /* ---- สแกนบัส "หลัง" init เท่านั้น ----
     *
     * สแกนก่อน init ไม่มีความหมายเลย เพราะ OV7675 ต้องได้รับ master clock (XCLK)
     * ก่อนถึงจะตอบ SCCB/I2C ได้ และ XCLK เพิ่งถูกเปิดข้างใน
     * mtb_dvp_cam_ov7675_init() (ลำดับ: DMA -> AXIDMAC -> IRQ -> XCLK -> I2C)
     * แม้ init จะล้มที่ขั้น I2C ตัว XCLK ก็เดินไปแล้ว จุดนี้จึงเป็นที่เดียว
     * ที่ผลสแกนบอกได้จริงว่ากล้องมีอยู่บนบัสหรือไม่
     *
     * ใช้ API แบบ interrupt (ตัวเดียวกับที่จอใช้) เพราะ API แบบ manual บน SCB5
     * ให้ผลลบลวง — มองไม่เห็นแม้แต่ 0x45 ที่จอเพิ่ง init ผ่านไปหมาด ๆ
     *
     * อ่านผล:  0x45 = จอ · 0x38 = ทัช · 0x21 = กล้อง */
    {
        uint32_t found = 0u;

        printf("[CAM] I2C scan (XCLK running):");
        for (uint8_t addr = 0x08u; addr <= 0x77u; addr++)
        {
            cy_stc_scb_i2c_master_xfer_config_t xfer;

            memset(&xfer, 0, sizeof(xfer));
            xfer.slaveAddress = addr;
            xfer.buffer       = NULL;
            xfer.bufferSize   = 0u;
            xfer.xferPending  = false;

            if (CY_SCB_I2C_SUCCESS == Cy_SCB_I2C_MasterWrite(DISPLAY_I2C_CONTROLLER_HW,
                                                             &xfer,
                                                             &disp_touch_i2c_controller_context))
            {
                uint32_t guard = 100000u;
                while ((0u != (CY_SCB_I2C_MASTER_BUSY &
                               Cy_SCB_I2C_MasterGetStatus(DISPLAY_I2C_CONTROLLER_HW,
                                                          &disp_touch_i2c_controller_context))) &&
                       (guard-- > 0u))
                {
                    /* รอให้ ISR ปิดทรานแซกชัน */
                }

                const uint32_t st = Cy_SCB_I2C_MasterGetStatus(
                    DISPLAY_I2C_CONTROLLER_HW, &disp_touch_i2c_controller_context);

                if (0u == (st & (CY_SCB_I2C_MASTER_ADDR_NAK | CY_SCB_I2C_MASTER_DATA_NAK |
                                 CY_SCB_I2C_MASTER_ARB_LOST | CY_SCB_I2C_MASTER_BUS_ERR |
                                 CY_SCB_I2C_MASTER_ABORT_START)))
                {
                    printf(" 0x%02X", (unsigned)addr);
                    found++;
                }
            }
        }
        printf("%s\n", (0u == found) ? "  <none>" : "");
    }

    return result;
}

/*******************************************************************************
 * Function Name: camera_ai_task
 *******************************************************************************/
static void camera_ai_task(void *arg)
{
    (void)arg;

    uint32_t frame_no = 0u;

    if (0 != YOLO_IMAI_init())
    {
        printf("[CAM] YOLO model init failed\n");
        vTaskDelete(NULL);
        return;
    }
    printf("[CAM] YOLOv5n ready (in %ux%ux3, out %ux%u)\n",
           (unsigned)AI_W, (unsigned)AI_H, (unsigned)DET_MAX, (unsigned)DET_STRIDE);

    for (;;)
    {
        if (!cam_frame_ready)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* ใบที่ "ไม่ใช่" ใบที่กล้องกำลังเขียนอยู่ คือใบที่อ่านได้ */
        const uint8_t *src = cam_frame_mem[cam_active_frame ? 0u : 1u];

        cam_frame_ready = false;

        frame_to_ai_input(src);

        const uint32_t t0 = (uint32_t)xTaskGetTickCount();
        const int rc = YOLO_IMAI_compute(ai_input, ai_output);
        const uint32_t infer_ms =
            (uint32_t)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS);

        frame_no++;

        if (0 != rc)
        {
            printf("[CAM] inference rc=%d\n", rc);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        publish_result(infer_ms);

        /* พิมพ์ค่าดิบเป็นระยะเพื่อถอดความหมายของ 5 ค่าต่อกล่อง
         * (header ที่ ImagiNet gen มาไม่ได้บอกไว้)
         * วิธีดู: เอามือบังกล้อง/ขยับวัตถุ แล้วดูว่าเลขคอลัมน์ไหนขยับตาม
         * เมื่อรู้รูปแบบแน่ชัดแล้วลบบล็อกนี้ทิ้งได้ */
        if (0u == (frame_no % 10u))
        {
            printf("[CAM] #%lu  %lums  raw:\n", (unsigned long)frame_no,
                   (unsigned long)infer_ms);
            for (uint32_t d = 0u; d < DET_MAX; d++)
            {
                const float *b = &ai_output[d * DET_STRIDE];
                printf("      [%lu] %7.3f %7.3f %7.3f %7.3f %7.3f\n",
                       (unsigned long)d,
                       (double)b[0], (double)b[1], (double)b[2],
                       (double)b[3], (double)b[4]);
            }
        }

        /* เว้นจังหวะให้ task อื่นได้ทำงาน โดยเฉพาะ AI เสียงที่ต้องตามสตรีมสด */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/*******************************************************************************
 * Function Name: create_camera_ai_task
 *******************************************************************************/
cy_rslt_t create_camera_ai_task(void)
{
    const BaseType_t rc = xTaskCreate(camera_ai_task, CAM_TASK_NAME,
                                      CAM_TASK_STACK_SIZE, NULL,
                                      CAM_TASK_PRIORITY, &cam_task_handle);

    return (pdPASS == rc) ? CY_RSLT_SUCCESS : (cy_rslt_t)~CY_RSLT_SUCCESS;
}
