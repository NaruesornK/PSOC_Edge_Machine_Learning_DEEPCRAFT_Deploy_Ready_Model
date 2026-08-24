#include "cloud_task.h"
#include "app_config.h"

#include "cybsp.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cy_wcm.h"
#include "cy_mqtt_api.h"
#include "cy_secure_sockets.h"
#include "tb_root_ca.h"
#include "babymate_shared.h"   /* ข้อมูลจริงจาก CM55 ผ่าน SOCMEM */

/* ====================================================================
 * ค่าคงที่
 * ==================================================================== */

#define CLOUD_TASK_STACK_WORDS      (1024u * 4u)
#define CLOUD_TASK_PRIORITY         3u

/* ลำดับความสำคัญของ interrupt — ตัวเลขมากคือสำคัญน้อย
 * ค่าพวกนี้เอามาจาก mtb-example-psoc-edge-wifi-secure-tcp-client */
#define APP_SDIO_INTERRUPT_PRIORITY     7u
#define APP_HOST_WAKE_INTERRUPT_PRIORITY 2u
/* อาการที่เจออยู่ตอนนี้: อ่าน register ผ่าน CMD52 ได้ปกติ (เห็น "chip ID: 55500")
 * แต่ CMD53 ที่โอนข้อมูลก้อนใหญ่ล้มเหลว:
 *   whd_bus_sdio_cmd53 ... SDIO_BYTE_MODE failed
 *   Error during SDIO receive, whd_bus_sdio_read_frame failed
 * ขั้นนั้นคือตอน WHD ดาวน์โหลดเฟิร์มแวร์ WLAN ลงชิป
 *
 * ลองลดคล็อกเป็น 10 MHz และ 5 MHz แล้ว "ไม่ช่วย" — พังเหมือนกันทั้งสามค่า
 * จึงคืนกลับเป็น 25 MHz ตามค่าอ้างอิงของ Infineon
 * สรุปว่าไม่ใช่ปัญหา signal integrity ที่แก้ได้ด้วยซอฟต์แวร์ */
#define APP_SDIO_FREQUENCY_HZ           25000000u
#define SDHC_SDIO_64BYTES_BLOCK         64u

/* buffer ที่ library MQTT ใช้เก็บ packet ระหว่างรับส่ง */
#define MQTT_NETWORK_BUFFER_SIZE        (2u * 1024u)

/* ====================================================================
 * สถานะภายใน
 * ==================================================================== */

static mtb_hal_sdio_t           sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t          wcm_config;

static cy_mqtt_t                mqtt_handle;
static uint8_t                  mqtt_network_buffer[MQTT_NETWORK_BUFFER_SIZE];
static bool                     mqtt_connected = false;
static bool                     cloud_online   = false;

/* จำ control_seq ล่าสุดที่เห็น เพื่อรู้ว่ามีใครกดปุ่ม (จอ TFT หรือ dashboard)
 * แล้วรีบส่งสถานะกลับขึ้นไปทันที ไม่ต้องรอครบรอบ telemetry */
static uint32_t                 last_control_seq = 0xFFFFFFFFu;

/* ประกาศล่วงหน้า — ตัวจริงอยู่ใต้ mqtt_start แต่ mqtt_start ต้องเรียกใช้ */
static void mqtt_event_cb(cy_mqtt_t handle, cy_mqtt_event_t event, void *user_data);
static bool mqtt_subscribe_control(void);
static void publish_state_attributes(void);
static bool publish_telemetry(const char *json);

/* รอเท่านี้ก่อนลอง scan + connect ใหม่ทั้งชุด */
#define CLOUD_RETRY_PERIOD_MS       20000u

/* ====================================================================
 * SDIO — ช่องทางที่ MCU คุยกับชิป WiFi
 * โครงทั้งหมดถอดมาจาก code example ของ Infineon ไม่ได้เขียนเดา
 * ==================================================================== */

static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

static bool app_sdio_init(void)
{
    cy_rslt_t          result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;

    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc      = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc      = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&sdio_intr_cfg, sdio_interrupt_handler))
    {
        printf("[CLOUD] SDIO interrupt init failed\n");
        return false;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config,
                                NULL, &sdhc_host_context);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] mtb_hal_sdio_setup failed (0x%08lX)\n", (unsigned long)result);
        return false;
    }

    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                    CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size      = SDHC_SDIO_64BYTES_BLOCK;
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

    /* ขา WL_REG_ON = สวิตช์เปิดไฟชิป WiFi · HOST_WAKE = ชิปปลุก MCU */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM, CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler))
    {
        printf("[CLOUD] host wake interrupt init failed\n");
        return false;
    }
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);

    return true;
}

/* ====================================================================
 * WiFi
 * ==================================================================== */

/* --------------------------------------------------------------------
 * สแกนหา AP รอบตัวแล้วพิมพ์ออกมา
 *
 * ทำไมต้องมี: ถ้า cy_wcm_connect_ap ล้มเหลว มันแยกไม่ออกว่า
 *   (ก) บอร์ดไม่เห็น AP เลย (ปิดอยู่ / อยู่คนละย่าน 5 GHz / ไกลเกิน)
 *   (ข) เห็นแต่รหัสผ่านผิด
 *   (ค) เห็นแต่ตั้ง security ผิดชนิด
 * สามอย่างนี้แก้คนละทางกัน จึงต้องดูด้วยตาว่าบอร์ดเห็นอะไรบ้าง
 * -------------------------------------------------------------------- */

static volatile bool     scan_done   = false;
static volatile bool     target_seen = false;
static volatile uint32_t scan_count  = 0u;

