/*
 * babymate_ui.c — UI 4 หน้าบนจอสัมผัส 800x480
 *
 * ข้อมูลทุกตัวมาจาก babymate_shared.h ซึ่ง CM55 เขียนอยู่แล้ว
 * ไม่ได้แตะเซนเซอร์เองเลย และปุ่มบนหน้า SETUP เขียนกลับบล็อกเดียวกัน
 * แล้วเพิ่ม control_seq -> cloud_task.c ฝั่ง CM33 เห็นเองแล้วซิงก์ขึ้น
 * ThingsBoard ให้ ไม่ต้องแก้โค้ดฝั่งนั้น
 */
#include "babymate_ui.h"

#include "lvgl.h"
#include "babymate_shared.h"

#include <stdio.h>

/*******************************************************************************
 * เลย์เอาต์
 ******************************************************************************/
#define SCR_W            800
#define SCR_H            480

#define BAR_H            56                       /* แถบบน */
#define NAV_H            56                       /* แถบปุ่มล่าง */
#define BODY_Y           BAR_H
#define BODY_H           (SCR_H - BAR_H - NAV_H)  /* 368 */

#define PAD              8

#define CARD_W           ((SCR_W - (3 * PAD)) / 2)   /* 392 */
#define CARD_H           150
#define ROW1_Y           PAD                         /* ในกรอบ body */
#define ROW2_Y           (ROW1_Y + CARD_H + PAD)
#define COL1_X           PAD
#define COL2_X           (COL1_X + CARD_W + PAD)
#define BANNER_Y         (ROW2_Y + CARD_H + PAD)
#define BANNER_H         (BODY_H - BANNER_Y - PAD)

/*******************************************************************************
 * สี — ธีมมืด อ่านง่ายในห้องเด็กตอนกลางคืน ไม่แยงตา
 ******************************************************************************/
#define C_BG             lv_color_hex(0x101418)
#define C_CARD           lv_color_hex(0x1C2229)
#define C_BAR            lv_color_hex(0x161C22)
#define C_TEXT           lv_color_hex(0xE6EAEE)
#define C_DIM            lv_color_hex(0x7C8894)
#define C_OK             lv_color_hex(0x36D399)
#define C_WARN           lv_color_hex(0xFBBD23)
#define C_BAD            lv_color_hex(0xF87272)
#define C_INFO           lv_color_hex(0x3ABFF8)
#define C_NAV_ON         lv_color_hex(0x2A6EF0)
#define C_NAV_OFF        lv_color_hex(0x232A32)

/*******************************************************************************
 * ตัวแปรของหน้าจอ
 ******************************************************************************/
typedef enum
{
    PAGE_HOME = 0,
    PAGE_BABY,
    PAGE_VITAL,
    PAGE_MOTION,
    PAGE_SETUP,
    PAGE_COUNT
} page_id_t;

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *nav_btns[PAGE_COUNT];
static page_id_t active_page = PAGE_HOME;

/* แถบบน */
static lv_obj_t *lbl_uptime;
static lv_obj_t *lbl_bar_env;
static lv_obj_t *lbl_wifi;    /* ไอคอน WiFi + สถานะ cloud */

/* HOME */
static lv_obj_t *lbl_temp, *lbl_hum, *lbl_bpm, *lbl_state;
static lv_obj_t *banner, *lbl_banner;

/* BABY */
static lv_obj_t *lbl_cry_now, *lbl_cry_total, *lbl_cry_last;
static lv_obj_t *lbl_cause, *lbl_cry_snapshot;
static lv_obj_t *lbl_pose, *lbl_pose_sub;   /* ท่านอนจากกล้อง AI */

/* VITAL */
static lv_obj_t *lbl_v_bpm, *lbl_v_state, *lbl_v_psr, *lbl_v_range, *lbl_v_motion;

/* MOTION */
static lv_obj_t *lbl_m_tilt, *lbl_m_tilt_state, *lbl_m_vib, *lbl_m_vib_state;
static lv_obj_t *lbl_m_pm25, *lbl_m_accel;

/* SETUP */
static lv_obj_t *sw_power, *sw_auto;
static lv_obj_t *sld_on, *sld_off;
static lv_obj_t *lbl_on_val, *lbl_off_val;

/* กัน callback ของ LVGL ยิงตอนเราเป็นคนตั้งค่าเอง ไม่งั้น control_seq
 * จะเด้งทุกครั้งที่ refresh ทำให้ ThingsBoard คิดว่ามีคนกดปุ่มตลอดเวลา */
static bool ui_is_loading = false;

/*******************************************************************************
 * ตัวช่วยแปลงค่าเป็นข้อความ
 ******************************************************************************/
