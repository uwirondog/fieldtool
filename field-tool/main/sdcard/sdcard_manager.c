#include "sdcard_manager.h"
#include "app_config.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>

static const char *TAG = "SDCARD";
static bool s_mounted = false;

esp_err_t sdcard_manager_init(void)
{
    ESP_LOGI(TAG, "Mounting SD card...");
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed: %s (insert card and reboot)", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", FT_SD_MOUNT_POINT);

    /* Ensure standard directories exist */
    sdcard_manager_ensure_dir(FT_FIRMWARE_DIR);
    sdcard_manager_ensure_dir(FT_LOGS_DIR);
    sdcard_manager_ensure_dir(FT_CONFIG_DIR);

    return ESP_OK;
}

bool sdcard_manager_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sdcard_manager_get_free_mb(uint32_t *free_mb)
{
    if (!s_mounted || free_mb == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t total_bytes, free_bytes;
    esp_err_t ret = esp_vfs_fat_info(FT_SD_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        return ret;
    }

    *free_mb = (uint32_t)(free_bytes / (1024 * 1024));
    return ESP_OK;
}

esp_err_t sdcard_manager_ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return ESP_OK; /* Already exists */
    }

    if (mkdir(path, 0775) != 0) {
        ESP_LOGW(TAG, "Failed to create directory: %s", path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Created directory: %s", path);
    return ESP_OK;
}