static const char *security_name(cy_wcm_security_t sec)
{
    switch (sec)
    {
        case CY_WCM_SECURITY_OPEN:              return "OPEN";
        case CY_WCM_SECURITY_WPA_TKIP_PSK:      return "WPA-TKIP";
        case CY_WCM_SECURITY_WPA_AES_PSK:       return "WPA-AES";
        case CY_WCM_SECURITY_WPA2_AES_PSK:      return "WPA2-AES";
        case CY_WCM_SECURITY_WPA2_TKIP_PSK:     return "WPA2-TKIP";
        case CY_WCM_SECURITY_WPA2_MIXED_PSK:    return "WPA2-MIXED";
        case CY_WCM_SECURITY_WPA3_SAE:          return "WPA3-SAE";
        case CY_WCM_SECURITY_WPA3_WPA2_PSK:     return "WPA3/WPA2";
        default:                                return "other";
    }
}

static void scan_callback(cy_wcm_scan_result_t *result, void *user_data,
                          cy_wcm_scan_status_t status)
{
    (void)user_data;

    if (CY_WCM_SCAN_COMPLETE == status)
    {
        scan_done = true;
        return;
    }

    if ((NULL == result) || (0 == result->SSID[0]))
    {
        return;  /* AP ที่ซ่อนชื่อไว้ */
    }

    scan_count++;

    const bool is_target = (0 == strcmp((const char *)result->SSID, WIFI_SSID));
    if (is_target)
    {
        target_seen = true;
    }

    printf("[SCAN] %-24s %4d dBm  ch %-3u %-10s %s%s\n",
           (const char *)result->SSID,
           (int)result->signal_strength,
           (unsigned)result->channel,
           security_name(result->security),
           (result->band == CY_WCM_WIFI_BAND_5GHZ) ? "5GHz" : "2.4GHz",
           is_target ? "   <<< TARGET" : "");
}

