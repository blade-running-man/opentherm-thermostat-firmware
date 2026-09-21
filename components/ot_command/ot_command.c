// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_command.h"

#include <math.h>

#include "ot_codec.h"
#include "ot_registry.h"
#include "ot_state.h"

ot_command_err_t ot_command_encode(const char *key, float value,
                                   ot_command_frame_t *out)
{
    const int i = ot_registry_index_of(key);
    if (i < 0 || !out)
        return OT_CMD_UNKNOWN_KEY;

    const ot_entity_t *e = ot_registry_at((uint16_t)i);
    if (!e->writable)
        return OT_CMD_NOT_WRITABLE;

    // A synthetic row has no Data-ID, and the cast below would turn -1 into 255: a
    // writable synthetic row with bounds would be queued as a frame to ID 255. As a FRAME it is
    // not writable; its writes are ot_command_check()'s CONTROL answer.
    // DO NOT move this below the cast.
    if (e->data_id < 0)
        return OT_CMD_NOT_WRITABLE;

    // The WRITE ID, not the read one: for an entity that is read under one identifier
    // and written under another, it may be exactly the second one that is unsupported,
    // and checking the first means queueing a frame the boiler will not accept.
    const uint8_t id = (e->write_id >= 0) ? (uint8_t)e->write_id : (uint8_t)e->data_id;

    if (ot_state_is_unsupported(id))
        return OT_CMD_UNSUPPORTED_BY_BOILER;

    // FIRST of all and BEFORE any comparison: a not-a-number lies in no range, but no
    // comparison with it is true either. `value < lo || value > hi` for NaN yields false
    // twice, that is, "the range check passed", while ot_codec_float_to_f88(NaN) returns
    // 0 (ot_codec.c:11-13 says in so many words that NaN here is a defect further up the
    // stack -- further up the stack is precisely this function). A flow setpoint of 0
    // °C, queued silently.
    //
    // From REST this is unreachable: ot_json_f32 rejects NaN and inf while parsing the
    // body. But ot_command.h declares this function the ONLY place where the legality of
    // a write is decided, and a caller feeding it a COMPUTED float -- a division by zero
    // or an uninitialised sensor from MQTT gives exactly NaN.
    //
    // OT_CMD_OUT_OF_RANGE, not a new code, and that is a decision, not thrift. The caller
    // is to do with it exactly the same as with a value out of bounds: refuse and not
    // queue the frame; ot_http_registry.c answers 422 -- "the body was understood, it is
    // the value that was rejected", which for NaN is literally true. A separate code
    // would oblige every switch over ot_command_err_t to grow a branch with the same
    // answer, and a caller that forgot it would fall into a default that means something
    // else.
    if (isnan(value) || isinf(value))
        return OT_CMD_OUT_OF_RANGE;

    // The bounds live in ot_state: it has already been decided there that the boiler
    // knows more about itself than the table does, and the read IDs 48/49 displace the
    // registry constants. Repeating the decision here means creating a second place
    // where it lives.
    //
    // false here is NOT "there are no bounds, so anything goes". ot_state_bounds()
    // answers false when the table declared min_value/max_value as NaN, and silently
    // carrying on would mean that the appearance of a writable entity without bounds
    // switches off the range check entirely and unnoticeably. Fail-closed: every writable
    // entity WITH A DATA-ID has table bounds (test_ot_command:
    // test_every_writable_entity_declares_bounds holds that), so for them the refusal never
    // fires -- it waits for the day the generator emits one without. The synthetic rows
    // (ch_enable, dhw_enable, heating_season) carry no table bounds on purpose: ot_control
    // bounds them, they never reach this line (the data_id guard above refuses them), and
    // they are written through ot_command_check().
    float lo = 0, hi = 0;
    if (!ot_state_bounds(key, &lo, &hi))
        return OT_CMD_OUT_OF_RANGE;
    if (value < lo || value > hi)
        return OT_CMD_OUT_OF_RANGE;

    uint16_t raw;
    switch (e->codec) {
    case OT_CODEC_F88:
        // Saturates at the bounds instead of overflowing: a calculation that has gone
        // absurd should hit the ceiling, not change sign.
        raw = ot_codec_float_to_f88(value);
        break;
    case OT_CODEC_U16:
        if (value < 0.0f || value > 65535.0f)
            return OT_CMD_OUT_OF_RANGE;
        raw = (uint16_t)(value + 0.5f);
        break;
    case OT_CODEC_S16:
        if (value < -32768.0f || value > 32767.0f)
            return OT_CMD_OUT_OF_RANGE;
        raw = (uint16_t)(int16_t)(value < 0 ? value - 0.5f : value + 0.5f);
        break;
    default:
        return OT_CMD_HALF_WORD_CODEC;
    }

    out->data_id = id;
    out->raw     = raw;
    return OT_CMD_OK;
}

const char *ot_command_strerror(ot_command_err_t e)
{
    switch (e) {
    case OT_CMD_OK:                   return "ok";
    case OT_CMD_UNKNOWN_KEY:          return "no such entity";
    case OT_CMD_NOT_WRITABLE:         return "entity is read-only";
    case OT_CMD_OUT_OF_RANGE:         return "value out of range";
    case OT_CMD_UNSUPPORTED_BY_BOILER:return "boiler does not support this data-id";
    case OT_CMD_HALF_WORD_CODEC:      return "codec covers half a word; cannot write";
    case OT_CMD_OWNED_BY_HA:          return "owned by Home Assistant: control_mode is ha";
    case OT_CMD_OWNED_BY_LOCAL:       return "owned by the thermostat: control_mode is local";
    case OT_CMD_SEASON_ON_IS_LOCAL:   return "only the thermostat may turn the heating season on";
    case OT_CMD_NOT_FOR_HA:           return "this entity is not Home Assistant's to write";
    }
    return "unknown error";
}
