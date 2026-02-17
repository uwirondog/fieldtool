#include "ui_serial_monitor.h"
#include "ui_manager.h"
#include "ui_styles.h"
#include "serial/serial_monitor.h"
#include "app_config.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "UI_SERIAL";

/* ── UI state ────────────────────────────────────────────────────────── */

static lv_obj_t *log_container = NULL;    /* Scrollable container for log lines */
static lv_obj_t *status_label = NULL;     /* Bottom status bar */
static lv_obj_t *btn_pause_label = NULL;

static bool s_paused = false;
static uint32_t s_last_displayed_count = 0;

/* Max lines shown on screen (older lines get deleted from the LVGL container) */
#define MAX_DISPLAY_LINES  200

/* ── Color for log level ─────────────────────────────────────────────── */

static lv_color_t level_color(char level)
{
    switch (level) {
    case 'I': return UI_COLOR_LOG_INFO;
    case 'W': return UI_COLOR_LOG_WARN;
    case 'E': return UI_COLOR_LOG_ERROR;
    case 'D': return UI_COLOR_LOG_DEBUG;
    case 'V': return UI_COLOR_LOG_DEBUG;
    default:  return UI_COLOR_LOG_DEFAULT;
    }
}

/* ── Refresh timer (runs in LVGL task context) ───────────────────────── */