static void wifi_scan(void)
{
    scan_done   = false;
    target_seen = false;
    scan_count  = 0u;

    /* อ่าน MAC ของชิป WiFi ก่อน — ถ้าอ่านได้เป็นเลขจริง แปลว่าชิปมีชีวิตและ
     * firmware โหลดเข้าไปแล้ว ช่วยแยกว่า "ชิปตาย" กับ "ชิปดีแต่ไม่เจอ AP" */
    cy_wcm_mac_t mac;
    if (CY_RSLT_SUCCESS == cy_wcm_get_mac_addr(CY_WCM_INTERFACE_TYPE_STA, &mac))
    {
        printf("[SCAN] WLAN MAC = %02X:%02X:%02X:%02X:%02X:%02X\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    else
    {
        printf("[SCAN] ! cannot read WLAN MAC - chip may not be responding\n");
    }

    printf("[SCAN] scanning for access points ...\n");

    if (CY_RSLT_SUCCESS != cy_wcm_start_scan(scan_callback, NULL, NULL))
    {
        printf("[SCAN] start_scan failed\n");
        return;
    }

    /* scan เป็น non-blocking รอผลไม่เกิน 15 วิ */
    for (uint32_t waited = 0u; (!scan_done) && (waited < 15000u); waited += 250u)
    {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    printf("[SCAN] finished: %lu AP(s) seen\n", (unsigned long)scan_count);

    if (target_seen)
    {
        printf("[SCAN] target SSID is visible -> problem is password or security type\n");
    }
    else if (0u == scan_count)
    {
        /* ไม่เห็น AP เลยแม้แต่ตัวเดียว = วิทยุไม่ทำงาน ไม่ใช่เรื่อง SSID ผิด */
        printf("[SCAN] ! NO AP AT ALL - the radio is not receiving !\n");
        printf("[SCAN]   -> not an SSID problem; the WLAN chip itself is not scanning\n");
    }
    else
    {
        printf("[SCAN] ! target SSID \"%s\" NOT visible !\n", WIFI_SSID);
        printf("[SCAN]   -> is the AP switched on? name is case-sensitive\n");
        printf("[SCAN]   -> if hotspot is set to 5 GHz, switch it to 2.4 GHz\n");
    }
}

static bool wifi_connect(void)
{
    cy_rslt_t               result;
    cy_wcm_connect_params_t connect_param;
    cy_wcm_ip_address_t     ip_address;

    /* ถ้ายังเกาะ AP อยู่ ไม่ต้องทำอะไรเลย
     *
     * ของเดิมพอ publish ล้มครั้งเดียวจะรื้อทั้งชุด แล้ว scan + join ใหม่หมด
     * ซึ่งกินบัฟเฟอร์ WHD หนักมากและเป็นตัวเร่งให้ชิป WiFi ค้าง
     * ทั้งที่ลิงก์ WiFi ยังดีอยู่ ปัญหาอยู่แค่ชั้น MQTT */
    if (cy_wcm_is_connected_to_ap() != 0u)
    {
        return true;
    }

    /* ดูก่อนว่าบอร์ดเห็น AP ตัวไหนบ้าง จะได้วินิจฉัยถูกจุดถ้าต่อไม่ติด */
    wifi_scan();

    memset(&connect_param, 0, sizeof(connect_param));
    strncpy((char *)connect_param.ap_credentials.SSID, WIFI_SSID,
            sizeof(connect_param.ap_credentials.SSID) - 1u);
    strncpy((char *)connect_param.ap_credentials.password, WIFI_PASSWORD,
            sizeof(connect_param.ap_credentials.password) - 1u);
    connect_param.ap_credentials.security = WIFI_SECURITY;

    /* บังคับ 2.4 GHz ไปเลย (สแกนยืนยันแล้วว่า AP อยู่ย่านนี้)
     * ค่า default คือ ANY ซึ่งทำให้ไล่หา 5 GHz ด้วย เสียเวลาเปล่าและมีโอกาส timeout */
    connect_param.band = CY_WCM_WIFI_BAND_2_4GHZ;

    for (uint32_t attempt = 1u; attempt <= WIFI_MAX_RETRIES; attempt++)
    {
        printf("[CLOUD] connecting to \"%s\" (attempt %lu/%lu) ...\n",
               WIFI_SSID, (unsigned long)attempt, (unsigned long)WIFI_MAX_RETRIES);

        /* เคลียร์สถานะค้างจากรอบก่อน — join ที่ล้มแบบ timeout มักทิ้งการเชื่อมต่อ
         * ค้างครึ่ง ๆ ไว้ ทำให้รอบถัดไปล้มตามไปด้วยทั้งที่สัญญาณดี */
        if (attempt > 1u)
        {
            (void)cy_wcm_disconnect_ap();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        result = cy_wcm_connect_ap(&connect_param, &ip_address);
        if (CY_RSLT_SUCCESS == result)
        {
            /* v4 เก็บแบบ network byte order แกะทีละไบต์ให้อ่านง่าย */
            const uint32_t ip = ip_address.ip.v4;
            printf("[CLOUD] WiFi connected. IP = %lu.%lu.%lu.%lu\n",
                   (unsigned long)( ip        & 0xFFu),
                   (unsigned long)((ip >>  8) & 0xFFu),
                   (unsigned long)((ip >> 16) & 0xFFu),
                   (unsigned long)((ip >> 24) & 0xFFu));
            bm_shared()->wifi_connected = 1u;   /* ให้จอฝั่ง CM55 ขึ้นไอคอน */
            return true;
        }

        bm_shared()->wifi_connected = 0u;

        printf("[CLOUD] connect failed (0x%08lX), retrying in %lu ms\n",
               (unsigned long)result, (unsigned long)WIFI_RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
    }

    printf("[CLOUD] ! WiFi connect failed this round !\n");
    printf("[CLOUD]   -> check SSID and password\n");
    printf("[CLOUD]   -> AP must be 2.4 GHz (board does not support 5 GHz)\n");
    return false;
}

/* cy_wcm_init ต้องเรียกครั้งเดียวตลอดอายุโปรแกรม แยกออกมาจาก wifi_connect
 * เพื่อให้วน retry ได้โดยไม่ init ซ้ำ */
static bool wcm_ready = false;

static bool wifi_stack_init(void)
{
    if (wcm_ready)
    {
        return true;
    }

    wcm_config.interface               = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    const cy_rslt_t result = cy_wcm_init(&wcm_config);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] cy_wcm_init failed (0x%08lX)\n", (unsigned long)result);

        /* เผื่อ init ไปได้ครึ่งทางแล้วค้างสถานะไว้ ล้างทิ้งก่อนลองใหม่
         * ไม่งั้นรอบถัดไปจะเจอ "already initialized" แล้วล้มซ้ำไปตลอด */
        (void)cy_wcm_deinit();
        return false;
    }

    wcm_ready = true;
    return true;
}

/* ====================================================================
 * MQTT (ThingsBoard)
 * ==================================================================== */

/* แยกให้ออกว่า MQTT ต่อไม่ได้เพราะอะไร
 *
 * cy_mqtt_connect คืน CONNECT_FAIL เฉย ๆ ซึ่งกลบสาเหตุจริงไว้หมด สามอย่างนี้
 * แก้คนละทางกันสิ้นเชิง จึงต้องแยกให้ได้ก่อน:
 *   - DNS แปลชื่อไม่ได้        -> เน็ตมีปัญหา หรือไม่มีทางออกอินเทอร์เน็ต
 *   - DNS ได้ แต่ TCP ต่อไม่ได้ -> พอร์ตถูกบล็อก (เครือข่ายมือถือมักบล็อก 1883)
 *   - TCP ได้ แต่ MQTT ไม่ผ่าน  -> access token ผิด
 */
static void diagnose_broker(void)
{
    cy_socket_ip_address_t addr;

    const cy_rslt_t dns = cy_socket_gethostbyname(TB_HOSTNAME, CY_SOCKET_IP_VER_V4, &addr);
    if (CY_RSLT_SUCCESS != dns)
    {
        printf("[NET] DNS lookup of %s FAILED (0x%08lX)\n",
               TB_HOSTNAME, (unsigned long)dns);
        printf("[NET]   -> no working internet path on this network\n");
        return;
    }

    const uint32_t ip = addr.ip.v4;
    printf("[NET] DNS ok: %s = %lu.%lu.%lu.%lu\n", TB_HOSTNAME,
           (unsigned long)( ip        & 0xFFu),
           (unsigned long)((ip >>  8) & 0xFFu),
           (unsigned long)((ip >> 16) & 0xFFu),
           (unsigned long)((ip >> 24) & 0xFFu));

    /* ลองเปิด TCP ตรง ๆ ไปที่พอร์ต broker */
    cy_socket_t          sock = NULL;
    cy_socket_sockaddr_t sa;

    if (CY_RSLT_SUCCESS != cy_socket_create(CY_SOCKET_DOMAIN_AF_INET,
                                            CY_SOCKET_TYPE_STREAM,
                                            CY_SOCKET_IPPROTO_TCP, &sock))
    {
        printf("[NET] cy_socket_create failed\n");
        return;
    }

    memset(&sa, 0, sizeof(sa));
    sa.ip_address = addr;
    sa.port       = TB_PORT;

    const cy_rslt_t conn = cy_socket_connect(sock, &sa, sizeof(sa));
    if (CY_RSLT_SUCCESS == conn)
    {
        printf("[NET] TCP to port %d OK -> network is fine, suspect the access token\n",
               TB_PORT);
    }
    else
    {
        /* อย่าเดาสาเหตุ ให้แปล error code ออกมาตรง ๆ
         * ของเดิมพิมพ์ "BLOCKED -> mobile networks block MQTT" ทุกกรณี
         * ซึ่งพาไปผิดทาง เพราะ error ส่วนใหญ่ไม่ได้แปลว่าโดนบล็อกเลย */
        const char *why;
        switch (conn & 0xFFFFu)
        {
            case 1u:  why = "TCPIP_ERROR - สแตกเครือข่ายมีปัญหา";              break;
            case 2u:  why = "BADARG - พารามิเตอร์ผิด (บั๊กในโค้ดเรา)";          break;
            case 3u:  why = "NOMEM - หน่วยความจำ/บัฟเฟอร์ไม่พอ";                break;
            case 4u:  why = "NOT_CONNECTED - ลิงก์ WiFi หลุด ไม่ใช่พอร์ตถูกบล็อก"; break;
            case 5u:  why = "CLOSED - ปลายทางปิดการเชื่อมต่อ";                  break;
            case 11u: why = "TIMEOUT - ไม่มีการตอบกลับ (อันนี้ถึงจะเข้าข่ายโดนบล็อก)"; break;
            case 13u: why = "HOST_NOT_FOUND";                                    break;
            case 14u: why = "TLS_ERROR";                                         break;
            default:  why = "ดูรหัสใน cy_secure_sockets_error.h";                break;
        }

        printf("[NET] TCP to port %d failed (0x%08lX)\n", TB_PORT, (unsigned long)conn);
        printf("[NET]   -> %s\n", why);
    }

    (void)cy_socket_disconnect(sock, 0);
    (void)cy_socket_delete(sock);
}

static bool mqtt_start(void)
{
    cy_rslt_t              result;
    cy_mqtt_broker_info_t  broker_info;
    cy_mqtt_connect_info_t connect_info;

    result = cy_mqtt_init();
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] cy_mqtt_init failed (0x%08lX)\n", (unsigned long)result);
        return false;
    }

    memset(&broker_info, 0, sizeof(broker_info));
    broker_info.hostname     = TB_HOSTNAME;
    broker_info.hostname_len = (uint16_t)strlen(TB_HOSTNAME);
    broker_info.port         = TB_PORT;

#if MQTT_USE_TLS
    /* TLS: ต้องมี root CA เพื่อยืนยันว่าเซิร์ฟเวอร์เป็น thingsboard.cloud ตัวจริง
     * และต้องเปิด SNI เพราะ ThingsBoard อยู่หลัง load balancer ที่โฮสต์หลายโดเมน
     * ถ้าไม่ส่ง SNI ไป เซิร์ฟเวอร์จะไม่รู้ว่าจะยื่น certificate ของใครให้ */
    cy_awsport_ssl_credentials_t security;
    memset(&security, 0, sizeof(security));
    security.root_ca            = tb_root_ca;
    security.root_ca_size       = sizeof(tb_root_ca);
    security.root_ca_verify_mode = CY_AWS_ROOTCA_VERIFY_REQUIRED;
    security.root_ca_location   = CY_AWS_CERT_KEY_LOCATION_RAM;
    security.sni_host_name      = TB_HOSTNAME;
    security.sni_host_name_size = strlen(TB_HOSTNAME);

    printf("[CLOUD] TLS enabled, using embedded ISRG Root X1\n");
#endif

    /* security = NULL คือต่อแบบไม่เข้ารหัส (พอร์ต 1883) */
    result = cy_mqtt_create(mqtt_network_buffer, sizeof(mqtt_network_buffer),
#if MQTT_USE_TLS
                            &security,
#else
                            NULL,
#endif
                            &broker_info, MQTT_CLIENT_ID, &mqtt_handle);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] cy_mqtt_create failed (0x%08lX)\n", (unsigned long)result);
        return false;
    }

    /* ต้องลงทะเบียน callback ก่อน subscribe ไม่งั้นข้อความที่ broker ส่งมา
     * จะถูกทิ้งไปเงียบ ๆ โดยไม่มีใครรับ */
    result = cy_mqtt_register_event_callback(mqtt_handle, mqtt_event_cb, NULL);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] register_event_callback failed (0x%08lX)\n",
               (unsigned long)result);
        (void)cy_mqtt_delete(mqtt_handle);
        mqtt_handle = NULL;
        return false;
    }

    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.client_id      = MQTT_CLIENT_ID;
    connect_info.client_id_len  = (uint16_t)strlen(MQTT_CLIENT_ID);
    /* ThingsBoard ใช้ access token เป็น username และไม่ต้องมี password */
    connect_info.username       = TB_ACCESS_TOKEN;
    connect_info.username_len   = (uint16_t)strlen(TB_ACCESS_TOKEN);
    connect_info.password       = NULL;
    connect_info.password_len   = 0u;
    connect_info.clean_session  = true;
    connect_info.keep_alive_sec = MQTT_KEEPALIVE_SEC;
    connect_info.will_info      = NULL;

    printf("[CLOUD] connecting to MQTT %s:%d ...\n", TB_HOSTNAME, TB_PORT);

    result = cy_mqtt_connect(mqtt_handle, &connect_info);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] cy_mqtt_connect failed (0x%08lX)\n", (unsigned long)result);
        diagnose_broker();
        (void)cy_mqtt_delete(mqtt_handle);
        mqtt_handle = NULL;
        return false;
    }

    printf("[CLOUD] MQTT connected to ThingsBoard\n");
    bm_shared()->cloud_connected = 1u;
    mqtt_connected = true;

    /* subscribe หลัง connect เท่านั้น ทำก่อนหน้านี้ broker จะปฏิเสธ */
    (void)mqtt_subscribe_control();

    /* ส่งสถานะปัจจุบันขึ้นไปทันทีที่ต่อได้ เพื่อให้ปุ่มบน dashboard
     * ตรงกับตัวเครื่องตั้งแต่วินาทีแรก ไม่ต้องรอให้ใครกดก่อน */
    publish_state_attributes();
    last_control_seq = bm_shared()->control_seq;

    return true;
}

