// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_sensor.h"

// The registry of room-temperature sources: N fixed slots, each
// wrapping an ot_sensor_t plus the metadata that decides whether it may be trusted to steer
// the failsafe. Composes ot_sensor rather than re-implementing its filter/freshness state
// machine -- see ot_sensor.h for the outlier rules and the FRESH/STALE/NEVER semantics this
// component relies on.
//
// OWNERSHIP: single-owner, like ot_sensor. In the firmware this is the ot_thermostat task;
// readings that arrive from another task (the DS18B20 reader, later MQTT) are NOT submitted
// here directly -- they land in a locked mailbox in ot_thermostat.c and are drained into
// ot_room_submit() on the owner's own tick. Calling ot_room_* from a
// second task concurrently is the caller's problem to serialise, exactly as for ot_sensor.
//
// FAILURE: nothing here can fail. ot_room_submit() returning false means the reading was
// rejected (a bad slot index, or ot_sensor's own filter) -- not an error to propagate, just
// nothing to do.
//
// Pure: no ESP-IDF header, no timers, no tasks, no allocation. Time enters only as an
// argument, so the two selections below are host-tested exactly like ot_sensor.
//
// DO NOT add a REQUIRES beyond ot_sensor here, and DO NOT reach for esp_timer or any lock:
// that is what turned ot_sensor and ot_control into pure, host-testable components, and the
// same rake (ot_bus_sched, ot_sensor's own header) applies here.

#ifdef __cplusplus
extern "C" {
#endif

// Fixed slot count: 4 covers the shield plus three MQTT/BLE sources. A larger count would
// commit to an NVS record shape prematurely.
#define OT_ROOM_MAX_SLOTS 4

// AMBIENT is the default (value 0): a struct zero-initialised before ot_room_init() fills it
// in reads as "never steers", which is the safe default for an uncommitted slot.
typedef enum {
    OT_ROOM_AMBIENT = 0,   // near the boiler, or otherwise not trusted to represent the room
    OT_ROOM_ROOM    = 1,   // a genuine room reading -- the only role ot_room_select_steer honours
} ot_room_role_t;

typedef struct {
    ot_room_role_t role;
    bool           ha_forwarded;    // Home Assistant's own forwarded reading -- the temperature HA's thermostat steers by; feeds the ha_blind check (a marked source going stale means HA is commanding while its own room sensor is dead)
    uint32_t       stale_after_ms;  // per slot, passed straight to ot_sensor_init()
} ot_room_slot_cfg_t;

typedef struct {
    ot_room_slot_cfg_t cfg[OT_ROOM_MAX_SLOTS];
    size_t             count;       // clamped to OT_ROOM_MAX_SLOTS by ot_room_init()
} ot_room_cfg_t;

typedef struct {
    ot_sensor_t         s[OT_ROOM_MAX_SLOTS];
    ot_room_slot_cfg_t  cfg[OT_ROOM_MAX_SLOTS];
    size_t              count;
} ot_room_t;

// Zeroes every slot's ot_sensor_t and copies cfg[0..count) verbatim; count above
// OT_ROOM_MAX_SLOTS is clamped rather than rejected, since a config typo must not be able to
// crash the caller -- the extra slots are simply not configured.
void ot_room_init(ot_room_t *r, const ot_room_cfg_t *cfg);

// Offers a measurement to one slot. Returns ot_sensor_update()'s verdict: false when
// slot >= r->count (nothing to submit into) or when ot_sensor's own filter rejected the
// value -- in neither case does anything change.
bool ot_room_submit(ot_room_t *r, size_t slot, float celsius, uint32_t now_ms);

// Advances every configured slot's ot_sensor_t by one tick (see ot_sensor_tick() for why
// this is an accumulator and not now_ms - last_ok). Called once per ot_thermostat tick,
// after the mailbox has been drained into ot_room_submit().
void ot_room_tick(ot_room_t *r, uint32_t now_ms);

// STEERING selection -- the safety-critical one (ambient never steers).
// Scans slots in index order (priority = slot index) and picks the first one that is BOTH
// role == OT_ROOM_ROOM AND FRESH. An ambient slot is excluded from the scan entirely, not
// merely out-prioritised: a boiler-side sensor can never end up here however the other
// slots are configured. ha_forwarded_stale is computed independently of the pick -- it is
// true iff ANY room-role, ha_forwarded slot is STALE, which lets a future caller detect
// "Home Assistant's forwarded reading went stale" (feeds the ha_blind failsafe input -- HA
// still commanding while its own room sensor is dead) even when
// fresh == false for an unrelated reason.
//
// DO NOT weaken the role check to "prefer room, fall back to ambient": that fallback is
// exactly the dangerous mistake to avoid -- a warm boiler-room reading holding heat off
// in a cold house.
typedef struct {
    bool    fresh;               // a room-role slot is FRESH; only then is value_dc meaningful
    int16_t value_dc;            // deci-°C, round(celsius * 10)
    int     active_slot;         // -1 if none
    bool    ha_forwarded_stale;  // a room-role, ha_forwarded slot is STALE (independent of fresh/active_slot)
} ot_room_steer_t;
void ot_room_select_steer(const ot_room_t *r, ot_room_steer_t *out);

// DISPLAY selection -- two-pass, room-role preferred: first the lowest-index FRESH
// role==OT_ROOM_ROOM slot; only when none is
// fresh, the lowest-index FRESH role==OT_ROOM_AMBIENT slot. This is a display-only
// preference, not a safety guard -- ot_room_select_steer below still excludes ambient
// entirely rather than merely de-prioritising it. The ambient fallback keeps the shield's
// reading visible in room_temperature_effective for a config with no room-role slot
// configured. active_role tells the panel
// which kind of reading it is looking at, so nobody mistakes a boiler-room reading for the
// room.
typedef struct {
    bool           have;
    int16_t        value_dc;     // deci-°C, round(celsius * 10)
    int            active_slot;  // -1 if none
    ot_room_role_t active_role;
} ot_room_display_t;
void ot_room_select_display(const ot_room_t *r, ot_room_display_t *out);

#ifdef __cplusplus
}
#endif
