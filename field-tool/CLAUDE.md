# Field Tool — Portable ESP32 Flasher & Serial Monitor

## What This Is

A standalone ESP-IDF app for the **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** board (7" 1024x600 touchscreen). It replaces needing a laptop in the field to flash, monitor, and connect to chemproject flowmeter devices.

## Three Features

1. **Serial Monitor** — Plug a flowmeter into the USB-A port (CH340), see colored logs on the touchscreen, auto-saved to SD card
2. **Firmware Flasher** — Flash new firmware onto flowmeters from .bin files on the SD card, one-touch button
3. **WiFi Hotspot** — Broadcast SSID "RCWM" with password "testRCWM2026!" so flowmeters auto-connect in the field

## Hardware

- **Board:** Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (ESP32-P4, 32MB PSRAM, 32MB flash, 7" touch)
- **Target devices:** Chemproject ESP32 flowmeters with CH340 USB bridge
- **Connection:** Flowmeter plugs into the P4 board's USB-A host port
- **SD card:** MicroSD formatted FAT32, holds firmware binaries and log files
- **Board repo (for reference/BSP):** `C:\ESP32-P4-WIFI6-Touch-LCD-7B` (cloned from waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B)

## Current Status (Feb 2026)

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Project skeleton + display + SD card | DONE |
| 2 | Serial monitor (USB CH340 → colored logs) | DONE, working |
| 3 | Firmware flasher (SD → flash via USB) | UI done, SD detection working, **flash not yet tested on real device** |
| 4 | Unencrypted device flashing (two-step) | Not started |
| 5 | WiFi hotspot (SoftAP via C6 coprocessor) | Not started |
| 6 | Settings & polish | Not started |

### What Works Right Now
- Touchscreen UI with home screen, navigation between screens
- CH340 auto-detect and connect via USB Host
- Real-time colored serial log display (green=Info, yellow=Warn, red=Error)
- Log parsing of ESP-IDF format lines
- PSRAM ring buffer (4096 entries) for log history
- Background log writer to SD card
- Flasher UI with progress bar, status, SD card file detection
- Firmware files detected on SD card (bootloader.bin, partition-table.bin, ota_data_initial.bin, flow_meter.bin)
- Pause/resume serial monitor for flasher mutual exclusion

### What Needs Testing
- Actually pressing FLASH DEVICE button and flashing a real flowmeter
- The esp-serial-flasher USB CDC-ACM path (connects to bootloader via DTR/RTS, flashes 4 binaries)

### Known Issues
- WiFi dependencies (esp_wifi_remote, esp_hosted) are commented out — they crash on boot when combined with USB host CDC-ACM components. Need to resolve for Phase 5.

## Build & Flash

Must use **ESP-IDF terminal** in VS Code (not regular PowerShell/bash):

```
cd C:\ESP32-P4-WIFI6-Touch-LCD-7B\field-tool
idf.py -p COM5 flash monitor
```

After changing `sdkconfig.defaults`:
```
idf.py fullclean
idf.py set-target esp32p4
idf.py -p COM5 flash monitor
```

Board is on **COM5**. ESP-IDF v5.5.

## SD Card Setup

Format microSD as **FAT32**. Copy these 4 files from the chemproject build output into a `firmware` folder:

```
firmware/bootloader.bin        ← from build/bootloader/
firmware/partition-table.bin   ← from build/partition_table/
firmware/ota_data_initial.bin  ← from build/
firmware/flow_meter.bin        ← from build/
```

Source: `C:\chemical-monitoring-project\firmware\esp-idf-flowmeter\build\`

The app auto-creates `firmware/`, `logs/`, and `config/` directories on boot.

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
├── app_main.c              # Entry point: init BSP, SD, USB, create UI
├── app_config.h            # Pin defs, paths, constants
├── idf_component.yml       # Dependencies
├── ui/                     # LVGL touchscreen UI
│   ├── ui_manager.c/h      # Screen switching
│   ├── ui_home.c/h         # Home screen with 3 tiles
│   ├── ui_serial_monitor.c/h  # Colored log viewer
│   ├── ui_flasher.c/h      # Flash progress + button
│   ├── ui_wifi_ap.c/h      # WiFi AP screen (stub)
│   ├── ui_settings.c/h     # Settings screen (stub)
│   └── ui_styles.c/h       # Shared colors/fonts
├── serial/                 # Serial monitor module
│   ├── serial_monitor.c/h  # USB Host CH340, ring buffer, pause/resume
│   ├── log_parser.c/h      # Parse ESP-IDF log format
│   └── log_storage.c/h     # Write logs to SD card
├── flasher/                # Firmware flasher module
│   └── flasher_manager.c/h # esp-serial-flasher integration, flash state machine
├── wifi/                   # WiFi hotspot (Phase 5)
└── sdcard/                 # SD card mount + helpers
    └── sdcard_manager.c/h
```

## Key Architecture Decisions

- **USB is shared** between serial monitor and flasher (same CH340 chip). Mutual exclusion via `serial_monitor_pause()`/`resume()`.
- **esp-serial-flasher** library handles the entire flash protocol (SLIP, stub loader, encrypted flash). Its built-in USB CDC-ACM port (`esp32_usb_cdc_acm_port.c`) does CH340 auto-detect and DTR/RTS bootloader entry.
- **Firmware loaded into PSRAM** before flashing (all 4 bins ~1.1MB total, 32MB PSRAM available).
- **FAT32 Long Filenames** must be enabled (`CONFIG_FATFS_LFN_HEAP=y`) or files show as 8.3 names.
- **LVGL v9** — use absolute pixel sizes (not `lv_pct()` mixed with pixel math). Screen is 1024x600.

## Key sdkconfig Settings

```
CONFIG_SPIRAM=y                              # 32MB PSRAM
CONFIG_FATFS_LFN_HEAP=y                      # Long filenames on SD card
CONFIG_SERIAL_FLASHER_INTERFACE_USB=y         # esp-serial-flasher via USB CDC
# CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y            # Commented out until Phase 5
```

## Bugs Fixed (for reference)

- `loader_port_change_transmission_rate()` doesn't exist for USB — removed (USB has no baud rate)
- `xSemaphoreTake` with 500ms timeout in connection_task — must check return value or it closes the device every 500ms
- FAT32 without LFN shows `BOOTLO~1.BIN` instead of `bootloader.bin` — enable `CONFIG_FATFS_LFN_HEAP`
- ESP-Hosted + USB Host CDC-ACM crash on boot — defer WiFi to Phase 5
- `snprintf` implicit declaration — add `#include <stdio.h>` to log_parser.c
- Serial monitor log container sizing — use absolute pixels, not `lv_pct(100) - pixels`
