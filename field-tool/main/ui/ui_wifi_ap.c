#include "ui_wifi_ap.h"
#include "ui_manager.h"
#include "ui_styles.h"
#include "wifi/wifi_manager.h"
#include "wifi/firmware_download.h"
#include "flasher/flasher_manager.h"
#include "app_config.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "UI_WIFI";

/* ── UI widgets — left panel (WiFi STA) ─────────────────────────────── */

static lv_obj_t *ta_ssid         = NULL;
static lv_obj_t *ta_pass         = NULL;
static lv_obj_t *btn_connect     = NULL;
static lv_obj_t *btn_connect_lbl = NULL;
static lv_obj_t *sta_status_lbl  = NULL;

/* ── UI widgets — right panel (AP + Firmware) ───────────────────────── */

static lv_obj_t *ap_status_lbl   = NULL;
static lv_obj_t *btn_ap          = NULL;
static lv_obj_t *btn_ap_lbl      = NULL;
static lv_obj_t *fw_status_lbl   = NULL;
static lv_obj_t *fw_changelog    = NULL;
static lv_obj_t *fw_progress_bar = NULL;
static lv_obj_t *fw_progress_lbl = NULL;
static lv_obj_t *btn_check       = NULL;
static lv_obj_t *btn_check_lbl   = NULL;
static lv_obj_t *btn_download    = NULL;
static lv_obj_t *btn_download_lbl = NULL;

/* ── Keyboard ───────────────────────────────────────────────────────── */

static lv_obj_t *keyboard       = NULL;
static lv_obj_t *content_area   = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void update_btn_look(lv_obj_t *btn, bool enabled)
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

/* ── Refresh timer ───────────────────────────────────────────────────── */