static const char *breath_state_text(uint8_t s)
{
    switch (s)
    {
        case BM_BREATH_WARMUP:      return "WARMING UP";
        case BM_BREATH_NO_TARGET:   return "NO TARGET";
        case BM_BREATH_MOVING:      return "MOVING";
        case BM_BREATH_MEASURING:   return "MEASURING";
        case BM_BREATH_BREATHING:   return "BREATHING";
        case BM_BREATH_APNEA_ALARM: return "APNEA ALARM";
        default:                    return "UNKNOWN";
    }
}

static lv_color_t breath_state_color(uint8_t s)
{
    switch (s)
    {
        case BM_BREATH_BREATHING:   return C_OK;
        case BM_BREATH_MEASURING:   return C_WARN;
        case BM_BREATH_MOVING:      return C_INFO;
        case BM_BREATH_APNEA_ALARM: return C_BAD;
        default:                    return C_DIM;
    }
}

static const char *cry_reason_text(uint8_t r)
{
    switch (r)
    {
        case BM_CRY_TOO_HOT:    return "TOO HOT";
        case BM_CRY_TOO_COLD:   return "TOO COLD";
        case BM_CRY_DUSTY:      return "DUSTY AIR";
        case BM_CRY_TILTED:     return "CRADLE TILTED";
        case BM_CRY_HUMID:      return "TOO HUMID";
        case BM_CRY_NO_BREATH:  return "NOT BREATHING";
        case BM_CRY_UNKNOWN:
        default:                return "NO OBVIOUS CAUSE";
    }
}

/* อุณหภูมิ: ใช้เกณฑ์ชุดเดียวกับที่ CM55 ใช้ตัดสินสาเหตุการร้อง
 * จะได้ไม่เกิดกรณี "จอเขียวแต่ระบบบอกว่าร้อนเกิน" */
static lv_color_t temp_color(float t)
{
    if (t >= BM_CRY_TEMP_HOT_C)  return C_BAD;
    if (t <= BM_CRY_TEMP_COLD_C) return C_INFO;
    return C_OK;
}

static lv_color_t hum_color(float h)
{
    if (h >= BM_CRY_HUMID_PCT) return C_WARN;
    return C_OK;
}

/*******************************************************************************
 * ตัวช่วยสร้าง widget
 ******************************************************************************/
static void strip_container(lv_obj_t *o)
{
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y,
                            const lv_font_t *font, lv_color_t color,
                            const char *text)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);

    return l;
}

/* การ์ดหนึ่งใบ = ชื่อเล็กด้านบน + ตัวเลขใหญ่ตรงกลาง
 * คืน label ของตัวเลขออกมาให้เอาไปอัปเดต */
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    strip_container(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);

    make_label(card, 16, 12, &lv_font_montserrat_16, C_DIM, title);

    lv_obj_t *value = lv_label_create(card);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(value, C_TEXT, 0);
    lv_label_set_text(value, "--");
    lv_obj_align(value, LV_ALIGN_CENTER, 0, 12);

    return value;
}

/*******************************************************************************
 * การสลับหน้า
 ******************************************************************************/
static void show_page(page_id_t id)
{
    for (uint32_t i = 0u; i < (uint32_t)PAGE_COUNT; i++)
    {
        if ((page_id_t)i == id)
        {
            lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_btns[i], C_NAV_ON, 0);
        }
        else
        {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(nav_btns[i], C_NAV_OFF, 0);
        }
    }
    active_page = id;
}

static void nav_event_cb(lv_event_t *e)
{
    const page_id_t id = (page_id_t)(uintptr_t)lv_event_get_user_data(e);
    show_page(id);
}

/*******************************************************************************
 * callback ของหน้า SETUP — จุดเดียวที่เขียนกลับ shared block
 ******************************************************************************/
static void bump_control_seq(void)
{
    volatile babymate_shared_t *s = bm_shared();
    s->control_seq++;
}

static void power_event_cb(lv_event_t *e)
{
    if (ui_is_loading) return;

    volatile babymate_shared_t *s = bm_shared();
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);

    s->power_on = on ? 1u : 0u;
    /* กดสวิตช์หลักด้วยมือ = ตั้งใจคุมเอง ปิด auto ให้เลย
     * ตรงกับที่ env_sensor.c ทำอยู่ (auto_mode = 0 -> ใช้ power_on ตรง ๆ) */
    s->auto_mode = 0u;
    bump_control_seq();

    if (NULL != sw_auto)
    {
        ui_is_loading = true;
        lv_obj_remove_state(sw_auto, LV_STATE_CHECKED);
        ui_is_loading = false;
    }
}

static void auto_event_cb(lv_event_t *e)
{
    if (ui_is_loading) return;

    volatile babymate_shared_t *s = bm_shared();

    s->auto_mode = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) ? 1u : 0u;
    bump_control_seq();
}

