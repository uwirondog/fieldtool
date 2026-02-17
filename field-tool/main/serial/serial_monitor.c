#include "serial_monitor.h"
#include "log_parser.h"
#include "log_storage.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>

static const char *TAG = "SERIAL_MON";

/* ── State ──────────────────────────────────────────────────────────── */

static volatile bool s_device_connected = false;
static volatile bool s_initialized = false;
static volatile bool s_flasher_mode = false;
static volatile uint32_t s_total_lines = 0;

/* Ring buffer in PSRAM */
static log_entry_t *s_ring_buf = NULL;
static volatile uint32_t s_ring_head = 0;  /* Next write position */
static volatile uint32_t s_ring_count = 0; /* Number of valid entries */
static SemaphoreHandle_t s_ring_mutex = NULL;

/* USB data stream buffer */
static StreamBufferHandle_t s_rx_stream = NULL;
#define RX_STREAM_SIZE  (4096)

/* Device handle */
static cdc_acm_dev_hdl_t s_cdc_dev = NULL;
static SemaphoreHandle_t s_device_disconnected_sem = NULL;

/* ── Ring buffer operations ──────────────────────────────────────────── */

static void ring_push(const log_entry_t *entry)
{
    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    memcpy(&s_ring_buf[s_ring_head], entry, sizeof(log_entry_t));
    s_ring_head = (s_ring_head + 1) % FT_LOG_RING_SIZE;
    if (s_ring_count < FT_LOG_RING_SIZE) {
        s_ring_count++;
    }
    xSemaphoreGive(s_ring_mutex);
}

/* ── USB callbacks ──────────────────────────────────────────────────── */

static bool usb_rx_callback(const uint8_t *data, size_t data_len, void *arg)
{
    if (s_rx_stream != NULL) {
        xStreamBufferSendFromISR(s_rx_stream, data, data_len, NULL);
    }
    return true;
}

static void usb_event_callback(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "USB device disconnected");
        s_device_connected = false;
        if (s_device_disconnected_sem) {
            xSemaphoreGive(s_device_disconnected_sem);
        }
        break;
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "USB CDC error: %d", event->data.error);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        break;
    default:
        break;
    }
}

/* ── USB Host library task ──────────────────────────────────────────── */

