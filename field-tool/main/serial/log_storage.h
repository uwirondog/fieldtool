#pragma once

#include "log_parser.h"
#include "esp_err.h"

/**
 * @brief Initialize the log storage writer (creates background task)
 * @return ESP_OK on success
 */
esp_err_t log_storage_init(void);

/**
 * @brief Queue a log entry to be written to SD card
 * @param entry The parsed log entry
 */
void log_storage_write(const log_entry_t *entry);
