/**
 * Burn flash encryption key to ESP32 BLOCK1 eFuses via esp-serial-flasher's
 * register read/write commands. Runs through the bootloader (ROM or stub).
 *
 * Register addresses are for the original ESP32 (target chip), NOT the P4 host.
 */

#include "efuse_burn.h"
#include "esp_loader.h"
#include "esp_loader_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "EFUSE_BURN";

/* ── ESP32 eFuse register addresses (from ESP32 TRM Section 20) ─────── */

#define EFUSE_BASE              0x3FF5A000

/* Block 1 read registers — flash encryption key readback */
#define EFUSE_BLK1_RDATA0_REG  (EFUSE_BASE + 0x038)
/* RDATA1 at +0x03C ... RDATA7 at +0x054 (8 words, 4 bytes apart) */

/* Block 1 write registers — load key data before programming */
#define EFUSE_BLK1_WDATA0_REG  (EFUSE_BASE + 0x098)
/* WDATA1 at +0x09C ... WDATA7 at +0x0B4 */

/* Control registers */
#define EFUSE_CONF_REG          (EFUSE_BASE + 0x0FC)
#define EFUSE_CMD_REG           (EFUSE_BASE + 0x104)

/* Operation codes written to EFUSE_CONF_REG before issuing commands */
#define EFUSE_WRITE_OP_CODE     0x5A5A
#define EFUSE_READ_OP_CODE      0x5AA5

/* EFUSE_CMD_REG bits */
#define EFUSE_PGM_CMD_BIT       (1 << 1)
#define EFUSE_READ_CMD_BIT      (1 << 0)

/* Block 0 read register — contains protection bits */
#define EFUSE_BLK0_RDATA0_REG  (EFUSE_BASE + 0x000)
/* Bit 16: RD_DIS for BLOCK1 (flash encryption key read-protected) */
#define EFUSE_RD_DIS_BLK1_BIT  (1 << 16)
/* Bit 7 in WR_DIS field: WR_DIS for BLOCK1 */
#define EFUSE_WR_DIS_BLK1_BIT  (1 << 7)

/* ── Wrappers with timer refresh ────────────────────────────────────── */

/* Each register command needs its own fresh timer since esp-serial-flasher
   checks loader_port_remaining_time() and returns TIMEOUT if it's 0. */

static esp_loader_error_t reg_read(uint32_t addr, uint32_t *val)
{
    loader_port_start_timer(3000);
    return esp_loader_read_register(addr, val);
}

static esp_loader_error_t reg_write(uint32_t addr, uint32_t val)
{
    loader_port_start_timer(3000);
    return esp_loader_write_register(addr, val);
}

/* ── Helpers ────────────────────────────────────────────────────────── */

static esp_loader_error_t efuse_reload(void)
{
    esp_loader_error_t err;

    err = reg_write(EFUSE_CONF_REG, EFUSE_READ_OP_CODE);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "reload: write CONF failed: %d", err);
        return err;
    }

    err = reg_write(EFUSE_CMD_REG, EFUSE_READ_CMD_BIT);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "reload: write CMD failed: %d", err);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    /* Wait for READ_CMD to clear */
    for (int i = 0; i < 50; i++) {
        uint32_t cmd = 0;
        err = reg_read(EFUSE_CMD_REG, &cmd);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "reload: read CMD failed: %d", err);
            return err;
        }
        if ((cmd & EFUSE_READ_CMD_BIT) == 0) return ESP_LOADER_SUCCESS;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGE(TAG, "eFuse reload: CMD bit never cleared");
    return ESP_LOADER_ERROR_TIMEOUT;
}

/* ── Public API ─────────────────────────────────────────────────────── */

esp_loader_error_t efuse_check_block1_empty(bool *is_empty)
{
    esp_loader_error_t err;
    *is_empty = true;

    /* First check BLOCK0 protection bits — if BLOCK1 is read or write
       protected, the key was already burned (reads return 0 when protected) */
    uint32_t blk0_rd0 = 0;
    err = reg_read(EFUSE_BLK0_RDATA0_REG, &blk0_rd0);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Failed to read BLK0_RDATA0: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "BLK0_RDATA0 = 0x%08lx", (unsigned long)blk0_rd0);

    bool rd_protected = (blk0_rd0 & EFUSE_RD_DIS_BLK1_BIT) != 0;
    bool wr_protected = (blk0_rd0 & EFUSE_WR_DIS_BLK1_BIT) != 0;
    ESP_LOGI(TAG, "BLOCK1 protection: RD_DIS=%d, WR_DIS=%d",
             rd_protected, wr_protected);

    if (rd_protected || wr_protected) {
        ESP_LOGW(TAG, "BLOCK1 is protected — key already burned");
        *is_empty = false;
        return ESP_LOADER_SUCCESS;
    }

    /* BLOCK1 is not protected — read the actual values */
    for (int i = 0; i < 8; i++) {
        uint32_t val = 0;
        err = reg_read(EFUSE_BLK1_RDATA0_REG + (i * 4), &val);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Failed to read BLK1_RDATA%d: %d", i, err);
            return err;
        }
        ESP_LOGI(TAG, "BLK1_RDATA%d = 0x%08lx", i, (unsigned long)val);
        if (val != 0) {
            *is_empty = false;
        }
    }

    return ESP_LOADER_SUCCESS;
}

