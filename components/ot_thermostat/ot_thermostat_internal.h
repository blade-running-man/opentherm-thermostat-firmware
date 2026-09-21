// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

// PRIVATE to components/ot_thermostat: the seams between the task (ot_thermostat.c), the memory it
// keeps across a reset (ot_thermostat_persist.c), what it says and shows
// (ot_thermostat_report.c), and the room-source registry it owns (ot_thermostat_room.c). Beside
// the sources, not in include/.
//
// Every function here is called outside the spinlock: they log, take ot_lock (ot_state) or write
// NVS. All but otth_restore() are the TASK's; otth_restore() runs in ot_thermostat_start() on the
// caller's task, before the task exists. So each file's statics are touched by one task at a time.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>   // abs(), for DC_ARG

#include "ot_bus.h"   // ot_bus_write_state_t, which otth_publish() reads ID 1 from
#include "ot_control.h"
#include "ot_thermostat_room_cfg.h"   // ot_thermostat_room_build_cfg() -- its own header, why above

// A tenths value for the log as a sign and integers. NO %f IN THIS COMPONENT: one float through
// ESP_LOG runs newlib's full float formatter twice (ot_log's hook and the console) on this stack.
#define DC_FMT     "%s%d.%d"
#define DC_ARG(dc) ((dc) < 0 ? "-" : ""), abs(dc) / 10, abs(dc) % 10

// --- ot_thermostat_persist.c ---------------------------------------------------------------------

// What ot_control_init() is handed at boot: the reset reason, the RTC_NOINIT overdue count and
// part-hour, and the heat hours from NVS, judged by ot_control_io_restore(). Logs one line. Call
// once, before the task exists.
void otth_restore(ot_control_restore_t *out);

// Mirrors the watchdog's overdue count and the heat-hours part-hour into RTC_NOINIT memory,
// together, under one magic word. Every step; no lock, no log.
void otth_mirror(uint32_t overdue_ms, uint32_t hh_ms);

// Writes heat hours to NVS (namespace "ctl", key "heat_h"). Only when ot_control asks -- at most
// once an hour, and never once the arm is spent. Logs a failure, never a success.
void otth_save_heat_hours(uint16_t heat_hours);

// --- ot_thermostat_report.c: what the task says and shows ---------------------------------------

// Applies the configured zone (ot_time_set_zone()) on the first call whatever it is, the empty
// one included, and after that whenever it changes. One log line each time.
void otth_follow_zone(const char *tz);

// Says ONCE per boot that the boiler answers UNKNOWN-DATAID to ID 1 (ot_state's mark).
void otth_warn_unsupported_id1(void);

// Says ONCE per boot, with the value, that the boiler answered DATA-INVALID to ID 1: `heard` and
// `raw` are ot_control_io_heard_t's id1_invalid and id1_invalid_raw, copied under the spinlock.
void otth_warn_invalid_id1(bool heard, uint16_t raw);

// A step's changes, logged on change only: the ID 1 now on the wire, the ladder state, and the
// status byte against `prev_status` -- the byte the task last handed to ot_bus_set_status().
void otth_report(const ot_control_in_t *in, const ot_control_out_t *out, uint8_t prev_status);

// The synthetic entities (ot_control_io_virtuals()) into ot_state, stamped `now`, the step's time.
void otth_publish(const ot_control_cfg_t *cfg, const ot_control_out_t *out, bool ch_command,
                  const ot_bus_write_state_t *ws, uint32_t now);

// --- ot_thermostat_room.c: the room-source registry (extracted from ot_thermostat.c) --------
// (ot_thermostat_room_build_cfg() itself is declared in ot_thermostat_room_cfg.h, not here.)

// Initialises the pure ot_room registry from the live configuration
// (ot_config_room_mqtt() -> ot_thermostat_room_build_cfg()). Call once, before the task exists
// (mirrors ot_control_init() in ot_thermostat_start()).
void ot_thermostat_room_init(void);

// Re-reads the configuration and re-initialises the registry when it has changed (a fingerprint,
// once a second, mirroring ot_mqtt_link's follow_settings()): enabling/disabling the MQTT slot, or
// changing its role/stale/ha_forwarded, then takes effect without a reboot. A no-op mid-tick
// re-init loses no reading that matters -- a dropped registry re-reads the mailbox's next
// submission on the very next tick, and the alternative (patching one slot in place) would need
// its own diffing logic for a case that changes maybe once an install.
void ot_thermostat_room_refresh_cfg(void);

// Drains the room mailbox (ot_thermostat_room_submit()'s target) into the pure ot_room, steps it,
// fills `in`'s room_fresh/room_dc/ha_forwarded_stale from the steer selection, and publishes
// room_temperature_effective/room_source from the display selection. Called once a tick, after
// `in` has been zeroed and before ot_control_step() reads it.
void ot_thermostat_room_tick(uint32_t now, ot_control_in_t *in);
