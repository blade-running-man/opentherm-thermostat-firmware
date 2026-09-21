// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char   s_lines[OT_LOG_LINES][OT_LOG_LINE_LEN];
static size_t s_next;   // where the next line goes
static size_t s_count;  // how many slots are in use, up to OT_LOG_LINES

void ot_log_reset(void)
{
    s_next  = 0;
    s_count = 0;
}

void ot_log_write(const char *line)
{
    if (line == NULL || line[0] == '\0')
        return;

    char *slot = s_lines[s_next];
    strncpy(slot, line, OT_LOG_LINE_LEN - 1);
    slot[OT_LOG_LINE_LEN - 1] = '\0';

    // Trailing newlines are the logger's framing, not content, and would otherwise be
    // escaped into every line of the rendered array.
    size_t n = strlen(slot);
    while (n > 0 && (slot[n - 1] == '\n' || slot[n - 1] == '\r'))
        slot[--n] = '\0';

    s_next = (s_next + 1) % OT_LOG_LINES;
    if (s_count < OT_LOG_LINES)
        s_count++;
}

// Log lines are the only arbitrary text on this device, so this is where invalid JSON would
// come from. Escaping is local rather than shared with ot_api so that this component
// stays independent of the entity model -- the log has to work when nothing else does.
static bool append_escaped(char *out, size_t cap, size_t *used, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8];
        const char *piece = esc;
        switch (*p) {
        case '"':  piece = "\\\""; break;
        case '\\': piece = "\\\\"; break;
        case '\n': piece = "\\n";  break;
        case '\r': piece = "\\r";  break;
        case '\t': piece = "\\t";  break;
        default:
            if (*p < 0x20 || *p > 0x7e)
                snprintf(esc, sizeof esc, "\\u%04x", (unsigned)*p);
            else {
                esc[0] = (char)*p;
                esc[1] = '\0';
            }
            break;
        }
        const size_t need = strlen(piece);
        if (*used + need + 1 > cap)
            return false;
        memcpy(out + *used, piece, need);
        *used += need;
    }
    return true;
}

size_t ot_log_render(char *out, size_t cap)
{
    if (out == NULL || cap < 3)
        return 0;

    size_t used = 0;
    out[used++] = '[';

    const size_t first = s_count == OT_LOG_LINES ? s_next : 0;
    for (size_t i = 0; i < s_count; i++) {
        if (i > 0) {
            if (used + 1 >= cap)
                return 0;
            out[used++] = ',';
        }
        if (used + 1 >= cap)
            return 0;
        out[used++] = '"';
        if (!append_escaped(out, cap - 1, &used, s_lines[(first + i) % OT_LOG_LINES]))
            return 0;
        if (used + 1 >= cap)
            return 0;
        out[used++] = '"';
    }

    if (used + 2 > cap)
        return 0;
    out[used++] = ']';
    out[used]   = '\0';
    return used;
}

#ifdef ESP_PLATFORM
#include "esp_log.h"

static vprintf_like_t s_previous;

static int log_hook(const char *format, va_list args)
{
    char line[OT_LOG_LINE_LEN];
    // vsnprintf on a copy: the original list is consumed by the console writer below, and
    // reusing a va_list after it has been walked is undefined.
    va_list copy;
    va_copy(copy, args);
    vsnprintf(line, sizeof line, format, copy);
    va_end(copy);

    ot_log_write(line);
    return s_previous != NULL ? s_previous(format, args) : 0;
}

void ot_log_install(void)
{
    // The console keeps working: USB is the only channel when the network is the thing that
    // is broken, and this buffer is an addition to it, never a replacement.
    s_previous = esp_log_set_vprintf(log_hook);
}
#else
void ot_log_install(void) {}
#endif
