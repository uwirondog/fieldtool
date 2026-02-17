#include "log_storage.h"
#include "sdcard/sdcard_manager.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "LOG_STORE";

#define LOG_QUEUE_LEN  64

static QueueHandle_t s_log_queue;
static FILE *s_log_file;

static void open_log_file(void)
{
    if (s_log_file != NULL) {
        return;
    }
    if (!sdcard_manager_is_mounted()) {
        return;
    }

    /* Generate filename from uptime since we may not have RTC time */
    char filename[128];
    int64_t uptime_sec = esp_log_timestamp() / 1000;
    snprintf(filename, sizeof(filename), "%s/log_%lld.txt", FT_LOGS_DIR, (long long)uptime_sec);

    s_log_file = fopen(filename, "a");
    if (s_log_file != NULL) {
        ESP_LOGI(TAG, "Logging to: %s", filename);
    } else {
        ESP_LOGW(TAG, "Failed to open log file: %s", filename);
    }
}

static void log_writer_task(void *arg)
{
    log_entry_t entry;

    open_log_file();

    while (true) {
        if (xQueueReceive(s_log_queue, &entry, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_log_file != NULL) {
                fprintf(s_log_file, "%s\n", entry.raw);
                /* Flush periodically (every line for now — could batch later) */
                fflush(s_log_file);
            }
        }
    }
}

esp_err_t log_storage_init(void)
{
    s_log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(log_entry_t));
    if (s_log_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(log_writer_task, "log_writer", 4096, NULL, 1, NULL, 1);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Log storage initialized");
    return ESP_OK;
}

void log_storage_write(const log_entry_t *entry)
{
    if (s_log_queue == NULL) {
        return;
    }
    /* Non-blocking: drop if queue is full (don't block serial RX) */
    xQueueSend(s_log_queue, entry, 0);
}
