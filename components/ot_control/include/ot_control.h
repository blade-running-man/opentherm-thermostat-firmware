// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// THE EXECUTOR CORE. Home Assistant is the
// controller; this is the firmware's half: who owns each command, what the boiler is told, and
// what happens when the controller dies. One pure decision, host-tested:
//
//   * the ladder -- six states, first match wins, the enum order IS the priority;
//   * the entries into HA ownership -- each one forgets what HA said before it;
//   * the watchdog and the bounded failsafe;
//   * the held ID 1 -- always valid, and the CH bit never rises before the bus has
//     carried it -- and the re-send cadence;
//   * the DHW bit and the ID 56 reconciliation;
//   * ownership and bounds of every command, and the boost as ladder row 2.
//
// PURE. No FreeRTOS, no ESP-IDF, no allocation, no globals; time enters only as a monotonic
// millisecond argument. DO NOT include esp_timer.h or nvs.h here or in any source of this
// component: PlatformIO compiles every source of a library whose header a suite includes, and
// ot_command's suite includes this one. Its only include beyond the C library is ot_bus_sched.h
// for the OT_STATUS_* names, and that is pure too.
//
// TEMPERATURES are int16_t tenths of a degree Celsius everywhere (suffix _dc), because ot_config
// has no floating-point type and a float that crosses the config store twice is a float
// that stops comparing equal.
//
// OWNERSHIP AND CONTEXT: the caller owns the ot_control_t and passes it to every call. There is
// no instance and no lock here. ot_thermostat holds the one instance by value and calls step()
// once a second from its task; the httpd and esp-mqtt tasks call apply() and the boost functions.
// The CALLER serialises every call on one ot_control_t under one spinlock, and apply() does its
// re-check and its watchdog stamp inside that one critical section.
//
// SNAPSHOTS MAY BE STALE. The configuration snapshot is built OUTSIDE that spinlock (building it
// takes ot_config's mutex), so the one handed to apply() or boost_start() can predate a mode flip
// step() has already seen. Therefore OWNERSHIP -- which mode, which season -- is decided against
// what the executor itself last observed in step(), and the snapshot supplies only value bounds;
// and the edges are observed by step() ALONE. A mode flip reaches HA's commands at most
// one step (<= 1 s) after it reaches the store.
//
// FAILURE: nothing returns esp_err_t. check(), apply() and boost_start() refuse with an
// ot_control_err_t and change nothing on refusal; step() cannot fail -- every input, a garbage
// configuration included, gives a defined output with one state.
//
// DURATIONS are accumulators advanced by step(), never `now - then` (uint32 milliseconds wrap
// after 49.7 days). The only moment stored is the boost's deadline, whose
// eight-hour span sits far inside the +-24.8 days a signed difference covers.

#include "ot_bus_sched.h"   // OT_STATUS_CH_ENABLE, OT_STATUS_DHW_ENABLE -- names only, pure

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { OT_CONTROL_MODE_LOCAL = 0, OT_CONTROL_MODE_HA = 1 } ot_control_mode_t;

// Who is writing. ot_command_check() and ot_control_apply() both take it.
typedef enum { OT_ORIGIN_WEB = 0, OT_ORIGIN_HA = 1 } ot_origin_t;

// The ladder, in its order. The numeric order IS the priority: DO NOT reorder.
// The generated control_state enum entity lists its options in this order, and Home Assistant
// stores the string -- a renumbering rewrites the owner's recorder history.
typedef enum {
    OT_CONTROL_SEASON_OFF = 0,
    OT_CONTROL_BOOST,
    OT_CONTROL_LOCAL,
    OT_CONTROL_HA_WAITING,
    OT_CONTROL_FAILSAFE,
    OT_CONTROL_HA,
    OT_CONTROL_STATE_COUNT
} ot_control_state_t;

