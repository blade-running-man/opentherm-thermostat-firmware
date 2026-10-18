// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_api.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ot_api_sink.h"
#include "ot_lock.h"
#include "ot_registry.h"
#include "ot_state.h"

// The accumulator is shared with ot_api_control.c through the private ot_api_sink.h, where its
// contract is written; it is defined here because this is where it was born.
void ot_api_emit(ot_api_sink_t *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *dst = (s->need < s->cap) ? s->out + s->need : NULL;
    const size_t room = (s->need < s->cap) ? s->cap - s->need : 0;
    const int n = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (n > 0)
        s->need += (size_t)n;
}

// The caller learns about a shortfall from the returned size, not from the content.
void ot_api_terminate(const ot_api_sink_t *s)
{
    if (s->cap)
        s->out[s->need < s->cap ? s->need : s->cap - 1] = '\0';
}

static const char *avail_name(ot_availability_t a)
{
    switch (a) {
    case OT_AVAIL_OK:          return "ok";
    case OT_AVAIL_INVALID:     return "invalid";
    case OT_AVAIL_UNSUPPORTED: return "unsupported";
    default:                   return "unknown";
    }
}

// Registry strings are printed raw: there is nothing to escape here, because
// tools/tests/test_registry.py does not let into the table a character that
// escaping would change. See the comment in ot_api.h.
static void emit_options(ot_api_sink_t *s, const ot_entity_t *e)
{
    const uint8_t n = ot_registry_option_count(e);
    if (n == 0)
        return;
    ot_api_emit(s, ",\"options\":[");
    for (uint8_t k = 0; k < n; k++) {
        size_t len = 0;
        const char *o = ot_registry_option(e, k, &len);
        ot_api_emit(s, "%s\"%.*s\"", k ? "," : "", (int)len, o);
    }
    ot_api_emit(s, "]");
}

static void emit_meta(ot_api_sink_t *s, const ot_entity_t *e)
{
    ot_api_emit(s, "{\"key\":\"%s\",\"name\":\"%s\",\"data_id\":", e->key, e->name);
    // null for a synthetic row. DO NOT print data_id with %u: an int16_t -1 becomes
    // 4294967295, a number a client would take for a Data-ID.
    if (e->data_id < 0)
        ot_api_emit(s, "null");
    else
        ot_api_emit(s, "%d", (int)e->data_id);
    ot_api_emit(s, ",\"writable\":%s", e->writable ? "true" : "false");
    if (e->unit)
        ot_api_emit(s, ",\"unit\":\"%s\"", e->unit);
    if (e->device_class)
        ot_api_emit(s, ",\"device_class\":\"%s\"", e->device_class);
    if (e->entity_category)
        ot_api_emit(s, ",\"entity_category\":\"%s\"", e->entity_category);
    emit_options(s, e);

    // ot_state_bounds() takes ot_lock() itself; the lock is recursive exactly for
    // this, so that the snapshot stays consistent across the whole document.
    float lo = 0, hi = 0;
    if (ot_state_bounds(e->key, &lo, &hi))
        ot_api_emit(s, ",\"min\":%.2f,\"max\":%.2f", (double)lo, (double)hi);
    ot_api_emit(s, "}");
}

// The option, not its index: Home Assistant stores an enum sensor's string. The checks
// stand before the cast -- (unsigned) of a NaN or a negative is undefined, and
// of 1.5 is 1, an option nobody set -- although ot_state_set_virtual() stores none of the three
// (test_an_enum_value_between_two_options_is_refused_and_the_last_one_renders pins that chain).
static void emit_option_value(ot_api_sink_t *s, const ot_entity_t *e, float index)
{
    size_t len = 0;
    const char *o = (index >= 0.0f && index < 256.0f && index == floorf(index))
                        ? ot_registry_option(e, (unsigned)index, &len) : NULL;
    if (o)
        ot_api_emit(s, "\"%.*s\"", (int)len, o);
    else
        ot_api_emit(s, "null");
}

// THE one place it is decided how a value prints, for GET /api/state and the /ws frame alike
// (ot_api.h, ot_api_render_frame, says what went wrong while there were two).
static void emit_bare_value(ot_api_sink_t *s, const ot_entity_t *e, const ot_value_t *v)
{
    // null, not zero: "there is no data" and "zero" are different statements, and
    // zero here would mean "it is 0 °C outside" where the boiler said nothing.
    if (v->availability != OT_AVAIL_OK)
        ot_api_emit(s, "null");
    else if (ot_registry_is_boolean(e))
        ot_api_emit(s, "%s", v->boolean ? "true" : "false");
    else if (e->kind == OT_KIND_ENUM)
        emit_option_value(s, e, v->number);
    else
        ot_api_emit(s, "%.2f", (double)v->number);
}

