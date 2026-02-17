#include "flasher_manager.h"
#include "flasher_port.h"
#include "efuse_burn.h"
#include "app_config.h"
#include "serial/serial_monitor.h"

#include "esp_loader.h"
#include "usb/cdc_acm_host.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

static const char *TAG = "FLASHER";

/* ── Firmware binary definitions ─────────────────────────────────────── */

typedef struct {
    const char *filename;
    uint32_t address;
    uint8_t *data;
    size_t size;
} firmware_bin_t;

#define NUM_BINS 4

static firmware_bin_t s_bins[NUM_BINS] = {
    { "bootloader.bin",       0x1000,  NULL, 0 },
    { "partition-table.bin",  0x10000, NULL, 0 },
    { "ota_data_initial.bin", 0x15000, NULL, 0 },
    { "flow_meter.bin",       0x20000, NULL, 0 },
};

/* ── State ───────────────────────────────────────────────────────────── */

static flasher_status_t s_status = {
    .state = FLASH_STATE_IDLE,
    .progress = 0,
    .status_msg = "Ready",
    .firmware_ready = false,
    .key_ready = false,
};

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void set_status(flash_state_t state, uint8_t progress, const char *msg)
{
    s_status.state = state;
    s_status.progress = progress;
    snprintf(s_status.status_msg, sizeof(s_status.status_msg), "%s", msg);
    ESP_LOGI(TAG, "[%d%%] %s", progress, msg);
}

static bool load_file_to_psram(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 2 * 1024 * 1024) {
        ESP_LOGE(TAG, "Invalid file size: %ld for %s", fsize, path);
        fclose(f);
        return false;
    }

    uint8_t *buf = heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %s (%ld bytes)", path, fsize);
        fclose(f);
        return false;
    }

    size_t read = fread(buf, 1, fsize, f);
    fclose(f);

    if (read != (size_t)fsize) {
        ESP_LOGE(TAG, "Short read: %u/%ld for %s", (unsigned)read, fsize, path);
        free(buf);
        return false;
    }

    *out_data = buf;
    *out_size = fsize;
    return true;
}

static void free_firmware(void)
{
    for (int i = 0; i < NUM_BINS; i++) {
        if (s_bins[i].data) {
            free(s_bins[i].data);
            s_bins[i].data = NULL;
            s_bins[i].size = 0;
        }
    }
}

static bool load_all_firmware(void)
{
    for (int i = 0; i < NUM_BINS; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", FT_FIRMWARE_DIR, s_bins[i].filename);

        set_status(FLASH_STATE_LOADING, (i * 10) / NUM_BINS,
                   s_bins[i].filename);

        if (!load_file_to_psram(path, &s_bins[i].data, &s_bins[i].size)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Failed to load %s", s_bins[i].filename);
            set_status(FLASH_STATE_ERROR, 0, msg);
            free_firmware();
            return false;
        }
        ESP_LOGI(TAG, "Loaded %s: %u bytes", s_bins[i].filename, (unsigned)s_bins[i].size);
    }
    return true;
}

/* ── Flash a single binary ───────────────────────────────────────────── */

static esp_loader_error_t flash_binary(int bin_index, uint8_t progress_start, uint8_t progress_end)
{
    firmware_bin_t *bin = &s_bins[bin_index];
    char msg[128];
    snprintf(msg, sizeof(msg), "Flashing %s @ 0x%lx (%u KB)",
             bin->filename, (unsigned long)bin->address, (unsigned)(bin->size / 1024));
    set_status(FLASH_STATE_FLASHING, progress_start, msg);

    esp_loader_error_t err;
    static uint8_t payload[1024];
    size_t block_size = sizeof(payload);

    err = esp_loader_flash_start(bin->address, bin->size, block_size);
    if (err != ESP_LOADER_SUCCESS) {
        snprintf(msg, sizeof(msg), "flash_start failed for %s: %d", bin->filename, err);
        set_status(FLASH_STATE_ERROR, progress_start, msg);
        return err;
    }

    size_t written = 0;
    while (written < bin->size) {
        size_t chunk = bin->size - written;
        if (chunk > block_size) chunk = block_size;

        memcpy(payload, bin->data + written, chunk);
        /* Pad last block with 0xFF */
        if (chunk < block_size) {
            memset(payload + chunk, 0xFF, block_size - chunk);
        }

        err = esp_loader_flash_write(payload, block_size);
        if (err != ESP_LOADER_SUCCESS) {
            snprintf(msg, sizeof(msg), "flash_write failed for %s: %d", bin->filename, err);
            set_status(FLASH_STATE_ERROR, progress_start, msg);
            return err;
        }

        written += chunk;
        uint8_t pct = progress_start +
            (uint8_t)((uint32_t)(progress_end - progress_start) * written / bin->size);
        s_status.progress = pct;
    }

    return ESP_LOADER_SUCCESS;
}

