#include "flasher_manager.h"
#include "app_config.h"
#include "serial/serial_monitor.h"

#include "esp_loader.h"
#include "esp32_usb_cdc_acm_port.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

    /* 2. Pause serial monitor so we can open the CH340 */
    serial_monitor_pause();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 3. Initialize the esp-serial-flasher USB CDC port */
    set_status(FLASH_STATE_CONNECTING, 12, "Opening USB device...");
    loader_esp32_usb_cdc_acm_config_t port_config = {
        .device_vid = USB_VID_PID_AUTO_DETECT,
        .device_pid = USB_VID_PID_AUTO_DETECT,
        .connection_timeout_ms = 5000,
        .out_buffer_size = 4096,
        .acm_host_error_callback = NULL,
        .device_disconnected_callback = NULL,
        .acm_host_serial_state_callback = NULL,
    };

    err = loader_port_esp32_usb_cdc_acm_init(&port_config);
    if (err != ESP_LOADER_SUCCESS) {
        set_status(FLASH_STATE_ERROR, 0, "Failed to open USB device");
        goto cleanup;
    }

    /* 4. Connect to bootloader (enters bootloader via DTR/RTS) */
    set_status(FLASH_STATE_CONNECTING, 15, "Entering bootloader...");
    esp_loader_connect_args_t connect_args = ESP_LOADER_CONNECT_DEFAULT();
    err = esp_loader_connect(&connect_args);
    if (err != ESP_LOADER_SUCCESS) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Bootloader connect failed: %d", err);
        set_status(FLASH_STATE_ERROR, 15, msg);
        loader_port_esp32_usb_cdc_acm_deinit();
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
            loader_port_esp32_usb_cdc_acm_deinit();
            goto cleanup;
        }
    }

    /* 7. Reset target */
    set_status(FLASH_STATE_FLASHING, 96, "Resetting target...");
    esp_loader_reset_target();
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 8. Close flasher port */
    loader_port_esp32_usb_cdc_acm_deinit();

    set_status(FLASH_STATE_DONE, 100, "Flash complete! Device rebooting.");

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

    for (int i = 0; i < NUM_BINS; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", FT_FIRMWARE_DIR, s_bins[i].filename);
        if (stat(path, &st) != 0) {
            ESP_LOGW(TAG, "Missing: %s", path);
            all_ok = false;
        }
    }

    s_status.firmware_ready = all_ok;
    return all_ok;
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

const flasher_status_t *flasher_get_status(void)
{
    return &s_status;
}
