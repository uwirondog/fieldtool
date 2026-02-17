#pragma once

#include "lvgl.h"

/* Log level colors */
#define UI_COLOR_LOG_INFO     lv_color_hex(0x00CC00)    /* Green */
#define UI_COLOR_LOG_WARN     lv_color_hex(0xFFCC00)    /* Yellow */
#define UI_COLOR_LOG_ERROR    lv_color_hex(0xFF3333)    /* Red */
#define UI_COLOR_LOG_DEBUG    lv_color_hex(0x888888)    /* Gray */
#define UI_COLOR_LOG_DEFAULT  lv_color_hex(0xCCCCCC)    /* Light gray */

/* UI theme colors */
#define UI_COLOR_BG           lv_color_hex(0x1A1A2E)    /* Dark navy */
#define UI_COLOR_PANEL        lv_color_hex(0x16213E)    /* Panel bg */
#define UI_COLOR_ACCENT       lv_color_hex(0x0F3460)    /* Accent blue */
#define UI_COLOR_HIGHLIGHT    lv_color_hex(0xE94560)    /* Highlight red */
#define UI_COLOR_TEXT         lv_color_hex(0xEEEEEE)    /* Main text */
#define UI_COLOR_TEXT_DIM     lv_color_hex(0x888888)    /* Dimmed text */
#define UI_COLOR_SUCCESS      lv_color_hex(0x00CC66)    /* Success green */

/* Tile colors for home screen */
#define UI_COLOR_TILE_SERIAL  lv_color_hex(0x0D7377)    /* Teal */
#define UI_COLOR_TILE_FLASH   lv_color_hex(0xE94560)    /* Red/orange */
#define UI_COLOR_TILE_WIFI    lv_color_hex(0x533483)    /* Purple */

/* Standard padding/sizing */
#define UI_PAD_SMALL    8
#define UI_PAD_MEDIUM   16
#define UI_PAD_LARGE    24

/* Tab bar height */
#define UI_TAB_BAR_H    48

/**
 * @brief Initialize shared UI styles (call once after LVGL init)
 */
void ui_styles_init(void);
