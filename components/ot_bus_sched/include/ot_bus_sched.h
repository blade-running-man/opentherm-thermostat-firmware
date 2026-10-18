// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The decision "what to ask and when". A pure function of state and time: no timers,
// no tasks, no GPIO -- which is why the polling pace and order are verified on the
// host and not with a stopwatch by the boiler.
//
// The OpenTherm timing rules, all three mandatory:
//   * the slave's reply arrives 20..800 ms after the end of the master's frame;
//   * between conversations the master waits AT LEAST 100 ms;
//   * the master must speak AT LEAST once every 1 s +15 %, that is, 1150 ms.
//
// The naive "a conversation every 950 ms" VIOLATES these rules: 34 ms of frame +
// 800 ms of waiting + 34 ms of reply = 868 ms, plus a 100 ms pause = 968 ms > 950.
// That is why the moment of the next conversation is the maximum of two deadlines,
// not a period.

#ifdef __cplusplus
extern "C" {
#endif

#define OT_BUS_PERIOD_MS      950u   // the desired pace
#define OT_BUS_MIN_GAP_MS     100u   // OpenTherm minimum between conversations
#define OT_BUS_DEADLINE_MS   1150u   // 1 s +15 %, the master must speak at least this often
#define OT_BUS_MAX_POLL        32u   // how many Data-IDs fit into the polling ring

// The bits of the master status -- the HIGH byte of the ID 0 request.
// Named here, next to that reference, because this is the only place they are defined: a
// caller writing a bare 0x01 would put the layout of someone else's protocol into its own
// file, and the boiler answers a wrong bit by heating.
//
// This firmware drives bits 0 and 1; the remaining three are named because the byte is
// theirs too and a future reader must not reuse those positions for something of ours.
#define OT_STATUS_CH_ENABLE   0x01u
#define OT_STATUS_DHW_ENABLE  0x02u
#define OT_STATUS_COOLING     0x04u
#define OT_STATUS_OTC_ACTIVE  0x08u
#define OT_STATUS_CH2_ENABLE  0x10u

typedef enum {
    OT_BUS_WAIT = 0,   // the time has not come; the delay_ms field says how long to wait
    OT_BUS_TALK,       // time to talk; the frame fields describe what to send
} ot_bus_verb_t;

typedef struct {
    ot_bus_verb_t verb;
    uint32_t      delay_ms;   // meaningful on WAIT
    uint8_t       data_id;    // meaningful on TALK
    bool          is_write;   // meaningful on TALK
    uint16_t      value;      // meaningful on TALK
    bool          overdue;    // TALK scheduled later than OT_BUS_DEADLINE_MS -- a defect, observe it
} ot_bus_step_t;

typedef struct {
    // The sweep of the Data-ID space. It occupies a slot of the polling ring rather
    // than a separate conversation: the bus is owned by one, and a second conversation
    // would simply overlap the first. The mandatory ID 0 meanwhile keeps going out
    // every other time, so a full pass over 0..127 costs 256 conversations, about four
    // minutes. Speeding it up by skipping the status was rejected: the rule "ID 0 on
    // every second step" is one and the same for all modes, and a special case that is
    // harmless today becomes a loss of control once a control loop drives the status byte.
    bool     scan_active;
    uint8_t  scan_from;
    uint8_t  scan_to;
    uint8_t  scan_next;

    uint8_t  poll[OT_BUS_MAX_POLL];  // what to poll around the ring, besides ID 0
    uint8_t  poll_count;
    uint8_t  next;                   // position in the ring

    bool     write_pending;
    uint8_t  write_id;
    uint16_t write_value;

    uint32_t last_start_ms;          // start of the previous conversation
    uint32_t last_end_ms;            // end of the previous reply
    bool     started;                // whether there has been any conversation at all

    // Alternates the mandatory status and the meaningful slot. ID 0 must go out on
    // every second step: it carries the master's ch_enable and returns the boiler's
    // flags, and skipping it means losing control for a cycle.
    bool     status_turn_done;

    // The master's flags, as they will go out in the high byte of the next ID 0 request.
    // Zero after init, and that is a SAFETY decision, not a default: a device that has
    // only just booted, or whose control loop has not decided anything yet, must not ask
    // the boiler for heat.
    uint8_t  status_high;
} ot_bus_sched_t;

void ot_bus_sched_init(ot_bus_sched_t *s, const uint8_t *poll, uint8_t count);

// Sets the master's flags for the ID 0 request -- an OR of the OT_STATUS_* bits above.
//
// It is a byte, not a queue and not an event: the status is a CURRENT state, and the
// boiler is told the latest one. Takes effect on the very next ID 0 slot ot_bus_sched_step()
// builds; a slot already handed out is not revised, so at worst the new byte reaches the
// boiler one conversation later -- under two seconds.
//
// DO NOT make this a "request" that step() consumes and clears: ID 0 goes out every other
// conversation forever, and a byte cleared after one send would drop ch_enable back to
// zero, that is, cancel the heat demand a second after asking for it.
void ot_bus_sched_set_status(ot_bus_sched_t *s, uint8_t high);

// Queues a write. The queue holds ONE write: the next evicts the unexecuted one.
// DO NOT grow it into a ring "so that nothing is lost": a flow setpoint sent a minute
// late is worse than a lost one -- the boiler would act on a stale decision of the
// control loop.
void ot_bus_sched_write(ot_bus_sched_t *s, uint8_t data_id, uint16_t value);

// Removes a Data-ID from the polling ring. Called when the boiler has answered
// unknown-dataid twice in a row: asking further means spending a ring slot on a
// knowingly empty answer once a minute.
//
// Idempotent; an identifier that is not in the ring is ignored silently.
//
// **The ring may become empty, and that is not a failure state.** The mandatory ID 0
// keeps going out on every second step, and on the meaningful slot, when there is
// nothing to ask, it goes out as well: the OpenTherm slave treats master silence longer
// than five seconds as a short-circuited thermostat and drives the boiler into a heat demand.
// DO NOT add an error return or a stop condition here.
//
// There is deliberately no inverse verb: the reset is a reboot. The result of a sweep
// does not survive it and must not (decision #36 of the log: this is diagnostics, not
// configuration).
void ot_bus_sched_disable(ot_bus_sched_t *s, uint8_t data_id);

// What to do at the moment now_ms. Does not change the state: the state is advanced by
// ot_bus_sched_done() upon the completion of a conversation.
ot_bus_step_t ot_bus_sched_step(const ot_bus_sched_t *s, uint32_t now_ms);

// Starts a sweep of [from..to] inclusive. A repeated call during a sweep restarts it
// from the beginning. The sweep ONLY reads: it produces no writes, and there is no code
// path here for that -- the price of a wrong number on a write is a changed boiler
// setting with no way to learn the previous one.
void ot_bus_sched_scan(ot_bus_sched_t *s, uint8_t from, uint8_t to);
void ot_bus_sched_scan_stop(ot_bus_sched_t *s);

// How many sweep identifiers have been covered and how many there are in total. With
// the sweep stopped, both are zero.
void ot_bus_sched_scan_progress(const ot_bus_sched_t *s, uint16_t *done, uint16_t *total);

// The conversation is finished (successfully or not -- the bus does not care, it is
// obliged to keep the pace).
void ot_bus_sched_done(ot_bus_sched_t *s, const ot_bus_step_t *step,
                       uint32_t start_ms, uint32_t end_ms);

#ifdef __cplusplus
}
#endif