/* ====================================================================
 * ตัวแยก JSON แบบง่าย
 *
 * ทำไมไม่ใช้ cJSON: payload จาก ThingsBoard มีแค่ไม่กี่คีย์และรูปแบบตายตัว
 * การลากไลบรารีมาทั้งก้อนเพื่อสามฟังก์ชันนี้ไม่คุ้ม
 *
 * ข้อจำกัดที่ต้องรู้: นี่ไม่ใช่ JSON parser ที่สมบูรณ์ มันแค่หา "key" แล้วอ่าน
 * ค่าที่ตามมา ใช้ได้กับ payload ชั้นเดียวแบบที่เราคุยกับ ThingsBoard เท่านั้น
 * ==================================================================== */

#define CLOUD_JSON_BUF   256u

/* หาตำแหน่งค่าที่อยู่หลัง "key": คืน NULL ถ้าไม่เจอ */
static const char *json_find(const char *json, size_t len, const char *key)
{
    static char buf[CLOUD_JSON_BUF];
    char pattern[40];

    const int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if ((n <= 0) || ((size_t)n >= sizeof(pattern))) return NULL;

    /* payload จาก MQTT ไม่การันตีว่าจบด้วย NUL จึงคัดลอกมาใส่ buffer ก่อน */
    if (len >= sizeof(buf)) len = sizeof(buf) - 1u;
    memcpy(buf, json, len);
    buf[len] = '\0';

    char *p = strstr(buf, pattern);
    if (p == NULL) return NULL;

    p += n;
    while ((*p == ' ') || (*p == ':')) p++;
    return p;
}

