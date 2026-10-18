// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ot_bus_sched.h"

// THE BUS'S BOOKKEEPING OF ITS WRITES, around ot_bus_sched's queue of one:
// which ID 1 went out and was answered, and the one race the queue of one leaves open to a task
// that queues while an exchange is on the wire.
//
// PURE, and a component of its own for the reason ot_bus_sched is one (its CMakeLists.txt): a host
// suite that includes a header of ot_bus compiles ot_bus.c, FreeRTOS and all. ot_bus keeps one of
// these beside its scheduler, under the same spinlock, and queues and finishes every write through
// it. ot_bus_sched itself is NOT touched: its host suite is up against the file-size ceiling.
//
// Ownership: both structs are the caller's. Every call must be made under the one lock the caller
// holds over both -- this file takes none. No call fails. A zeroed ot_bus_track_t is the start.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // +1 for every write queued, by anybody. ot_bus reads it in the critical section that builds a
    // step and hands it back to ot_bus_track_done(): a change means a write arrived while that
    // step's exchange was on the wire.
    uint32_t gen;
    // +1 for every ID 1 write the boiler answered, whoever queued it; 0 before the first. A count,
    // not a "last write sent" field, for the reason ot_bus.h gives above ot_bus_write_state_t.
    uint32_t id1_seq;
    uint16_t id1_raw;        // the value of the last of them
} ot_bus_track_t;

// A write for the next meaningful slot. The next write evicts an unexecuted one -- the queue of one
// of ot_bus_sched_write(), whose header says why.
void ot_bus_track_write(ot_bus_track_t *t, ot_bus_sched_t *s, uint8_t data_id, uint16_t value);

// The same, ONLY if no write is pending; says whether it queued. For the executor's own writes,
// which must never evict a hand write: a check and a queueing in two calls leave a
// window in which another task's write lands and is evicted. false means "not now", and the
// executor asks again on its next step.
bool ot_bus_track_write_if_idle(ot_bus_track_t *t, ot_bus_sched_t *s, uint8_t data_id,
                                uint16_t value);

// The exchange of `step` is over; ot_bus_sched_done() is called here and nowhere else.
// gen_at_step is t->gen as it was in the critical section that built the step. answered: the
// boiler replied, whatever the reply -- WRITE-ACK, DATA-INVALID, UNKNOWN-DATAID all mean it heard
// the frame. An ID 1 write counts only then: a frame nobody answered may not have been heard, and
// the invariant is that the value "has gone out on the bus" -- not that the boiler took
// it. A DATA-INVALID counts too, deliberately: the held value is always inside the flow bounds,
// and a range disagreement that held CH down would be a silent no-heat, the failsafe included.
// ot_thermostat logs the first one.
//
// THE RACE THIS CLOSES. ot_bus_sched_done() clears the pending write for WHATEVER is queued when it
// runs, and the exchange runs outside the lock: a write queued during it -- a hand write already
// answered 202 -- would be dropped unsent. When the generation moved since the step, the write
// queued since is re-armed for the next meaningful slot.
void ot_bus_track_done(ot_bus_track_t *t, ot_bus_sched_t *s, const ot_bus_step_t *step,
                       uint32_t gen_at_step, bool answered, uint32_t start_ms, uint32_t end_ms);

#ifdef __cplusplus
}
#endif