// Why the state decides what it decides. Published as control_state's attributes:
// `reason` carries what the CH bit is doing, `cause` why the state is failsafe (see out).
typedef enum {
    OT_CONTROL_REASON_NONE = 0,
    OT_CONTROL_REASON_WATCHDOG,       // cause: no accepted HA CH command for watchdog_s
    OT_CONTROL_REASON_HA_BLIND,       // cause: the ha_forwarded room source is stale
    OT_CONTROL_REASON_FS_DISARMED,    // failsafe, CH held down: no HA heat request within failsafe_heat_days
    OT_CONTROL_REASON_FS_BLIND,       // failsafe, CH up: no fresh room source, heating blind
    OT_CONTROL_REASON_FS_ROOM_COLD,   // failsafe hysteresis, CH up: the room fell to target - 0.3 K
    OT_CONTROL_REASON_FS_ROOM_WARM,   // failsafe hysteresis, CH down: the room rose to target + 0.3 K
    OT_CONTROL_REASON_MIN_CYCLE,      // failsafe hysteresis: the bit held by failsafe_min_cycle_s
    OT_CONTROL_REASON_AWAIT_SETPOINT, // CH wanted, held ID 1 not yet confirmed on the bus
    OT_CONTROL_REASON_COUNT
} ot_control_reason_t;

// The wire spellings. Out of range gives "unknown", never NULL: renderers print it into JSON.
const char *ot_control_state_name(ot_control_state_t s);   // "season_off", "boost", "local", "ha_waiting", "failsafe", "ha"
const char *ot_control_reason_name(ot_control_reason_t r); // "none", "watchdog", "ha_blind", ...

// A snapshot of the configuration, built by the task layer from ot_config. step() takes its edges
// from it; apply() and boost_start() take only bounds from it (see the top of this file).
typedef struct {
    ot_control_mode_t mode;          // anything but OT_CONTROL_MODE_HA is LOCAL
    bool     heating_season;
    uint16_t watchdog_s;
    int16_t  failsafe_setpoint_dc;
    int16_t  failsafe_room_target_dc;
    uint8_t  failsafe_heat_days;
    uint16_t failsafe_min_cycle_s;
    int16_t  flow_min_dc;            // multiples of 5, or "quantised" and "in bounds" can conflict:
    int16_t  flow_max_dc;            // the bounds win (see held_setpoint_dc)
    bool     local_ch_enable;
    int16_t  local_ch_setpoint_dc;
    bool     dhw_enable;
    bool     dhw_setpoint_set;       // false until someone has written dhw_setpoint
    int16_t  dhw_setpoint_dc;
} ot_control_cfg_t;

// Inputs that are not configuration. Room fields are fed "nothing fresh" when no room source is available.
typedef struct {
    bool    room_fresh;              // the room-source registry has a fresh effective value
    int16_t room_dc;                 // meaningful when room_fresh
    bool    ha_forwarded_stale;      // a ROOM-role ha_forwarded source is STALE (ambient ones are filtered out first)
    bool    dhw_readback_valid;      // ID 56 read back from the boiler (ot_state)
    int16_t dhw_readback_dc;
    bool    setpoint_confirmed;      // the bus confirms ID 1 went out with confirmed_dc since the last step
    int16_t confirmed_dc;
} ot_control_in_t;

// What survives a reset. The task layer restores it: overdue from RTC_NOINIT (valid only after a
// soft reset), heat hours from NVS. init() accepts NULL for "nothing restored".
typedef struct {
    bool     overdue_valid;
    uint32_t overdue_ms;
    uint16_t heat_hours;             // powered hours since HA last asked for heat; saturates at 0xFFFF
    bool     hh_valid;               // the part-hour below: RTC_NOINIT, the same validity rule as overdue
    uint32_t hh_ms;                  // without it a reboot loop faster than an hour never disarms
} ot_control_restore_t;

// Caller-owned state. Not opaque in C, because the task layer holds one by value; but other
// components touch it ONLY through the functions below -- a field read from outside is a second
// place the decision lives. A zeroed struct is NOT valid: call ot_control_init().
typedef struct ot_control ot_control_t;

struct ot_control {
    // The configuration as step() last observed it: the edges, and the ownership that
    // apply() and boost_start() judge by (ot_control.c).
    bool     seen_ha;
    bool     seen_season;
    uint32_t last_step_ms;
    ot_control_state_t state;        // the previous step's; zero (season_off) before the first,
                                     // and all that is asked of it then is "not failsafe"

    // HA's command: RAM only, forgotten at every entry into HA ownership.
    bool     ha_heard;               // an accepted HA CH command since ownership began
    bool     ha_ch_enable;
    int16_t  ha_setpoint_dc;