/* อ่านค่า boolean รับได้ทั้ง true/false และ 1/0 */
static bool json_get_bool(const char *json, size_t len, const char *key, bool *out)
{
    const char *v = json_find(json, len, key);
    if (v == NULL) return false;

    if ((strncmp(v, "true", 4) == 0) || (*v == '1'))  { *out = true;  return true; }
    if ((strncmp(v, "false", 5) == 0) || (*v == '0')) { *out = false; return true; }
    return false;
}

static bool json_get_float(const char *json, size_t len, const char *key, float *out)
{
    const char *v = json_find(json, len, key);
    if (v == NULL) return false;

    char *end = NULL;
    const float f = strtof(v, &end);
    if (end == v) return false;
    *out = f;
    return true;
}

/* ====================================================================
 * ลงมือทำตามคำสั่ง
 *
 * เขียนลงบล็อกร่วมโดยตรง CM55 จะเห็นภายใน 1 วินาที (รอบ publish ของมัน)
 * ฟิลด์ควบคุมเป็นขนาดคำเดียว การเขียนบน ARM เป็น atomic จึงไม่ต้องล็อก
 * ==================================================================== */
static void apply_command(const char *name, const char *payload, size_t len)
{
    volatile babymate_shared_t *s = bm_shared();
    bool  b = false;
    float f = 0.0f;
    bool  changed = false;

    if (strcmp(name, "setPower") == 0)
    {
        /* RPC ส่งค่ามาใน params ส่วน attribute ส่งมาเป็น power ตรง ๆ */
        if (json_get_bool(payload, len, "params", &b) ||
            json_get_bool(payload, len, "power",  &b))
        {
            s->power_on  = b ? 1u : 0u;
            s->auto_mode = 0u;            /* สั่งมือ = ออกจากโหมด auto */
            s->output_on = s->power_on;
            changed = true;
            printf("[CTRL] power -> %s (auto ถูกปิด)\n", b ? "ON" : "OFF");
        }
    }
    else if (strcmp(name, "setAuto") == 0)
    {
        if (json_get_bool(payload, len, "params", &b) ||
            json_get_bool(payload, len, "auto",   &b))
        {
            s->auto_mode = b ? 1u : 0u;
            changed = true;
            printf("[CTRL] auto -> %s\n", b ? "ON" : "OFF");
        }
    }
    else if (strcmp(name, "setTempOn") == 0)
    {
        if (json_get_float(payload, len, "params", &f) ||
            json_get_float(payload, len, "tempOn", &f))
        {
            s->temp_on_c = f;
            if (s->temp_off_c >= f) s->temp_off_c = f - 1.0f;  /* กันตั้งกลับด้าน */
            changed = true;
            printf("[CTRL] tempOn -> %d C\n", (int)f);
        }
    }
    else if (strcmp(name, "setTempOff") == 0)
    {
        if (json_get_float(payload, len, "params",  &f) ||
            json_get_float(payload, len, "tempOff", &f))
        {
            s->temp_off_c = f;
            if (s->temp_on_c <= f) s->temp_on_c = f + 1.0f;
            changed = true;
            printf("[CTRL] tempOff -> %d C\n", (int)f);
        }
    }
    else if (strcmp(name, "setAutoSource") == 0)
    {
        /* รับได้ทั้งตัวเลข (0/1) และข้อความ ("temp"/"pm25")
         * เพราะ widget แต่ละแบบบน dashboard ส่งมาไม่เหมือนกัน */
        const char *v = json_find(payload, len, "params");
        if (v == NULL) v = json_find(payload, len, "autoSource");

        if (v != NULL)
        {
            uint8_t src = BM_AUTO_SRC_TEMP;

            if ((strstr(v, "pm") != NULL) || (strstr(v, "PM") != NULL) || (*v == '1'))
            {
                src = BM_AUTO_SRC_PM25;
            }

            s->auto_source = src;
            changed = true;
            printf("[CTRL] autoSource -> %s\n",
                   (src == BM_AUTO_SRC_PM25) ? "PM2.5" : "Temp");
        }
    }
    else if (strcmp(name, "setPm25On") == 0)
    {
        if (json_get_float(payload, len, "params", &f) ||
            json_get_float(payload, len, "pm25On", &f))
        {
            s->pm25_on = f;
            if (s->pm25_off >= f) s->pm25_off = f - 1.0f;
            changed = true;
            printf("[CTRL] pm25On -> %d ug/m3\n", (int)f);
        }
    }
    else if (strcmp(name, "setPm25Off") == 0)
    {
        if (json_get_float(payload, len, "params",  &f) ||
            json_get_float(payload, len, "pm25Off", &f))
        {
            s->pm25_off = f;
            if (s->pm25_on <= f) s->pm25_on = f + 1.0f;
            changed = true;
            printf("[CTRL] pm25Off -> %d ug/m3\n", (int)f);
        }
    }

    if (changed) s->control_seq++;
}

