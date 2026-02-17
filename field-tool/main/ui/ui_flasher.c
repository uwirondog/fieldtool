#include "ui_flasher.h"
#include "ui_manager.h"
#include "ui_styles.h"
#include "flasher/flasher_manager.h"
#include "serial/serial_monitor.h"
#include "app_config.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "UI_FLASH";

/* ── UI widgets ──────────────────────────────────────────────────────── */

static lv_obj_t *status_label   = NULL;
static lv_obj_t *usb_label      = NULL;
static lv_obj_t *key_label      = NULL;
static lv_obj_t *progress_bar   = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *log_label      = NULL;
static lv_obj_t *btn_flash      = NULL;
static lv_obj_t *btn_flash_lbl  = NULL;
static lv_obj_t *btn_virgin     = NULL;
static lv_obj_t *btn_virgin_lbl = NULL;

/* ── Refresh timer ───────────────────────────────────────────────────── */

static void update_btn_state(lv_obj_t *btn, bool enabled)
{
    if (!btn) return;
    if (enabled) {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    const flasher_status_t *st = flasher_get_status();
    bool busy = (st->state == FLASH_STATE_LOADING ||
                 st->state == FLASH_STATE_CONNECTING ||
                 st->state == FLASH_STATE_FLASHING);

    /* Update progress bar */
    if (progress_bar) {
        lv_bar_set_value(progress_bar, st->progress, LV_ANIM_ON);
    }
    if (progress_label) {
        lv_label_set_text_fmt(progress_label, "%d%%", st->progress);
    }

    /* Update status log */
    if (log_label) {
        lv_label_set_text(log_label, st->status_msg);
    }

    /* Update USB status */
    if (usb_label) {
        bool connected = serial_monitor_is_connected();
        if (busy) {
            lv_label_set_text(usb_label, "USB: Flashing in progress...");
            lv_obj_set_style_text_color(usb_label, lv_color_hex(0xFFA726), 0);
        } else {
            lv_label_set_text(usb_label, connected ? "USB: Connected (CH340)" : "USB: Not connected");
            lv_obj_set_style_text_color(usb_label,
                connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_DIM, 0);
        }
    }

    /* Update SD status */
    if (status_label) {
        if (st->firmware_ready) {
            lv_label_set_text(status_label, "Firmware: Ready on SD card");
            lv_obj_set_style_text_color(status_label, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(status_label, "Firmware: Missing files on SD card");
            lv_obj_set_style_text_color(status_label, UI_COLOR_LOG_ERROR, 0);
        }
    }

    /* Update key status */
    if (key_label) {
        if (st->key_ready) {
            lv_label_set_text(key_label, "Encryption Key: Ready on SD card");
            lv_obj_set_style_text_color(key_label, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(key_label, "Encryption Key: Not found (virgin flash disabled)");
            lv_obj_set_style_text_color(key_label, UI_COLOR_TEXT_DIM, 0);
        }
    }

    /* Enable/disable buttons */
    bool can_act = serial_monitor_is_connected() && !busy &&
                   (st->state == FLASH_STATE_IDLE ||
                    st->state == FLASH_STATE_DONE ||
                    st->state == FLASH_STATE_ERROR);

    update_btn_state(btn_flash, can_act && st->firmware_ready);
    update_btn_state(btn_virgin, can_act && st->firmware_ready && st->key_ready);

    /* Update button labels based on state */
    if (btn_flash_lbl) {
        if (busy) {
            lv_label_set_text(btn_flash_lbl, "FLASHING...");
        } else if (st->state == FLASH_STATE_DONE) {
            lv_label_set_text(btn_flash_lbl, "DONE - FLASH AGAIN");
        } else if (st->state == FLASH_STATE_ERROR) {
            lv_label_set_text(btn_flash_lbl, "RETRY FLASH");
        } else {
            lv_label_set_text(btn_flash_lbl, "REFLASH ENCRYPTED");
        }
    }
    if (btn_virgin_lbl) {
        if (busy) {
            lv_label_set_text(btn_virgin_lbl, "FLASHING...");
        } else if (st->state == FLASH_STATE_DONE) {
            lv_label_set_text(btn_virgin_lbl, "DONE - FLASH AGAIN");
        } else {
            lv_label_set_text(btn_virgin_lbl, "FLASH VIRGIN CHIP");
        }
    }
}

/* ── Button handlers ─────────────────────────────────────────────────── */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_manager_show_screen(UI_SCREEN_HOME);
}

static void on_flash_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Flash Device button pressed");
    flasher_start();
}

static void on_virgin_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Flash New Chip button pressed");
    flasher_start_virgin();
}

