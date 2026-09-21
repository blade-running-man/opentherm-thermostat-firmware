// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_bus_track.h"

// The contracts are in the header; test_ot_bus_track pins them through the real scheduler.

void ot_bus_track_write(ot_bus_track_t *t, ot_bus_sched_t *s, uint8_t data_id, uint16_t value)
{
    t->gen++;
    ot_bus_sched_write(s, data_id, value);
}

// Through ot_bus_track_write(), so the generation has one writer. An idle slot means no write is
// on the wire, so done() could not drop this one anyway -- the bump costs nothing and keeps the
// rule "every queued write moves the generation" free of an exception to reason about.
bool ot_bus_track_write_if_idle(ot_bus_track_t *t, ot_bus_sched_t *s, uint8_t data_id,
                                uint16_t value)
{
    const bool idle = !s->write_pending;
    if (idle)
        ot_bus_track_write(t, s, data_id, value);
    return idle;
}

void ot_bus_track_done(ot_bus_track_t *t, ot_bus_sched_t *s, const ot_bus_step_t *step,
                       uint32_t gen_at_step, bool answered, uint32_t start_ms, uint32_t end_ms)
{
    ot_bus_sched_done(s, step, start_ms, end_ms);
    // DO NOT re-arm unconditionally, or on step->is_write alone: with no write queued since the
    // step, the frame just sent would go out twice. The generation is the only witness.
    // Raising the flag alone is enough ONLY because ot_bus_sched_done() clears write_pending and
    // never write_id / write_value, which still hold the write queued since. DO NOT make done()
    // clear them too: this re-arm would then send whatever the clear left instead of that write.
    if (t->gen != gen_at_step)
        s->write_pending = true;
    // step->value, NOT s->write_value: what went out is the step's; the queue may already hold
    // the next write.
    if (step->is_write && answered && step->data_id == 1) {
        t->id1_seq++;
        t->id1_raw = step->value;
    }
}
