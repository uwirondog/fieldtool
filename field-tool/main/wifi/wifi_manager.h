#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_STA_DISCONNECTED = 0,
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_STA_FAILED,
} wifi_sta_state_t;

typedef struct {
    wifi_sta_state_t sta_state;
    char             sta_ip[16];       /* "xxx.xxx.xxx.xxx" */
    bool             ap_active;
    int              ap_connected_count;
} wifi_mgr_status_t;

/* Call once from app_main, before any other wifi_mgr calls */
esp_err_t wifi_mgr_init(void);

/* Station (client) mode */
esp_err_t wifi_mgr_sta_connect(const char *ssid, const char *password);
esp_err_t wifi_mgr_sta_disconnect(void);

/* SoftAP mode */
esp_err_t wifi_mgr_ap_start(void);
esp_err_t wifi_mgr_ap_stop(void);

/* Status (safe to call from any task) */
const wifi_mgr_status_t *wifi_mgr_get_status(void);