esp_loader_error_t efuse_burn_flash_encryption_key(const uint8_t key[32])
{
    esp_loader_error_t err;

    /*
     * Encode the key: espefuse.py reverses the entire byte order for ESP32
     * flash_encryption keys before writing to BLOCK1. We do the same here
     * so that chips burned by the field tool are interchangeable with those
     * burned by espefuse.py using the same key file.
     *
     * File bytes:    [k0,  k1,  k2,  ..., k31]
     * Reversed:      [k31, k30, k29, ..., k0 ]
     * WDATA0 (LE):   k31 | (k30<<8) | (k29<<16) | (k28<<24)
     * WDATA7 (LE):   k3  | (k2<<8)  | (k1<<16)  | (k0<<24)
     */
    uint32_t words[8];
    for (int w = 0; w < 8; w++) {
        int b = 31 - (w * 4);
        words[w] = (uint32_t)key[b]       |
                   ((uint32_t)key[b - 1] << 8)  |
                   ((uint32_t)key[b - 2] << 16) |
                   ((uint32_t)key[b - 3] << 24);
    }

    ESP_LOGI(TAG, "Key words to burn (reversed encoding):");
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "  WDATA%d = 0x%08lx", i, (unsigned long)words[i]);
    }

    /* 1. Write key words to BLOCK1 WDATA registers (loads the buffer) */
    for (int i = 0; i < 8; i++) {
        err = reg_write(EFUSE_BLK1_WDATA0_REG + (i * 4), words[i]);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Failed to write BLK1_WDATA%d: %d", i, err);
            return err;
        }
    }
    ESP_LOGI(TAG, "Key data loaded into WDATA registers");

    /* 2. Set program operation code */
    err = reg_write(EFUSE_CONF_REG, EFUSE_WRITE_OP_CODE);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Failed to set write op code: %d", err);
        return err;
    }

    /* 3. Trigger eFuse programming */
    err = reg_write(EFUSE_CMD_REG, EFUSE_PGM_CMD_BIT);
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "Failed to trigger PGM: %d", err);
        return err;
    }

    /* 4. Wait for programming to complete (typically <1ms) */
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int retry = 0; retry < 100; retry++) {
        uint32_t cmd_val = 0;
        err = reg_read(EFUSE_CMD_REG, &cmd_val);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Failed to read CMD reg: %d", err);
            return err;
        }
        if ((cmd_val & EFUSE_PGM_CMD_BIT) == 0) {
            ESP_LOGI(TAG, "eFuse programming complete");
            break;
        }
        if (retry == 99) {
            ESP_LOGE(TAG, "eFuse programming timed out");
            return ESP_LOADER_ERROR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* 5. Reload eFuses so RDATA registers reflect new values */
    err = efuse_reload();
    if (err != ESP_LOADER_SUCCESS) {
        ESP_LOGE(TAG, "eFuse reload after burn failed: %d", err);
        return err;
    }

    /* 6. Read back BLOCK1 and verify against what we wrote */
    ESP_LOGI(TAG, "Verifying burned key...");
    bool match = true;
    for (int i = 0; i < 8; i++) {
        uint32_t val = 0;
        err = reg_read(EFUSE_BLK1_RDATA0_REG + (i * 4), &val);
        if (err != ESP_LOADER_SUCCESS) {
            ESP_LOGE(TAG, "Failed to readback BLK1_RDATA%d: %d", i, err);
            return err;
        }
        ESP_LOGI(TAG, "  RDATA%d = 0x%08lx (expected 0x%08lx) %s",
                 i, (unsigned long)val, (unsigned long)words[i],
                 (val == words[i]) ? "OK" : "MISMATCH!");
        if (val != words[i]) {
            match = false;
        }
    }

    if (!match) {
        ESP_LOGE(TAG, "Key verification FAILED — eFuse data mismatch");
        return ESP_LOADER_ERROR_FAIL;
    }

    ESP_LOGI(TAG, "Key burn + verification PASSED");
    return ESP_LOADER_SUCCESS;
}
