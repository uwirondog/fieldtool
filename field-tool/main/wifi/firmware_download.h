#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    FW_DL_IDLE = 0,
    FW_DL_CHECKING,
    FW_DL_UPDATE_AVAILABLE,
    FW_DL_NO_UPDATE,
    FW_DL_DOWNLOADING,
    FW_DL_VERIFYING,
    FW_DL_DONE,
    FW_DL_ERROR,
} fw_dl_state_t;

typedef struct {
    fw_dl_state_t state;
    int           progress;            /* 0-100 */
    char          sd_version[16];       /* version currently on SD card */
    char          available_version[16]; /* latest version from server */
    char          changelog[256];
    char          error_msg[128];
} fw_dl_status_t;

/* Check for firmware update (runs in background task) */
esp_err_t fw_dl_check_update(void);

/* Download firmware to SD card (runs in background task) */
esp_err_t fw_dl_start_download(void);

/* Status (safe to call from any task) */
const fw_dl_status_t *fw_dl_get_status(void);