static void slider_event_cb(lv_event_t *e)
{
    if (ui_is_loading) return;

    volatile babymate_shared_t *s = bm_shared();
    lv_obj_t *slider = lv_event_get_target(e);
    const float v = (float)lv_slider_get_value(slider);

    if (slider == sld_on)
    {
        s->temp_on_c = v;
        /* hysteresis ต้องไม่กลับด้าน ไม่งั้นพัดลมจะเปิดปิดรัวตอนค่าแกว่ง */
        if (s->temp_off_c > (v - 1.0f))
        {
            s->temp_off_c = v - 1.0f;
        }
    }
    else
    {
        s->temp_off_c = v;
        if (s->temp_on_c < (v + 1.0f))
        {
            s->temp_on_c = v + 1.0f;
        }
    }
    bump_control_seq();
}

/*******************************************************************************
 * สร้างแต่ละหน้า
 ******************************************************************************/
static void build_page_home(lv_obj_t *p)
{
    lbl_temp  = make_card(p, COL1_X, ROW1_Y, CARD_W, CARD_H, "TEMPERATURE");
    lbl_hum   = make_card(p, COL2_X, ROW1_Y, CARD_W, CARD_H, "HUMIDITY");
    lbl_bpm   = make_card(p, COL1_X, ROW2_Y, CARD_W, CARD_H, "BREATHING");
    lbl_state = make_card(p, COL2_X, ROW2_Y, CARD_W, CARD_H, "STATUS");

    /* ตัว STATUS เป็นข้อความยาว ใช้ฟอนต์เล็กลงไม่งั้นล้นการ์ด */
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_28, 0);

    banner = lv_obj_create(p);
    lv_obj_set_pos(banner, PAD, BANNER_Y);
    lv_obj_set_size(banner, SCR_W - (2 * PAD), BANNER_H);
    strip_container(banner);
    lv_obj_set_style_radius(banner, 8, 0);
    lv_obj_set_style_bg_color(banner, C_BAD, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);

    lbl_banner = lv_label_create(banner);
    lv_obj_set_style_text_font(lbl_banner, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl_banner, lv_color_hex(0x101418), 0);
    lv_label_set_text(lbl_banner, "");
    lv_obj_center(lbl_banner);
}

static void build_page_baby(lv_obj_t *p)
{
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_pos(box, PAD, PAD);
    lv_obj_set_size(box, SCR_W - (2 * PAD), 150);
    strip_container(box);
    lv_obj_set_style_bg_color(box, C_CARD, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);

    make_label(box, 16, 10, &lv_font_montserrat_16, C_DIM, "AI BABY CRY DETECTION");

    lbl_cry_now   = make_label(box, 16,  40, &lv_font_montserrat_40, C_OK,   "QUIET");
    lbl_cry_total = make_label(box, 16, 100, &lv_font_montserrat_16, C_TEXT, "total 0 times");
    lbl_cry_last  = make_label(box, 16, 122, &lv_font_montserrat_16, C_DIM,  "no cry yet");

    lv_obj_t *box2 = lv_obj_create(p);
    lv_obj_set_pos(box2, PAD, PAD + 150 + PAD);
    lv_obj_set_size(box2, SCR_W - (2 * PAD), BODY_H - (150 + (3 * PAD)));
    strip_container(box2);
    lv_obj_set_style_bg_color(box2, C_CARD, 0);
    lv_obj_set_style_bg_opa(box2, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box2, 10, 0);

    make_label(box2, 16, 10, &lv_font_montserrat_16, C_DIM, "PROBABLE CAUSE");

    lbl_cause        = make_label(box2, 16, 34, &lv_font_montserrat_28, C_TEXT, "-");
    lbl_cry_snapshot = make_label(box2, 16, 84, &lv_font_montserrat_16, C_DIM,
                                  "sensor snapshot at cry: -");

    /* ---- กล้อง AI: ท่านอน ---- วางครึ่งขวาของกล่องล่าง */
    make_label(box2, 430, 10, &lv_font_montserrat_16, C_DIM, "SLEEP POSITION (CAMERA AI)");

    lbl_pose     = make_label(box2, 430, 34, &lv_font_montserrat_28, C_DIM, "NO BABY");
    lbl_pose_sub = make_label(box2, 430, 84, &lv_font_montserrat_16, C_DIM,
                              "camera starting ...");
}

