// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Private to components/ot_wire: the bounded writer every renderer of this component writes
// through, and the four result constructors every parser answers with.
//
// Beside the sources and NOT in include/, the same arrangement as ot_api_sink.h: INCLUDE_DIRS
// "include" is all another component is given, so nothing declared here can be reached from
// outside this directory. DO NOT move it into include/ -- that makes these helpers public API, and
// the cut that created this file was allowed only because ot_wire.h did not change.
//
// static inline rather than exported: each was a file-local static of ot_wire.c until the file was
// split, each is a few lines, and keeping the names keeps every call site and every comment that names one
// exactly as it was. The two rules at the top of ot_wire.c bind everything here: no secret is
// rendered, and no submitted value is quoted in an error.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ot_json.h"
#include "ot_wire.h"

// --- writing ------------------------------------------------------------------------------

// A bounded append, so truncation is handled once rather than at forty call sites. The same shape
// as ot_api.cpp's Writer and for the same reason: a caller that gets a plausible short
// length back serves valid-looking JSON that stops mid-object with a 200 in front of it, and
// nothing in the log says so. The entity list once came twenty-three bytes from exactly that.
typedef struct {
    char  *buf;
    size_t cap;
    size_t used;
    bool   overflowed;
} writer_t;

static inline void put(writer_t *w, const char *s)
{
    if (w->buf == NULL || w->cap == 0) {
        w->overflowed = true;
        return;
    }
    while (*s) {
        if (w->used + 1 >= w->cap) {
            w->overflowed = true;
            return;
        }
        w->buf[w->used++] = *s++;
    }
    w->buf[w->used] = '\0';
}

static inline void put_u32(writer_t *w, uint32_t value)
{
    char tmp[12];
    snprintf(tmp, sizeof tmp, "%u", (unsigned)value);
    put(w, tmp);
}

static inline void put_i32(writer_t *w, int32_t value)
{
    char tmp[13];
    snprintf(tmp, sizeof tmp, "%d", (int)value);
    put(w, tmp);
}

static inline void put_bool(writer_t *w, bool value) { put(w, value ? "true" : "false"); }

// Every string that leaves this file goes through the escaper, including the ones that "cannot"
// contain anything interesting. An SSID is chosen by whoever set up the router next door, and a
// device name is chosen by the owner; both reach a browser as JSON.
static inline void put_string(writer_t *w, const char *s)
{
    char escaped[512];
    ot_json_escape(s != NULL ? s : "", escaped, sizeof escaped);
    put(w, "\"");
    put(w, escaped);
    put(w, "\"");
}

static inline void put_key(writer_t *w, const char *key)
{
    put(w, "\"");
    put(w, key);
    put(w, "\":");
}

static inline size_t finish(writer_t *w) { return w->overflowed ? 0 : w->used; }

// --- how a submission failed ------------------------------------------------------------------

static inline ot_wire_result_t ok(void)
{
    const ot_wire_result_t r = {OT_WIRE_OK, OT_CONFIG_OK, NULL};
    return r;
}

static inline ot_wire_result_t bad_body(void)
{
    const ot_wire_result_t r = {OT_WIRE_BAD_BODY, OT_CONFIG_OK, NULL};
    return r;
}

static inline ot_wire_result_t bad_field(const char *field)
{
    const ot_wire_result_t r = {OT_WIRE_BAD_FIELD, OT_CONFIG_OK, field};
    return r;
}

static inline ot_wire_result_t refused(ot_config_err_t err, const char *field)
{
    const ot_wire_result_t r = {OT_WIRE_REFUSED, err, field};
    return r;
}