/* ====================================================================
 * รับข้อความจาก broker
 *
 * callback นี้ถูกเรียกจาก thread ภายในของ library MQTT ห้ามบล็อกนาน
 * เราจึงแค่แก้ค่าในบล็อกร่วม แล้วปล่อยให้ cloud_task เห็น control_seq
 * เปลี่ยนแล้วส่งสถานะกลับขึ้น dashboard เอง
 * ==================================================================== */
static void mqtt_event_cb(cy_mqtt_t handle, cy_mqtt_event_t event, void *user_data)
{
    (void)user_data;

    if (event.type == CY_MQTT_EVENT_TYPE_DISCONNECT)
    {
        printf("[CLOUD] broker ตัดการเชื่อมต่อ\n");
        bm_shared()->cloud_connected = 0u;
        mqtt_connected = false;
        cloud_online   = false;
        return;
    }

    if (event.type != CY_MQTT_EVENT_TYPE_SUBSCRIPTION_MESSAGE_RECEIVE) return;

    const cy_mqtt_received_msg_info_t *msg = &event.data.pub_msg.received_message;
    if ((msg->payload == NULL) || (msg->payload_len == 0u)) return;

    /* topic ไม่จบด้วย NUL ต้องคัดลอกก่อนใช้ */
    char   topic[96];
    size_t tlen = msg->topic_len;
    if (tlen >= sizeof(topic)) tlen = sizeof(topic) - 1u;
    memcpy(topic, msg->topic, tlen);
    topic[tlen] = '\0';

    printf("[CLOUD] <- %s : %.*s\n", topic, (int)msg->payload_len, msg->payload);

    if (strstr(topic, "/rpc/request/") != NULL)
    {
        /* {"method":"setPower","params":true} */
        const char *m = json_find(msg->payload, msg->payload_len, "method");
        if (m == NULL) return;

        while (*m == '"') m++;

        char   method[32];
        size_t i = 0u;
        while ((m[i] != '"') && (m[i] != '\0') && (i < (sizeof(method) - 1u)))
        {
            method[i] = m[i];
            i++;
        }
        method[i] = '\0';

        apply_command(method, msg->payload, msg->payload_len);

        /* ต้องตอบกลับด้วย requestId เดิม ไม่งั้น widget บน dashboard
         * จะค้างหมุนรอจน timeout ทั้งที่คำสั่งทำงานไปแล้ว */
        const char *id = strrchr(topic, '/');
        if (id != NULL)
        {
            volatile babymate_shared_t *s = bm_shared();
            char resp_topic[128];
            char resp[96];

            snprintf(resp_topic, sizeof(resp_topic), "%s%s",
                     TB_RPC_RESPONSE_PREFIX, id + 1);
            snprintf(resp, sizeof(resp),
                     "{\"power\":%s,\"auto\":%s,\"output\":%s}",
                     s->power_on  ? "true" : "false",
                     s->auto_mode ? "true" : "false",
                     s->output_on ? "true" : "false");

            cy_mqtt_publish_info_t pub;
            memset(&pub, 0, sizeof(pub));
            pub.qos         = CY_MQTT_QOS0;
            pub.topic       = resp_topic;
            pub.topic_len   = (uint16_t)strlen(resp_topic);
            pub.payload     = resp;
            pub.payload_len = strlen(resp);
            (void)cy_mqtt_publish(handle, &pub);
        }
    }
    else
    {
        /* shared attribute update เช่น {"power":true,"auto":false,"tempOn":30}
         * ลองทุกคีย์ ตัวที่ไม่มีใน payload จะถูกข้ามไปเอง */
        apply_command("setPower",   msg->payload, msg->payload_len);
        apply_command("setAuto",    msg->payload, msg->payload_len);
        apply_command("setTempOn",  msg->payload, msg->payload_len);
        apply_command("setTempOff", msg->payload, msg->payload_len);
        apply_command("setAutoSource", msg->payload, msg->payload_len);
        apply_command("setPm25On",     msg->payload, msg->payload_len);
        apply_command("setPm25Off",    msg->payload, msg->payload_len);
    }
}

/* subscribe ทั้งสองช่องทางหลังต่อ MQTT ได้แล้ว */
static bool mqtt_subscribe_control(void)
{
    cy_mqtt_subscribe_info_t sub[2];

    memset(sub, 0, sizeof(sub));
    sub[0].qos       = CY_MQTT_QOS0;
    sub[0].topic     = TB_RPC_REQUEST_SUB;
    sub[0].topic_len = (uint16_t)strlen(TB_RPC_REQUEST_SUB);
    sub[1].qos       = CY_MQTT_QOS0;
    sub[1].topic     = TB_ATTR_TOPIC;
    sub[1].topic_len = (uint16_t)strlen(TB_ATTR_TOPIC);

    const cy_rslt_t r = cy_mqtt_subscribe(mqtt_handle, sub, 2u);
    if (r != CY_RSLT_SUCCESS)
    {
        printf("[CLOUD] subscribe failed (0x%08lX)\n", (unsigned long)r);
        return false;
    }

    printf("[CLOUD] subscribed: RPC + shared attributes\n");
    return true;
}