/* ── Flash task ──────────────────────────────────────────────────────── */

static void flash_task(void *arg)
{
    esp_loader_error_t err;

    /* 1. Load firmware from SD card into PSRAM */
    set_status(FLASH_STATE_LOADING, 0, "Loading firmware from SD card...");
    if (!load_all_firmware()) {
        goto done;
    }
    set_status(FLASH_STATE_LOADING, 10, "Firmware loaded into PSRAM");

    /* 2. Pause serial monitor so RX data routes to flasher */
    serial_monitor_pause();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 3. Initialize custom flasher port using serial monitor's CH340 handle */
    set_status(FLASH_STATE_CONNECTING, 12, "Initializing flasher port...");
    cdc_acm_dev_hdl_t cdc_dev = (cdc_acm_dev_hdl_t)serial_monitor_get_device();
    if (cdc_dev == NULL) {
        set_status(FLASH_STATE_ERROR, 0, "No USB device connected");
        goto cleanup;
    }

    err = flasher_port_init(cdc_dev);
    if (err != ESP_LOADER_SUCCESS) {
        set_status(FLASH_STATE_ERROR, 0, "Failed to init flasher port");
        goto cleanup;
    }

    /* 4. Connect to bootloader (enters bootloader via DTR/RTS) */
    set_status(FLASH_STATE_CONNECTING, 15, "Entering bootloader...");
    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    err = esp_loader_connect(&connect_args);
    ESP_LOGI(TAG, "esp_loader_connect result: %d, total RX bytes: %lu",
             err, (unsigned long)flasher_port_get_rx_count());
    if (err != ESP_LOADER_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Bootloader connect failed: %d (RX: %lu bytes)",
                 err, (unsigned long)flasher_port_get_rx_count());
        set_status(FLASH_STATE_ERROR, 15, msg);
        flasher_port_deinit();
        goto cleanup;
    }

    target_chip_t chip = esp_loader_get_target();
    ESP_LOGI(TAG, "Connected to target chip: %d", chip);
    set_status(FLASH_STATE_CONNECTING, 18, "Connected to bootloader!");

    /* 5. Flash all 4 binaries */
    /* Progress distribution: 20-35, 35-45, 45-55, 55-95 */
    uint8_t prog_ranges[NUM_BINS][2] = {
        {20, 35},  /* bootloader (~64KB) */
        {35, 45},  /* partition-table (~4KB) */
        {45, 55},  /* ota_data (~8KB) */
        {55, 95},  /* flow_meter (~1.1MB) */
    };

    for (int i = 0; i < NUM_BINS; i++) {
        err = flash_binary(i, prog_ranges[i][0], prog_ranges[i][1]);
        if (err != ESP_LOADER_SUCCESS) {
            flasher_port_deinit();
            goto cleanup;
        }
    }

    /* 7. Reset target */
    set_status(FLASH_STATE_FLASHING, 96, "Resetting target...");
    esp_loader_reset_target();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 8. Close flasher port */
    flasher_port_deinit();

    set_status(FLASH_STATE_DONE, 100, "Flash complete! Device rebooting.");

cleanup:
    free_firmware();
    vTaskDelay(pdMS_TO_TICKS(1000));
    serial_monitor_resume();

done:
    vTaskDelete(NULL);
}

/* ── Virgin chip flash task ──────────────────────────────────────────── */

