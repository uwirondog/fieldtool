#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Mount the SD card using the BSP driver
 * @return ESP_OK on success
 */
esp_err_t sdcard_manager_init(void);

/**
 * @brief Check if the SD card is currently mounted
 */
bool sdcard_manager_is_mounted(void);

/**
 * @brief Get free space on the SD card in MB
 * @param[out] free_mb  Free space in megabytes
 * @return ESP_OK on success
 */
esp_err_t sdcard_manager_get_free_mb(uint32_t *free_mb);

/**
 * @brief Ensure a directory exists on the SD card, creating if needed
 * @param path Full path to the directory
 * @return ESP_OK on success
 */
esp_err_t sdcard_manager_ensure_dir(const char *path);
