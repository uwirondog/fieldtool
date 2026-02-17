#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

/**
 * Parsed log entry from ESP-IDF serial output
 */
typedef struct {
    char     level;                         /* 'I', 'W', 'E', 'D', 'V', or ' ' for unknown */
    uint32_t timestamp_ms;                  /* ESP-IDF timestamp in parentheses */
    char     tag[FT_LOG_TAG_MAX_LEN];       /* e.g. "MAIN", "WIFI_MANAGER" */
    char     message[FT_LOG_MSG_MAX_LEN];   /* The log message text */
    char     raw[FT_LOG_MSG_MAX_LEN + 48];  /* Original raw line for SD card storage */
} log_entry_t;

/**
 * @brief Parse a single line of ESP-IDF log output.
 *
 * Expected format: "X (12345) TAG: message text here"
 * where X is one of I, W, E, D, V.
 *
 * If the line doesn't match, level=' ' and message contains the raw line.
 *
 * @param line     Raw line (null-terminated, no trailing newline)
 * @param entry    Output parsed entry
 * @return true if parsed as a valid ESP-IDF log line
 */
bool log_parser_parse(const char *line, log_entry_t *entry);