/* ── Screen creation ─────────────────────────────────────────────────── */

lv_obj_t *ui_flasher_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);

    /* ── Header bar ──────────────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 1024, UI_TAB_BAR_H);
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
    lv_label_set_text(title, "Firmware Flasher");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    /* ── Content area ────────────────────────────────────────────────── */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, 1024, 600 - UI_TAB_BAR_H);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, UI_TAB_BAR_H);
    lv_obj_set_style_bg_color(content, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, UI_PAD_SMALL, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* Firmware status */
    status_label = lv_label_create(content);
    lv_label_set_text(status_label, "Firmware: Checking...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, UI_COLOR_TEXT_DIM, 0);

    /* Encryption key status */
    key_label = lv_label_create(content);
    lv_label_set_text(key_label, "Encryption Key: Checking...");
    lv_obj_set_style_text_font(key_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key_label, UI_COLOR_TEXT_DIM, 0);

    /* USB status */
    usb_label = lv_label_create(content);
    lv_label_set_text(usb_label, "USB: Checking...");
    lv_obj_set_style_text_font(usb_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(usb_label, UI_COLOR_TEXT_DIM, 0);

    /* Progress bar */
    progress_bar = lv_bar_create(content);
    lv_obj_set_size(progress_bar, 900, 30);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_color(progress_bar, UI_COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar, 4, 0);
    lv_obj_set_style_radius(progress_bar, 4, LV_PART_INDICATOR);

    /* Progress percentage */
    progress_label = lv_label_create(content);
    lv_label_set_text(progress_label, "0%");
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(progress_label, UI_COLOR_TEXT, 0);

    /* Status log text */
    log_label = lv_label_create(content);
    lv_label_set_text(log_label, "Ready. Insert SD card with firmware and plug in device.");
    lv_obj_set_style_text_font(log_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(log_label, lv_color_hex(0xFFA726), 0);
    lv_obj_set_width(log_label, lv_pct(100));
    lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);

    /* ── Button row ──────────────────────────────────────────────────── */
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, UI_PAD_LARGE, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);

    /* FLASH DEVICE button (for already-encrypted chips) */
    btn_flash = lv_btn_create(btn_row);
    lv_obj_set_size(btn_flash, 380, 70);
    lv_obj_set_style_bg_color(btn_flash, UI_COLOR_TILE_FLASH, 0);
    lv_obj_set_style_radius(btn_flash, 12, 0);
    lv_obj_add_event_cb(btn_flash, on_flash_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_font(btn_flash, &lv_font_montserrat_20, 0);
    btn_flash_lbl = lv_label_create(btn_flash);
    lv_label_set_text(btn_flash_lbl, "REFLASH ENCRYPTED");
    lv_obj_center(btn_flash_lbl);

    /* FLASH NEW CHIP button (for virgin/unencrypted chips) */
    btn_virgin = lv_btn_create(btn_row);
    lv_obj_set_size(btn_virgin, 380, 70);
    lv_obj_set_style_bg_color(btn_virgin, lv_color_hex(0xFF8C00), 0);  /* Dark orange */
    lv_obj_set_style_radius(btn_virgin, 12, 0);
    lv_obj_add_event_cb(btn_virgin, on_virgin_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_text_font(btn_virgin, &lv_font_montserrat_20, 0);
    btn_virgin_lbl = lv_label_create(btn_virgin);
    lv_label_set_text(btn_virgin_lbl, "FLASH VIRGIN CHIP");
    lv_obj_center(btn_virgin_lbl);

    /* Check firmware and key on screen creation */
    flasher_check_firmware();
    flasher_check_encryption_key();

    /* Refresh timer (200ms) */
    lv_timer_create(refresh_timer_cb, 200, NULL);

    return scr;
}
