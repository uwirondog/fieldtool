set(SRCS
    "app_main.c"
    "sdcard/sdcard_manager.c"
    "serial/serial_monitor.c"
    "serial/log_parser.c"
    "serial/log_storage.c"
    "ui/ui_styles.c"
    "ui/ui_manager.c"
    "ui/ui_home.c"
    "ui/ui_serial_monitor.c"
    "ui/ui_flasher.c"
    "ui/ui_wifi_ap.c"
    "ui/ui_settings.c"
    "flasher/flasher_manager.c"
)

set(INCLUDE_DIRS
    "."
    "ui"
    "sdcard"
    "serial"
    "flasher"
    "wifi"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
)
