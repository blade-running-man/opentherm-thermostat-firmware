// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_room.h"

#include <math.h>
#include <string.h>

// deci-°C rounding, shared by both selections.
static int16_t to_dc(float celsius) {
    return (int16_t)lroundf(celsius * 10.0f);
}

void ot_room_init(ot_room_t *r, const ot_room_cfg_t *cfg) {
    // Zero first: slots beyond count are left with a benign default (ot_sensor_t zeroed ==
    // NEVER/NaN, cfg zeroed == role AMBIENT, never steers) rather than stale memory, in case
    // a caller reads count-out-of-range by mistake.
    memset(r, 0, sizeof(*r));

    size_t count = cfg->count;
    if (count > OT_ROOM_MAX_SLOTS) {
        count = OT_ROOM_MAX_SLOTS;   // a config typo must not crash the caller
    }
    r->count = count;

    for (size_t i = 0; i < count; i++) {
        r->cfg[i] = cfg->cfg[i];
        ot_sensor_init(&r->s[i], cfg->cfg[i].stale_after_ms);
    }
}

bool ot_room_submit(ot_room_t *r, size_t slot, float celsius, uint32_t now_ms) {
    if (slot >= r->count) {
        return false;   // not configured -- nothing to submit into
    }
    return ot_sensor_update(&r->s[slot], celsius, now_ms);
}

void ot_room_tick(ot_room_t *r, uint32_t now_ms) {
    for (size_t i = 0; i < r->count; i++) {
        ot_sensor_tick(&r->s[i], now_ms);
    }
}

void ot_room_select_steer(const ot_room_t *r, ot_room_steer_t *out) {
    out->fresh = false;
    out->value_dc = 0;
    out->active_slot = -1;
    out->ha_forwarded_stale = false;

    // One pass over every slot, not a break on the first match: the picked slot and the
    // ha_forwarded_stale flag are independent questions, so a slot after the winning
    // one can still set ha_forwarded_stale.
    for (size_t i = 0; i < r->count; i++) {
        if (r->cfg[i].role != OT_ROOM_ROOM) {
            // Ambient slots are filtered out here, before any pick is made -- the structural
            // safety guard: an ambient-role slot must never steer. DO NOT move this check after the FRESH test or
            // fold it into a fallback: that reopens the "boiler-room reading holds heat off"
            // mistake the whole component exists to prevent.
            continue;
        }

        ot_sensor_state_t state = ot_sensor_state(&r->s[i]);

        if (r->cfg[i].ha_forwarded && state == OT_SENSOR_STALE) {
            out->ha_forwarded_stale = true;
        }

        if (!out->fresh && state == OT_SENSOR_FRESH) {
            out->fresh = true;
            out->value_dc = to_dc(ot_sensor_value(&r->s[i]));
            out->active_slot = (int)i;
        }
    }
}

// Fills *out from the first FRESH slot (index order) whose role matches want_role. Returns
// true iff a slot was picked, so the caller can try a second role without duplicating the
// scan-and-assign logic below.
static bool pick_fresh_by_role(const ot_room_t *r, ot_room_role_t want_role, ot_room_display_t *out) {
    for (size_t i = 0; i < r->count; i++) {
        if (r->cfg[i].role != want_role) {
            continue;
        }
        if (ot_sensor_state(&r->s[i]) == OT_SENSOR_FRESH) {
            out->have = true;
            out->value_dc = to_dc(ot_sensor_value(&r->s[i]));
            out->active_slot = (int)i;
            out->active_role = r->cfg[i].role;
            return true;
        }
    }
    return false;
}

void ot_room_select_display(const ot_room_t *r, ot_room_display_t *out) {
    out->have = false;
    out->value_dc = 0;
    out->active_slot = -1;
    out->active_role = OT_ROOM_AMBIENT;

    // Two-pass, room-role preferred: a fresh
    // room reading is the one worth showing whenever there is one, so it wins display too and
    // not merely steer. Falling back to the first fresh ambient slot when no room slot is
    // fresh keeps the fallback behavior: the shield's ambient reading still
    // shows up in room_temperature_effective when it is the only thing available.
    if (pick_fresh_by_role(r, OT_ROOM_ROOM, out)) {
        return;
    }
    pick_fresh_by_role(r, OT_ROOM_AMBIENT, out);
}
