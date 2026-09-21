// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <assert.h>   // static_assert, below ot_thermostat_err_t
#include <stdbool.h>
#include <stddef.h>   // size_t, ot_thermostat_room_submit()'s slot index
#include <stdint.h>

#include "esp_err.h"
#include "ot_api_control.h"   // ot_api_control_t, the document ot_thermostat_control_get() fills
#include "ot_config.h"        // ot_config_err_t, why an accepted command was not saved
#include "ot_control.h"       // the executor's vocabulary: origin, command, configuration, verdict
#include "ot_frame.h"         // ot_msg_type_t, the reply ot_thermostat_heard() records

// The task layer of the executor: the ONE writer of the ID 0 high byte and of
// the held ID 1.
//
// ot_control decides and ot_control_io translates, both pure and host-tested. This component
// carries the decision out and is NOT host-tested -- it is FreeRTOS, NVS and the bus -- so it is
// kept thin. Once a second its task:
//   * snapshots the configuration (ot_net) into ot_control's snapshot (ot_control_io_cfg);
//   * reads the bus's report of what went out (ot_bus_write_state) and the last READ-ACK of ID 56
//     (ot_thermostat_heard);
//   * steps the executor under a spinlock, and outside it: hands the status byte to the bus,
//     queues the held ID 1 and ID 56 when the bus has no write pending (an ID 56 it refused is
//     owed to the next idle slot), mirrors the watchdog into RTC_NOINIT memory, persists heat
//     hours when asked and publishes the synthetic entities;
//   * follows the configured time zone (ot_time_set_zone): applied on the first tick whatever it
//     is, and again whenever it changes, so a saved zone applies at once.
//
// LOCKING: one portMUX spinlock guards the executor and the last step's snapshot and output.
// ESP_LOG, ot_net_config_apply() (a mutex and NVS), ot_state_* (ot_lock) and NVS are never
// called inside it -- taking a mutex with interrupts off is not
// allowed, and a log line there is one half of a lock-order inversion (ot_bus.c records the rule).
// A FreeRTOS mutex, taken outside the spinlock and never by the task, serialises
// ot_thermostat_control_apply() with its persist (see there).
//
// FAILURE: ot_thermostat_start() can fail only by being unable to create its task, and the caller
// must not treat that as fatal: NOTHING REBOOTS BECAUSE A PEER OR A TASK IS ABSENT, and the
// boiler keeps being polled either way. Every other call answers with an ot_thermostat_err_t and
// changes nothing on refusal.