/* แปลรหัสสาเหตุเป็นข้อความ ให้อ่านออกบน dashboard โดยไม่ต้องเปิดตาราง */
static const char *cry_reason_name(uint8_t r)
{
    switch (r)
    {
        case BM_CRY_TOO_HOT:   return "too_hot";
        case BM_CRY_TOO_COLD:  return "too_cold";
        case BM_CRY_DUSTY:     return "dusty";
        case BM_CRY_TILTED:    return "tilted";
        case BM_CRY_HUMID:     return "humid";
        case BM_CRY_NO_BREATH: return "no_breathing";
        default:               return "unknown";
    }
}

/* ส่งเหตุการณ์ "เด็กร้อง" พร้อมบริบทของเซนเซอร์ ณ วินาทีนั้น
 *
 * ส่งแยกจาก telemetry ปกติ เพราะเป็นเหตุการณ์ไม่ใช่ค่าต่อเนื่อง
 * บน ThingsBoard เอาไปทำ alarm หรือ timeline ได้ตรง ๆ */
static void publish_cry_event(const babymate_shared_t *s)
{
    char json[288];
    const int t10 = (int)(s->cry_temperature * 10.0f);
    const int h10 = (int)(s->cry_humidity    * 10.0f);

    snprintf(json, sizeof(json),
             "{\"cryEvent\":1,\"cryCount\":%lu,\"cryReason\":\"%s\","
             "\"cryAt\":%lu,\"cryGap\":%lu,"
             "\"cryTemp\":%d.%d,\"cryHum\":%d.%d,\"cryPm25\":%d,"
             "\"cryTilt\":%d,\"cryBpm\":%d,\"cryBreathState\":%u}",
             (unsigned long)s->cry_count, cry_reason_name(s->cry_reason),
             (unsigned long)s->cry_at_uptime_s, (unsigned long)s->cry_gap_s,
             t10 / 10, abs(t10 % 10), h10 / 10, abs(h10 % 10),
             (int)s->cry_pm25, (int)s->cry_tilt_deg, (int)s->cry_bpm,
             (unsigned)s->cry_breath_state);

    (void)publish_telemetry(json);
}

/* ส่งสถานะสวิตช์กลับขึ้นไปเป็น attribute เพื่อให้ปุ่มบน dashboard
 * แสดงค่าตรงกับตัวเครื่องจริง ไม่ว่าจะถูกสั่งมาจากทางไหน */
static void publish_state_attributes(void)
{
    volatile babymate_shared_t *s = bm_shared();
    char json[224];

    snprintf(json, sizeof(json),
             "{\"power\":%s,\"auto\":%s,\"output\":%s,"
             "\"autoSource\":\"%s\",\"tempOn\":%d,\"tempOff\":%d,"
             "\"pm25On\":%d,\"pm25Off\":%d}",
             s->power_on  ? "true" : "false",
             s->auto_mode ? "true" : "false",
             s->output_on ? "true" : "false",
             (s->auto_source == BM_AUTO_SRC_PM25) ? "pm25" : "temp",
             (int)s->temp_on_c, (int)s->temp_off_c,
             (int)s->pm25_on,   (int)s->pm25_off);

    cy_mqtt_publish_info_t pub;
    memset(&pub, 0, sizeof(pub));
    pub.qos         = CY_MQTT_QOS0;
    pub.topic       = TB_ATTR_TOPIC;
    pub.topic_len   = (uint16_t)strlen(TB_ATTR_TOPIC);
    pub.payload     = json;
    pub.payload_len = strlen(json);

    if (cy_mqtt_publish(mqtt_handle, &pub) == CY_RSLT_SUCCESS)
    {
        printf("[CLOUD] attr -> %s\n", json);
    }
}

static bool publish_telemetry(const char *json)
{
    cy_mqtt_publish_info_t pub;

    memset(&pub, 0, sizeof(pub));
    pub.qos         = CY_MQTT_QOS0;
    pub.topic       = TB_TELEMETRY_TOPIC;
    pub.topic_len   = (uint16_t)strlen(TB_TELEMETRY_TOPIC);
    pub.payload     = json;
    pub.payload_len = strlen(json);

    const cy_rslt_t result = cy_mqtt_publish(mqtt_handle, &pub);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[CLOUD] publish failed (0x%08lX)\n", (unsigned long)result);
        return false;
    }

    printf("[CLOUD] -> %s\n", json);
    return true;
}

/* ====================================================================
 * Task
 * ==================================================================== */