static void build_page_vital(lv_obj_t *p)
{
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_pos(box, PAD, PAD);
    lv_obj_set_size(box, CARD_W, BODY_H - (2 * PAD));
    strip_container(box);
    lv_obj_set_style_bg_color(box, C_CARD, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);

    make_label(box, 16, 14, &lv_font_montserrat_16, C_DIM, "RESPIRATION RATE");

    lbl_v_bpm = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_v_bpm, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(lbl_v_bpm, C_TEXT, 0);
    lv_label_set_text(lbl_v_bpm, "-- BPM");
    lv_obj_align(lbl_v_bpm, LV_ALIGN_CENTER, 0, -20);

    lbl_v_state = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_v_state, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_v_state, C_DIM, 0);
    lv_label_set_text(lbl_v_state, "WARMING UP");
    lv_obj_align(lbl_v_state, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *box2 = lv_obj_create(p);
    lv_obj_set_pos(box2, COL2_X, PAD);
    lv_obj_set_size(box2, CARD_W, BODY_H - (2 * PAD));
    strip_container(box2);
    lv_obj_set_style_bg_color(box2, C_CARD, 0);
    lv_obj_set_style_bg_opa(box2, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box2, 10, 0);

    make_label(box2, 16, 14, &lv_font_montserrat_16, C_DIM, "RADAR DETAIL");

    make_label(box2, 16,  60, &lv_font_montserrat_16, C_DIM, "SIGNAL QUALITY");
    lbl_v_psr    = make_label(box2, 200,  58, &lv_font_montserrat_20, C_TEXT, "--");

    make_label(box2, 16, 110, &lv_font_montserrat_16, C_DIM, "DISTANCE");
    lbl_v_range  = make_label(box2, 200, 108, &lv_font_montserrat_20, C_TEXT, "--");

    make_label(box2, 16, 160, &lv_font_montserrat_16, C_DIM, "MOTION");
    lbl_v_motion = make_label(box2, 200, 158, &lv_font_montserrat_20, C_TEXT, "--");
}

