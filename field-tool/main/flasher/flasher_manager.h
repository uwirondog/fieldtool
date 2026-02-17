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
    bool key_ready;           /* true if encryption key is on SD card */
} flasher_status_t;

/**
 * @brief Check SD card for firmware files and update status
 * @return true if all 4 firmware files are present
 */
bool flasher_check_firmware(void);

/**
 * @brief Check SD card for encryption key file
 * @return true if /sdcard/keys/flash_encryption_key.bin exists and is 32 bytes
 */
bool flasher_check_encryption_key(void);

/**
 * @brief Flash an already-encrypted device (runs on a background task)
 *
 * Pauses serial monitor, connects via CH340, flashes all 4 binaries,
 * resets target, then resumes serial monitor.
 */
void flasher_start(void);

/**
 * @brief Flash a virgin/unencrypted device (runs on a background task)
 *
 * Full sequence: burn encryption key to eFuses → erase flash → flash
 * all 4 binaries → reset. First boot auto-enables flash encryption.
 */
void flasher_start_virgin(void);

/**
 * @brief Get current flasher status (thread-safe, polled by UI)
 */
const flasher_status_t *flasher_get_status(void);
