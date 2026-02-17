#pragma once

#include "lvgl.h"

/**
 * Screen identifiers
 */
typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_SERIAL_MONITOR,
    UI_SCREEN_FLASHER,
    UI_SCREEN_WIFI_AP,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_COUNT,
} ui_screen_id_t;

/**
 * @brief Initialize all UI screens (call with LVGL lock held)
 */
void ui_manager_init(void);

/**
 * @brief Switch to a different screen
 * @param screen_id Target screen
 */
void ui_manager_show_screen(ui_screen_id_t screen_id);

/**
 * @brief Get the current active screen ID
 */
ui_screen_id_t ui_manager_get_current_screen(void);