    // The watchdog and the failsafe (ot_control_failsafe.c).
    uint32_t overdue_ms;             // since the last accepted HA CH command; 0 while LOCAL
    bool     blind;                  // ha_forwarded_stale as of the last step
    bool     fs_latched;             // failsafe until an accepted HA CH_ENABLE with the cause gone
    bool     fs_want;                // the hysteresis' own verdict, kept apart from the bit sent
    ot_control_reason_t fs_cause;
    uint32_t fs_ms;                  // since the last entry into failsafe; read only on its exit
    uint32_t failsafe_count;
    uint32_t last_failsafe_duration_s;
    uint16_t heat_hours;
    uint32_t hh_ms;                  // the powered part-hour not yet counted
    bool     hh_dirty;               // a reset to 0 not yet reported as persist_heat_hours

    // The held ID 1 and the CH bit.
    int16_t  held_dc;
    bool     confirmed;              // the last ID 1 the bus reported was held_dc
    uint32_t ok_age_ms;              // since that report
    bool     ch_out;                 // the CH bit the last step sent
    uint32_t ch_age_ms;              // since ch_out last changed; UINT32_MAX = no change known

    // The ID 56 reconciliation.
    int16_t  dhw_target_dc;
    uint8_t  dhw_tries;              // writes of this target the readback has not yet agreed with
    uint32_t dhw_age_ms;             // since the last write request
    bool     dhw_reopen;             // an accepted DHW_SETPOINT not yet seen in the store...
    int16_t  dhw_reopen_dc;          // ...with this value
    bool     dhw_unread_once;        // one write allowed with no readback, per accepted command

    // The boost, row 2 (ot_control_boost.c).
    bool     boost_active;
    int16_t  boost_setpoint_dc;
    uint32_t boost_expires_ms;       // monotonic; the only stored moment, see the top of this file
};

typedef enum {
    OT_CONTROL_CMD_CH_ENABLE    = 1,   // value 0 or 1
    OT_CONTROL_CMD_CH_SETPOINT  = 2,   // value in dc
    OT_CONTROL_CMD_DHW_ENABLE   = 3,   // value 0 or 1
    OT_CONTROL_CMD_DHW_SETPOINT = 4,   // value in dc
    OT_CONTROL_CMD_SEASON       = 5,   // value 0 or 1; HA may only send 0
} ot_control_cmd_t;                    // explicit values: the generator emits them into the registry

typedef enum {
    OT_CONTROL_OK = 0,
    OT_CONTROL_OWNED_BY_HA,           // a WEB write while mode is HA (in apply(): as the executor last saw it)
    OT_CONTROL_OWNED_BY_LOCAL,        // an HA write while mode is LOCAL (likewise)
    OT_CONTROL_SEASON_ON_IS_LOCAL,    // HA tried to turn heating_season on
    OT_CONTROL_OUT_OF_RANGE,          // a CH setpoint outside [flow_min, flow_max], a bool not 0/1,
                                      // a DHW setpoint <= 0 (0 is the store's "unset"), an unknown command
    OT_CONTROL_SEASON_IS_OFF,         // a boost while heating_season is false
    OT_CONTROL_BAD_MINUTES,           // a boost of 0 or more than OT_CONTROL_BOOST_MAX_MINUTES
} ot_control_err_t;

#define OT_CONTROL_BOOST_MAX_MINUTES 480u
#define OT_CONTROL_RESEND_MS         10000u   // re-assert the held ID 1 every 10 s: OpenTherm gives TSet no lifetime, so the master must keep refreshing it

// What an accepted command asks the task layer to persist. Carried out OUTSIDE every lock:
// ot_net_config_apply() takes a mutex and writes NVS. Filled for every accepted WEB command and
// for HA's DHW and season commands, EVEN WHEN the value equals the stored one: comparing against
// a snapshot here would lose an update when two writers race, and skipping an unchanged NVS
// write is the store's job, under its own mutex. local_ch_setpoint_dc is ALREADY on the store's
// 5 dc grid: apply() quantises a CH setpoint before it persists or uses it (45.05 C -> 450).
typedef struct {
    bool    any;
    bool    set_local_ch_enable;   bool    local_ch_enable;
    bool    set_local_ch_setpoint; int16_t local_ch_setpoint_dc;
    bool    set_dhw_enable;        bool    dhw_enable;
    bool    set_dhw_setpoint;      int16_t dhw_setpoint_dc;
    bool    set_heating_season;    bool    heating_season;
} ot_control_persist_t;

