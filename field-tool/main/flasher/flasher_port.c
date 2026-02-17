/**
 * Custom esp-serial-flasher port that reuses the serial monitor's
 * existing CH340 USB connection, avoiding close/reopen issues on ESP32-P4.
 *
 * Implements the loader_port_* functions required by esp-serial-flasher.
 */

#include "flasher_port.h"
#include "esp_loader_io.h"
#include "usb/cdc_acm_host.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#include <unistd.h>
#include <string.h>

static const char *TAG = "FLASH_PORT";

static cdc_acm_dev_hdl_t s_device = NULL;
static StreamBufferHandle_t s_rx_buf = NULL;
static uint32_t s_time_end = 0;

/* ── RX feed (called by serial monitor's USB callback during flash) ── */

static volatile uint32_t s_rx_total = 0;

void flasher_port_feed_rx(const uint8_t *data, size_t len)
{
    if (s_rx_buf) {
        s_rx_total += len;
        /* Use FromISR variant since this may be called from USB host callback context */
        xStreamBufferSendFromISR(s_rx_buf, data, len, NULL);
    }
}

uint32_t flasher_port_get_rx_count(void)
{
    return s_rx_total;
}

/* ── Init/deinit ──────────────────────────────────────────────────── */

esp_loader_error_t flasher_port_init(cdc_acm_dev_hdl_t device)
{
    s_device = device;

    s_rx_buf = xStreamBufferCreate(4096, 1);
    if (!s_rx_buf) {
        ESP_LOGE(TAG, "Failed to create RX stream buffer");
        return ESP_LOADER_ERROR_FAIL;
    }

    ESP_LOGI(TAG, "Flasher port initialized with existing CH340 handle");
    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t flasher_port_deinit(void)
{
    if (s_rx_buf) {
        vStreamBufferDelete(s_rx_buf);
        s_rx_buf = NULL;
    }
    s_device = NULL;
    return ESP_LOADER_SUCCESS;
}

/* ── Required esp-serial-flasher port functions ───────────────────── */

esp_loader_error_t loader_port_write(const uint8_t *data, const uint16_t size,
                                     const uint32_t timeout)
{
    if (!s_device) return ESP_LOADER_ERROR_FAIL;

    esp_err_t err = cdc_acm_host_data_tx_blocking(s_device, (uint8_t *)data, size, timeout);
    if (err == ESP_OK) {
        return ESP_LOADER_SUCCESS;
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "TX timeout (%u bytes)", size);
        return ESP_LOADER_ERROR_TIMEOUT;
    } else {
        ESP_LOGE(TAG, "TX error: %s", esp_err_to_name(err));
        return ESP_LOADER_ERROR_FAIL;
    }
}

esp_loader_error_t loader_port_read(uint8_t *data, const uint16_t size, const uint32_t timeout)
{
    if (!s_rx_buf) return ESP_LOADER_ERROR_FAIL;

    size_t received = xStreamBufferReceive(s_rx_buf, data, size, pdMS_TO_TICKS(timeout));
    if (received == size) {
        return ESP_LOADER_SUCCESS;
    } else {
        return ESP_LOADER_ERROR_TIMEOUT;
    }
}

void loader_port_enter_bootloader(void)
{
    if (!s_device) return;

    /*
     * ESP-IDF CH34x VCP driver polarity bug:
     * The driver sends DTR/RTS bits directly in the CH340 vendor command 0xA4,
     * but the CH340 hardware uses INVERTED logic (bit=0 means ACTIVE, bit=1 means INACTIVE).
     * The Linux kernel CH341 driver correctly inverts: wValue = ~control.
     * The ESP-IDF driver does NOT invert.
     *
     * Workaround: negate both boolean parameters to get the correct hardware behavior.
     * set_control_line_state(true, x)  → CH340 DTR INACTIVE (GPIO0 released)
     * set_control_line_state(false, x) → CH340 DTR ACTIVE   (GPIO0 pulled LOW)
     * set_control_line_state(x, true)  → CH340 RTS INACTIVE (EN released)
     * set_control_line_state(x, false) → CH340 RTS ACTIVE   (EN pulled LOW)
     */
    s_rx_total = 0;
    xStreamBufferReset(s_rx_buf);

    ESP_LOGI(TAG, "Entering bootloader (inverted polarity for CH34x)...");

    /* Step 1: Hold EN LOW (reset), GPIO0 free
     * RTS=false → ACTIVE → EN LOW, DTR=true → INACTIVE → GPIO0 free */
    cdc_acm_host_set_control_line_state(s_device, true, false);
    loader_port_delay_ms(100);

    /* Step 2: Release EN (boot), hold GPIO0 LOW (download mode)
     * DTR=false → ACTIVE → GPIO0 LOW, RTS=true → INACTIVE → EN HIGH */
    cdc_acm_host_set_control_line_state(s_device, false, true);
    loader_port_delay_ms(50);

    /* Step 3: Release both
     * Both true → INACTIVE → both released */
    cdc_acm_host_set_control_line_state(s_device, true, true);

    ESP_LOGI(TAG, "Bootloader entry complete");
}

void loader_port_reset_target(void)
{
    if (!s_device) return;

    xStreamBufferReset(s_rx_buf);
    /* EN LOW (reset): RTS=false → ACTIVE */
    cdc_acm_host_set_control_line_state(s_device, true, false);
    loader_port_delay_ms(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
    /* Release: both true → INACTIVE */
    cdc_acm_host_set_control_line_state(s_device, true, true);
}

void loader_port_delay_ms(const uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void loader_port_start_timer(const uint32_t ms)
{
    s_time_end = esp_timer_get_time() + ms * 1000;
}

uint32_t loader_port_remaining_time(void)
{
    int64_t remaining = (s_time_end - esp_timer_get_time()) / 1000;
    return (remaining > 0) ? (uint32_t)remaining : 0;
}

void loader_port_debug_print(const char *str)
{
    ESP_LOGI(TAG, "%s", str);
}

esp_loader_error_t loader_port_change_transmission_rate(const uint32_t baudrate)
{
    if (!s_device) return ESP_LOADER_ERROR_FAIL;

    cdc_acm_line_coding_t line_coding;
    if (cdc_acm_host_line_coding_get(s_device, &line_coding) != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    line_coding.dwDTERate = baudrate;
    if (cdc_acm_host_line_coding_set(s_device, &line_coding) != ESP_OK) {
        return ESP_LOADER_ERROR_FAIL;
    }
    return ESP_LOADER_SUCCESS;
}
