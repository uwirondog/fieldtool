#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FLASH_STATE_IDLE,
    FLASH_STATE_LOADING,
    FLASH_STATE_CONNECTING,
    FLASH_STATE_FLASHING,
    FLASH_STATE_DONE,
    FLASH_STATE_ERROR,
} flash_state_t;

typedef struct {
    flash_state_t state;
    uint8_t progress;         /* 0-100 */
    char status_msg[128];
    bool firmware_ready;      /* true if SD card has all 4 files */
} flasher_status_t;

/**
 * @brief Check SD card for firmware files and update status
 * @return true if all 4 firmware files are present
 */
bool flasher_check_firmware(void);

/**
 * @brief Start the flash process (runs on a background task)
 *
 * Pauses serial monitor, opens CH340 via esp-serial-flasher USB port,
 * flashes all 4 binaries, then resumes serial monitor.
 */
void flasher_start(void);

/**
 * @brief Get current flasher status (thread-safe, polled by UI)
 */
const flasher_status_t *flasher_get_status(void);
