// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Private to components/ot_config: what the pure sources share, and nothing outside may use.
//
// Beside the sources and NOT in include/, and the placement is the contract: INCLUDE_DIRS
// "include" is all another component is given, so no name declared here can be reached from
// outside this directory. DO NOT move it into include/ -- that makes these helpers public API,
// and the cut that created this file was allowed only because ot_config.h did not change.
//
// The three helpers are static inline, not exported: each was a file-local static before the cut,
// each is a few lines, and keeping the names keeps every call site and every comment that names
// one exactly as it was. ot_config_parse_record() is exported because it is fifty lines long and
// two files call it.
//
// C only, and pure like everything that includes it: no ESP-IDF header may appear here.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ot_config.h"

static inline bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// True when the array holds a terminated string. An array with no NUL anywhere in it was not
// written by this build -- every writer here terminates -- so it came from a short read, a bit
// flip, or a firmware that is not this one.
//
// DO NOT delete this because the tests pass without it. THEY DO: every array here is exactly
// MAX+1 bytes, so an unterminated one always measures longer than its own limit and the length
// check rejects it either way -- the outcome is identical and Unity cannot tell the difference.
// What differs is that strlen() got there first. Measured with AddressSanitizer over a
// heap-allocated ot_config_t filled with 0xff: with this guard, clean; without it,
// `READ of size 743, 0 bytes after 840-byte region` inside ot_config_check_host(), because
// the last member of the struct is a bool and a true one gives strlen nothing to stop at.
static inline bool terminated(const char *buf, size_t cap)
{
    return memchr(buf, '\0', cap) != NULL;
}

static inline void copy_str(char *dst, size_t cap, const char *src)
{
    // Everything committed through here was length-checked before anything was written, so this
    // cannot truncate in practice. It is bounded anyway: a field added later without its checker
    // then loses characters instead of smashing the struct behind it.
    const size_t n = strlen(src);
    const size_t k = n < cap ? n : cap - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
}

typedef struct {
    uint32_t    iterations;
    uint8_t     salt[OT_CONFIG_SALT_LEN];
    const char *digest_hex;  // borrowed from the record; exactly DIGEST_LEN*2 characters
} password_record_t;

// Parses a record; with `out` NULL it only answers whether the record would parse, which is the
// question ot_config_sanitize() asks. Defined in ot_config_record.c, beside the format's reasons.
bool ot_config_parse_record(const char *record, password_record_t *out);

// A setpoint moved into [lo, hi]: the nearest edge when it is outside, itself when it is inside.
// Both edges are flow bounds and so on the half-degree grid, which keeps the result on it. One
// rule for the two places a stored local setpoint can be stranded by its band -- a band saved by
// ot_config_apply() and a band repaired by ot_config_sanitize() -- and for the failsafe default
// sanitize repairs a damaged failsafe to inside a band it keeps.
static inline uint16_t into_band(uint16_t value, uint16_t lo, uint16_t hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}
