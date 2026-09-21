// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_state.h"

#include <math.h>
#include <string.h>

#include "ot_codec.h"
#include "ot_lock.h"

// Two unknown-dataid in a row, not one: parity does not catch every corruption, and a
// single spoiled frame must not permanently kill a working entity. There is no counter
// in the other direction -- the reset is a reboot.
#define OT_UNSUPPORTED_STREAK 2

#define OT_DATA_IDS 128

static ot_value_t s_values[OT_ENTITY_COUNT];
static uint32_t   s_dirty[OT_CONSUMER_COUNT][(OT_ENTITY_COUNT + 31) / 32];
static uint8_t    s_unknown_streak[OT_DATA_IDS];
static bool       s_unsupported[OT_DATA_IDS];
// Bounds read, keyed by the source Data-ID. Kept raw: the reader picks the bounds
// codec, because s8_hb/s8_lb is a property of ID 48 and 49, not of the entity.
static uint16_t   s_bounds_raw[OT_DATA_IDS];
static bool       s_bounds_seen[OT_DATA_IDS];

static void mark_dirty(int index)
{
    for (int c = 0; c < OT_CONSUMER_COUNT; c++)
        s_dirty[c][index / 32] |= (uint32_t)1u << (index % 32);
}

void ot_state_reset(void)
{
    ot_lock();
    memset(s_values, 0, sizeof s_values);
    for (uint16_t i = 0; i < OT_ENTITY_COUNT; i++)
        s_values[i].number = NAN;
    memset(s_dirty, 0, sizeof s_dirty);
    memset(s_unknown_streak, 0, sizeof s_unknown_streak);
    memset(s_unsupported, 0, sizeof s_unsupported);
    memset(s_bounds_raw, 0, sizeof s_bounds_raw);
    memset(s_bounds_seen, 0, sizeof s_bounds_seen);
    ot_unlock();
}

static float decode(const ot_entity_t *e, uint16_t raw)
{
    switch (e->codec) {
    case OT_CODEC_F88:   return ot_codec_f88_to_float(raw);
    case OT_CODEC_U16:   return (float)raw;
    case OT_CODEC_S16:   return (float)ot_codec_s16(raw);
    case OT_CODEC_U8_HB: return (float)ot_codec_u8_hb(raw);
    case OT_CODEC_U8_LB: return (float)ot_codec_u8_lb(raw);
    case OT_CODEC_S8_HB: return (float)ot_codec_s8_hb(raw);
    case OT_CODEC_S8_LB: return (float)ot_codec_s8_lb(raw);
    case OT_CODEC_FLAG:  return NAN;
    case OT_CODEC_NONE:  return NAN;   // synthetic: the iterators never hand one here
    }
    return NAN;
}

// The one place a value is stored, for both writers. The mark is set only on a CHANGE:
// otherwise every conversation would trigger publication of the whole registry once a
// minute, and the dirty bits would mean nothing. Caller holds ot_lock().
static void store(int i, ot_availability_t avail, float number, bool boolean,
                  uint32_t now_ms)
{
    ot_value_t *v = &s_values[i];
    const bool number_changed =
        (isnan(v->number) != isnan(number)) ||
        (!isnan(v->number) && !isnan(number) && v->number != number);
    const bool changed = v->availability != avail || number_changed || v->boolean != boolean;

    v->availability = avail;
    v->number = number;
    v->boolean = boolean;
    v->updated_ms = now_ms;
    if (changed)
        mark_dirty(i);
}

