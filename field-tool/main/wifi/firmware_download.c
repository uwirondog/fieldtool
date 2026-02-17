#include "firmware_download.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "FW_DL";

static fw_dl_status_t s_status = {0};
static char s_download_url[256] = {0};
static char s_expected_sha256[65] = {0};

#define FW_VERSION_FILE  FT_FIRMWARE_DIR "/version.txt"

/* Read SD card firmware version from version.txt */
static void load_sd_version(void)
{
    FILE *f = fopen(FW_VERSION_FILE, "r");
    if (f) {
        if (fgets(s_status.sd_version, sizeof(s_status.sd_version), f)) {
            /* Strip newline */
            char *nl = strchr(s_status.sd_version, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);
        ESP_LOGI(TAG, "SD card firmware version: %s", s_status.sd_version);
    } else {
        s_status.sd_version[0] = '\0';
        ESP_LOGI(TAG, "No version.txt on SD card (version unknown)");
    }
}

/* Save version to SD card after successful download */
static void save_sd_version(const char *version)
{
    FILE *f = fopen(FW_VERSION_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", version);
        fclose(f);
        snprintf(s_status.sd_version, sizeof(s_status.sd_version), "%s", version);
        ESP_LOGI(TAG, "Saved SD version: %s", version);
    }
}

/* ── Check for update (background task) ─────────────────────────────── */

static void check_update_task(void *arg)
{
    s_status.state = FW_DL_CHECKING;
    s_status.progress = 0;
    s_status.error_msg[0] = '\0';

    /* Load current SD card version */
    load_sd_version();

    /* Use SD version if known, otherwise 8.0 (ensures encrypted inference) */
    const char *check_ver = (s_status.sd_version[0] != '\0') ? s_status.sd_version : "8.0";
    char url[512];
    snprintf(url, sizeof(url), "%s?device_id=%s&current_version=%s",
             FT_FW_CHECK_URL, FT_FW_DEVICE_ID, check_ver);

    ESP_LOGI(TAG, "Checking for update: %s", url);

    /* HTTP GET */
    char response_buf[1024] = {0};
    int response_len = 0;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Connection failed: %s", esp_err_to_name(err));
        s_status.state = FW_DL_ERROR;
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status=%d, content_length=%d", status_code, content_length);

    if (status_code != 200) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Server returned HTTP %d", status_code);
        s_status.state = FW_DL_ERROR;
        goto cleanup;
    }

    response_len = esp_http_client_read(client, response_buf, sizeof(response_buf) - 1);
    if (response_len <= 0) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg), "Empty response");
        s_status.state = FW_DL_ERROR;
        goto cleanup;
    }
    response_buf[response_len] = '\0';
    ESP_LOGI(TAG, "Response: %s", response_buf);

    /* Parse JSON */
    cJSON *root = cJSON_Parse(response_buf);
    if (!root) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg), "Invalid JSON");
        s_status.state = FW_DL_ERROR;
        goto cleanup;
    }

    cJSON *update_avail = cJSON_GetObjectItem(root, "updateAvailable");
    if (!cJSON_IsTrue(update_avail)) {
        ESP_LOGI(TAG, "No update available");
        s_status.state = FW_DL_NO_UPDATE;
        cJSON_Delete(root);
        goto cleanup;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    cJSON *download_url = cJSON_GetObjectItem(root, "downloadUrl");
    cJSON *sha256 = cJSON_GetObjectItem(root, "sha256");
    cJSON *changelog = cJSON_GetObjectItem(root, "changelog");

    if (cJSON_IsString(version)) {
        strncpy(s_status.available_version, version->valuestring,
                sizeof(s_status.available_version) - 1);
    }
    if (cJSON_IsString(download_url)) {
        strncpy(s_download_url, download_url->valuestring,
                sizeof(s_download_url) - 1);
    }
    if (cJSON_IsString(sha256)) {
        strncpy(s_expected_sha256, sha256->valuestring,
                sizeof(s_expected_sha256) - 1);
    }
    if (cJSON_IsString(changelog)) {
        strncpy(s_status.changelog, changelog->valuestring,
                sizeof(s_status.changelog) - 1);
    }

    ESP_LOGI(TAG, "Update available: v%s", s_status.available_version);
    ESP_LOGI(TAG, "Download URL: %s", s_download_url);
    s_status.state = FW_DL_UPDATE_AVAILABLE;

    cJSON_Delete(root);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/* ── Download firmware (background task) ────────────────────────────── */

static void download_task(void *arg)
{
    s_status.state = FW_DL_DOWNLOADING;
    s_status.progress = 0;
    s_status.error_msg[0] = '\0';

    if (s_download_url[0] == '\0') {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg), "No download URL");
        s_status.state = FW_DL_ERROR;
        vTaskDelete(NULL);
        return;
    }

    /* Build output path: /sdcard/firmware/flow_meter.bin */
    const char *out_path = FT_FIRMWARE_DIR "/flow_meter.bin";
    ESP_LOGI(TAG, "Downloading to: %s", out_path);

    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Failed to open %s for writing", out_path);
        s_status.state = FW_DL_ERROR;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = s_download_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Download connection failed: %s", esp_err_to_name(err));
        s_status.state = FW_DL_ERROR;
        fclose(fp);
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Download HTTP status=%d, size=%d", status_code, content_length);

    if (status_code != 200) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Download HTTP %d", status_code);
        s_status.state = FW_DL_ERROR;
        fclose(fp);
        goto cleanup;
    }

    /* Stream download in 4KB chunks with SHA256 */
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);  /* 0 = SHA-256 (not SHA-224) */

    char buf[4096];
    int total_read = 0;
    int bytes_read;

    while ((bytes_read = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, bytes_read, fp);
        mbedtls_sha256_update(&sha_ctx, (const unsigned char *)buf, bytes_read);
        total_read += bytes_read;

        if (content_length > 0) {
            s_status.progress = (total_read * 100) / content_length;
        }
    }

    fclose(fp);
    ESP_LOGI(TAG, "Downloaded %d bytes", total_read);

    /* Verify SHA256 */
    s_status.state = FW_DL_VERIFYING;
    unsigned char sha_result[32];
    mbedtls_sha256_finish(&sha_ctx, sha_result);
    mbedtls_sha256_free(&sha_ctx);

    char sha_hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&sha_hex[i * 2], "%02x", sha_result[i]);
    }
    sha_hex[64] = '\0';

    ESP_LOGI(TAG, "SHA256 computed: %s", sha_hex);
    ESP_LOGI(TAG, "SHA256 expected: %s", s_expected_sha256);

    if (s_expected_sha256[0] != '\0' && strcasecmp(sha_hex, s_expected_sha256) != 0) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "SHA256 mismatch!");
        s_status.state = FW_DL_ERROR;
        /* Delete corrupted file */
        remove(out_path);
        goto cleanup;
    }

    s_status.progress = 100;
    s_status.state = FW_DL_DONE;
    save_sd_version(s_status.available_version);
    ESP_LOGI(TAG, "Firmware download + verification complete");

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/* ── Public API ─────────────────────────────────────────────────────── */

esp_err_t fw_dl_check_update(void)
{
    if (s_status.state == FW_DL_CHECKING || s_status.state == FW_DL_DOWNLOADING) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreate(check_update_task, "fw_check", 8192,
                                  NULL, 5, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t fw_dl_start_download(void)
{
    if (s_status.state == FW_DL_DOWNLOADING || s_status.state == FW_DL_CHECKING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_download_url[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreate(download_task, "fw_download", 12288,
                                  NULL, 5, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

const fw_dl_status_t *fw_dl_get_status(void)
{
    return &s_status;
}
