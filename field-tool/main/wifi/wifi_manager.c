#include "wifi_manager.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include <string.h>

static const char *TAG = "WIFI_MGR";

static wifi_mgr_status_t s_status = {0};
static bool s_initialized = false;
static bool s_sta_wanted  = false;
static bool s_ap_wanted   = false;

static char s_sta_ssid[33] = {0};
static char s_sta_pass[65] = {0};
static int  s_sta_retries  = 0;
#define MAX_STA_RETRIES 5

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif  = NULL;

/* ── Helpers ────────────────────────────────────────────────────────── */

static wifi_mode_t compute_mode(void)
{
    if (s_sta_wanted && s_ap_wanted) return WIFI_MODE_APSTA;
    if (s_sta_wanted)                return WIFI_MODE_STA;
    if (s_ap_wanted)                 return WIFI_MODE_AP;
    return WIFI_MODE_NULL;
}

static esp_err_t apply_mode(void)
{
    wifi_mode_t mode = compute_mode();
    if (mode == WIFI_MODE_NULL) {
        return esp_wifi_stop();
    }

    esp_err_t err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) return err;

    /* Configure STA if wanted */
    if (s_sta_wanted && s_sta_ssid[0] != '\0') {
        wifi_config_t sta_cfg = {0};
        memcpy(sta_cfg.sta.ssid, s_sta_ssid, sizeof(sta_cfg.sta.ssid));
        memcpy(sta_cfg.sta.password, s_sta_pass, sizeof(sta_cfg.sta.password));
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        if (err != ESP_OK) return err;
    }

    /* Configure AP if wanted */
    if (s_ap_wanted) {
        wifi_config_t ap_cfg = {
            .ap = {
                .max_connection = FT_WIFI_AP_MAX_CONN,
                .channel = FT_WIFI_AP_CHANNEL,
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        };
        strncpy((char *)ap_cfg.ap.ssid, FT_WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
        ap_cfg.ap.ssid_len = strlen(FT_WIFI_AP_SSID);
        strncpy((char *)ap_cfg.ap.password, FT_WIFI_AP_PASS, sizeof(ap_cfg.ap.password) - 1);
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (err != ESP_OK) return err;
    }

    return esp_wifi_start();
}

/* ── Event handler ──────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            s_status.sta_state = WIFI_STA_CONNECTING;
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *dis = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "STA disconnected, reason=%d", dis->reason);
            if (s_sta_wanted && s_sta_retries < MAX_STA_RETRIES) {
                s_sta_retries++;
                ESP_LOGW(TAG, "STA retry %d/%d", s_sta_retries, MAX_STA_RETRIES);
                s_status.sta_state = WIFI_STA_CONNECTING;
                esp_wifi_connect();
            } else if (s_sta_wanted) {
                ESP_LOGE(TAG, "STA connection failed after %d retries", MAX_STA_RETRIES);
                s_status.sta_state = WIFI_STA_FAILED;
            } else {
                s_status.sta_state = WIFI_STA_DISCONNECTED;
            }
            s_status.sta_ip[0] = '\0';
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t *)data;
            s_status.ap_connected_count++;
            ESP_LOGI(TAG, "AP: station joined (AID=%d), total=%d",
                     evt->aid, s_status.ap_connected_count);
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;
            if (s_status.ap_connected_count > 0) s_status.ap_connected_count--;
            ESP_LOGI(TAG, "AP: station left (AID=%d), total=%d",
                     evt->aid, s_status.ap_connected_count);
            break;
        }

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_status.sta_ip, sizeof(s_status.sta_ip),
                 IPSTR, IP2STR(&evt->ip_info.ip));
        s_status.sta_state = WIFI_STA_CONNECTED;
        s_sta_retries = 0;
        ESP_LOGI(TAG, "STA connected, IP: %s", s_status.sta_ip);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

esp_err_t wifi_mgr_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing WiFi...");

    /* Network interface + event loop (NVS already initialized in app_main) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    /* WiFi init with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_sta_connect(const char *ssid, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';
    strncpy(s_sta_pass, password, sizeof(s_sta_pass) - 1);
    s_sta_pass[sizeof(s_sta_pass) - 1] = '\0';

    s_sta_wanted = true;
    s_sta_retries = 0;
    s_status.sta_state = WIFI_STA_CONNECTING;

    ESP_LOGI(TAG, "Connecting to SSID: %s", s_sta_ssid);
    return apply_mode();
}

esp_err_t wifi_mgr_sta_disconnect(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_sta_wanted = false;
    s_status.sta_state = WIFI_STA_DISCONNECTED;
    s_status.sta_ip[0] = '\0';

    esp_wifi_disconnect();
    return apply_mode();
}

esp_err_t wifi_mgr_ap_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_ap_wanted = true;
    s_status.ap_active = true;
    s_status.ap_connected_count = 0;

    ESP_LOGI(TAG, "Starting SoftAP: %s", FT_WIFI_AP_SSID);
    return apply_mode();
}

esp_err_t wifi_mgr_ap_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    s_ap_wanted = false;
    s_status.ap_active = false;
    s_status.ap_connected_count = 0;

    ESP_LOGI(TAG, "Stopping SoftAP");
    return apply_mode();
}

const wifi_mgr_status_t *wifi_mgr_get_status(void)
{
    return &s_status;
}
