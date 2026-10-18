// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ot_frame.h"
#include "ot_observe.h"

// The task that owns the OpenTherm bus. The ONLY caller of ot_master_exchange().
//
// **This task's loop has no exit, and that is a load-bearing property.** In OpenTherm a
// slave that has not seen a correct frame for 5..15 s treats this as a short-circuited
// thermostat and goes into a heat demand. That means "going silent" is not a safe
// state but the hottest one possible. Neither a frame error, nor a missing reply, nor
// a WiFi failure, nor a stale sensor has the right to break the loop.
// DO NOT add a `break`, a `return` or a stop condition here.

#ifdef __cplusplus
extern "C" {
#endif

// Called from the bus task after every successful reply.
// DO NOT BLOCK: while the handler computes, the conversation is not happening, and we
// are obliged to keep the pace.
typedef void (*ot_bus_response_cb)(uint8_t data_id, ot_msg_type_t type, uint16_t raw,
                                   void *ctx);

typedef struct {
    uint32_t cycles;            // how many conversations were started
    uint32_t ok;                // how many ended with a parsed reply
    uint32_t failed;            // timeout, frame error or parity error
    uint32_t overdue;           // how many times the 1150 ms deadline was missed
    uint32_t consecutive_fail;  // unanswered in a row right now
    bool     boiler_answering;  // whether the boiler answered in the last conversations
    uint16_t scan_done;         // scan identifiers covered
    uint16_t scan_total;        // how many in total; zero means no scan is running
} ot_bus_stats_t;

// Starts the task. poll is the polling ring besides the mandatory ID 0; may be NULL.
// The task never stops, the absence of a stop function is deliberate.
//
// First of all the master introduces itself to the boiler ONCE with three writes --
// ID 2 (master configuration and MemberID), 124 (OpenTherm version) and 126 (product
// version). They go one at a time, taking up the meaningful slot of the first
// conversations: the write queue is sized for one, and the next would evict the
// unexecuted one. The mandatory ID 0 meanwhile keeps going out on every second step,
// so the 5 s silence rule is not violated on any of them. The introduction will not be repeated: the
// boiler has remembered it, and a slot of the ring costs more.
//
// A non-answering boiler DOES NOT HOLD UP the introduction: a step counts as done when
// the write is queued, not when it is answered, so the three slots are spent exactly
// once whatever the slave does.
esp_err_t ot_bus_start(const uint8_t *poll, uint8_t count, ot_bus_response_cb cb, void *ctx);

// Queues a write for one slot. Safe from any task.
// The next call evicts an unexecuted write: a stale setpoint is worse than a lost one. A write
// queued while another is on the wire is NOT lost with it: it takes the next meaningful slot
// (ot_bus_track.h says how).
void ot_bus_write(uint8_t data_id, uint16_t value);

// Queues a write ONLY if no write is queued or on the wire, and says whether it did. Safe from
// any task.
//
// For the executor's own writes -- the held ID 1 and the ID 56 reconciliation -- which must never
// evict a hand write. Reading the queue and then calling ot_bus_write() leaves a
// window in which another task's write lands and is evicted; this is the same check and the same
// queueing under one lock. false means "not now": ot_control asks for the held ID 1 again on every
// step until the bus reports it sent, so the caller keeps no memory of a skip.
bool ot_bus_write_if_idle(uint8_t data_id, uint16_t value);

// What the bus did with the ID 1 writes it was given. "The CH bit never rises
// before the held ID 1 has gone out" needs to know that it WENT OUT, not that it was queued: a
// later write evicts a queued one before its slot (ot_bus_write() above).
//
// An ID 1 write counts as SENT when its frame went out and the boiler answered it, whatever the
// answer -- WRITE-ACK, DATA-INVALID, UNKNOWN-DATAID: the boiler heard it. A frame nobody answered
// does not count; it may not have been heard. "Went out", not "was taken": a DATA-INVALID counts
// (ot_bus_track.h says why), and the thermostat logs the first one.
//
// A count, not a flag: the thermostat reads this once a second, and a late tick can let two
// writes leave between two reads -- an ID 1 hidden behind a later write of another ID, a FOREIGN
// ID 1 above all, which must un-confirm the held value, would otherwise be missed. Nothing else
// about the queue is reported because nothing reads it; DO NOT add a field without a reader.
typedef struct {
    uint32_t id1_seq;   // +1 per ID 1 write answered, whoever queued it; 0 before the first
    uint16_t id1_raw;   // the value of the last of them
} ot_bus_write_state_t;

// One consistent copy, taken under the write queue's lock. Safe from any task; never blocks.
void ot_bus_write_state(ot_bus_write_state_t *out);

// Sets the master's flags for the high byte of the ID 0 request -- an OR of the OT_STATUS_*
// bits. The caller includes "ot_bus_sched.h" for those names: they are defined once, beside
// the ID 0 master-status layout, and this header deliberately does not pull the pure scheduler's
// struct into every task-layer file that only needs the byte.
//
// Safe from any task, and it does not block: the value is stored under the same spinlock as
// the write queue and read by the bus task on its next ID 0 slot, so it reaches the boiler
// within one conversation.
//
// **This is the only way the firmware asks the boiler for heat.** If this byte is never
// sent, ID 0 goes out as DATA-VALUE 0x0000 and writing ID 1 changes a flow setpoint the
// boiler ignores while CH enable is down.
//
// It cannot fail and it cannot stop the conversation, deliberately: OpenTherm makes master
// silence longer than five seconds a heat demand, so there is no state of this call in
// which falling silent would be safer than sending the byte we have.
void ot_bus_set_status(uint8_t high);

void ot_bus_stats(ot_bus_stats_t *out);

// Checking the line with a multimeter: for duration_ms the bus task stops conversing
// and slowly toggles the output, half_period_ms in each state.
//
// Why: there is no other way to check that the adapter's output stage is sound. A
// frame takes 34 ms once a second -- a multimeter will not show that. With the boiler
// disconnected one looks at the resistance between the OT terminals, with it connected
// at how the voltage across them collapses from 15..24 V down to 7 and below.
//
// **A warning that must reach the caller.** During the test there are no correct
// frames, and the OpenTherm slave treats master silence longer than five
// seconds as a short-circuited thermostat and goes into a heat demand. That is, the
// boiler will most likely switch on for the duration of the test. This is expected and
// safe, but surprising if not warned about.
//
// Returns false if a test is already running OR the parameters are outside the limits
// below. Two different "no"s under one false, so a caller that can tell them apart must
// check the limits ITSELF and before the call: otherwise an invalid number is
// indistinguishable from a busy device (and so it was -- {"half_period_ms":10} answered
// 409 "already running" to a request that was simply invalid). For that reason the
// limits are named here rather than sitting as literals in ot_bus.c: a second writing of
// them will one day drift apart from the first.
//
// Half period: below 100 ms the multimeter's needle cannot keep up with the toggling
// that the test exists for, and above 5 s it is already easier to look at the line's
// state through the frames.
#define OT_BUS_TEST_MAX_MS      30000u
#define OT_BUS_TEST_HALF_MIN_MS 100u
#define OT_BUS_TEST_HALF_MAX_MS 5000u
bool ot_bus_line_test(uint32_t duration_ms, uint32_t half_period_ms);
bool ot_bus_line_test_active(void);

// A sweep of the Data-ID space: ask every identifier in [from..to] once.
// The results accumulate in the same ot_bus_observed() table -- there is deliberately
// no separate storage: "what this ID answered" is one fact, not two.
//
// READ ONLY. There is no code path in the sweep that produces a write, and there must
// not be one.
//
// An identifier absent from the table AFTER a completed sweep did not answer at all;
// one present with type unknown-dataid answered "I do not have that". These are
// different things, and both are useful. There is no second pass over the non-answering
// ones: a lost frame is cured by running the sweep again, and that is more honest than
// hiding the loss inside a single button.
void ot_bus_scan(uint8_t from, uint8_t to);
void ot_bus_scan_stop(void);

// What the bus has already heard from the boiler. The table is kept by the bus itself,
// not by the caller: the replies pass through it, and making every consumer subscribe
// with a callback for something that already lies right there would mean breeding
// copies of one fact.
//
// A pointer to internal storage that lives for the whole life of the program. The data
// is changed by the bus task without a lock: a reader may see a row updated at the
// moment of reading. For a diagnostic table that is acceptable -- the price of
// consistency here would be a lock on the conversation's path, and the conversation
// matters more.
const ot_observe_t *ot_bus_observed(void);

#ifdef __cplusplus
}
#endif