static void refresh_timer_cb(lv_timer_t *timer)
{
    if (s_paused || log_container == NULL) {
        return;
    }

    uint32_t total = serial_monitor_get_count();
    if (total == s_last_displayed_count) {
        /* Update status bar even if no new lines */
        if (status_label) {
            bool connected = serial_monitor_is_connected();
            lv_label_set_text_fmt(status_label, "%s | %d baud | Lines: %lu",
                                  connected ? "USB: Connected" : "USB: Waiting...",
                                  FT_UART_BAUD_RATE,
                                  (unsigned long)serial_monitor_get_total_lines());
            lv_obj_set_style_text_color(status_label,
                                        connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_DIM, 0);
        }
        return;
    }

    /* New lines available — add them */
    uint32_t new_start = s_last_displayed_count;
    uint32_t new_count = total - new_start;

    /* Read new entries */
    log_entry_t entry;
    for (uint32_t i = 0; i < new_count; i++) {
        if (!serial_monitor_get_entry(new_start + i, &entry)) {
            break;
        }

        /* Build display text: "[I] (1234) TAG: message" */
        char display_text[384];
        if (entry.tag[0] != '\0') {
            snprintf(display_text, sizeof(display_text), "[%c] (%lu) %s: %s",
                     entry.level, (unsigned long)entry.timestamp_ms,
                     entry.tag, entry.message);
        } else {
            snprintf(display_text, sizeof(display_text), "%s", entry.message);
        }

        /* Create label for this line */
        lv_obj_t *line_label = lv_label_create(log_container);
        lv_label_set_text(line_label, display_text);
        lv_obj_set_width(line_label, lv_pct(100));
        lv_label_set_long_mode(line_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(line_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(line_label, level_color(entry.level), 0);
        lv_obj_set_style_pad_ver(line_label, 1, 0);
    }

    s_last_displayed_count = total;

    /* Trim old lines from display to keep memory bounded */
    uint32_t child_count = lv_obj_get_child_count(log_container);
    while (child_count > MAX_DISPLAY_LINES) {
        lv_obj_t *oldest = lv_obj_get_child(log_container, 0);
        if (oldest) {
            lv_obj_delete(oldest);
        }
        child_count--;
    }

    /* Auto-scroll to bottom */
    lv_obj_scroll_to_y(log_container, LV_COORD_MAX, LV_ANIM_OFF);

    /* Update status */
    if (status_label) {
        bool connected = serial_monitor_is_connected();
        lv_label_set_text_fmt(status_label, "%s | %d baud | Lines: %lu",
                              connected ? "USB: Connected" : "USB: Waiting...",
                              FT_UART_BAUD_RATE,
                              (unsigned long)serial_monitor_get_total_lines());
        lv_obj_set_style_text_color(status_label,
                                    connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_DIM, 0);
    }
}

/* ── Button handlers ─────────────────────────────────────────────────── */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_manager_show_screen(UI_SCREEN_HOME);
}

static void on_pause_clicked(lv_event_t *e)
{
    (void)e;
    s_paused = !s_paused;
    if (btn_pause_label) {
        lv_label_set_text(btn_pause_label, s_paused ? "Resume" : "Pause");
    }
    if (!s_paused) {
        /* Catch up: reset display counter to force refresh */
        /* We keep the ring buffer position but re-render from current state */
        uint32_t total = serial_monitor_get_count();
        if (total > MAX_DISPLAY_LINES) {
            s_last_displayed_count = total - MAX_DISPLAY_LINES;
        } else {
            s_last_displayed_count = 0;
        }
        /* Clear and re-render */
        lv_obj_clean(log_container);
    }
    ESP_LOGI(TAG, "Monitor %s", s_paused ? "paused" : "resumed");
}

static void on_clear_clicked(lv_event_t *e)
{
    (void)e;
    serial_monitor_clear();
    s_last_displayed_count = 0;
    if (log_container) {
        lv_obj_clean(log_container);
    }
    ESP_LOGI(TAG, "Log cleared");
}

/* ── Screen creation ─────────────────────────────────────────────────── */

lv_obj_t *ui_serial_monitor_create(void)
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
    lv_obj_set_style_pad_hor(header, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, UI_PAD_SMALL, 0);

    /* Back button */
    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_center(lbl_back);

    /* Title */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Serial Monitor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(title, 1);

    /* Pause button */
    lv_obj_t *btn_pause = lv_btn_create(header);
    lv_obj_set_size(btn_pause, 90, 36);
    lv_obj_set_style_bg_color(btn_pause, UI_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(btn_pause, on_pause_clicked, LV_EVENT_CLICKED, NULL);
    btn_pause_label = lv_label_create(btn_pause);
    lv_label_set_text(btn_pause_label, "Pause");
    lv_obj_center(btn_pause_label);

    /* Clear button */
    lv_obj_t *btn_clear = lv_btn_create(header);
    lv_obj_set_size(btn_clear, 80, 36);
    lv_obj_set_style_bg_color(btn_clear, UI_COLOR_HIGHLIGHT, 0);
    lv_obj_add_event_cb(btn_clear, on_clear_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_clear = lv_label_create(btn_clear);
    lv_label_set_text(lbl_clear, "Clear");
    lv_obj_center(lbl_clear);

    /* ── Log container (scrollable) ──────────────────────────────────── */
    /* Screen is 600px tall: 48px header + log area + 28px status bar */
    log_container = lv_obj_create(scr);
    lv_obj_set_size(log_container, 1024, 600 - UI_TAB_BAR_H - 28);
    lv_obj_align(log_container, LV_ALIGN_TOP_MID, 0, UI_TAB_BAR_H);
    lv_obj_set_style_bg_color(log_container, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_bg_opa(log_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(log_container, 0, 0);
    lv_obj_set_style_radius(log_container, 0, 0);
    lv_obj_set_style_pad_all(log_container, UI_PAD_SMALL, 0);
    lv_obj_set_flex_flow(log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(log_container, 0, 0);
    lv_obj_set_scroll_dir(log_container, LV_DIR_VER);
    lv_obj_add_flag(log_container, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* ── Status bar (bottom) ─────────────────────────────────────────── */
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "USB: Waiting for device...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_size(status_label, 1024, 28);
    lv_obj_set_style_pad_hor(status_label, UI_PAD_MEDIUM, 0);
    lv_obj_set_style_pad_ver(status_label, 4, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(status_label, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);

    /* ── Refresh timer (100ms = ~10fps update rate) ──────────────────── */
    lv_timer_create(refresh_timer_cb, 100, NULL);

    return scr;
}
