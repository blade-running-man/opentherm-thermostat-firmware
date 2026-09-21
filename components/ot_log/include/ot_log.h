// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The device log the web UI reads, kept in RAM.
//
// A fixed ring, because the alternative is a device that runs out of memory after a week of
// uptime. When it fills, the OLDEST line goes: during an incident the interesting lines are
// always the most recent ones, and a log that stops recording when full is worse than none.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 80 lines of 160 characters is about 13 KB, which buys roughly the last minute of a busy
// boot. Both are deliberate round numbers rather than measurements; raise them only against
// a measured RAM figure, not a feeling.
#define OT_LOG_LINES    80
#define OT_LOG_LINE_LEN 160

void ot_log_reset(void);

// Records one line, truncating anything longer than a slot. Never allocates and never
// blocks: it is called from whatever task happened to log, including at boot.
void ot_log_write(const char *line);

// Renders the buffer as a JSON array of strings, oldest first. Returns 0 if it does not fit
// -- the same contract as the other renderers, so a caller cannot serve half a document
// with 200 OK.
size_t ot_log_render(char *out, size_t cap);

// Routes ESP-IDF's own logging through this buffer as well as the console. Device only.
void ot_log_install(void);

#ifdef __cplusplus
}
#endif