static void refresh_timer_cb(lv_timer_t *timer)
{
    /* --- WiFi STA status --- */
    const wifi_mgr_status_t *ws = wifi_mgr_get_status();

    if (sta_status_lbl) {
        switch (ws->sta_state) {
        case WIFI_STA_DISCONNECTED:
            lv_label_set_text(sta_status_lbl, "Status: Disconnected");
            lv_obj_set_style_text_color(sta_status_lbl, UI_COLOR_TEXT_DIM, 0);
            break;
        case WIFI_STA_CONNECTING:
            lv_label_set_text(sta_status_lbl, "Status: Connecting...");
            lv_obj_set_style_text_color(sta_status_lbl, lv_color_hex(0xFFA726), 0);
            break;
        case WIFI_STA_CONNECTED:
            lv_label_set_text_fmt(sta_status_lbl, "Status: Connected  IP: %s", ws->sta_ip);
            lv_obj_set_style_text_color(sta_status_lbl, UI_COLOR_SUCCESS, 0);
            break;
        case WIFI_STA_FAILED:
            lv_label_set_text(sta_status_lbl, "Status: Connection failed");
            lv_obj_set_style_text_color(sta_status_lbl, UI_COLOR_LOG_ERROR, 0);
            break;
        }
    }

    if (btn_connect_lbl) {
        if (ws->sta_state == WIFI_STA_CONNECTED) {
            lv_label_set_text(btn_connect_lbl, "DISCONNECT");
        } else if (ws->sta_state == WIFI_STA_CONNECTING) {
            lv_label_set_text(btn_connect_lbl, "CONNECTING...");
        } else {
            lv_label_set_text(btn_connect_lbl, "CONNECT");
        }
    }
    update_btn_look(btn_connect, ws->sta_state != WIFI_STA_CONNECTING);

    /* --- SoftAP status --- */
    if (ap_status_lbl) {
        if (ws->ap_active) {
            lv_label_set_text_fmt(ap_status_lbl,
                "AP: Active  SSID: %s  Clients: %d",
                FT_WIFI_AP_SSID, ws->ap_connected_count);
            lv_obj_set_style_text_color(ap_status_lbl, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(ap_status_lbl, "AP: Stopped");
            lv_obj_set_style_text_color(ap_status_lbl, UI_COLOR_TEXT_DIM, 0);
        }
    }
    if (btn_ap_lbl) {
        lv_label_set_text(btn_ap_lbl, ws->ap_active ? "STOP AP" : "START AP");
    }

    /* --- Firmware download status --- */
    const fw_dl_status_t *fs = fw_dl_get_status();

    if (fw_status_lbl) {
        switch (fs->state) {
        case FW_DL_IDLE:
            lv_label_set_text(fw_status_lbl, "Firmware: Press CHECK to look for updates");
            lv_obj_set_style_text_color(fw_status_lbl, UI_COLOR_TEXT_DIM, 0);
            break;
        case FW_DL_CHECKING:
            lv_label_set_text(fw_status_lbl, "Firmware: Checking for updates...");
            lv_obj_set_style_text_color(fw_status_lbl, lv_color_hex(0xFFA726), 0);
            break;
        case FW_DL_UPDATE_AVAILABLE: {
            const char *sd_ver = (fs->sd_version[0] != '\0') ? fs->sd_version : "unknown";
            lv_label_set_text_fmt(fw_status_lbl, "SD: v%s | Latest: v%s — tap DOWNLOAD",
                                  sd_ver, fs->available_version);
            lv_obj_set_style_text_color(fw_status_lbl, lv_color_hex(0xFFA726), 0);
            break;
        }
        case FW_DL_NO_UPDATE: {
            const char *sd_ver = (fs->sd_version[0] != '\0') ? fs->sd_version : "unknown";
            lv_label_set_text_fmt(fw_status_lbl, "SD: v%s — up to date", sd_ver);
        }
            lv_obj_set_style_text_color(fw_status_lbl, UI_COLOR_SUCCESS, 0);
            break;
        case FW_DL_DOWNLOADING:
            lv_label_set_text_fmt(fw_status_lbl, "Firmware: Downloading... %d%%", fs->progress);
            lv_obj_set_style_text_color(fw_status_lbl, lv_color_hex(0xFFA726), 0);
            break;
        case FW_DL_VERIFYING:
            lv_label_set_text(fw_status_lbl, "Firmware: Verifying SHA256...");
            lv_obj_set_style_text_color(fw_status_lbl, lv_color_hex(0xFFA726), 0);
            break;
        case FW_DL_DONE:
            lv_label_set_text_fmt(fw_status_lbl, "SD: v%s — downloaded and verified!", fs->available_version);
            lv_obj_set_style_text_color(fw_status_lbl, UI_COLOR_SUCCESS, 0);
            break;
        case FW_DL_ERROR:
            lv_label_set_text_fmt(fw_status_lbl, "Firmware: Error - %s", fs->error_msg);
            lv_obj_set_style_text_color(fw_status_lbl, UI_COLOR_LOG_ERROR, 0);
            break;
        }
    }

    if (fw_changelog && fs->changelog[0] != '\0' &&
        (fs->state == FW_DL_UPDATE_AVAILABLE || fs->state == FW_DL_DONE)) {
        lv_label_set_text(fw_changelog, fs->changelog);
    }

    if (fw_progress_bar) {
        lv_bar_set_value(fw_progress_bar, fs->progress, LV_ANIM_ON);
    }
    if (fw_progress_lbl) {
        lv_label_set_text_fmt(fw_progress_lbl, "%d%%", fs->progress);
    }

    /* Enable check button when STA connected and not busy */
    bool sta_ok = (ws->sta_state == WIFI_STA_CONNECTED);
    bool dl_busy = (fs->state == FW_DL_CHECKING || fs->state == FW_DL_DOWNLOADING ||
                    fs->state == FW_DL_VERIFYING);
    update_btn_look(btn_check, sta_ok && !dl_busy);

    if (btn_check_lbl) {
        lv_label_set_text(btn_check_lbl, dl_busy ? "CHECKING..." : "CHECK FOR UPDATES");
    }

    /* Enable download button when update available and not downloading */
    bool can_dl = (fs->state == FW_DL_UPDATE_AVAILABLE) && !dl_busy;
    update_btn_look(btn_download, can_dl);
    if (btn_download_lbl) {
        if (fs->state == FW_DL_DONE) {
            lv_label_set_text(btn_download_lbl, "DOWNLOADED");
        } else if (fs->state == FW_DL_DOWNLOADING || fs->state == FW_DL_VERIFYING) {
            lv_label_set_text(btn_download_lbl, "DOWNLOADING...");
        } else {
            lv_label_set_text(btn_download_lbl, "DOWNLOAD TO SD");
        }
    }

    /* Notify flasher to re-check firmware if download just completed */
    if (fs->state == FW_DL_DONE) {
        flasher_check_firmware();
    }
}

/* ── Button handlers ─────────────────────────────────────────────────── */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    /* Hide keyboard before leaving */
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    ui_manager_show_screen(UI_SCREEN_HOME);
}

