#pragma once

#include "log_parser.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the serial monitor (USB host + CH340 driver)
 *
 * Starts USB host, installs CDC-ACM driver, and creates the RX processing task.
 * The monitor will auto-detect and connect to CH340 devices.
 *
 * @return ESP_OK on success
 */
esp_err_t serial_monitor_init(void);

/**
 * @brief Check if a USB serial device is currently connected
 */
bool serial_monitor_is_connected(void);

/**
 * @brief Get the number of log entries in the ring buffer
 */
uint32_t serial_monitor_get_count(void);

/**
 * @brief Read a log entry from the ring buffer by index (0 = oldest)
 * @param index  Ring buffer index (0..count-1)
 * @param entry  Output entry
 * @return true if the entry was valid
 */
bool serial_monitor_get_entry(uint32_t index, log_entry_t *entry);

/**
 * @brief Get the latest N entries (for UI display)
 * @param entries  Array of entries to fill
 * @param max_entries  Size of the array
 * @param start_from  Index to start reading from (for scrollback)
 * @return Number of entries actually filled
 */
uint32_t serial_monitor_get_recent(log_entry_t *entries, uint32_t max_entries, uint32_t start_from);

/**
 * @brief Clear all entries from the ring buffer
 */
void serial_monitor_clear(void);

/**
 * @brief Get the total number of lines received since boot
 */
uint32_t serial_monitor_get_total_lines(void);

/**
 * @brief Pause the serial monitor for flashing
 *
 * Stops processing serial data but keeps the USB CDC device open.
 * Returns the device handle so the flasher can reuse it.
 */
void serial_monitor_pause(void);

/**
 * @brief Resume the serial monitor after flashing
 *
 * Restores serial data processing on the existing connection.
 */
void serial_monitor_resume(void);

/**
 * @brief Get the CDC device handle (for flasher to reuse)
 * Only valid after serial_monitor_pause() and before resume().
 */
void *serial_monitor_get_device(void);
