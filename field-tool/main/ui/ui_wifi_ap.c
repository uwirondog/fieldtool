#include "ui_wifi_ap.h"
#include "ui_manager.h"
#include "ui_styles.h"

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_manager_show_screen(UI_SCREEN_HOME);
}

lv_obj_t *ui_wifi_ap_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);

    /* Header */
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
    lv_obj_set_style_pad_column(header, UI_PAD_MEDIUM, 0);

    lv_obj_t *btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 80, 36);
    lv_obj_set_style_bg_color(btn_back, UI_COLOR_ACCENT, 0);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "< Back");
    lv_obj_center(lbl_back);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "WiFi Hotspot");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    /* Placeholder content */
    lv_obj_t *content = lv_label_create(scr);
    lv_label_set_text(content, "WiFi hotspot will be implemented in Phase 5.\n\n"
                               "Configure SSID and password to match your\n"
                               "lab WiFi so flowmeters auto-connect.");
    lv_obj_set_style_text_color(content, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(content, &lv_font_montserrat_16, 0);
    lv_obj_set_width(content, lv_pct(90));
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);

    return scr;
}