static void on_connect_clicked(lv_event_t *e)
{
    (void)e;
    const wifi_mgr_status_t *ws = wifi_mgr_get_status();

    if (ws->sta_state == WIFI_STA_CONNECTED) {
        ESP_LOGI(TAG, "Disconnecting STA");
        wifi_mgr_sta_disconnect();
    } else {
        const char *ssid = lv_textarea_get_text(ta_ssid);
        const char *pass = lv_textarea_get_text(ta_pass);
        if (ssid && ssid[0] != '\0') {
            ESP_LOGI(TAG, "Connecting to: %s", ssid);
            wifi_mgr_sta_connect(ssid, pass);
        }
    }
    /* Hide keyboard */
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void on_ap_clicked(lv_event_t *e)
{
    (void)e;
    const wifi_mgr_status_t *ws = wifi_mgr_get_status();
    if (ws->ap_active) {
        wifi_mgr_ap_stop();
    } else {
        wifi_mgr_ap_start();
    }
}

static void on_check_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Checking for firmware updates");
    fw_dl_check_update();
}

static void on_download_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Starting firmware download");
    fw_dl_start_download();
}

/* ── Keyboard show/hide on textarea focus ────────────────────────────── */

static void on_ta_focused(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (keyboard) {
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_ta_defocused(lv_event_t *e)
{
    (void)e;
    /* Don't hide keyboard here — let the keyboard's own ready/cancel do it */
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* ── Screen creation ─────────────────────────────────────────────────── */

lv_obj_t *ui_wifi_ap_create(void)
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
    lv_label_set_text(title, "WiFi & Firmware");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    /* ── Content area (two columns) ──────────────────────────────────── */
    content_area = lv_obj_create(scr);
    lv_obj_set_size(content_area, 1024, 600 - UI_TAB_BAR_H);
    lv_obj_align(content_area, LV_ALIGN_TOP_MID, 0, UI_TAB_BAR_H);
    lv_obj_set_style_bg_color(content_area, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_area, 0, 0);
    lv_obj_set_style_radius(content_area, 0, 0);
    lv_obj_set_style_pad_all(content_area, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(content_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(content_area, UI_PAD_MEDIUM, 0);
    lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);

    /* ── LEFT PANEL — WiFi Station ───────────────────────────────────── */
    lv_obj_t *left = lv_obj_create(content_area);
    lv_obj_set_size(left, 470, lv_pct(100));
    lv_obj_set_style_bg_color(left, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left, 8, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, UI_PAD_SMALL, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    /* Section title */
    lv_obj_t *sta_title = lv_label_create(left);
    lv_label_set_text(sta_title, "WiFi Connection");
    lv_obj_set_style_text_font(sta_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sta_title, UI_COLOR_TEXT, 0);

    /* SSID label + textarea */
    lv_obj_t *ssid_lbl = lv_label_create(left);
    lv_label_set_text(ssid_lbl, "SSID:");
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_lbl, UI_COLOR_TEXT_DIM, 0);

    ta_ssid = lv_textarea_create(left);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "Enter WiFi SSID");
    lv_textarea_set_text(ta_ssid, "paradise");
    lv_obj_set_width(ta_ssid, lv_pct(100));
    lv_obj_set_style_bg_color(ta_ssid, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_text_color(ta_ssid, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(ta_ssid, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(ta_ssid, 1, 0);
    lv_obj_add_event_cb(ta_ssid, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_ssid, on_ta_defocused, LV_EVENT_DEFOCUSED, NULL);

    /* Password label + textarea */
    lv_obj_t *pass_lbl = lv_label_create(left);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pass_lbl, UI_COLOR_TEXT_DIM, 0);

    ta_pass = lv_textarea_create(left);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_placeholder_text(ta_pass, "Enter password");
    lv_textarea_set_password_mode(ta_pass, false);
    lv_textarea_set_text(ta_pass, "foilersarecool");
    lv_obj_set_width(ta_pass, lv_pct(100));
    lv_obj_set_style_bg_color(ta_pass, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_text_color(ta_pass, UI_COLOR_TEXT, 0);
    lv_obj_set_style_border_color(ta_pass, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(ta_pass, 1, 0);
    lv_obj_add_event_cb(ta_pass, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_pass, on_ta_defocused, LV_EVENT_DEFOCUSED, NULL);

    /* Connect button */
    btn_connect = lv_btn_create(left);
    lv_obj_set_size(btn_connect, lv_pct(100), 50);
    lv_obj_set_style_bg_color(btn_connect, UI_COLOR_TILE_WIFI, 0);
    lv_obj_set_style_radius(btn_connect, 8, 0);
    lv_obj_add_event_cb(btn_connect, on_connect_clicked, LV_EVENT_CLICKED, NULL);
    btn_connect_lbl = lv_label_create(btn_connect);
    lv_label_set_text(btn_connect_lbl, "CONNECT");
    lv_obj_set_style_text_font(btn_connect_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(btn_connect_lbl);

    /* Status */
    sta_status_lbl = lv_label_create(left);
    lv_label_set_text(sta_status_lbl, "Status: Disconnected");
    lv_obj_set_style_text_font(sta_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sta_status_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_width(sta_status_lbl, lv_pct(100));
    lv_label_set_long_mode(sta_status_lbl, LV_LABEL_LONG_WRAP);

    /* ── RIGHT PANEL — SoftAP + Firmware ─────────────────────────────── */
    lv_obj_t *right = lv_obj_create(content_area);
    lv_obj_set_size(right, 470, lv_pct(100));
    lv_obj_set_style_bg_color(right, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right, 8, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, UI_PAD_MEDIUM, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, UI_PAD_SMALL, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* --- SoftAP section --- */
    lv_obj_t *ap_title = lv_label_create(right);
    lv_label_set_text(ap_title, "SoftAP Hotspot");
    lv_obj_set_style_text_font(ap_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ap_title, UI_COLOR_TEXT, 0);

    /* AP status row: label + button */
    lv_obj_t *ap_row = lv_obj_create(right);
    lv_obj_set_size(ap_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ap_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ap_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(ap_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ap_row, 0, 0);
    lv_obj_set_style_pad_all(ap_row, 0, 0);

    ap_status_lbl = lv_label_create(ap_row);
    lv_label_set_text(ap_status_lbl, "AP: Stopped");
    lv_obj_set_style_text_font(ap_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ap_status_lbl, UI_COLOR_TEXT_DIM, 0);

    btn_ap = lv_btn_create(ap_row);
    lv_obj_set_size(btn_ap, 120, 36);
    lv_obj_set_style_bg_color(btn_ap, lv_color_hex(0xFF8C00), 0);
    lv_obj_set_style_radius(btn_ap, 6, 0);
    lv_obj_add_event_cb(btn_ap, on_ap_clicked, LV_EVENT_CLICKED, NULL);
    btn_ap_lbl = lv_label_create(btn_ap);
    lv_label_set_text(btn_ap_lbl, "START AP");
    lv_obj_set_style_text_font(btn_ap_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_ap_lbl);

    /* --- Divider --- */
    lv_obj_t *divider = lv_obj_create(right);
    lv_obj_set_size(divider, lv_pct(100), 2);
    lv_obj_set_style_bg_color(divider, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    /* --- Firmware section --- */
    lv_obj_t *fw_title = lv_label_create(right);
    lv_label_set_text(fw_title, "Firmware Download");
    lv_obj_set_style_text_font(fw_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(fw_title, UI_COLOR_TEXT, 0);

    fw_status_lbl = lv_label_create(right);
    lv_label_set_text(fw_status_lbl, "Firmware: Press CHECK to look for updates");
    lv_obj_set_style_text_font(fw_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fw_status_lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_width(fw_status_lbl, lv_pct(100));
    lv_label_set_long_mode(fw_status_lbl, LV_LABEL_LONG_WRAP);

    /* Changelog (hidden until update available) */
    fw_changelog = lv_label_create(right);
    lv_label_set_text(fw_changelog, "");
    lv_obj_set_style_text_font(fw_changelog, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(fw_changelog, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_width(fw_changelog, lv_pct(100));
    lv_label_set_long_mode(fw_changelog, LV_LABEL_LONG_WRAP);

    /* Progress bar */
    fw_progress_bar = lv_bar_create(right);
    lv_obj_set_size(fw_progress_bar, lv_pct(100), 20);
    lv_bar_set_range(fw_progress_bar, 0, 100);
    lv_bar_set_value(fw_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fw_progress_bar, lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_bg_color(fw_progress_bar, UI_COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_radius(fw_progress_bar, 4, 0);
    lv_obj_set_style_radius(fw_progress_bar, 4, LV_PART_INDICATOR);

    fw_progress_lbl = lv_label_create(right);
    lv_label_set_text(fw_progress_lbl, "0%");
    lv_obj_set_style_text_font(fw_progress_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(fw_progress_lbl, UI_COLOR_TEXT_DIM, 0);

    /* Button row: CHECK + DOWNLOAD */
    lv_obj_t *fw_btn_row = lv_obj_create(right);
    lv_obj_set_size(fw_btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fw_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fw_btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(fw_btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fw_btn_row, 0, 0);
    lv_obj_set_style_pad_all(fw_btn_row, 0, 0);

    btn_check = lv_btn_create(fw_btn_row);
    lv_obj_set_size(btn_check, 190, 44);
    lv_obj_set_style_bg_color(btn_check, UI_COLOR_TILE_WIFI, 0);
    lv_obj_set_style_radius(btn_check, 8, 0);
    lv_obj_add_event_cb(btn_check, on_check_clicked, LV_EVENT_CLICKED, NULL);
    btn_check_lbl = lv_label_create(btn_check);
    lv_label_set_text(btn_check_lbl, "CHECK FOR UPDATES");
    lv_obj_set_style_text_font(btn_check_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(btn_check_lbl);

    btn_download = lv_btn_create(fw_btn_row);
    lv_obj_set_size(btn_download, 190, 44);
    lv_obj_set_style_bg_color(btn_download, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(btn_download, 8, 0);
    lv_obj_add_event_cb(btn_download, on_download_clicked, LV_EVENT_CLICKED, NULL);
    btn_download_lbl = lv_label_create(btn_download);
    lv_label_set_text(btn_download_lbl, "DOWNLOAD TO SD");
    lv_obj_set_style_text_font(btn_download_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(btn_download_lbl);
    update_btn_look(btn_download, false);  /* Disabled until update available */

    /* ── On-screen keyboard (hidden by default) ──────────────────────── */
    keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(keyboard, 1024, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, on_kb_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, on_kb_ready, LV_EVENT_CANCEL, NULL);

    /* ── Refresh timer (200ms) ───────────────────────────────────────── */
    lv_timer_create(refresh_timer_cb, 200, NULL);

    return scr;
}
