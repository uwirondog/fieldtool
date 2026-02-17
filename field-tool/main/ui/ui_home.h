#pragma once

#include "lvgl.h"

/**
 * @brief Create the home/dashboard screen
 * @return Pointer to the screen object
 */
lv_obj_t *ui_home_create(void);

/**
 * @brief Update SD card status text on the home screen
 * @param mounted  true if SD card is mounted
 * @param free_mb  free space in MB (ignored if not mounted)
 */
void ui_home_update_sd_status(bool mounted, uint32_t free_mb);
