// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// ot_origin_t, ot_control_cfg_t and ot_control_cmd_t, for ot_command_check() below. ot_control
// is pure -- stdbool, stdint and ot_bus_sched.h (itself pure, for the OT_STATUS_* bit names), no
// ESP-IDF header -- so this include drags no framework and no state into a suite that includes
// this header. The linking rake described further down was about ot_state and ot_lock, and it
// is not reopened by this.
#include "ot_control.h"

// A registry key plus a value -- into a write frame. And a refusal when writing is not
// allowed.
//
// The only place where it is decided whether a write is legal -- through two entry points,
// and which one a caller uses follows from what the caller is:
//  - ot_command_check() is for every WRITER: REST and the web interface (OT_ORIGIN_WEB),
//    MQTT (OT_ORIGIN_HA). It carries the origin, decides ownership for the executor's
//    inputs and hands every other entity to ot_command_encode().
//  - ot_command_encode() is ot_command_check()'s frame path, and has no other caller (its
//    own suite aside): the entity route asks ot_command_check(), the boost
//    asks ot_control, and the executor encodes its own writes (below).
// DO NOT call ot_command_encode() from a writer's surface: it knows no owner, so a write
// through it bypasses the ownership 409. And DO NOT let the executor re-send ID 1 or ID 56
// through it: the state model's "unsupported" flag is never cleared, so it would
// refuse every re-send forever, and by the invariant that CH never rises before the held ID 1 has gone out, CH would never rise again. The
// executor encodes those two values itself with the f8.8 codec. A check duplicated on the
// surface will one day diverge from the check in the depths, and it will diverge silently.
//
// Does not depend on hardware: verified on the host in full. The frame is
// not sent out -- the caller passes it to ot_bus_write() itself, because the bus queue
// and its eviction rule belong to the bus.
//
// Ownership: nothing is allocated, `out` belongs to the caller.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OT_CMD_OK = 0,
    OT_CMD_UNKNOWN_KEY,           // there is no such key in the registry
    OT_CMD_NOT_WRITABLE,          // the registry declares the entity read-only
    // Out of bounds; the bounds come from the boiler if it has named them. NOT-A-NUMBER
    // belongs here too: NaN and infinity are rejected before the comparison, because no
    // comparison with NaN is true and "the range check passed" would come out by
    // itself. And so does a writable entity that has no bounds at all -- see
    // ot_command.c.
    OT_CMD_OUT_OF_RANGE,
    OT_CMD_UNSUPPORTED_BY_BOILER, // the boiler answered unknown-dataid twice for this ID
    OT_CMD_HALF_WORD_CODEC,       // the codec describes half a word -- see below
    // Ownership, returned by ot_command_check() only. APPENDED: the codes above
    // keep their numbers. DO NOT insert above this line.
    //
    // They say who may write, not what is wrong with the value, which is why the HTTP answer
    // is 409 and not 422. That is NOT a promise that the same value is accepted after a mode
    // switch: the executor's own bounds are asked after ownership (the order stated at
    // ot_command_check()), so the web's ch_setpoint = 99 in HA mode is 409 now and 422 in
    // LOCAL; and OT_CMD_SEASON_ON_IS_LOCAL refuses HA in both modes (as OWNED_BY_LOCAL in LOCAL).
    OT_CMD_OWNED_BY_HA,           // a web write while control_mode is HA
    OT_CMD_OWNED_BY_LOCAL,        // a Home Assistant write while control_mode is LOCAL
    OT_CMD_SEASON_ON_IS_LOCAL,    // Home Assistant tried to turn heating_season on
    // A Home Assistant write of an OpenTherm FRAME row -- an entity with no control command.
    // APPENDED below the three above, and NOT one of them: the entity is
    // nobody's, so this refusal holds in BOTH modes and no change of owner cures it. It would be
    // a 409 like them; no HTTP route can reach it, because REST writes with OT_ORIGIN_WEB.
    OT_CMD_NOT_FOR_HA,
} ot_command_err_t;

typedef struct {
    uint8_t  data_id;
    uint16_t raw;
} ot_command_frame_t;

// The types "a value that came from outside" and "operation parameters" are NOT here,
// and that is deliberate. They describe the result of PARSING a request body, so they
// live in ot_wire (`ot_wire_value_t`, `ot_wire_params_t`) together with what produces
// them.
//
// The reason is not purity but linking, and it was measured. While the types lay here,
// ot_wire required ot_command, and the test_wire_command suite -- a test of TEXT
// PARSING -- dragged in ot_state, ot_registry and the whole boiler model; linking
// failed on ot_lock, which that test has nowhere to get. Splitting them into separate
// files of one component would not have helped: PlatformIO compiles all the sources of
// a library (decision #15 of the log).
//
// The cut follows responsibility: ot_wire is bytes into a parsed value, ot_command is a
// parsed value plus the registry and the state into a frame. They know nothing about
// each other, the gluing happens in ot_http.
// DO NOT bring these types back here "so that the command layer is in one place".

