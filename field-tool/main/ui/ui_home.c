#include "ui_home.h"
#include "ui_manager.h"
#include "ui_styles.h"
#include "app_config.h"
#include "esp_log.h"

static const char *TAG = "UI_HOME";

static lv_obj_t *sd_status_label;

/* ── Tile click handlers ─────────────────────────────────────────────── */

static void on_serial_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Serial Monitor selected");
    ui_manager_show_screen(UI_SCREEN_SERIAL_MONITOR);
}

static void on_flasher_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Flasher selected");
    ui_manager_show_screen(UI_SCREEN_FLASHER);
}

static void on_wifi_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "WiFi AP selected");
    ui_manager_show_screen(UI_SCREEN_WIFI_AP);
}

static void on_settings_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Settings selected");
    ui_manager_show_screen(UI_SCREEN_SETTINGS);
}

/* ── Helper: create a tile button ────────────────────────────────────── */

static lv_obj_t *create_tile(lv_obj_t *parent, const char *title,
                             const char *subtitle, lv_color_t color,
                             lv_event_cb_t click_cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 280, 200);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_pad_all(btn, UI_PAD_LARGE, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, click_cb, LV_EVENT_CLICKED, NULL);

    /* Pressed state feedback */
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_PRESSED);

    /* Title */
    lv_obj_t *lbl_title = lv_label_create(btn);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);

    /* Subtitle */
    lv_obj_t *lbl_sub = lv_label_create(btn);
    lv_label_set_text(lbl_sub, subtitle);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);

    return btn;
}

/* ── Screen creation ─────────────────────────────────────────────────── */

lv_obj_t *ui_home_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);

    /* ── Header bar ──────────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, lv_pct(100), UI_TAB_BAR_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, UI_PAD_LARGE, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Field Tool v" FT_APP_VERSION);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    sd_status_label = lv_label_create(header);
    lv_label_set_text(sd_status_label, "SD: checking...");
    lv_obj_set_style_text_font(sd_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sd_status_label, UI_COLOR_TEXT_DIM, 0);

    /* ── Main content: 3 tiles + settings ────────────────────────────── */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, UI_TAB_BAR_H + UI_PAD_MEDIUM);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, UI_PAD_LARGE, 0);

    create_tile(content, "Serial\nMonitor", "View device logs",
                UI_COLOR_TILE_SERIAL, on_serial_clicked);

    create_tile(content, "Flash\nDevice", "Program firmware",
                UI_COLOR_TILE_FLASH, on_flasher_clicked);

    create_tile(content, "WiFi\nHotspot", "Broadcast AP",
                UI_COLOR_TILE_WIFI, on_wifi_clicked);

    /* ── Settings button (bottom-right) ──────────────────────────────── */
    lv_obj_t *btn_settings = lv_btn_create(scr);
    lv_obj_set_size(btn_settings, 140, 44);
    lv_obj_align(btn_settings, LV_ALIGN_BOTTOM_RIGHT, -UI_PAD_LARGE, -UI_PAD_LARGE);
    lv_obj_set_style_bg_color(btn_settings, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_settings, 8, 0);
    lv_obj_add_event_cb(btn_settings, on_settings_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_settings = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings, "Settings");
    lv_obj_set_style_text_font(lbl_settings, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_settings);

    return scr;
}

void ui_home_update_sd_status(bool mounted, uint32_t free_mb)
{
    if (sd_status_label == NULL) {
        return;
    }
    if (mounted) {
        lv_label_set_text_fmt(sd_status_label, "SD: %lu MB free", (unsigned long)free_mb);
        lv_obj_set_style_text_color(sd_status_label, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(sd_status_label, "SD: not inserted");
        lv_obj_set_style_text_color(sd_status_label, UI_COLOR_HIGHLIGHT, 0);
    }
}
