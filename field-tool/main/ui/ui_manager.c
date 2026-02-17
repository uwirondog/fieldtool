#include "ui_manager.h"
#include "ui_styles.h"
#include "ui_home.h"
#include "ui_serial_monitor.h"
#include "ui_flasher.h"
#include "ui_wifi_ap.h"
#include "ui_settings.h"
#include "esp_log.h"

static const char *TAG = "UI_MGR";

static lv_obj_t *screens[UI_SCREEN_COUNT];
static ui_screen_id_t current_screen = UI_SCREEN_HOME;

void ui_manager_init(void)
{
    ui_styles_init();

    /* Create all screens */
    screens[UI_SCREEN_HOME] = ui_home_create();
    screens[UI_SCREEN_SERIAL_MONITOR] = ui_serial_monitor_create();
    screens[UI_SCREEN_FLASHER] = ui_flasher_create();
    screens[UI_SCREEN_WIFI_AP] = ui_wifi_ap_create();
    screens[UI_SCREEN_SETTINGS] = ui_settings_create();

    /* Show home screen */
    lv_screen_load(screens[UI_SCREEN_HOME]);
    current_screen = UI_SCREEN_HOME;

    ESP_LOGI(TAG, "UI initialized");
}

void ui_manager_show_screen(ui_screen_id_t screen_id)
{
    if (screen_id >= UI_SCREEN_COUNT) {
        return;
    }
    if (screens[screen_id] == NULL) {
        ESP_LOGW(TAG, "Screen %d not created", screen_id);
        return;
    }

    lv_screen_load_anim(screens[screen_id], LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    current_screen = screen_id;
}

ui_screen_id_t ui_manager_get_current_screen(void)
{
    return current_screen;
}