#ifdef __cplusplus
extern "C" {
#endif

// Starts the task. Idempotent: a second call does nothing and returns ESP_OK.
//
// Call it AFTER ot_bus_start() and ot_net_start() and BEFORE ot_http_start(): the first owns the
// byte's destination, the second the configuration document, and the third calls in here. It
// restores what survives a reset (ot_control_io_restore) and initialises the executor BEFORE
// creating the task, so a request never meets an uninitialised one.
esp_err_t ot_thermostat_start(void);

// One reply the bus heard -- any Data-ID, any type -- recorded by ot_control_io_heard(), which
// decides what it means: the ID 56 readback (only a READ-ACK of ID 56; every step
// reads the last one), and the first DATA-INVALID answer to ID 1, which the task logs once -- it
// still counts as sent -- "sent" meaning "has gone out on the bus". Called by main.cpp's response
// callback beside ot_state_apply_dataid(), so it runs on the BUS TASK, on the conversation's path
// (ot_bus.h, DO NOT BLOCK): the thermostat's spinlock for that one pure call and nothing else --
// no log, no mutex, no NVS. DO NOT add work here. Safe before ot_thermostat_start(): a reply is a
// fact about the boiler, and the first step reads it.
void ot_thermostat_heard(uint8_t data_id, ot_msg_type_t type, uint16_t raw);

// A room-temperature reading from ANY task (today: ds18b20_task in main.cpp; later: MQTT/BLE
// adapters) for the given ot_room slot (a locked mailbox in front of
// the pure ot_room registry). Only stashes the latest value under the spinlock -- it does NOT
// touch ot_room itself, which is single-owner (ot_room.h) and is drained into ot_room_submit()
// once per tick, on the thermostat task alone. `slot` out of range is silently ignored: a bad
// caller must not be able to corrupt or crash the mailbox. Safe before ot_thermostat_start(): a
// reading is a fact about the sensor, and the first tick after start drains whatever arrived.
void ot_thermostat_room_submit(size_t slot, float celsius);

// The task layer's verdict: ot_control's own, EQUAL IN VALUE by the definitions below, plus the
// two only this layer can give. A type of its own because "is the task running" and "did the store
// take it" are not questions a pure component can answer.
typedef enum {
    OT_THERMOSTAT_OK                 = OT_CONTROL_OK,
    OT_THERMOSTAT_OWNED_BY_HA        = OT_CONTROL_OWNED_BY_HA,
    OT_THERMOSTAT_OWNED_BY_LOCAL     = OT_CONTROL_OWNED_BY_LOCAL,
    OT_THERMOSTAT_SEASON_ON_IS_LOCAL = OT_CONTROL_SEASON_ON_IS_LOCAL,
    OT_THERMOSTAT_OUT_OF_RANGE       = OT_CONTROL_OUT_OF_RANGE,
    OT_THERMOSTAT_SEASON_IS_OFF      = OT_CONTROL_SEASON_IS_OFF,
    OT_THERMOSTAT_BAD_MINUTES        = OT_CONTROL_BAD_MINUTES,
    // ot_thermostat_start() failed at boot: nothing would step, send or EXPIRE anything. 0x40 and
    // not the next number: this layer's codes sit clear of ot_control's, so a verdict ot_control
    // appends does not renumber them -- the assertion below keeps the two ranges apart.
    OT_THERMOSTAT_NO_TASK            = 0x40,
    // ot_control accepted the command and the store REFUSED what it asked to persist -- the reason
    // is *store_err -- so the command has no effect: the next step reads the store. Refused, and
    // only that: a patch the store accepted whose flash write then failed answers OK (ot_net logs
    // the save error), and the change holds until the next reboot.
    OT_THERMOSTAT_NOT_SAVED,
} ot_thermostat_err_t;

// ot_thermostat.c hands an ot_control verdict on by value, (ot_thermostat_err_t)e: one that
// reached 0x40 would read as NO_TASK, a 503 for a refusal the client could fix. BAD_MINUTES is
// ot_control_err_t's last member; a verdict appended after it replaces it here. static_assert
// through <assert.h> and not _Static_assert, as ot_provision.h does: main.cpp includes this in C++.
static_assert((int)OT_CONTROL_BAD_MINUTES < (int)OT_THERMOSTAT_NO_TASK,
              "ot_control's verdicts must stay below this layer's own codes");

// The configuration snapshot the last step used, for ot_command_check()'s early answer: the SAME
// builder and the same snapshot ot_control_apply() is handed below (ot_control.h asks both calls
// to read one store). Up to one tick old, and that is the executor's own rule now: apply() and
// boost_start() judge ownership by the mode (and season) the last step OBSERVED and take only the
// value bounds from a snapshot, so a web write is refused, or HA's first command after a flip is,
// for up to one step -- correct by design (ot_control.h). Task-safe. Before ot_thermostat_start()
// it is a zeroed snapshot: LOCAL, season off, flow bounds 0 -- every setpoint is refused, which is
// the truth about a device with no executor.
void ot_thermostat_control_cfg(ot_control_cfg_t *out);

// A command for the executor from `origin` -- the final answer on ownership. ot_control_apply()
// re-checks ownership against the mode the executor last observed, inside the spinlock, and lands
// the command in the same critical section -- a CH setpoint quantised to 0.5 °C before it is
// persisted; what it asks to persist is then written through
// ot_net_config_apply(), outside every lock, on the CALLER's task. Task-safe; meant for the httpd
// task and ot_mqtt_link's publisher task -- never the esp-mqtt task itself. Two calls never interleave: a mutex holds apply and its
// persist together, so a caller may wait for one NVS write of another's (ot_thermostat.c says
// why, and what the mutex does not cover).
//
// OT_THERMOSTAT_NOT_SAVED sets *store_err (may be NULL): OT_CONFIG_ERR_READ_ONLY for a store
// written by a newer firmware, anything else for a store that contradicts the executor -- which
// quantises and bounds before it asks, so that is a defect, answered 500 and logged. Every other
// result leaves it OT_CONFIG_OK.
ot_thermostat_err_t ot_thermostat_control_apply(ot_origin_t origin, ot_control_cmd_t cmd,
                                                int16_t value, ot_config_err_t *store_err);

// Starts a boost -- ladder row 2, LOCAL only -- or replaces the running one. Every check is
// ot_control_boost_start()'s: OWNED_BY_HA, SEASON_IS_OFF, BAD_MINUTES (0 or more than
// OT_CONTROL_BOOST_MAX_MINUTES), OUT_OF_RANGE (outside the flow bounds); plus NO_TASK. The held
// ID 1 takes the boost's setpoint on the next step, and the CH bit rises once the bus has carried
// it. A boost lives in RAM only: a reboot ends it, deliberately -- a boost is something a
// person asked for NOW, and resurrecting one after an OTA is a boiler heating nobody remembers.
ot_thermostat_err_t ot_thermostat_boost_start(int16_t setpoint_dc, uint32_t minutes);

// Ends the boost. Task-safe, and harmless when none runs. Before ot_thermostat_start() has
// initialised the executor it returns at once: there is no boost to end.
void ot_thermostat_boost_cancel(void);

// GET /api/control's document: the last step's snapshot and output, the boost as it is now, and
// the task's stack high-water mark. One consistent copy, task-safe. Before ot_thermostat_start()
// has initialised the executor it is a zeroed document, stack_known false -- as
// ot_thermostat_control_cfg() is a zeroed snapshot, the truth about a device with no executor.
typedef ot_api_control_t ot_thermostat_control_info_t;
void ot_thermostat_control_get(ot_thermostat_control_info_t *out);

#ifdef __cplusplus
}
#endif
