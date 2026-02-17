#include "ui_settings.h"
#include "ui_manager.h"
#include "ui_styles.h"
#include "app_config.h"

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_manager_show_screen(UI_SCREEN_HOME);
}

lv_obj_t *ui_settings_create(void)
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
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    /* Content */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, lv_pct(90), lv_pct(75));
    lv_obj_align(content, LV_ALIGN_CENTER, 0, UI_PAD_LARGE);
    lv_obj_set_style_bg_color(content, UI_COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(content, 12, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, UI_PAD_LARGE, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, UI_PAD_MEDIUM, 0);

    /* Version info */
    lv_obj_t *ver = lv_label_create(content);
    lv_label_set_text(ver, "Field Tool v" FT_APP_VERSION "\n"
                           "ESP-IDF v5.5 | ESP32-P4\n"
                           "Waveshare Touch LCD 7B");
    lv_obj_set_style_text_color(ver, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_16, 0);

    /* UART info */
    lv_obj_t *uart = lv_label_create(content);
    lv_label_set_text_fmt(uart, "UART: Port %d, %d baud\n"
                                "TX=GPIO%d, RX=GPIO%d",
                          FT_UART_PORT_NUM, FT_UART_BAUD_RATE,
                          FT_UART_TXD, FT_UART_RXD);
    lv_obj_set_style_text_color(uart, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(uart, &lv_font_montserrat_14, 0);

    /* Placeholder note */
    lv_obj_t *note = lv_label_create(content);
    lv_label_set_text(note, "Settings persistence will be added in Phase 6.");
    lv_obj_set_style_text_color(note, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);

    return scr;
}