static bool load_encryption_key(uint8_t key_out[32])
{
    FILE *f = fopen(FT_ENCRYPTION_KEY, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open encryption key: %s", FT_ENCRYPTION_KEY);
        return false;
    }

    size_t rd = fread(key_out, 1, 32, f);
    fclose(f);

    if (rd != 32) {
        ESP_LOGE(TAG, "Invalid key file size: %u (expected 32)", (unsigned)rd);
        return false;
    }

    ESP_LOGI(TAG, "Encryption key loaded from SD card");
    return true;
}

static void virgin_flash_task(void *arg)
{
    esp_loader_error_t err;
    uint8_t enc_key[32];

    /* 1. Load encryption key from SD card */
    set_status(FLASH_STATE_LOADING, 0, "Loading encryption key...");
    if (!load_encryption_key(enc_key)) {
        set_status(FLASH_STATE_ERROR, 0, "Encryption key not found on SD card");
        goto done;
    }
    set_status(FLASH_STATE_LOADING, 2, "Encryption key loaded");

    /* 2. Load firmware from SD card into PSRAM */
    set_status(FLASH_STATE_LOADING, 3, "Loading firmware from SD card...");
    if (!load_all_firmware()) {
        goto done;
    }
    set_status(FLASH_STATE_LOADING, 10, "Firmware loaded into PSRAM");

    /* 3. Pause serial monitor so RX data routes to flasher */
    serial_monitor_pause();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 4. Initialize flasher port using serial monitor's CH340 handle */
    set_status(FLASH_STATE_CONNECTING, 12, "Initializing flasher port...");
    cdc_acm_dev_hdl_t cdc_dev = (cdc_acm_dev_hdl_t)serial_monitor_get_device();
    if (cdc_dev == NULL) {
        set_status(FLASH_STATE_ERROR, 0, "No USB device connected");
        goto cleanup;
    }

    err = flasher_port_init(cdc_dev);
    if (err != ESP_LOADER_SUCCESS) {
        set_status(FLASH_STATE_ERROR, 0, "Failed to init flasher port");
        goto cleanup;
    }

    /* 5. Connect to bootloader + load stub (needed for WRITE_REG / eFuse ops) */
    set_status(FLASH_STATE_CONNECTING, 15, "Entering bootloader...");
    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    err = esp_loader_connect_with_stub(&connect_args);
    ESP_LOGI(TAG, "esp_loader_connect_with_stub result: %d", err);
    if (err != ESP_LOADER_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Bootloader connect failed: %d", err);
        set_status(FLASH_STATE_ERROR, 15, msg);
        flasher_port_deinit();
        goto cleanup;
    }

    target_chip_t chip = esp_loader_get_target();
    ESP_LOGI(TAG, "Connected to target chip: %d (stub loaded)", chip);
    set_status(FLASH_STATE_CONNECTING, 18, "Connected (stub loaded)");

    /* 6. Verify BLOCK1 eFuses are empty (key not already burned) */
    set_status(FLASH_STATE_FLASHING, 20, "Checking eFuses...");
    bool block1_empty = false;
    err = efuse_check_block1_empty(&block1_empty);
    if (err != ESP_LOADER_SUCCESS) {
        set_status(FLASH_STATE_ERROR, 20, "Failed to read eFuses");
        flasher_port_deinit();
        goto cleanup;
    }
    if (!block1_empty) {
        set_status(FLASH_STATE_ERROR, 20,
            "Key already burned! Use FLASH DEVICE instead.");
        flasher_port_deinit();
        goto cleanup;
    }
    ESP_LOGI(TAG, "BLOCK1 confirmed empty — safe to burn key");

    /* 7. Burn encryption key to BLOCK1 eFuses */
    set_status(FLASH_STATE_FLASHING, 22, "Burning encryption key...");
    err = efuse_burn_flash_encryption_key(enc_key);
    memset(enc_key, 0, sizeof(enc_key));  /* Clear key from memory */
    if (err != ESP_LOADER_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "eFuse burn failed: %d", err);
        set_status(FLASH_STATE_ERROR, 22, msg);
        flasher_port_deinit();
        goto cleanup;
    }
    set_status(FLASH_STATE_FLASHING, 28, "Key burned and verified!");

    /* 8. Erase entire flash */
    set_status(FLASH_STATE_FLASHING, 30, "Erasing flash (~30 seconds)...");
    err = esp_loader_flash_erase();
    if (err != ESP_LOADER_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Flash erase failed: %d", err);
        set_status(FLASH_STATE_ERROR, 30, msg);
        flasher_port_deinit();
        goto cleanup;
    }
    set_status(FLASH_STATE_FLASHING, 40, "Flash erased");

    /* 9. Flash all 4 binaries */
    uint8_t vp[NUM_BINS][2] = {
        {40, 55},  /* bootloader */
        {55, 65},  /* partition-table */
        {65, 75},  /* ota_data */
        {75, 95},  /* flow_meter (~1.1MB) */
    };

    for (int i = 0; i < NUM_BINS; i++) {
        err = flash_binary(i, vp[i][0], vp[i][1]);
        if (err != ESP_LOADER_SUCCESS) {
            flasher_port_deinit();
            goto cleanup;
        }
    }

    /* 10. Reset target — first boot will activate flash encryption */
    set_status(FLASH_STATE_FLASHING, 96, "Resetting target...");
    esp_loader_reset_target();
    vTaskDelay(pdMS_TO_TICKS(500));

    flasher_port_deinit();
    set_status(FLASH_STATE_DONE, 100,
        "New chip complete! First boot will enable encryption.");

