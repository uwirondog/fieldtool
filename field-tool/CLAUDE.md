# Field Tool — Portable ESP32 Flasher, Serial Monitor & WiFi Hub

## What This Is

A standalone ESP-IDF app for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** board (7" 1024x600 touchscreen). It replaces needing a laptop in the field to flash, monitor, and connect to chemproject flowmeter devices.

## Features

1. **Serial Monitor** — Plug a flowmeter into the USB-A port (CH340), see colored logs on the touchscreen, auto-saved to SD card. Flowmeter output is also echoed to the field tool's debug UART for remote troubleshooting.
2. **Firmware Flasher** — Flash encrypted firmware onto flowmeters from .bin files on SD card, one-touch button. Supports both already-encrypted (v8.0+) and virgin chip workflows.
3. **Virgin Chip Setup** — Burns flash encryption key + FLASH_CRYPT_CNT eFuse on brand new ESP32 chips, then flashes firmware (two-step process).
4. **WiFi Station** — Connect to internet via touchscreen keyboard to check for and download firmware updates.
5. **WiFi Hotspot (SoftAP)** — Broadcast SSID "RCWM" so flowmeters auto-connect in the field for local communication.
6. **Firmware Download** — Check chemproject GCS bucket for latest firmware, download with SHA256 verification, save to SD card. Tracks versions via `version.txt`.

## Hardware

- **Board:** Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (ESP32-P4, 32MB PSRAM, 32MB flash, 7" touch)
- **WiFi coprocessor:** ESP32-C6 via ESP-Hosted SDIO (`esp_wifi_remote`)
- **Target devices:** Chemproject ESP32 flowmeters with CH340 USB bridge
- **Connection:** Flowmeter plugs into the P4 board's USB-A host port
- **SD card:** MicroSD formatted FAT32, holds firmware binaries, encryption keys, and log files
- **Debug UART:** COM6 (USB-Serial/JTAG on the P4 board)

## Current Status (Feb 2026)

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Project skeleton + display + SD card | DONE |
| 2 | Serial monitor (USB CH340 → colored logs) | DONE |
| 3 | Encrypted firmware flasher (SD → flash via USB) | DONE |
| 4 | Virgin chip eFuse burn + flash | DONE |
| 5 | WiFi STA + SoftAP + firmware download | DONE |
| 6 | Settings & polish | Not started |

### All Verified Working
- Touchscreen UI with home screen, navigation between 4 screens
- CH340 auto-detect and connect via USB Host
- Real-time colored serial log display (green=Info, yellow=Warn, red=Error)
- Log parsing of ESP-IDF format lines
- PSRAM ring buffer (4096 entries) for log history
- Background log writer to SD card
- Flasher UI with progress bar, status, SD card file detection
- Encrypted flash of 4 binaries (bootloader, partition-table, ota_data, flow_meter)
- eFuse burn for virgin chips (flash_encryption key + FLASH_CRYPT_CNT)
- WiFi STA connection with on-screen keyboard (prefilled with default credentials)
- WiFi SoftAP hotspot — flowmeters connect and get DHCP addresses
- Firmware update check against chemproject API
- Firmware download with streaming SHA256 verification
- SD card version tracking (version.txt)

## Build & Flash

### Quick build (from any terminal):
```
C:\ESP32-P4-WIFI6-Touch-LCD-7B\field-tool\build.bat
```

### Flash (from any terminal):
```cmd
python -m esptool --chip esp32p4 -p COM6 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\field_tool.bin
```

### Full clean rebuild (ESP-IDF terminal in VS Code):
```
cd C:\ESP32-P4-WIFI6-Touch-LCD-7B\field-tool
idf.py fullclean
idf.py set-target esp32p4
idf.py -p COM6 flash monitor
```

Board debug UART is on **COM6**. ESP-IDF v5.5.

## Troubleshooting with Dual Serial Output

The field tool has a unique debugging capability: **you can see both the field tool's own debug logs AND the connected flowmeter's serial output on the same COM6 debug UART.**

### How it works

1. The field tool outputs its own logs via `ESP_LOGx()` to the debug UART (COM6)
2. When a flowmeter is plugged into the USB-A port, its serial output is captured via CH340 USB CDC
3. Each line from the flowmeter is echoed to the debug UART with the prefix `SERIAL_MON: >>`

### What you see on COM6

```
I (4395) SERIAL_MON: Scanning for CH340 device...          ← field tool's own log
I (4396) SERIAL_MON: CH340 device connected!                ← field tool detected flowmeter
I (12465) SERIAL_MON: >> I (301335) WIFI_MANAGER: Retry...  ← flowmeter's output echoed
I (42741) SERIAL_MON: >> E (331585) esp-tls: couldn't...    ← flowmeter error echoed
I (340320) SERIAL_MON: >> I (629185) wifi:connected with RCWM  ← flowmeter connected to hotspot!
```

### How to monitor

Start `idf_monitor.py` on COM6 with `--no-reset` to avoid rebooting the field tool:

```
python C:\Espressif\frameworks\esp-idf-v5.5\tools\idf_monitor.py -p COM6 --no-reset build\field_tool.elf
```

This lets you see in real-time:
- Field tool boot sequence, WiFi connection, SoftAP start
- Flowmeter connecting to RCWM hotspot
- Flowmeter's WiFi retry attempts, API upload failures, sensor readings
- Flash progress when flashing firmware
- Any errors from either device

### Implementation

The echo is a single line in `serial_monitor.c` in the `serial_rx_task()` function:
```c
ESP_LOGI(TAG, ">> %s", line_buf);
```

## SD Card Setup

Format microSD as **FAT32**. The app auto-creates directories on boot.

### Required for flashing:
```
firmware/bootloader.bin        ← from chemproject build/bootloader/
firmware/partition-table.bin   ← from chemproject build/partition_table/
firmware/ota_data_initial.bin  ← from chemproject build/
firmware/flow_meter.bin        ← downloaded via WiFi, or manually copied
firmware/version.txt           ← auto-created after firmware download
```

### Required for virgin chip eFuse burn:
```
keys/flash_encryption_key.bin  ← from GCP Secret Manager (32 bytes)
```

To get the encryption key:
```
gcloud secrets versions access latest --secret=esp32-flash-encryption-key --project=chemical-monitor-api --out-file=keys/flash_encryption_key.bin
```

### Auto-created:
```
logs/log_1.txt                 ← serial monitor log files (rotated)
config/                        ← reserved for future settings
```

Source for firmware binaries: `C:\chemical-monitoring-project\firmware\esp-idf-flowmeter\build\`

## Firmware Download

The field tool can download firmware updates over WiFi:

1. Connect to WiFi via the touchscreen (STA mode)
2. Tap **CHECK FOR UPDATES** — queries the chemproject API
3. If update available, tap **DOWNLOAD TO SD** — streams binary with SHA256 verification
4. Version saved to `firmware/version.txt` for tracking

### API endpoint
```
GET https://chemical-monitor-api-57459833175.us-central1.run.app/api/checkFirmwareUpdate
    ?device_id=field-tool&current_version=<sd_version or 8.0>
```

Response: `{ updateAvailable, version, downloadUrl, sha256, changelog }`

**Important:** The API infers encryption from version number (>= 7.9 = encrypted). The field tool always sends >= 8.0 to get encrypted firmware.

### Download source
```
https://storage.googleapis.com/chemical-monitor-buffers/firmware/flow_meter_v{VERSION}.bin
```

The other 3 binaries (bootloader, partition-table, ota_data_initial) don't change between firmware versions and stay on the SD card permanently.

## WiFi Configuration

### SoftAP (Hotspot)
- **SSID:** RCWM
- **Password:** testRCWM2026!
- **Max connections:** 4
- **Channel:** 1
- **IP:** 192.168.4.1 (DHCP server assigns 192.168.4.2+)

### STA (Client)
- Prefilled with default credentials (SSID: "paradise", password: "foilersarecool")
- Changeable via on-screen keyboard
- Password field is visible (not hidden) for easy entry

### STA + AP Simultaneous
WiFi runs in APSTA mode — both STA and SoftAP can be active at the same time. The flowmeter connects to the RCWM hotspot while the field tool connects to the internet via STA for firmware downloads.

## Target Flash Layout (Flowmeter ESP32)

| Binary | Address |
|--------|---------|
| bootloader.bin | 0x1000 |
| partition-table.bin | 0x10000 |
| ota_data_initial.bin | 0x15000 |
| flow_meter.bin | 0x20000 |

## Project Structure

```
main/
├── app_main.c              # Entry point: init SD, WiFi, display, USB
├── app_config.h            # Pin defs, paths, WiFi config, API URLs
├── idf_component.yml       # Dependencies (esp_hosted, esp_wifi_remote, etc.)
├── ui/                     # LVGL touchscreen UI
│   ├── ui_manager.c/h      # Screen switching
│   ├── ui_home.c/h         # Home screen with 4 tiles
│   ├── ui_serial_monitor.c/h  # Colored log viewer
│   ├── ui_flasher.c/h      # Flash progress + button + virgin chip flow
│   ├── ui_wifi_ap.c/h      # WiFi STA + SoftAP + firmware download UI
│   ├── ui_settings.c/h     # Settings screen (stub)
│   └── ui_styles.c/h       # Shared colors/fonts
├── serial/                 # Serial monitor module
│   ├── serial_monitor.c/h  # USB Host CH340, ring buffer, pause/resume, debug echo
│   ├── log_parser.c/h      # Parse ESP-IDF log format
│   └── log_storage.c/h     # Write logs to SD card
├── flasher/                # Firmware flasher module
│   ├── flasher_manager.c/h # esp-serial-flasher integration, flash state machine
│   ├── flasher_port.c/h    # USB CDC-ACM port for esp-serial-flasher
│   └── efuse_burn.c/h      # eFuse burn for virgin chips (encryption key + FLASH_CRYPT_CNT)
├── wifi/                   # WiFi management
│   ├── wifi_manager.c/h    # STA + SoftAP control, event handling, status
│   └── firmware_download.c/h  # HTTP firmware check + download with SHA256
└── sdcard/                 # SD card mount + helpers
    └── sdcard_manager.c/h
```

## Key Architecture Decisions

- **USB is shared** between serial monitor and flasher (same CH340 chip). Mutual exclusion via `serial_monitor_pause()`/`resume()`.
- **esp-serial-flasher** library handles the flash protocol (SLIP, stub loader, encrypted flash).
- **Firmware loaded into PSRAM** before flashing (all 4 bins ~1.1MB total, 32MB PSRAM available).
- **L2 cache reduced to 128KB** (from 256KB) to free 128KB internal SRAM for DMA — needed because ESP-Hosted SDIO and USB Host both require internal DMA-capable memory.
- **USB Host DMA in PSRAM** (`CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM=y`) to further reduce internal RAM pressure.
- **WiFi init before USB Host** in app_main because ESP-Hosted's constructor runs before app_main anyway. Serial monitor init is non-fatal (graceful error if USB Host can't allocate DMA memory).
- **FAT32 Long Filenames** must be enabled (`CONFIG_FATFS_LFN_HEAP=y`) or files show as 8.3 names.
- **LVGL v9** — use absolute pixel sizes (not `lv_pct()` mixed with pixel math). Screen is 1024x600.

## Key sdkconfig Settings

```
CONFIG_SPIRAM=y                              # 32MB PSRAM
CONFIG_CACHE_L2_CACHE_128KB=y                # 128KB L2 (frees 128KB internal SRAM for DMA)
CONFIG_CACHE_L2_CACHE_LINE_128B=y            # Required with 128KB L2
CONFIG_FATFS_LFN_HEAP=y                      # Long filenames on SD card
CONFIG_SERIAL_FLASHER_INTERFACE_USB=y         # esp-serial-flasher via USB CDC
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y             # SoftAP hotspot
CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM=y  # USB Host DMA buffers in PSRAM
```

## Bugs Fixed (for reference)

### ESP-IDF Framework Patches
- **TCM assert crash (ESP-IDF #15996):** `xTaskCreateStaticPinnedToCore` asserts because 7KB TCM memory at `0x30100000` has `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` but `esp_ptr_internal()` doesn't recognize TCM addresses. Fix: patched `heap_idf.c` to add `esp_ptr_in_tcm()` checks to `xPortCheckValidListMem`, `xPortCheckValidTCBMem`, and `xPortcheckValidStackMem`. File: `C:\Espressif\frameworks\esp-idf-v5.5\components\freertos\heap_idf.c`

### Memory / DMA
- **WiFi + USB Host DMA conflict:** ESP-Hosted SDIO consumed all DMA-capable internal memory (only 64 bytes left), causing USB Host install to fail with `ESP_ERR_NO_MEM`. Fix: reduced L2 cache 256KB→128KB + USB Host PSRAM DMA. This freed ~120KB internal DMA memory.
- **ESP-Hosted constructor:** `esp_hosted_host_init()` uses `__attribute__((constructor))` and runs before `app_main()`, so you can't init USB Host before WiFi.

### WiFi
- **Auth failures (reason=2, reason=205):** Wrong password from touchscreen. Fix: made password visible + prefilled defaults.
- **API encryption_mismatch:** Firmware check API infers encryption from version (>=7.9 = encrypted). Sending `current_version=0.0` returned wrong firmware. Fix: always send >=8.0.

### Build / Code
- `loader_port_change_transmission_rate()` doesn't exist for USB — removed (USB has no baud rate)
- `xSemaphoreTake` with 500ms timeout in connection_task — must check return value
- FAT32 without LFN shows `BOOTLO~1.BIN` instead of `bootloader.bin` — enable `CONFIG_FATFS_LFN_HEAP`
- `snprintf` implicit declaration — add `#include <stdio.h>` to log_parser.c
- `strncpy` truncation warning (`-Werror=stringop-truncation`) — replaced with `snprintf`
- Serial monitor log container sizing — use absolute pixels, not `lv_pct(100) - pixels`
