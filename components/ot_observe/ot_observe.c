// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_observe.h"

#include <stdarg.h>

#include <stdio.h>
#include <string.h>

void ot_observe_reset(ot_observe_t *o) { memset(o, 0, sizeof *o); }

void ot_observe_record(ot_observe_t *o, uint8_t data_id, ot_msg_type_t type,
                       uint16_t raw, uint32_t now_ms)
{
    if (data_id >= OT_OBSERVE_IDS) return;
    ot_observe_entry_t *e = &o->e[data_id];
    e->seen    = true;
    e->type    = (uint8_t)type;
    e->raw     = raw;
    e->last_ms = now_ms;
    if (e->count < UINT32_MAX) e->count++;
}

const ot_observe_entry_t *ot_observe_get(const ot_observe_t *o, uint8_t data_id)
{
    return data_id < OT_OBSERVE_IDS ? &o->e[data_id] : NULL;
}

uint32_t ot_observe_seen_count(const ot_observe_t *o)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < OT_OBSERVE_IDS; ++i)
        if (o->e[i].seen) n++;
    return n;
}

// A word for each type. "Unknown identifier" and "data invalid" are DIFFERENT answers
// from the boiler: the first means "I do not have such a thing", the second "I do, but
// I have nothing to say right now". Merging them into one would lose half the
// diagnostics.
static const char *type_word(uint8_t t)
{
    switch ((ot_msg_type_t)t) {
    case OT_MSG_READ_DATA:      return "read-data";
    case OT_MSG_WRITE_DATA:     return "write-data";
    case OT_MSG_INVALID_DATA:   return "invalid-data";
    case OT_MSG_RESERVED:       return "reserved";
    case OT_MSG_READ_ACK:       return "read-ack";
    case OT_MSG_WRITE_ACK:      return "write-ack";
    case OT_MSG_DATA_INVALID:   return "data-invalid";
    case OT_MSG_UNKNOWN_DATAID: return "unknown-dataid";
    }
    return "?";
}

// Appends to the buffer with snprintf semantics, accumulating the REQUIRED length even
// after the room has run out. That is where the renderer's return value comes from.
static void appendf(char *out, size_t cap, size_t *need, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void appendf(char *out, size_t cap, size_t *need, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // Write only if there is still room in the buffer for the terminating NUL.
    char  *dst  = (*need < cap) ? out + *need : NULL;
    size_t room = (*need < cap) ? cap - *need : 0;
    const int n = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (n > 0) *need += (size_t)n;
}

size_t ot_observe_render_json(const ot_observe_t *o, const ot_observe_header_t *h,
                              uint32_t now_ms, char *out, size_t cap)
{
    size_t need = 0;
    if (cap > 0) out[0] = '\0';

    appendf(out, cap, &need,
            "{\"uptime_ms\":%u,\"cycles\":%u,\"ok\":%u,\"failed\":%u,"
            "\"overdue\":%u,\"answering\":%s,\"in_duty\":%u,"
            "\"timeout\":%u,\"frame_error\":%u,\"parity_error\":%u,"
            "\"rx_edges\":%u,\"pins_shorted\":%s,"
            "\"scan_done\":%u,\"scan_total\":%u,\"ids\":[",
            (unsigned)h->uptime_ms, (unsigned)h->cycles, (unsigned)h->ok,
            (unsigned)h->failed, (unsigned)h->overdue, h->answering ? "true" : "false",
            (unsigned)h->in_duty, (unsigned)h->timeout,
            (unsigned)h->frame_error, (unsigned)h->parity_error,
            (unsigned)h->rx_edges, h->pins_shorted ? "true" : "false",
            (unsigned)h->scan_done, (unsigned)h->scan_total);

    bool first = true;
    // In ascending order of identifier -- because the table is read by eye, and rows
    // that jump around ruin it. The array traversal order is the output order.
    for (unsigned i = 0; i < OT_OBSERVE_IDS; ++i) {
        const ot_observe_entry_t *e = &o->e[i];
        if (!e->seen) continue;
        // A difference in unsigned arithmetic survives the millisecond overflow that
        // arrives after 49 days; a straightforward signed subtraction would give a
        // negative age and a table that cannot be trusted.
        const uint32_t age = now_ms - e->last_ms;
        appendf(out, cap, &need,
                "%s{\"id\":%u,\"type\":\"%s\",\"raw\":%u,\"age_ms\":%u,\"count\":%u}",
                first ? "" : ",", i, type_word(e->type), (unsigned)e->raw,
                (unsigned)age, (unsigned)e->count);
        first = false;
    }

    appendf(out, cap, &need, "]}");
    return need;
}