// The value behind a registry entry, or one that never arrived when ot_state has none for it --
// which prints as null. The same answer in all three renderers: the /ws frame used to SKIP such a
// key while the documents read an uninitialised value. null, not a skip, because a `state` frame
// replaces the client's whole map, so a skipped key would vanish instead of reading "no data".
// Unreachable today (every key here comes from the registry ot_state is indexed by).
static ot_value_t value_of(const ot_entity_t *e)
{
    ot_value_t v = { .availability = OT_AVAIL_UNKNOWN, .number = NAN };
    (void)ot_state_get(e->key, &v);   // leaves v untouched on failure (ot_state.h)
    return v;
}

static void emit_value(ot_api_sink_t *s, const ot_entity_t *e, const ot_value_t *v,
                       uint32_t now_ms)
{
    ot_api_emit(s, "{\"availability\":\"%s\",\"value\":", avail_name(v->availability));
    emit_bare_value(s, e, v);

    if (v->updated_ms)
        ot_api_emit(s, ",\"age_ms\":%u", (unsigned)(now_ms - v->updated_ms));
    else
        ot_api_emit(s, ",\"age_ms\":null");
    ot_api_emit(s, "}");
}

size_t ot_api_render_entities(char *out, size_t cap)
{
    ot_api_sink_t s = { out, cap, 0 };
    ot_lock();
    ot_api_emit(&s, "{\"schema\":%u,\"entities\":[", (unsigned)OT_SCHEMA_VERSION);
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        if (i)
            ot_api_emit(&s, ",");
        emit_meta(&s, ot_registry_at(i));
    }
    ot_api_emit(&s, "]}");
    ot_unlock();
    ot_api_terminate(&s);
    return s.need;
}

size_t ot_api_render_state(char *out, size_t cap, uint32_t now_ms)
{
    ot_api_sink_t s = { out, cap, 0 };
    ot_lock();
    ot_api_emit(&s, "{\"schema\":%u,\"state\":{", (unsigned)OT_SCHEMA_VERSION);
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        const ot_value_t   v = value_of(e);
        if (i)
            ot_api_emit(&s, ",");
        ot_api_emit(&s, "\"%s\":", e->key);
        emit_value(&s, e, &v, now_ms);
    }
    ot_api_emit(&s, "}}");
    ot_unlock();
    ot_api_terminate(&s);
    return s.need;
}

size_t ot_api_render_entity(const char *key, char *out, size_t cap, uint32_t now_ms)
{
    const int i = ot_registry_index_of(key);
    if (i < 0)
        return 0;

    ot_api_sink_t s = { out, cap, 0 };
    ot_lock();
    const ot_entity_t *e = ot_registry_at((uint16_t)i);
    const ot_value_t   v = value_of(e);
    ot_api_emit(&s, "{\"meta\":");
    emit_meta(&s, e);
    ot_api_emit(&s, ",\"value\":");
    emit_value(&s, e, &v, now_ms);
    ot_api_emit(&s, "}");
    ot_unlock();
    ot_api_terminate(&s);
    return s.need;
}

size_t ot_api_render_value(uint16_t index, char *out, size_t cap)
{
    const ot_entity_t *e = ot_registry_at(index);
    if (e == NULL)
        return 0;
    ot_api_sink_t s = { out, cap, 0 };
    ot_lock();
    const ot_value_t v = value_of(e);
    emit_bare_value(&s, e, &v);
    ot_unlock();
    ot_api_terminate(&s);
    return s.need;
}

size_t ot_api_render_frame(const char *type, const uint32_t *mask, char *out, size_t cap)
{
    ot_api_sink_t s = { out, cap, 0 };
    // Under the lock as a whole, like every document here: otherwise half the frame would belong
    // to one conversation with the boiler and half to the next.
    ot_lock();
    ot_api_emit(&s, "{\"type\":\"%s\",\"values\":{", type);
    bool first = true;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        if (mask != NULL && (mask[i / 32] & ((uint32_t)1u << (i % 32))) == 0)
            continue;
        const ot_entity_t *e = ot_registry_at(i);
        if (e == NULL)
            continue;
        const ot_value_t v = value_of(e);
        ot_api_emit(&s, "%s\"%s\":", first ? "" : ",", e->key);
        emit_bare_value(&s, e, &v);
        first = false;
    }
    ot_api_emit(&s, "}}");
    ot_unlock();
    ot_api_terminate(&s);
    return s.need;
}
