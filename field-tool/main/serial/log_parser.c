#include "log_parser.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool log_parser_parse(const char *line, log_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->raw, sizeof(entry->raw), "%s", line);

    /* Must start with a valid log level character */
    if (line[0] != 'I' && line[0] != 'W' && line[0] != 'E' &&
        line[0] != 'D' && line[0] != 'V') {
        /* Not a standard ESP-IDF log line — store as-is */
        entry->level = ' ';
        snprintf(entry->message, sizeof(entry->message), "%s", line);
        return false;
    }

    /* Check for " (" after the level character */
    if (line[1] != ' ' || line[2] != '(') {
        entry->level = ' ';
        snprintf(entry->message, sizeof(entry->message), "%s", line);
        return false;
    }

    entry->level = line[0];

    /* Parse timestamp: "(12345) " */
    const char *ts_start = line + 3;
    const char *ts_end = strchr(ts_start, ')');
    if (ts_end == NULL) {
        entry->level = ' ';
        snprintf(entry->message, sizeof(entry->message), "%s", line);
        return false;
    }

    entry->timestamp_ms = (uint32_t)strtoul(ts_start, NULL, 10);

    /* After ") " comes the tag, then ": ", then the message */
    const char *tag_start = ts_end + 2; /* skip ") " */
    const char *colon = strstr(tag_start, ": ");
    if (colon == NULL) {
        /* No colon separator — treat rest as message */
        snprintf(entry->message, sizeof(entry->message), "%s", tag_start);
        return true;
    }

    /* Extract tag */
    size_t tag_len = colon - tag_start;
    if (tag_len >= sizeof(entry->tag)) {
        tag_len = sizeof(entry->tag) - 1;
    }
    memcpy(entry->tag, tag_start, tag_len);
    entry->tag[tag_len] = '\0';

    /* Extract message */
    const char *msg_start = colon + 2;
    snprintf(entry->message, sizeof(entry->message), "%s", msg_start);

    return true;
}