static void cloud_task(void *arg)
{
    (void)arg;
    char     payload[400];
    uint32_t seq = 0u;

    printf("[CLOUD] task started\n");

    if (!app_sdio_init())
    {
        printf("[CLOUD] SDIO init failed, task idle\n");
        vTaskDelete(NULL);
        return;
    }

    /* พยายาม init WiFi stack ซ้ำจนกว่าจะสำเร็จ แทนที่จะฆ่า task ทิ้ง
     *
     * ทำไมต้องแก้: cy_wcm_init ล้มได้จากสาเหตุชั่วคราว โดยเฉพาะหลัง reset
     * ผ่าน debugger ซึ่งไม่ได้ตัดไฟชิป WLAN จริง ทำให้ชิปค้างสถานะเดิมอยู่
     * ของเดิมเจอแบบนี้แล้ว vTaskDelete ทิ้งตัวเอง = WiFi ตายถาวรจนกว่าจะรีบูต
     * ทั้งที่รอบถัดไปอาจต่อได้ */
    {
        uint32_t attempt = 0u;
        while (!wifi_stack_init())
        {
            attempt++;
            printf("[CLOUD] WiFi stack init ล้มเหลว (ครั้งที่ %lu) ลองใหม่ใน %lu s\n",
                   (unsigned long)attempt,
                   (unsigned long)(CLOUD_RETRY_PERIOD_MS / 1000u));
            vTaskDelay(pdMS_TO_TICKS(CLOUD_RETRY_PERIOD_MS));
        }
    }

    /* วน scan -> connect -> mqtt ใหม่เรื่อย ๆ แทนที่จะยอมแพ้แล้วตายไป
     * ข้อดี 2 อย่าง:
     *   1. เปิด hotspot ทีหลังก็ต่อเองได้ ไม่ต้องรีบูตบอร์ด
     *   2. ดีบักง่ายขึ้นมาก เพราะ log การสแกนพิมพ์ซ้ำเรื่อย ๆ ไม่ต้องรีบจับตอนบูต */
    for (;;)
    {
        if (!cloud_online)
        {
            if (wifi_connect() && mqtt_start())
            {
                cloud_online = true;
            }
            else
            {
                printf("[CLOUD] retrying whole cycle in %lu s\n",
                       (unsigned long)(CLOUD_RETRY_PERIOD_MS / 1000u));
                vTaskDelay(pdMS_TO_TICKS(CLOUD_RETRY_PERIOD_MS));
                continue;
            }
        }

        seq++;

        /* ดึงค่าจริงจาก CM55 ผ่านบล็อกร่วมใน SOCMEM
         * ถ้า CM55 ยังบูตไม่เสร็จ (magic ยังไม่ถูกตั้ง) ให้ข้ามรอบนี้ไป
         * ดีกว่าส่งเลขศูนย์ขึ้น dashboard แล้วทำให้กราฟเพี้ยน */
        babymate_shared_t snap;
        if (!bm_snapshot(&snap))
        {
            printf("[CLOUD] CM55 ยังไม่พร้อม ข้ามรอบนี้\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* ThingsBoard รับ float ได้ตรง ๆ แต่ newlib-nano ที่ลิงก์อยู่ไม่รองรับ
         * %f ใน snprintf จึงคูณ 10 แล้วประกอบเป็นทศนิยมหนึ่งตำแหน่งเอง */
        const int t10   = (int)(snap.temperature * 10.0f);
        const int h10   = (int)(snap.humidity    * 10.0f);
        const int bpm10 = (int)(snap.bpm         * 10.0f);
        const int tilt  = (int)snap.tilt_deg;

        snprintf(payload, sizeof(payload),
                 "{\"seq\":%lu,\"uptime\":%lu,"
                 "\"temperature\":%d.%d,\"humidity\":%d.%d,\"pm25\":%d,"
                 "\"bpm\":%d.%d,\"breathState\":%u,\"tilt\":%d,"
                 "\"isMoving\":%u,\"fallenOver\":%u,\"babyCrying\":%u,"
                 "\"power\":%s,\"auto\":%s,\"output\":%s}",
                 (unsigned long)seq, (unsigned long)snap.uptime_s,
                 t10 / 10, abs(t10 % 10), h10 / 10, abs(h10 % 10),
                 (int)snap.pm25,
                 bpm10 / 10, abs(bpm10 % 10), (unsigned)snap.breath_state, tilt,
                 (unsigned)snap.is_moving, (unsigned)snap.fallen_over,
                 (unsigned)snap.baby_crying,
                 snap.power_on  ? "true" : "false",
                 snap.auto_mode ? "true" : "false",
                 snap.output_on ? "true" : "false");

        /* มีเหตุการณ์เด็กร้องค้างอยู่ ⇒ ส่งทันทีไม่ต้องรอรอบถัดไป
         * เคลียร์ธงหลังส่งสำเร็จ เพื่อไม่ให้เหตุการณ์เดิมถูกส่งซ้ำ */
        if (snap.cry_pending != 0u)
        {
            publish_cry_event(&snap);
            bm_shared()->cry_pending = 0u;
        }

        /* มีใครกดปุ่ม (จอ TFT หรือ dashboard) ระหว่างรอบที่แล้วกับรอบนี้
         * ⇒ ส่ง attribute ตามไปด้วย ให้ปุ่มบน dashboard เด้งตามทันที */
        const uint32_t cseq = bm_shared()->control_seq;
        if (cseq != last_control_seq)
        {
            last_control_seq = cseq;
            publish_state_attributes();
        }

        if (!publish_telemetry(payload))
        {
            /* ส่งไม่ผ่าน = รื้อเฉพาะชั้น MQTT ไม่แตะ WiFi
             *
             * wifi_connect() จะเห็นว่ายังเกาะ AP อยู่แล้วคืนค่าทันที
             * ⇒ ไม่ scan ใหม่ ไม่ join ใหม่ ลิงก์ WiFi ค้างไว้ตลอด
             * ลดงานของชิป WiFi ลงมาก ซึ่งเป็นตัวที่ทำให้มันค้าง */
            mqtt_connected = false;
            cloud_online   = false;
            (void)cy_mqtt_disconnect(mqtt_handle);
            (void)cy_mqtt_delete(mqtt_handle);
            mqtt_handle = NULL;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

void create_cloud_task(void)
{
    if (xTaskCreate(cloud_task, "Cloud", CLOUD_TASK_STACK_WORDS, NULL,
                    CLOUD_TASK_PRIORITY, NULL) != pdPASS)
    {
        printf("[CLOUD] ! xTaskCreate FAILED - not enough FreeRTOS heap !\n");
    }
}

bool cloud_is_connected(void)
{
    return mqtt_connected;
}