static void usb_host_task(void *arg)
{
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

/* ── Line processing task ───────────────────────────────────────────── */

static void serial_rx_task(void *arg)
{
    char line_buf[512];
    size_t line_pos = 0;
    uint8_t byte_buf[256];
    log_entry_t entry;

    while (true) {
        /* Read from stream buffer (blocks until data or timeout) */
        size_t bytes_read = xStreamBufferReceive(s_rx_stream, byte_buf, sizeof(byte_buf),
                                                  pdMS_TO_TICKS(100));
        if (bytes_read == 0) {
            continue;
        }

        /* Process byte-by-byte, splitting on newlines */
        for (size_t i = 0; i < bytes_read; i++) {
            char c = (char)byte_buf[i];

            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';

                    /* Parse the line */
                    log_parser_parse(line_buf, &entry);

                    /* Push to ring buffer */
                    ring_push(&entry);

                    /* Write to SD card */
                    log_storage_write(&entry);

                    s_total_lines++;
                    line_pos = 0;
                }
            } else if (line_pos < sizeof(line_buf) - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }
}

/* ── Connection management task ─────────────────────────────────────── */

static void connection_task(void *arg)
{
    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 5000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .event_cb = usb_event_callback,
        .data_cb = usb_rx_callback,
        .user_arg = NULL,
    };

    while (true) {
        /* In flasher mode, don't try to connect */
        if (s_flasher_mode) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (s_device_connected) {
            /* Wait for disconnection or flasher mode */
            BaseType_t got = xSemaphoreTake(s_device_disconnected_sem, pdMS_TO_TICKS(500));
            if (s_flasher_mode) continue;
            if (got == pdFALSE) continue;  /* Timeout — device still connected */
            /* Device actually disconnected */
            if (s_cdc_dev != NULL) {
                cdc_acm_host_close(s_cdc_dev);
                s_cdc_dev = NULL;
            }
            ESP_LOGI(TAG, "Device closed. Waiting for new connection...");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Try to open a CH340 device */
        ESP_LOGI(TAG, "Scanning for CH340 device...");
        esp_err_t err = ch34x_vcp_open(CH34X_PID_AUTO, 0, &dev_config, &s_cdc_dev);

        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "CH340 device connected!");

        /* Configure 115200 8N1 */
        cdc_acm_line_coding_t line_coding = {
            .dwDTERate = FT_UART_BAUD_RATE,
            .bCharFormat = 0,   /* 1 stop bit */
            .bParityType = 0,   /* No parity */
            .bDataBits = 8,
        };
        cdc_acm_host_line_coding_set(s_cdc_dev, &line_coding);

        /* Enable DTR + RTS */
        cdc_acm_host_set_control_line_state(s_cdc_dev, true, true);

        s_device_connected = true;
        ESP_LOGI(TAG, "Serial monitor active at %d baud", FT_UART_BAUD_RATE);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

esp_err_t serial_monitor_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Allocate ring buffer in PSRAM */
    s_ring_buf = heap_caps_calloc(FT_LOG_RING_SIZE, sizeof(log_entry_t), MALLOC_CAP_SPIRAM);
    if (s_ring_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Ring buffer: %d entries (%d KB) in PSRAM",
             FT_LOG_RING_SIZE, (int)(FT_LOG_RING_SIZE * sizeof(log_entry_t) / 1024));

    s_ring_mutex = xSemaphoreCreateMutex();
    s_device_disconnected_sem = xSemaphoreCreateBinary();
    s_rx_stream = xStreamBufferCreate(RX_STREAM_SIZE, 1);

    if (!s_ring_mutex || !s_device_disconnected_sem || !s_rx_stream) {
        return ESP_ERR_NO_MEM;
    }

    /* Install USB Host library */
    ESP_LOGI(TAG, "Installing USB Host...");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    /* Install CDC-ACM driver */
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    /* Initialize log storage (SD card writer) */
    log_storage_init();

    /* Create tasks */
    xTaskCreatePinnedToCore(usb_host_task, "usb_host", 4096, NULL, 20, NULL, 0);
    xTaskCreatePinnedToCore(serial_rx_task, "serial_rx", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(connection_task, "usb_conn", 4096, NULL, 4, NULL, 0);

    s_initialized = true;
    ESP_LOGI(TAG, "Serial monitor initialized (USB Host + CH340)");
    return ESP_OK;
}

bool serial_monitor_is_connected(void)
{
    return s_device_connected;
}

uint32_t serial_monitor_get_count(void)
{
    return s_ring_count;
}

bool serial_monitor_get_entry(uint32_t index, log_entry_t *entry)
{
    if (index >= s_ring_count || entry == NULL) {
        return false;
    }

    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    /* Calculate actual position: oldest entry is at (head - count) mod size */
    uint32_t start = (s_ring_head + FT_LOG_RING_SIZE - s_ring_count) % FT_LOG_RING_SIZE;
    uint32_t pos = (start + index) % FT_LOG_RING_SIZE;
    memcpy(entry, &s_ring_buf[pos], sizeof(log_entry_t));
    xSemaphoreGive(s_ring_mutex);

    return true;
}

uint32_t serial_monitor_get_recent(log_entry_t *entries, uint32_t max_entries, uint32_t start_from)
{
    if (entries == NULL || s_ring_count == 0) {
        return 0;
    }

    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    uint32_t available = s_ring_count;
    if (start_from >= available) {
        xSemaphoreGive(s_ring_mutex);
        return 0;
    }

    uint32_t to_copy = available - start_from;
    if (to_copy > max_entries) {
        to_copy = max_entries;
    }

    uint32_t ring_start = (s_ring_head + FT_LOG_RING_SIZE - available) % FT_LOG_RING_SIZE;
    for (uint32_t i = 0; i < to_copy; i++) {
        uint32_t pos = (ring_start + start_from + i) % FT_LOG_RING_SIZE;
        memcpy(&entries[i], &s_ring_buf[pos], sizeof(log_entry_t));
    }
    xSemaphoreGive(s_ring_mutex);

    return to_copy;
}

void serial_monitor_clear(void)
{
    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    s_ring_head = 0;
    s_ring_count = 0;
    xSemaphoreGive(s_ring_mutex);
}

uint32_t serial_monitor_get_total_lines(void)
{
    return s_total_lines;
}

void serial_monitor_pause(void)
{
    ESP_LOGI(TAG, "Pausing serial monitor for flasher...");
    s_flasher_mode = true;

    /* Close current CDC device so flasher can open it */
    if (s_cdc_dev != NULL) {
        cdc_acm_host_close(s_cdc_dev);
        s_cdc_dev = NULL;
    }
    s_device_connected = false;

    /* Give tasks time to notice the mode change */
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Serial monitor paused");
}

void serial_monitor_resume(void)
{
    ESP_LOGI(TAG, "Resuming serial monitor...");
    s_flasher_mode = false;
    /* connection_task will auto-reconnect on next iteration */
}
