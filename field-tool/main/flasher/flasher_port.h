#pragma once

#include "esp_loader_io.h"
#include "usb/cdc_acm_host.h"

/**
 * @brief Initialize the custom flasher port using an existing CDC device handle
 *
 * This avoids closing/reopening the CH340 on the ESP32-P4's USB host,
 * which is unreliable (TX timeouts, 25-second re-open delays).
 * Instead, the serial monitor hands over its working connection.
 *
 * @param device  The CDC-ACM device handle from the serial monitor
 * @return ESP_LOADER_SUCCESS on success
 */
esp_loader_error_t flasher_port_init(cdc_acm_dev_hdl_t device);

/**
 * @brief Deinitialize the flasher port (does NOT close the CDC device)
 */
esp_loader_error_t flasher_port_deinit(void);

/**
 * @brief Feed received USB data into the flasher's RX buffer
 *
 * Called by the serial monitor's USB RX callback during flash mode
 * to route incoming data to the flasher instead of the log parser.
 *
 * @param data  Pointer to received bytes
 * @param len   Number of bytes
 */
void flasher_port_feed_rx(const uint8_t *data, size_t len);

/**
 * @brief Get total RX bytes received (for debug)
 */
uint32_t flasher_port_get_rx_count(void);
