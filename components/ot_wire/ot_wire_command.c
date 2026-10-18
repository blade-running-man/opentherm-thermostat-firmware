// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Command-layer bodies: a write to an entity and the launch of an operation.
//
// A SEPARATE FILE, NOT A TAIL OF ot_wire.c. That one was 616 lines against a ceiling
// of 350 when this file left it, and the rest of it was cut the same way
// (ot_wire_config.c, ot_wire_provision.c); the seam runs exactly here -- these two functions have a different responsibility (the command layer, not
// configuration and provisioning) and a different dependency (ot_command). The public
// header is nonetheless one and the same: from the outside the component remained a
// single component.
//
// The rule from the ot_wire.h header holds here too: not one message and not one return
// code carries a value sent by the client. The name of an operation parameter was
// invented by the client -- it does not get into the log that GET /api/log serves over
// the open access point.
#include <string.h>

#include "ot_json.h"
#include "ot_wire.h"

// A linear search over four slots: anything cleverer here would optimize the parsing of
// a request that happens once every few minutes, at the price of code one has to read.
bool ot_wire_param(const ot_wire_params_t *p, const char *name, float *out)
{
    if (p == NULL || name == NULL || out == NULL)
        return false;
    for (uint8_t i = 0; i < p->count; i++)
        if (strcmp(p->name[i], name) == 0) { *out = p->value[i]; return true; }
    return false;
}

ot_wire_status_t ot_wire_parse_entity_write(const char *body, ot_wire_value_t *out)
{
    if (out == NULL)
        return OT_WIRE_BAD_BODY;

    // As a whole, before reading the key. The getters are individually safe on garbage,
    // but only this pass says whether the body was understood as a document -- and only
    // that turns into a 400. `{"value":1}{"value":2}` would otherwise read as the first
    // one, silently.
    if (ot_json_check(body) != OT_JSON_DOC_OK)
        return OT_WIRE_BAD_BODY;

    // A boolean is tried FIRST and separately. `true` and `1.0` are different answers:
    // an entity expecting a flag, on receiving a number, must hear a refusal rather than
    // see a guess.
    bool flag = false;
    if (ot_json_bool(body, "value", &flag) == OT_JSON_FOUND) {
        out->is_bool = true;
        out->boolean = flag;
        out->number  = flag ? 1.0f : 0.0f;
        return OT_WIRE_OK;
    }

    // ot_json_f32, not _i32: a setpoint of 18.5 is what a thermostat exists for, while
    // _i32 rejects a fraction deliberately (port 18.5 is a typo). See ot_json.h.
    float number = 0.0f;
    if (ot_json_f32(body, "value", &number) != OT_JSON_FOUND)
        return OT_WIRE_BAD_FIELD;

    out->is_bool = false;
    out->boolean = false;
    out->number  = number;
    return OT_WIRE_OK;
}

ot_wire_status_t ot_wire_parse_operation(const char *body, ot_wire_params_t *out)
{
    if (out == NULL)
        return OT_WIRE_BAD_BODY;
    if (ot_json_check(body) != OT_JSON_DOC_OK)
        return OT_WIRE_BAD_BODY;

    memset(out, 0, sizeof *out);

    for (size_t i = 0;; i++) {
        // One slot MORE than fits: the enumeration continues past the boundary so that
        // a surplus parameter is noticed and rejected. Stopping at OT_WIRE_PARAMS_MAX
        // would mean the operation is launched with four out of the five values passed
        // to it and nobody finds out.
        char                 name[sizeof out->name[0]];
        const ot_json_read_t got = ot_json_key_at(body, i, name, sizeof name);
        if (got == OT_JSON_MISSING)
            break;
        if (got != OT_JSON_FOUND)
            return OT_WIRE_BAD_FIELD;  // including a name longer than the slot
        if (i >= OT_WIRE_PARAMS_MAX)
            return OT_WIRE_BAD_FIELD;

        float value = 0.0f;
        if (ot_json_f32(body, name, &value) != OT_JSON_FOUND)
            return OT_WIRE_BAD_FIELD;

        memcpy(out->name[i], name, strlen(name) + 1);
        out->value[i] = value;
        out->count    = (uint8_t)(i + 1);
    }

    return OT_WIRE_OK;
}