typedef struct {
    uint8_t             status_high;        // OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE bits only
    int16_t             held_setpoint_dc;   // ALWAYS valid: quantised to 5, inside the flow bounds
    bool                send_setpoint;      // queue ID 1 = held_setpoint_dc now, if the bus has no write pending;
                                            // true on EVERY step until the bus confirms it, so a skipped
                                            // request is simply asked again -- the caller keeps no memory
    bool                send_dhw_setpoint;  // queue ID 56 = cfg.dhw_setpoint_dc now (reconciliation, <= 1/60 s)
    ot_control_state_t  state;
    ot_control_reason_t reason;             // NONE, AWAIT_SETPOINT, or in failsafe one of the FS_* / MIN_CYCLE
    ot_control_reason_t cause;              // the CURRENT reason the state is failsafe, recomputed every
                                            // step: WATCHDOG or HA_BLIND while that cause is
                                            // live, else NONE -- including a failsafe held only by the
                                            // latch (the acute cause cleared, awaiting an accepted
                                            // HA CH_ENABLE). NONE in every non-failsafe state
    uint32_t            overdue_ms;         // mirror into RTC_NOINIT every step
    uint32_t            hh_ms;              // likewise: the powered part-hour, restored as hh_ms
    bool                persist_heat_hours; // write heat_hours to NVS now (<= once an hour, only while < limit)
    uint16_t            heat_hours;
    uint32_t            failsafe_count;     // entries into failsafe since this boot: RAM only, there is no
                                            // field for it in ot_control_restore_t; it is published retained
    uint32_t            last_failsafe_duration_s; // of the last failsafe that ended since this boot; 0 before
} ot_control_out_t;

void ot_control_init(ot_control_t *c, const ot_control_cfg_t *cfg,
                     const ot_control_restore_t *r, uint32_t now_ms);

// Pure and stateless: ownership and bounds of one command against a configuration. The early
// answer (ot_command_check calls it) and the first thing ot_control_apply does.
ot_control_err_t ot_control_check(const ot_control_cfg_t *cfg, ot_origin_t origin,
                                  ot_control_cmd_t cmd, int16_t value);

// The final answer: re-checks ownership against the mode the executor last observed (not the
// snapshot's), bounds against the snapshot; applies; stamps the watchdog for accepted HA CH
// commands; fills *persist. Observes no edge. Leaves *c untouched on refusal. *persist is ALWAYS
// written: all false on refusal.
ot_control_err_t ot_control_apply(ot_control_t *c, const ot_control_cfg_t *cfg,
                                  ot_origin_t origin, ot_control_cmd_t cmd, int16_t value,
                                  uint32_t now_ms, ot_control_persist_t *persist);

// Once a second. Runs the ladder, the transitions, the watchdog and failsafe, the invariant and
// the re-send cadence. *out is written in full.
void ot_control_step(ot_control_t *c, const ot_control_cfg_t *cfg, const ot_control_in_t *in,
                     uint32_t now_ms, ot_control_out_t *out);

// The boost, row 2 of the ladder, LOCAL only. Validation lives HERE, in pure code, so a
// host suite can reach it -- seven unkillable mutations once hid in the
// HTTP glue that used to hold it. Ownership and the season are judged as the executor last saw
// them, the setpoint's bounds from the snapshot. A start REPLACES a running boost. The deadline is
// monotonic and ends the boost on the first step at or after it; any mode edge ends it too. The
// season going off does NOT end it: ladder row 1 outranks it while the season is off, and
// its deadline keeps running. boost_active() reads the flag only; remaining_s() is 0 from the deadline
// on, rounded UP before it, and never ends a boost by being read.
ot_control_err_t ot_control_boost_start(ot_control_t *c, const ot_control_cfg_t *cfg,
                                        int16_t setpoint_dc, uint32_t minutes, uint32_t now_ms);
void     ot_control_boost_cancel(ot_control_t *c);
bool     ot_control_boost_active(const ot_control_t *c);
int16_t  ot_control_boost_setpoint_dc(const ot_control_t *c);
uint32_t ot_control_boost_remaining_s(const ot_control_t *c, uint32_t now_ms);

// The CH command of whoever owns it, for the ch_enable entity: the stored LOCAL switch in
// LOCAL mode, HA's command held in RAM in HA mode -- false until HA has spoken since its ownership
// began. The COMMAND, not the bit: ch_enable_effective is the bit, which the invariant, the
// season and the failsafe may hold down while the command says 1. Reads *c only.
bool     ot_control_ch_command(const ot_control_t *c, const ot_control_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
