#pragma once

/*******************************************************************************
 * Field Tool Configuration
 ******************************************************************************/

/* UART — RS485 header on the P4 board */
#define FT_UART_PORT_NUM    (1)
#define FT_UART_TXD         (27)    /* P4 GPIO27 → target RXD0 */
#define FT_UART_RXD         (26)    /* P4 GPIO26 ← target TXD0 */
#define FT_UART_BAUD_RATE   (115200)

/* Bootloader entry GPIOs (expansion header — update after checking schematic) */
#define FT_TARGET_GPIO0     (21)    /* Pull low to enter bootloader */
#define FT_TARGET_EN        (22)    /* Pulse low to reset target */

/* SD card mount point (set by BSP config, usually /sdcard) */
#define FT_SD_MOUNT_POINT   "/sdcard"

/* Log ring buffer (allocated in PSRAM) */
#define FT_LOG_RING_SIZE    (4096)  /* ~4000 log entries */
#define FT_LOG_MSG_MAX_LEN  (256)
#define FT_LOG_TAG_MAX_LEN  (24)

/* Firmware storage paths on SD card */
#define FT_FIRMWARE_DIR     FT_SD_MOUNT_POINT "/firmware"
#define FT_LOGS_DIR         FT_SD_MOUNT_POINT "/logs"
#define FT_CONFIG_DIR       FT_SD_MOUNT_POINT "/config"
#define FT_ENCRYPTION_KEY   FT_SD_MOUNT_POINT "/keys/flash_encryption_key.bin"

/* WiFi Hotspot */
#define FT_WIFI_AP_SSID     "RCWM"
#define FT_WIFI_AP_PASS     "testRCWM2026!"
#define FT_WIFI_AP_MAX_CONN (4)
#define FT_WIFI_AP_CHANNEL  (1)

/* Firmware download — chemproject GCS bucket */
#define FT_FW_CHECK_URL     "https://chemical-monitor-api-57459833175.us-central1.run.app/api/checkFirmwareUpdate"
#define FT_FW_DEVICE_ID     "field-tool"

/* App version */
#define FT_APP_VERSION      "0.1.0"
