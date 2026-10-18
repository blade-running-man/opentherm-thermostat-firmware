// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_thermostat.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ot_config.h"   // ot_config_room_mqtt_t, ot_config_room_mqtt()
#include "ot_control.h"
#include "ot_room.h"
#include "ot_state.h"   // ot_state_set_virtual(): room_temperature_effective, room_source
#include "ot_thermostat_internal.h"
#include "ot_thermostat_room_cfg.h"   // the pure config->ot_room mapping, host-tested on its own

// The room-source registry (extracted from ot_thermostat.c for the
// file ceiling -- a pure move, no behavior change). Owned by the ot_thermostat task alone
// (ot_room.h, OWNERSHIP): a reading from a FOREIGN task (ds18b20_task today, MQTT) is
// stashed in the mailbox below and drained into the pure ot_room only on this task's own tick.

// A spinlock of its own, NOT ot_thermostat.c's executor s_mux: an unrelated room submission must
// not block on, or be blocked by, the executor's critical section.
static portMUX_TYPE s_room_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool  have;
    float celsius;
} s_room_inbox[OT_ROOM_MAX_SLOTS];
// Owned and stepped by this file alone (ot_room.h, OWNERSHIP). Slot 0 is the DS18B20
// shield, AMBIENT (never steers) -- plus the MQTT room slot when enabled.
static ot_room_t s_room;

// The fingerprint of the last configuration the registry was built from, mirroring
// ot_mqtt_link.c's fingerprint()/follow_settings() (same cadence -- once a tick, here once a
// second since TICK_MS is 1000). FNV-1a over the four room_mqtt_* fields, field by field: a
// struct's padding is not ours to hash (ot_mqtt_link.c says why).
static uint32_t s_fp;
static bool     s_fp_known;

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t fingerprint(const ot_config_room_mqtt_t *m)
{
    uint32_t h = 2166136261u;
    h = fnv(h, &m->enable, sizeof m->enable);
    h = fnv(h, &m->role, sizeof m->role);
    h = fnv(h, &m->stale_s, sizeof m->stale_s);
    return fnv(h, &m->ha_forwarded, sizeof m->ha_forwarded);
}

// Reads the live configuration, maps it through the pure ot_thermostat_room_build_cfg() and
// (re)initialises the registry. DO NOT call ot_room_submit/ot_room_tick here -- ot_room_init()
// zeroes every slot's ot_sensor_t, so a re-init loses any reading in flight; that is an accepted
// cost (ot_thermostat_internal.h, ot_thermostat_room_refresh_cfg()'s comment says why) and NOT one
// that may be worked around by skipping the zero for an unchanged slot -- ot_room_init() has no
// such partial mode, and inventing one here would duplicate what it already promises not to do.
static void load_cfg(void)
{
    ot_config_room_mqtt_t m;
    ot_config_room_mqtt(&m);
    ot_room_cfg_t cfg;
    ot_thermostat_room_build_cfg(&m, &cfg);
    ot_room_init(&s_room, &cfg);
    s_fp       = fingerprint(&m);
    s_fp_known = true;
}

void ot_thermostat_room_init(void)
{
    load_cfg();
}

void ot_thermostat_room_refresh_cfg(void)
{
    ot_config_room_mqtt_t m;
    ot_config_room_mqtt(&m);
    const uint32_t fp = fingerprint(&m);
    if (!s_fp_known || fp != s_fp)
        load_cfg();   // re-reads the config a second time; simplicity over one extra copy a tick
}

// Called on a FOREIGN task (ds18b20_task today). DO NOT call ot_room_* from here -- ot_room is
// single-owner (ot_room.h) and this task is not the owner; ot_thermostat_room_tick() below is the
// only place that drains s_room_inbox into it. Silently drops an out-of-range slot: a bad caller
// must not corrupt the mailbox or crash the device over a room reading.
void ot_thermostat_room_submit(size_t slot, float celsius)
{
    if (slot >= OT_ROOM_MAX_SLOTS)
        return;
    taskENTER_CRITICAL(&s_room_mux);
    s_room_inbox[slot].have    = true;
    s_room_inbox[slot].celsius = celsius;
    taskEXIT_CRITICAL(&s_room_mux);
}

void ot_thermostat_room_tick(uint32_t now, ot_control_in_t *in)
{
    // Drain the mailbox (spinlock, copy out, clear `have`) into the pure ot_room, then step it and
    // pick a steer value. With only the ambient DS18B20 slot configured, the role check in
    // ot_room_select_steer (ot_room.h, "ambient never steers") keeps steer.fresh false, so
    // in->room_fresh below is unchanged: the failsafe still heats blind.
    struct {
        bool  have;
        float celsius;
    } drained[OT_ROOM_MAX_SLOTS];
    taskENTER_CRITICAL(&s_room_mux);
    memcpy(drained, s_room_inbox, sizeof drained);
    for (size_t i = 0; i < OT_ROOM_MAX_SLOTS; i++)
        s_room_inbox[i].have = false;
    taskEXIT_CRITICAL(&s_room_mux);
    for (size_t i = 0; i < OT_ROOM_MAX_SLOTS; i++)
        if (drained[i].have)
            ot_room_submit(&s_room, i, drained[i].celsius, now);
    ot_room_tick(&s_room, now);

    ot_room_steer_t steer;
    ot_room_select_steer(&s_room, &steer);
    in->room_fresh         = steer.fresh;
    in->room_dc            = steer.value_dc;
    in->ha_forwarded_stale = steer.ha_forwarded_stale;

    // DISPLAY prefers a fresh room-role slot over ambient (ot_room.h); with only the ambient
    // shield configured (MQTT slot disabled) this keeps room_temperature_effective exactly as it
    // was before ot_room existed. Published only when something is fresh -- untouched otherwise,
    // so the entity's own age_ms is what drives it stale (matches the DS18B20 preview's
    // behaviour, main.cpp before ot_room existed).
    ot_room_display_t disp;
    ot_room_select_display(&s_room, &disp);
    if (disp.have) {
        ot_state_set_virtual("room_temperature_effective", disp.value_dc / 10.0f, now);
        // room_source is an ENUM published by option INDEX, like control_state/control_mode
        // (ot_state.h: an enum's value IS its option index, there is no free-text virtual).
        // tools/opentherm_ids.py: none=0, shield=1, mqtt=2. Slot 0 is always the shield, slot 1
        // (when configured) is always the MQTT source (ot_thermostat_room_build_cfg()) -- so the
        // slot index alone decides the map, with no need to look at active_role.
        ot_state_set_virtual("room_source", disp.active_slot == 1 ? 2.0f : 1.0f, now);
    } else {
        ot_state_set_virtual("room_source", 0.0f, now);   // "none" -- nothing fresh to show
    }
}