void ot_state_apply_dataid(uint8_t data_id, ot_msg_type_t type, uint16_t raw,
                           uint32_t now_ms)
{
    if (data_id >= OT_DATA_IDS)
        return;

    ot_lock();

    if (type == OT_MSG_UNKNOWN_DATAID) {
        if (s_unknown_streak[data_id] < OT_UNSUPPORTED_STREAK)
            s_unknown_streak[data_id]++;
        if (s_unknown_streak[data_id] >= OT_UNSUPPORTED_STREAK)
            s_unsupported[data_id] = true;
    } else {
        s_unknown_streak[data_id] = 0;
    }

    ot_availability_t avail;
    switch (type) {
    case OT_MSG_READ_ACK:
    case OT_MSG_WRITE_ACK:
        avail = OT_AVAIL_OK;
        break;
    case OT_MSG_DATA_INVALID:
        avail = OT_AVAIL_INVALID;
        break;
    case OT_MSG_UNKNOWN_DATAID:
        avail = s_unsupported[data_id] ? OT_AVAIL_UNSUPPORTED : OT_AVAIL_UNKNOWN;
        break;
    default:
        // The slave answered with a type the protocol does not provide for here.
        // Fixing it is not our business: we do not accept the value, but we do not
        // crash either.
        ot_unlock();
        return;
    }

    if (avail == OT_AVAIL_OK) {
        s_bounds_raw[data_id] = raw;
        s_bounds_seen[data_id] = true;
    }

    for (int i = ot_registry_first_for_id(data_id); i >= 0;
         i = ot_registry_next_for_id(data_id, i)) {
        const ot_entity_t *e = ot_registry_at((uint16_t)i);
        float number = NAN;
        bool  boolean = false;
        if (avail == OT_AVAIL_OK) {
            if (e->codec == OT_CODEC_FLAG)
                boolean = ot_codec_flag(raw, e->flag_high_byte, e->flag_bit);
            else
                number = decode(e, raw);
        }
        store(i, avail, number, boolean, now_ms);
    }

    ot_unlock();
}

bool ot_state_set_virtual(const char *key, float value, uint32_t now_ms)
{
    const int i = ot_registry_index_of(key);
    if (i < 0)
        return false;
    const ot_entity_t *e = ot_registry_at((uint16_t)i);
    // The value of a boiler entity may arrive only from the wire. A
    // test, not a comment: test_set_virtual_refuses_a_boiler_entity, ID 0 included.
    if (e->data_id >= 0)
        return false;
    // Refused, not stored as "no value": ot_api prints any plain number with %.2f, on
    // /api/state and /ws alike, and NaN prints as "nan" -- one such value breaks every document.
    if (!isfinite(value))
        return false;

    float number = value;
    bool  boolean = false;
    if (ot_registry_is_boolean(e)) {
        boolean = value != 0.0f;
        // 0/1, NOT NaN as for a flag: a renderer that has not learned ot_registry_is_boolean()
        // yet falls to the number and must still print JSON.
        number = boolean ? 1.0f : 0.0f;
    } else if (e->kind == OT_KIND_ENUM) {
        if (value < 0.0f || value >= (float)ot_registry_option_count(e) ||
            value != floorf(value))
            return false;
    }

    ot_lock();
    store(i, OT_AVAIL_OK, number, boolean, now_ms);
    ot_unlock();
    return true;
}

bool ot_state_get(const char *key, ot_value_t *out)
{
    const int i = ot_registry_index_of(key);
    if (i < 0 || !out)
        return false;
    ot_lock();
    *out = s_values[i];
    ot_unlock();
    return true;
}

bool ot_state_bounds(const char *key, float *min_out, float *max_out)
{
    const int i = ot_registry_index_of(key);
    if (i < 0 || !min_out || !max_out)
        return false;
    const ot_entity_t *e = ot_registry_at((uint16_t)i);
    if (isnan(e->min_value) || isnan(e->max_value))
        return false;

    ot_lock();
    float lo = e->min_value;
    float hi = e->max_value;
    // The boiler knows more about itself than the table does: once the bounds have been
    // read, they displace the constants. For ID 48 and 49 the upper bound is in
    // the high byte, the lower bound in the low byte.
    if (e->bounds_from >= 0 && s_bounds_seen[e->bounds_from]) {
        const uint16_t raw = s_bounds_raw[e->bounds_from];
        hi = (float)ot_codec_s8_hb(raw);
        lo = (float)ot_codec_s8_lb(raw);
    }
    ot_unlock();

    *min_out = lo;
    *max_out = hi;
    return true;
}

bool ot_state_is_unsupported(uint8_t data_id)
{
    return data_id < OT_DATA_IDS && s_unsupported[data_id];
}

bool ot_state_take_dirty(ot_consumer_t consumer, int index)
{
    if (consumer >= OT_CONSUMER_COUNT || index < 0 || index >= OT_ENTITY_COUNT)
        return false;
    ot_lock();
    const uint32_t mask = (uint32_t)1u << (index % 32);
    const bool was = (s_dirty[consumer][index / 32] & mask) != 0;
    s_dirty[consumer][index / 32] &= ~mask;
    ot_unlock();
    return was;
}
