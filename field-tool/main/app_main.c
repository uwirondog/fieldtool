#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "app_config.h"
#include "sdcard/sdcard_manager.h"
#include "serial/serial_monitor.h"
#include "wifi/wifi_manager.h"
#include "ui/ui_manager.h"
#include "ui/ui_home.h"

static const char *TAG = "FIELD_TOOL";

void app_main(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  FIELD TOOL v%s", FT_APP_VERSION);
    ESP_LOGI(TAG, "  ESP32-P4 Waveshare Touch LCD 7B");
    ESP_LOGI(TAG, "====================================");

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Mount SD card (non-fatal if missing — needs LDO channel 4 internally) */
    sdcard_manager_init();

    /* Initialize WiFi (C6 coprocessor via esp_hosted SDIO) */
    wifi_mgr_init();

    /* Initialize display + touch via Waveshare BSP */
    ESP_LOGI(TAG, "Initializing display...");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = true,
        }
    };
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    if (disp != NULL) {
        bsp_display_rotate(disp, LV_DISPLAY_ROTATION_180);
    }

    /* Create UI (must hold LVGL lock) */
    bsp_display_lock(0);
    ui_manager_init();

    /* Update SD card status on home screen */
    uint32_t free_mb = 0;
    bool sd_ok = sdcard_manager_is_mounted();
    if (sd_ok) {
        sdcard_manager_get_free_mb(&free_mb);
    }
    ui_home_update_sd_status(sd_ok, free_mb);

    bsp_display_unlock();

    /* Start serial monitor (USB Host + CH340 auto-detect, non-fatal) */
    ret = serial_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Serial monitor unavailable: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Field Tool ready. Touch the screen to navigate.");
    ESP_LOGI(TAG, "Plug a flowmeter into the USB-A port to monitor serial output.");
}