// Validates and encodes. On any refusal `*out` is left untouched.
//
// **Part-byte codecs are not allowed for writing** (OT_CMD_HALF_WORD_CODEC).
// u8_hb / u8_lb / s8_hb / s8_lb describe half of DATA-VALUE, while the frame carries
// all sixteen bits. Assembling the word by taking the other half from the last read
// means sending the boiler a stale byte disguised as a fresh one -- and doing it
// silently. Today no writable entity has such a codec; the check stands for the future.
//
// **A synthetic row (data_id -1) is OT_CMD_NOT_WRITABLE here**, writable flag or not: it
// has no Data-ID to put in a frame. Its writes go through ot_command_check().
ot_command_err_t ot_command_encode(const char *key, float value,
                                   ot_command_frame_t *out);

// What an accepted write asks the caller to do: put a frame on the bus, or hand a command to
// the executor. A tagged result rather than two functions, so that no caller can reach the
// frame path for an entity that is an executor input.
typedef enum { OT_CMD_OUT_FRAME = 0, OT_CMD_OUT_CONTROL = 1 } ot_command_out_kind_t;

typedef struct {
    ot_command_out_kind_t kind;
    ot_command_frame_t    frame;     // kind FRAME: put it on the bus with ot_bus_write(); zero otherwise
    ot_control_cmd_t      control;   // kind CONTROL: hand it to ot_control_apply(); 0 otherwise
    int16_t               value;     // kind CONTROL: 0/1, or tenths of a degree; 0 otherwise
} ot_command_out_t;

// Every refusal of every writer, with an origin. REST calls it with
// OT_ORIGIN_WEB, MQTT with OT_ORIGIN_HA; nothing else decides a write.
//
// THE ORDER OF REFUSALS IS ONE RULE, and this is its one statement: what is wrong with the
// request WHOEVER sends it comes first, because no change of owner cures it; then who may send
// it; then the executor's own bounds, which are the owner's to learn -- a caller is not told the
// bounds of a command it may not send. For a registry row that names a control command (entity
// `control != 0`: ch_enable, ch_setpoint, dhw_enable, dhw_setpoint, heating_season) the answer
// is kind CONTROL after, in this order:
//   1. the key and writability;
//   2. the boiler's support of the write ID -- not for ch_setpoint (below);
//   3. the representation: `value` into ot_control's int16_t (conversion, below);
//   4. for dhw_setpoint, the boiler's own bounds (ID 48 or the table, through
//      ot_command_encode()) on the value that will be persisted;
//   5. ot_control_check(cfg, origin, …): ownership, then the executor's own bounds (0/1 for a
//      switch, flow_min..flow_max, the DHW "unset" 0).
// So a web write in HA mode answers ch_enable = 0.5 and dhw_setpoint = 99 with
// OT_CMD_OUT_OF_RANGE, and ch_enable = 2 and ch_setpoint = 99 with OT_CMD_OWNED_BY_HA
// (test_the_order_of_refusals_is_one_rule). DO NOT restate the order elsewhere: point here.
//
// ch_setpoint is never OT_CMD_UNSUPPORTED_BY_BOILER: ID 1's flag never clears and the
// executor's own re-sends can raise it, so asking it would freeze the setpoint until a reboot
// while the executor went on sending ID 1 anyway (ot_command_check.c).
//
// For every other row -- `control == 0`, a plain OpenTherm write -- it is ot_command_encode(),
// kind FRAME, for OT_ORIGIN_WEB; for OT_ORIGIN_HA it is OT_CMD_NOT_FOR_HA, asked AFTER
// ot_command_encode() has judged the request itself, so the order above holds here too (an HA
// write of an out-of-range value is OT_CMD_OUT_OF_RANGE, not this). Home Assistant drives the
// EXECUTOR, never the bus: Home Assistant may send no OpenTherm frame (IDs 16 and 24 are kept away from it) and every HA command is refused in LOCAL mode,
// and a frame write a second would hold the bus's one write slot and
// defer the executor's ID 1 re-send -- and since CH never rises before the held ID 1 has gone out, the CH bit could then never rise,
// the failsafe's included. DO NOT move this refusal into a transport: one entry point decides
// every refusal, and re-answering it must stay one line HERE.
//
// **This is the EARLY answer, not the final one.** The mode may flip between this call and
// the caller's ot_control_apply(), which re-checks under its own lock; the
// caller must handle a refusal from ot_control_apply() as well.
//
// Conversion: a temperature becomes tenths of a degree, rounded half away from zero
// (45.05 -> 451); a switch value must be a whole number (0.5 is refused, 2 goes on to
// ot_control, which refuses it); NaN, infinity and anything without an int16_t are
// OT_CMD_OUT_OF_RANGE.
//
// Task context: any task. Reads ot_state (which takes ot_lock inside) and nothing else; the
// configuration arrives as plain values in `cfg`, so this component does not link ot_config.
//
// Failure: every ot_command_err_t value. The caller maps OT_CMD_OWNED_BY_HA,
// OT_CMD_OWNED_BY_LOCAL and OT_CMD_SEASON_ON_IS_LOCAL to 409. A NULL key, `cfg` or `out` is
// OT_CMD_UNKNOWN_KEY, as a NULL `out` is for ot_command_encode(). On refusal `*out` is
// untouched.
ot_command_err_t ot_command_check(const char *key, float value, ot_origin_t origin,
                                  const ot_control_cfg_t *cfg, ot_command_out_t *out);

// A literal fit for a response body. Never NULL.
const char *ot_command_strerror(ot_command_err_t e);

#ifdef __cplusplus
}
#endif