cleanup:
    free_firmware();
    vTaskDelay(pdMS_TO_TICKS(1000));
    serial_monitor_resume();

done:
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */

bool flasher_check_firmware(void)
{
    struct stat st;
    bool all_ok = true;

    /* Debug: list what's actually in the firmware directory */
    ESP_LOGI(TAG, "Checking firmware dir: %s", FT_FIRMWARE_DIR);
    DIR *dir = opendir(FT_FIRMWARE_DIR);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  Found: %s", ent->d_name);
        }
        closedir(dir);
    } else {
        ESP_LOGW(TAG, "Cannot open firmware dir: %s", FT_FIRMWARE_DIR);
    }

    for (int i = 0; i < NUM_BINS; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", FT_FIRMWARE_DIR, s_bins[i].filename);
        if (stat(path, &st) != 0) {
            ESP_LOGW(TAG, "Missing: %s", path);
            all_ok = false;
        } else {
            ESP_LOGI(TAG, "OK: %s (%ld bytes)", path, (long)st.st_size);
        }
    }

    s_status.firmware_ready = all_ok;
    return all_ok;
}

bool flasher_check_encryption_key(void)
{
    struct stat st;
    if (stat(FT_ENCRYPTION_KEY, &st) != 0 || st.st_size != 32) {
        ESP_LOGW(TAG, "Encryption key missing or invalid: %s", FT_ENCRYPTION_KEY);
        s_status.key_ready = false;
        return false;
    }
    ESP_LOGI(TAG, "Encryption key OK: %s (32 bytes)", FT_ENCRYPTION_KEY);
    s_status.key_ready = true;
    return true;
}

void flasher_start(void)
{
    if (s_status.state == FLASH_STATE_FLASHING ||
        s_status.state == FLASH_STATE_CONNECTING ||
        s_status.state == FLASH_STATE_LOADING) {
        return;  /* Already in progress */
    }

    set_status(FLASH_STATE_LOADING, 0, "Starting flash process...");
    xTaskCreatePinnedToCore(flash_task, "flash_task", 8192, NULL, 5, NULL, 1);
}

void flasher_start_virgin(void)
{
    if (s_status.state == FLASH_STATE_FLASHING ||
        s_status.state == FLASH_STATE_CONNECTING ||
        s_status.state == FLASH_STATE_LOADING) {
        return;  /* Already in progress */
    }

    set_status(FLASH_STATE_LOADING, 0, "Starting virgin chip flash...");
    xTaskCreatePinnedToCore(virgin_flash_task, "virgin_flash", 8192, NULL, 5, NULL, 1);
}

const flasher_status_t *flasher_get_status(void)
{
    return &s_status;
}