static void build_page_motion(lv_obj_t *p)
{
    /* แถวบน 2 การ์ด: เอียง | สั่นสะเทือน */
    lbl_m_tilt = make_card(p, COL1_X, ROW1_Y, CARD_W, CARD_H, "CRADLE TILT");
    lbl_m_tilt_state = lv_label_create(lv_obj_get_parent(lbl_m_tilt));
    lv_obj_set_style_text_font(lbl_m_tilt_state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_m_tilt_state, C_OK, 0);
    lv_label_set_text(lbl_m_tilt_state, "UPRIGHT");
    lv_obj_align(lbl_m_tilt_state, LV_ALIGN_BOTTOM_MID, 0, -10);

    lbl_m_vib = make_card(p, COL2_X, ROW1_Y, CARD_W, CARD_H, "VIBRATION");
    lbl_m_vib_state = lv_label_create(lv_obj_get_parent(lbl_m_vib));
    lv_obj_set_style_text_font(lbl_m_vib_state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_m_vib_state, C_DIM, 0);
    lv_label_set_text(lbl_m_vib_state, "STILL");
    lv_obj_align(lbl_m_vib_state, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* แถวล่าง: PM2.5 (ข้อความล้วนตามที่ขอ) | ค่าดิบ accel */
    lbl_m_pm25 = make_card(p, COL1_X, ROW2_Y, CARD_W, CARD_H, "PM 2.5 AIR QUALITY");

    lv_obj_t *box = lv_obj_create(p);
    lv_obj_set_pos(box, COL2_X, ROW2_Y);
    lv_obj_set_size(box, CARD_W, CARD_H);
    strip_container(box);
    lv_obj_set_style_bg_color(box, C_CARD, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);

    make_label(box, 16, 12, &lv_font_montserrat_16, C_DIM, "ACCELEROMETER (g)");
    lbl_m_accel = make_label(box, 16, 52, &lv_font_montserrat_22, C_TEXT,
                             "X  --   Y  --   Z  --");
}

static void build_page_setup(lv_obj_t *p)
{
    volatile babymate_shared_t *s = bm_shared();

    ui_is_loading = true;

    make_label(p, PAD + 16, PAD + 12, &lv_font_montserrat_22, C_TEXT, "POWER");
    sw_power = lv_switch_create(p);
    lv_obj_set_size(sw_power, 90, 46);
    lv_obj_set_pos(sw_power, 300, PAD + 6);
    if (0u != s->power_on)
    {
        lv_obj_add_state(sw_power, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_power, power_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(p, PAD + 16, PAD + 78, &lv_font_montserrat_22, C_TEXT, "AUTO MODE");
    sw_auto = lv_switch_create(p);
    lv_obj_set_size(sw_auto, 90, 46);
    lv_obj_set_pos(sw_auto, 300, PAD + 72);
    if (0u != s->auto_mode)
    {
        lv_obj_add_state(sw_auto, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_auto, auto_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ช่วง 18..38 องศา ครอบคลุมทุกสภาพห้องในไทย */
    make_label(p, PAD + 16, PAD + 150, &lv_font_montserrat_20, C_DIM, "TURN ON ABOVE");
    sld_on = lv_slider_create(p);
    lv_obj_set_size(sld_on, 420, 20);
    lv_obj_set_pos(sld_on, 260, PAD + 154);
    lv_slider_set_range(sld_on, 18, 38);
    lv_slider_set_value(sld_on, (int32_t)s->temp_on_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(sld_on, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lbl_on_val = make_label(p, 700, PAD + 148, &lv_font_montserrat_22, C_TEXT, "--");

    make_label(p, PAD + 16, PAD + 210, &lv_font_montserrat_20, C_DIM, "TURN OFF BELOW");
    sld_off = lv_slider_create(p);
    lv_obj_set_size(sld_off, 420, 20);
    lv_obj_set_pos(sld_off, 260, PAD + 214);
    lv_slider_set_range(sld_off, 18, 38);
    lv_slider_set_value(sld_off, (int32_t)s->temp_off_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(sld_off, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lbl_off_val = make_label(p, 700, PAD + 208, &lv_font_montserrat_22, C_TEXT, "--");

    ui_is_loading = false;
}

/*******************************************************************************
 * Function Name: babymate_ui_create
 *******************************************************************************/
void babymate_ui_create(void)
{
    static const char *nav_text[PAGE_COUNT] = { "HOME", "BABY", "VITAL", "MOTION", "SETUP" };

    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---------- แถบบน ---------- */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, SCR_W, BAR_H);
    strip_container(bar);
    lv_obj_set_style_bg_color(bar, C_BAR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    make_label(bar, 16, 14, &lv_font_montserrat_24, C_TEXT, "BabyMate");
    lbl_uptime  = make_label(bar, 260, 18, &lv_font_montserrat_20, C_DIM, "00:00:00");
    lbl_bar_env = make_label(bar, 470, 18, &lv_font_montserrat_20, C_DIM, "--");

    /* ไอคอน WiFi ชิดขวาสุด — LV_SYMBOL_* ฝังมากับฟอนต์ montserrat อยู่แล้ว
     * ไม่ต้องเพิ่มฟอนต์หรือรูปภาพ */
    lbl_wifi = make_label(bar, 690, 16, &lv_font_montserrat_24, C_DIM,
                          LV_SYMBOL_WIFI "  " LV_SYMBOL_CLOSE);

    /* ---------- กรอบเนื้อหา 4 หน้า ซ้อนกัน โชว์ทีละใบ ---------- */
    for (uint32_t i = 0u; i < (uint32_t)PAGE_COUNT; i++)
    {
        pages[i] = lv_obj_create(scr);
        lv_obj_set_pos(pages[i], 0, BODY_Y);
        lv_obj_set_size(pages[i], SCR_W, BODY_H);
        strip_container(pages[i]);
        lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_page_home(pages[PAGE_HOME]);
    build_page_baby(pages[PAGE_BABY]);
    build_page_vital(pages[PAGE_VITAL]);
    build_page_motion(pages[PAGE_MOTION]);
    build_page_setup(pages[PAGE_SETUP]);

    /* ---------- ปุ่มสลับหน้า ---------- */
    {
        const int btn_w = SCR_W / PAGE_COUNT;   /* 200 px ต่อปุ่ม กดด้วยนิ้วสบาย */

        for (uint32_t i = 0u; i < (uint32_t)PAGE_COUNT; i++)
        {
            lv_obj_t *b = lv_button_create(scr);

            lv_obj_set_pos(b, (int)i * btn_w, SCR_H - NAV_H);
            lv_obj_set_size(b, btn_w - 2, NAV_H);
            lv_obj_set_style_radius(b, 0, 0);
            lv_obj_set_style_bg_color(b, C_NAV_OFF, 0);
            lv_obj_add_event_cb(b, nav_event_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);

            lv_obj_t *t = lv_label_create(b);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(t, C_TEXT, 0);
            lv_label_set_text(t, nav_text[i]);
            lv_obj_center(t);

            nav_btns[i] = b;
        }
    }

    show_page(PAGE_HOME);
}

/*******************************************************************************
 * Function Name: babymate_ui_refresh
 *******************************************************************************/
void babymate_ui_refresh(void)
{
    babymate_shared_t d;
    char buf[96];

    /* ใช้ snapshot แทนการอ่านตรง เพราะ task เซนเซอร์อาจแทรกกลางคัน
     * แล้วได้ค่าครึ่ง ๆ กลาง ๆ (temp ของรอบใหม่ + bpm ของรอบเก่า) */
    if (!bm_snapshot(&d))
    {
        /* วินิจฉัยชั่วคราว: snapshot ล้มได้ 2 สาเหตุ แยกให้ออกว่าอันไหน
         *   magic ไม่ตรง = โครงสร้างหรือที่อยู่ผิด
         *   magic ตรงแต่ล้ม = seqlock ไม่นิ่ง (มีคนเขียนถี่เกิน 5 รอบ retry) */
        static uint32_t fail_n = 0u;
        if (0u == (fail_n++ % 10u))
        {
            volatile babymate_shared_t *s = bm_shared();
            volatile const uint32_t *w = (volatile const uint32_t *)(void *)s;

            printf("[UI] fail#%lu addr=%p want=0x%08lX raw: %08lX %08lX %08lX %08lX %08lX %08lX\n",
                   (unsigned long)fail_n, (void *)s, (unsigned long)BM_SHARED_MAGIC,
                   (unsigned long)w[0], (unsigned long)w[1], (unsigned long)w[2],
                   (unsigned long)w[3], (unsigned long)w[4], (unsigned long)w[5]);
        }
        return;
    }

    /* ---------- แถบบน ---------- */
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (unsigned long)(d.uptime_s / 3600u),
             (unsigned long)((d.uptime_s / 60u) % 60u),
             (unsigned long)(d.uptime_s % 60u));
    lv_label_set_text(lbl_uptime, buf);

    snprintf(buf, sizeof(buf), "%.1f C   %.0f %%", (double)d.temperature, (double)d.humidity);
    lv_label_set_text(lbl_bar_env, buf);

    /* ไอคอนเชื่อมต่อ — แยก 2 ชั้นเพราะมันพังคนละแบบ
     *   ต่อ AP ไม่ได้        = ปัญหา WiFi (SSID/รหัส/สัญญาณ)
     *   ต่อ AP ได้แต่ cloud ไม่ติด = ปัญหา token/อินเทอร์เน็ต
     * ถ้ารวมเป็นไอคอนเดียวจะแยกไม่ออกว่าพังตรงไหน */
    if (0u == d.wifi_connected)
    {
        lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  " LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(lbl_wifi, C_BAD, 0);
    }
    else if (0u == d.cloud_connected)
    {
        lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  " LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(lbl_wifi, C_WARN, 0);
    }
    else
    {
        lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI "  " LV_SYMBOL_OK);
        lv_obj_set_style_text_color(lbl_wifi, C_OK, 0);
    }

    /* ---------- HOME ---------- */
    snprintf(buf, sizeof(buf), "%.1f C", (double)d.temperature);
    lv_label_set_text(lbl_temp, buf);
    lv_obj_set_style_text_color(lbl_temp, temp_color(d.temperature), 0);

    snprintf(buf, sizeof(buf), "%.0f %%", (double)d.humidity);
    lv_label_set_text(lbl_hum, buf);
    lv_obj_set_style_text_color(lbl_hum, hum_color(d.humidity), 0);

    snprintf(buf, sizeof(buf), "%.1f", (double)d.bpm);
    lv_label_set_text(lbl_bpm, buf);

    lv_label_set_text(lbl_state, breath_state_text(d.breath_state));
    lv_obj_set_style_text_color(lbl_state, breath_state_color(d.breath_state), 0);

    /* แถบเตือน — เรียงตามความด่วน หยุดหายใจมาก่อนเสมอ */
    if (0u != d.breath_alarm)
    {
        /* กล้องยืนยันว่ามีเด็กอยู่ แต่เรดาร์ไม่เจอการหายใจ
         * = หลักฐานแน่นกว่า APNEA จากเรดาร์อย่างเดียว จึงมาก่อน */
        lv_label_set_text(lbl_banner,
                          (BM_POSE_PRONE == d.cam_pose)
                              ? "!! BABY FACE DOWN + NO BREATHING - CHECK NOW !!"
                              : "!! BABY VISIBLE BUT NO BREATHING - CHECK NOW !!");
        lv_obj_set_style_bg_color(banner, C_BAD, 0);
        lv_obj_remove_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    else if (BM_BREATH_APNEA_ALARM == d.breath_state)
    {
        lv_label_set_text(lbl_banner, "!! APNEA - CHECK THE BABY NOW !!");
        lv_obj_set_style_bg_color(banner, C_BAD, 0);
        lv_obj_remove_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    else if (BM_POSE_PRONE == d.cam_pose)
    {
        lv_label_set_text(lbl_banner, "BABY IS SLEEPING FACE DOWN");
        lv_obj_set_style_bg_color(banner, C_WARN, 0);
        lv_obj_remove_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    else if (0u != d.baby_crying)
    {
        snprintf(buf, sizeof(buf), "BABY CRYING  -  %s", cry_reason_text(d.cry_reason));
        lv_label_set_text(lbl_banner, buf);
        lv_obj_set_style_bg_color(banner, C_WARN, 0);
        lv_obj_remove_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    else if (0u != d.fallen_over)
    {
        lv_label_set_text(lbl_banner, "CRADLE TIPPED OVER");
        lv_obj_set_style_bg_color(banner, C_WARN, 0);
        lv_obj_remove_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---------- BABY ---------- */
    if (0u != d.baby_crying)
    {
        lv_label_set_text(lbl_cry_now, "CRYING");
        lv_obj_set_style_text_color(lbl_cry_now, C_BAD, 0);
    }
    else
    {
        lv_label_set_text(lbl_cry_now, "QUIET");
        lv_obj_set_style_text_color(lbl_cry_now, C_OK, 0);
    }

    snprintf(buf, sizeof(buf), "total %lu times", (unsigned long)d.cry_count);
    lv_label_set_text(lbl_cry_total, buf);

    if (0u == d.cry_count)
    {
        lv_label_set_text(lbl_cry_last, "no cry since power on");
        lv_label_set_text(lbl_cause, "-");
        lv_label_set_text(lbl_cry_snapshot, "sensor snapshot at cry: -");
    }
    else
    {
        snprintf(buf, sizeof(buf), "last at %02lu:%02lu:%02lu   gap %lu s",
                 (unsigned long)(d.cry_at_uptime_s / 3600u),
                 (unsigned long)((d.cry_at_uptime_s / 60u) % 60u),
                 (unsigned long)(d.cry_at_uptime_s % 60u),
                 (unsigned long)d.cry_gap_s);
        lv_label_set_text(lbl_cry_last, buf);

        lv_label_set_text(lbl_cause, cry_reason_text(d.cry_reason));
        lv_obj_set_style_text_color(lbl_cause,
                                    (BM_CRY_NO_BREATH == d.cry_reason) ? C_BAD : C_WARN, 0);

        snprintf(buf, sizeof(buf), "at cry:  %.1f C   %.0f %%   tilt %.0f deg   %.1f BPM",
                 (double)d.cry_temperature, (double)d.cry_humidity,
                 (double)d.cry_tilt_deg, (double)d.cry_bpm);
        lv_label_set_text(lbl_cry_snapshot, buf);
    }

    /* ---------- BABY: ท่านอนจากกล้อง AI ---------- */
    switch (d.cam_pose)
    {
        case BM_POSE_PRONE:
            /* คว่ำหน้าเป็นท่าเสี่ยง SIDS สูงสุด ต้องเด่นที่สุดบนจอ */
            lv_label_set_text(lbl_pose, "FACE DOWN");
            lv_obj_set_style_text_color(lbl_pose, C_BAD, 0);
            break;

        case BM_POSE_SUPINE:
            lv_label_set_text(lbl_pose, "FACE UP");
            lv_obj_set_style_text_color(lbl_pose, C_OK, 0);
            break;

        case BM_POSE_NONE:
        default:
            lv_label_set_text(lbl_pose, "NO BABY");
            lv_obj_set_style_text_color(lbl_pose, C_DIM, 0);
            break;
    }

    if (0u == d.cam_frames)
    {
        lv_label_set_text(lbl_pose_sub, "camera starting ...");
    }
    else
    {
        snprintf(buf, sizeof(buf), "conf %.2f   %lu ms/frame   %lu frames",
                 (double)d.cam_confidence, (unsigned long)d.cam_infer_ms,
                 (unsigned long)d.cam_frames);
        lv_label_set_text(lbl_pose_sub, buf);
    }

    /* ---------- VITAL ---------- */
    snprintf(buf, sizeof(buf), "%.1f BPM", (double)d.bpm);
    lv_label_set_text(lbl_v_bpm, buf);

    lv_label_set_text(lbl_v_state, breath_state_text(d.breath_state));
    lv_obj_set_style_text_color(lbl_v_state, breath_state_color(d.breath_state), 0);

    snprintf(buf, sizeof(buf), "PSR %.2f", (double)d.breath_psr);
    lv_label_set_text(lbl_v_psr, buf);

    snprintf(buf, sizeof(buf), "%.2f m", (double)d.range_m);
    lv_label_set_text(lbl_v_range, buf);

    lv_label_set_text(lbl_v_motion, (0u != d.is_moving) ? "MOVING" : "STILL");
    lv_obj_set_style_text_color(lbl_v_motion, (0u != d.is_moving) ? C_INFO : C_DIM, 0);

    /* ---------- MOTION ---------- */
    snprintf(buf, sizeof(buf), "%.0f deg", (double)d.tilt_deg);
    lv_label_set_text(lbl_m_tilt, buf);

    if (0u != d.fallen_over)
    {
        lv_label_set_text(lbl_m_tilt_state, "FALLEN OVER");
        lv_obj_set_style_text_color(lbl_m_tilt_state, C_BAD, 0);
        lv_obj_set_style_text_color(lbl_m_tilt, C_BAD, 0);
    }
    else if (d.tilt_deg >= BM_CRY_TILT_DEG)
    {
        lv_label_set_text(lbl_m_tilt_state, "TILTED");
        lv_obj_set_style_text_color(lbl_m_tilt_state, C_WARN, 0);
        lv_obj_set_style_text_color(lbl_m_tilt, C_WARN, 0);
    }
    else
    {
        lv_label_set_text(lbl_m_tilt_state, "UPRIGHT");
        lv_obj_set_style_text_color(lbl_m_tilt_state, C_OK, 0);
        lv_obj_set_style_text_color(lbl_m_tilt, C_OK, 0);
    }

    /* สั่นสะเทือน — เกณฑ์มาจากที่ env_sensor.c ใช้ตัดสินว่า "ขยับ" (0.008 g)
     * แบ่งเป็น 3 ช่วงให้อ่านง่ายแทนที่จะดูตัวเลขดิบอย่างเดียว */
    snprintf(buf, sizeof(buf), "%.3f g", (double)d.vibration_g);
    lv_label_set_text(lbl_m_vib, buf);

    if (d.vibration_g >= 0.10f)
    {
        lv_label_set_text(lbl_m_vib_state, "STRONG");
        lv_obj_set_style_text_color(lbl_m_vib_state, C_BAD, 0);
        lv_obj_set_style_text_color(lbl_m_vib, C_BAD, 0);
    }
    else if (d.vibration_g >= 0.008f)
    {
        lv_label_set_text(lbl_m_vib_state, "MOVING");
        lv_obj_set_style_text_color(lbl_m_vib_state, C_INFO, 0);
        lv_obj_set_style_text_color(lbl_m_vib, C_INFO, 0);
    }
    else
    {
        lv_label_set_text(lbl_m_vib_state, "STILL");
        lv_obj_set_style_text_color(lbl_m_vib_state, C_DIM, 0);
        lv_obj_set_style_text_color(lbl_m_vib, C_TEXT, 0);
    }

    /* PM2.5 — เอาแค่ LOW / HIGH ตามที่สั่ง
     * ตอนนี้ BMV080 ถอดออกแล้ว pm25 = 0 เสมอ จึงค้างที่ LOW
     * พอใส่เซนเซอร์กลับมา ค่าจะขยับเองโดยไม่ต้องแก้โค้ดตรงนี้ */
    if (d.pm25 >= BM_CRY_PM25_HIGH)
    {
        lv_label_set_text(lbl_m_pm25, "HIGH");
        lv_obj_set_style_text_color(lbl_m_pm25, C_BAD, 0);
    }
    else
    {
        lv_label_set_text(lbl_m_pm25, "LOW");
        lv_obj_set_style_text_color(lbl_m_pm25, C_OK, 0);
    }

    snprintf(buf, sizeof(buf), "X %+.2f   Y %+.2f   Z %+.2f",
             (double)d.accel_x, (double)d.accel_y, (double)d.accel_z);
    lv_label_set_text(lbl_m_accel, buf);

    /* ---------- SETUP ----------
     * ค่าอาจถูกแก้จาก ThingsBoard ได้ด้วย จอเลยต้องตามให้ทัน
     * ui_is_loading กัน callback ยิงกลับตอนเราเป็นคนตั้งเอง */
    ui_is_loading = true;

    if (0u != d.power_on) { lv_obj_add_state(sw_power, LV_STATE_CHECKED); }
    else                  { lv_obj_remove_state(sw_power, LV_STATE_CHECKED); }

    if (0u != d.auto_mode) { lv_obj_add_state(sw_auto, LV_STATE_CHECKED); }
    else                   { lv_obj_remove_state(sw_auto, LV_STATE_CHECKED); }

    /* อย่าดึงสไลเดอร์กลับขณะนิ้วยังลากอยู่ ไม่งั้นมันจะกระตุกสู้กับนิ้ว */
    if (!lv_obj_has_state(sld_on, LV_STATE_PRESSED))
    {
        lv_slider_set_value(sld_on, (int32_t)d.temp_on_c, LV_ANIM_OFF);
    }
    if (!lv_obj_has_state(sld_off, LV_STATE_PRESSED))
    {
        lv_slider_set_value(sld_off, (int32_t)d.temp_off_c, LV_ANIM_OFF);
    }

    snprintf(buf, sizeof(buf), "%.0f C", (double)d.temp_on_c);
    lv_label_set_text(lbl_on_val, buf);
    snprintf(buf, sizeof(buf), "%.0f C", (double)d.temp_off_c);
    lv_label_set_text(lbl_off_val, buf);

    ui_is_loading = false;
}
