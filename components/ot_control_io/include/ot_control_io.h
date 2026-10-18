// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_api_control.h"
#include "ot_config.h"
#include "ot_control.h"
#include "ot_frame.h"

// THE EXECUTOR'S TRANSLATIONS. ot_control decides and ot_thermostat carries the decision out;
// this is the arithmetic between them and their neighbours -- the configuration store, the bus,
// the reset that just happened, the state model and the document GET /api/control renders --
// which would otherwise sit in a task no host suite reaches.
//
// PURE. No FreeRTOS, no ESP-IDF header, no allocation, no globals: every function maps its
// arguments. DO NOT add a task, a lock or an NVS call here: PlatformIO compiles every source of a
// library whose header a suite includes, and test_ot_control_io would stop linking -- the rake
// recorded for ot_bus_sched and ot_sensor. ot_thermostat is the impure half, and it stays thin
// because this file is thick.
//
// Ownership: every pointer is the caller's, and nothing is kept. No function fails: each gives a
// defined answer for every input, a damaged configuration included; ot_control_io_dc() and
// ot_control_io_minutes() refuse, and leave their output alone.

#ifdef __cplusplus
extern "C" {
#endif

// --- the configuration -------------------------------------------------------------------------

// ot_control's configuration snapshot, from the projection ot_net hands out. ONE builder: the
// thermostat builds every step's snapshot with it and hands the same result to
// ot_command_check() and ot_control_apply(), so the early answer and the final one read one
// mapping (ot_control.h asks both to come from the same store).
//
// A control_mode other than OT_CONFIG_MODE_HA is LOCAL, and a number too large for the narrower
// field saturates rather than wraps: the store's sanitize repairs both, and this does not rely on
// it. dhw_setpoint_set is dhw_setpoint_dc != 0 -- 0 is the store's "nobody has written it".
void ot_control_io_cfg(const ot_config_public_t *pub, ot_control_cfg_t *out);

// What an accepted command asks to persist, as the patch ot_net_config_apply() takes. Only the
// fields the executor asked for are present; everything else is zero, which the store reads as
// "absent, leave it alone". Returns persist->any: false means there is nothing to write.
//
// NO comparison with the stored value, deliberately (ot_control.h, ot_control_persist_t): two
// writers comparing against their own snapshots lose an update when they race. An unchanged value
// costs nothing on flash anyway -- nvs_set_* compares and skips an identical item itself
// (ESP-IDF nvs_storage.cpp, Storage::writeItem, "the item in question is actually being modified").
bool ot_control_io_patch(const ot_control_persist_t *persist, ot_config_patch_t *patch);

// --- the bus -----------------------------------------------------------------------------------

#define OT_CONTROL_IO_ID_TSET     1u    // OpenTherm v2.2 Data-ID 1, TSet: the CH setpoint
#define OT_CONTROL_IO_ID_TDHW_SET 56u   // OpenTherm v2.2 Data-ID 56, TdhwSet: the DHW setpoint

// Tenths of a degree from an f8.8 word (ID 1, ID 56), rounded half away from zero, in integers.
// Exact for everything the executor sends: every multiple of 0.5 is exact in f8.8.
int16_t ot_control_io_f88_dc(uint16_t raw);

// The f8.8 word for tenths of a degree, as ot_codec_float_to_f88() would give it (pinned by a
// test for every value in -100.0..100.0), clamped to 0x7FFF/0x8000 beyond +-127.99.
//
// THE EXECUTOR'S OWN WRITES ARE ENCODED WITH THIS, NOT WITH ot_command_encode(). That gate refuses
// an ID the state model has marked unsupported, and the mark is never cleared (ot_state.c): two
// UNKNOWN-DATAIDs for ID 1 -- garbage on the line will do -- and every re-send of the held ID 1 is
// refused, it is never confirmed, and by the invariant CH never rises again, failsafe
// included, until a reboot. The bounds this gate would check are ot_control's already: the held
// value is inside [flow_min, flow_max] by construction, the DHW target passed ID 48's bounds when
// it was written. ot_command_encode() stays the gate for HAND writes.
uint16_t ot_control_io_dc_f88(int16_t dc);

// The bus's report (ot_bus_write_state()) as ot_control's input. id1_seq counts the
// ID 1 writes the boiler answered; *seen is the count this caller last acted on and is updated.
// A count that moved means an ID 1 went out since the last step -- ours or ANYBODY'S: a foreign
// value on the wire must un-confirm the held one, and ot_control_step() does that when
// confirmed_dc differs from what it holds. The other fields of *in are left alone.
void ot_control_io_confirm(uint32_t id1_seq, uint16_t id1_raw, uint32_t *seen,
                           ot_control_in_t *in);

// The ID 56 readback: what the BOILER says it keeps, for the reconciliation. The task
// records every reply the bus hears here -- main.cpp's response callback sees each one -- and
// hands the result to each step with ot_control_io_readback_in().
//
// ONLY A READ-ACK OF ID 56 IS A READBACK. A WRITE-ACK echoes the value written, and ot_state
// stores it like a READ-ACK (its availability switch): read from there, the executor's own write
// reads back as "the boiler agrees", resets the three tries, and a boiler that stores
// whole degrees is written once a minute for ever -- the probe in test_ot_control_io counts it.
// DATA-INVALID and UNKNOWN-DATAID say nothing about what the boiler keeps and leave the last
// readback as it was; the cap bounds the writes either way.
typedef struct {
    bool     valid;          // a READ-ACK of ID 56 has been heard; a zeroed struct is "none yet"
    uint16_t raw;            // its f8.8 value
} ot_control_io_readback_t;

// One reply the bus heard, of any Data-ID and any type.
void ot_control_io_readback(ot_control_io_readback_t *rb, uint8_t data_id, ot_msg_type_t type,
                            uint16_t raw);

// Fills in->dhw_readback_valid and in->dhw_readback_dc from *rb; the other fields of *in are left
// alone.
void ot_control_io_readback_in(const ot_control_io_readback_t *rb, ot_control_in_t *in);

// Everything the task keeps from the replies the bus hears: the ID 56 readback above, and the
// FIRST DATA-INVALID answer to ID 1 with its value. The boiler refusing the held setpoint that way
// most likely means its range and flow_min_dc..flow_max_dc disagree; the task says so ONCE, with
// the value, from its own step -- the bus task that records it may not log (ot_bus.h, DO NOT
// BLOCK). The write still counts as sent (ot_bus_track.h), deliberately: holding CH down on it
// would turn a range disagreement into a silent no-heat, failsafe included. A zeroed record has
// heard nothing; the flag is never cleared.
typedef struct {
    ot_control_io_readback_t rb;
    bool                     id1_invalid;       // a DATA-INVALID answer to ID 1 has been heard
    uint16_t                 id1_invalid_raw;   // the f8.8 value of the first of them
} ot_control_io_heard_t;

// One reply the bus heard, of any Data-ID and any type: ot_control_io_readback() on h->rb, then
// the ID 1 rule. The one call ot_thermostat_heard() makes, inside its spinlock.
void ot_control_io_heard(ot_control_io_heard_t *h, uint8_t data_id, ot_msg_type_t type,
                         uint16_t raw);

// The executor's own writes for one step -- the held ID 1 and the ID 56 -- offered to the
// bus through `write`: ot_bus_write_if_idle() on the device, which queues ONLY into an idle slot
// and says whether it did, so a hand write is never evicted.
//
// ID 1 FIRST: the invariant waits on it, and ot_control asks for it on every step until
// the bus confirms it, so a refused ID 1 is simply asked again. ID 56 is not: send_dhw_setpoint is
// a ONE-SHOT -- ot_control spends the try on the step that asks (with no readback, the one write
// an accepted command gets) and does not ask again for 60 s, or at all. So an ask the bus
// refuses -- to ID 1 queued first, to a hand write, to the identity writes at boot -- is OWED:
// kept in *owed and written on the first later step whose slot is idle, with the CURRENT target,
// so a newer one accepted meanwhile is the one the boiler gets. ot_control is not asked again and
// no try is counted: one ask, one write. An unset target (0, the store's "nobody wrote it") drops
// what is owed. DHW off and a mode flip do not: ID 56 is what the boiler keeps for when DHW is
// on, whoever owns the value, and ot_control writes it in either case too.
//
// Owed means "not queued yet". A queued write that a later hand write evicts before its slot is
// still lost (ot_bus_write()); with a readback, the 60 s retry heals it. A zeroed *owed owes nothing.
typedef struct {
    bool dhw;        // an ID 56 ot_control asked for has not been queued yet
} ot_control_io_owed_t;

// Queues one write if the bus has none pending, and says whether it did (ot_bus_write_if_idle()).
typedef bool (*ot_control_io_write_fn)(uint8_t data_id, uint16_t value);

void ot_control_io_send(ot_control_io_owed_t *owed, const ot_control_cfg_t *cfg,
                        const ot_control_out_t *out, ot_control_io_write_fn write);

// --- numbers from outside ----------------------------------------------------------------------

// A temperature in degrees as tenths. lroundf(celsius * 10.0f) IN FLOAT, the form
// ot_command_check.c measured right at 20000 of 20000 x.x5 values (the double form is wrong 8000
// times). false for NaN, infinity, or a value with no int16_t -- *dc is then untouched.
bool ot_control_io_dc(float celsius, int16_t *dc);

// A boost's minutes from a JSON number: a whole number >= 0 that fits a uint32_t, or false. 1.5
// is refused -- it would silently become 1, a boost shorter than was asked for. 0 and anything
// above OT_CONTROL_BOOST_MAX_MINUTES are NOT refused here: that is ot_control_boost_start()'s
// BAD_MINUTES, one answer in one place.
bool ot_control_io_minutes(float minutes, uint32_t *out);

// --- what survives a reset ---------------------------------------------------------

// The executor's two counts that must survive a soft reset, as the thermostat keeps them in
// RTC_NOINIT memory: RAM that a software reset, a panic and a watchdog leave alone, and a power-on
// fills with noise. The watchdog's overdue count, so a reboot loop faster than watchdog_s cannot
// hold ha_waiting for ever; the heat-hours part-hour, so a loop faster than an hour cannot keep
// summer bound 2 from ever disarming. ONE magic word and ONE check word cover
// both: they are written together every step and believed together, or not at all.
typedef struct {
    uint32_t magic;          // OT_CONTROL_IO_RTC_MAGIC
    uint32_t overdue_ms;
    uint32_t hh_ms;
    uint32_t check;          // ~(magic ^ overdue_ms ^ hh_ms): a torn or random blob fails it
} ot_control_io_rtc_t;

#define OT_CONTROL_IO_RTC_MAGIC 0x4F544332u   // "OTC2": the layout with the part-hour

void ot_control_io_rtc_store(ot_control_io_rtc_t *rtc, uint32_t overdue_ms, uint32_t hh_ms);

// esp_reset_reason_t, by value. This header may not include esp_system.h (see the top), so
// ot_thermostat pins every one of these to ESP-IDF's enum with a _Static_assert.
enum {
    OT_CONTROL_IO_RST_UNKNOWN   = 0,
    OT_CONTROL_IO_RST_POWERON   = 1,
    OT_CONTROL_IO_RST_EXT       = 2,
    OT_CONTROL_IO_RST_SW        = 3,
    OT_CONTROL_IO_RST_PANIC     = 4,
    OT_CONTROL_IO_RST_INT_WDT   = 5,
    OT_CONTROL_IO_RST_TASK_WDT  = 6,
    OT_CONTROL_IO_RST_WDT       = 7,
    OT_CONTROL_IO_RST_DEEPSLEEP = 8,
    OT_CONTROL_IO_RST_BROWNOUT  = 9,
};

// What ot_control_init() is handed at boot.
//
// The overdue count and the part-hour are restored -- both, or neither -- only after a reset a
// reboot LOOP is made of -- SW, PANIC, INT_WDT, TASK_WDT, WDT and BROWNOUT -- AND with an intact
// blob: a loop faster than watchdog_s would otherwise hold ha_waiting with CH off for ever.
// A brown-out counts: a brown-out loop -- WiFi transmitting on a weak supply -- is the
// commonest reboot loop of an ESP32-C3, and the CHECK WORD, not the reason, is what rejects RTC
// RAM the dip corrupted. A power-on and every other reason start from zero: power-on is no loop.
//
// heat_hours comes from NVS. NONE STORED MEANS HA HAS NEVER ASKED FOR HEAT, which is the disarmed
// end of the scale (UINT16_MAX), not the armed one: a fresh device whose HA dies before it ever
// asked must not heat blind on the strength of a missing key (bound 2, the DIYLESS July).
void ot_control_io_restore(int reset_reason, const ot_control_io_rtc_t *rtc, bool hh_found,
                           uint16_t hh, ot_control_restore_t *out);

// "poweron", "sw", "panic", "int_wdt", "task_wdt", "wdt", "brownout", ... for the boot log line;
// "other" for a value outside the list. Never NULL.
const char *ot_control_io_reset_name(int reset_reason);

// --- what the executor shows -------------------------------------------------------------------

typedef struct {
    const char *key;
    float       value;
} ot_control_io_virtual_t;

#define OT_CONTROL_IO_VIRTUAL_MAX 9

// The executor's synthetic entities with their values, for ot_state_set_virtual(). An
// enum's value is its option INDEX, which is the ot_control ordinal -- test_ot_control_io pins the
// registry's option order to ot_control_state_name() and to the mode enum. A switch or a binary
// is 0 or 1. `ch_command` is ot_control_ch_command(): what the owner commands, where
// ch_enable_effective is the bit actually sent. ch_setpoint_effective is the last ID 1 the bus
// reported, not the held value -- "the ID 1 value actually on the wire" -- and is left out
// until one has gone out. Returns the number of entries written to v.
size_t ot_control_io_virtuals(const ot_control_cfg_t *cfg, const ot_control_out_t *out,
                              bool ch_command, bool id1_known, int16_t id1_dc,
                              ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX]);

// GET /api/control's document from one step's snapshot and output. The boost's three fields and
// the stack mark are zeroed here and are the caller's to fill: they come from ot_control's boost
// accessors and from the task's own FreeRTOS high-water mark, not from `out`.
void ot_control_io_document(const ot_control_cfg_t *cfg, const ot_control_out_t *out,
                            ot_api_control_t *doc);

#ifdef __cplusplus
}
#endif
