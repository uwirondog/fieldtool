#pragma once

#include "esp_loader.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Burn a 32-byte flash encryption key to ESP32 BLOCK1 eFuses
 *
 * Must be called while connected to the target bootloader (after esp_loader_connect).
 * Key bytes are automatically reversed to match espefuse.py encoding convention.
 *
 * @param key  32-byte encryption key (same format as espefuse.py key file)
 * @return ESP_LOADER_SUCCESS on success
 */
esp_loader_error_t efuse_burn_flash_encryption_key(const uint8_t key[32]);

/**
 * @brief Check if BLOCK1 eFuses are empty (key not yet burned)
 * @param is_empty  Output: true if all 8 BLOCK1 words are zero
 * @return ESP_LOADER_SUCCESS on success reading eFuses
 */
esp_loader_error_t efuse_check_block1_empty(bool *is_empty);
