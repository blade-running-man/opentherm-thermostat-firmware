// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_bus_sched.h"

#include <string.h>

// A difference of moments that survives uint32 overflow.
//
// The direct comparison `now >= deadline` breaks once every 49 days and breaks
// SILENTLY: the bus would go quiet, and the OpenTherm slave turns silence into a heat demand.
// Subtraction in unsigned arithmetic overflows correctly, so the difference is what is
// compared.
static uint32_t since(uint32_t now, uint32_t then) { return now - then; }

void ot_bus_sched_init(ot_bus_sched_t *s, const uint8_t *poll, uint8_t count)
{
    memset(s, 0, sizeof *s);
    if (poll != NULL && count > 0u) {
        if (count > OT_BUS_MAX_POLL) count = OT_BUS_MAX_POLL;
        memcpy(s->poll, poll, count);
        s->poll_count = count;
    }
}

void ot_bus_sched_write(ot_bus_sched_t *s, uint8_t data_id, uint16_t value)
{
    // Eviction, not a queue. The justification is in the header.
    s->write_pending = true;
    s->write_id      = data_id;
    s->write_value   = value;
}

// A plain assignment, and it stays one: the status is a current state, so the newest byte
// is the only one that matters. The contract is in the header.
void ot_bus_sched_set_status(ot_bus_sched_t *s, uint8_t high)
{
    s->status_high = high;
}

// In-place compaction: the order of the remaining entries is preserved, so after one
// identifier drops out the ring keeps going with the same alternation rather than a
// reshuffled one.
void ot_bus_sched_disable(ot_bus_sched_t *s, uint8_t data_id)
{
    uint8_t out     = 0;
    uint8_t dropped = 0;   // how many were thrown out TO THE LEFT of position s->next
    for (uint8_t i = 0; i < s->poll_count; i++) {
        if (s->poll[i] != data_id)
            s->poll[out++] = s->poll[i];
        else if (i < s->next)
            dropped++;
    }
    s->poll_count = out;

    // Correcting the position is mandatory, and the reason for it is IN THE ORDER OF
    // CALLS in bus_task() (`components/ot_bus/ot_bus.c`): step() -> exchange -> done()
    // -> ot_state_is_unsupported() -> disable(). By that moment done() has already
    // advanced s->next past the slot just asked, and the array compaction above shifts
    // the SUCCESSOR of the departed one into that very slot the position has passed.
    // Without the subtraction the successor loses a whole turn of the ring -- about a
    // minute.
    //
    // Found on live hardware (ESP32-C6, Intergas Kombi Kompakt HRE): 15, 28, 49, 57,
    // 117, 119, 121, 125 dropped out of the ring, and exactly their successors 17, 33,
    // 56, 100, 118, 120, 122, 127 were missing from the log a turn later.
    //
    // DO NOT "simplify" this to an unconditional s->next = 0: that would cost the
    // not-yet-asked elements of the ring another turn, while the departure of the LAST
    // element (the position has already wrapped to zero, nothing to the left of it was
    // thrown out) needs no correction at all.
    s->next -= dropped;

    // The position could have pointed past the new end of the ring.
    if (s->poll_count == 0 || s->next >= s->poll_count)
        s->next = 0;
}

ot_bus_step_t ot_bus_sched_step(const ot_bus_sched_t *s, uint32_t now_ms)
{
    ot_bus_step_t st;
    memset(&st, 0, sizeof st);

    if (!s->started) {
        // The first conversation -- immediately. There is nothing to wait for, and
        // silence is more dangerous than haste.
        st.verb = OT_BUS_TALK;
    } else {
        const uint32_t by_period = since(now_ms, s->last_start_ms);
        const uint32_t by_gap    = since(now_ms, s->last_end_ms);
        if (by_period < OT_BUS_PERIOD_MS) {
            st.verb     = OT_BUS_WAIT;
            st.delay_ms = OT_BUS_PERIOD_MS - by_period;
            // The pause may turn out longer than the period -- then wait by it.
            if (by_gap < OT_BUS_MIN_GAP_MS) {
                const uint32_t rest = OT_BUS_MIN_GAP_MS - by_gap;
                if (rest > st.delay_ms) st.delay_ms = rest;
            }
            return st;
        }
        if (by_gap < OT_BUS_MIN_GAP_MS) {
            st.verb     = OT_BUS_WAIT;
            st.delay_ms = OT_BUS_MIN_GAP_MS - by_gap;
            return st;
        }
        st.verb    = OT_BUS_TALK;
        st.overdue = by_period > OT_BUS_DEADLINE_MS;
    }

    // The order of the slots is part of the contract, not a convenience of the
    // implementation.
    // ID 0 goes out on EVERY odd step: it carries the master's ch_enable and returns
    // the boiler's flags, and skipping it means losing control for a cycle.
    if (!s->status_turn_done) {
        st.data_id = 0;
        // The master's own flags ride the HIGH byte of a READ-DATA request; the low byte
        // is reserved for the slave's answer, which is why is_write stays false. That is
        // the ID 0 protocol, not an oversight -- DO NOT "fix" it into a WRITE-DATA,
        // the boiler would refuse the frame and the status would stop being exchanged at
        // all.
        st.value = (uint16_t)s->status_high << 8;
        return st;
    }
    // The sweep runs instead of the polling ring, but NEVER instead of the mandatory
    // status and never instead of a pending write: diagnostics has no right to delay a
    // setpoint the control loop has already computed.
    if (s->write_pending) {
        st.is_write = true;
        st.data_id  = s->write_id;
        st.value    = s->write_value;
        return st;
    }
    if (s->scan_active) {
        st.data_id = s->scan_next;
        return st;
    }
    if (s->poll_count > 0u) {
        st.data_id = s->poll[s->next % s->poll_count];
        return st;
    }
    // The ring is empty -- either init made it so, or ot_bus_sched_disable() carried
    // out of it everything the boiler did not support. There is no write either, there
    // is nothing to ask: we occupy the meaningful slot with the status. The OpenTherm slave
    // treats master silence longer than five seconds as a short-circuited thermostat and
    // demands heat, therefore DO NOT turn this into an
    // OT_BUS_WAIT or into skipping the conversation.
    st.data_id = 0;
    // The same high byte as on the mandatory slot, and BOTH assignments are needed: this
    // branch carries every second conversation once the ring has emptied, and a status of
    // zero here would ask for heat every other time.
    st.value = (uint16_t)s->status_high << 8;
    return st;
}

void ot_bus_sched_scan(ot_bus_sched_t *s, uint8_t from, uint8_t to)
{
    if (to < from) { const uint8_t t = from; from = to; to = t; }
    s->scan_from   = from;
    s->scan_to     = to;
    s->scan_next   = from;
    s->scan_active = true;
}

void ot_bus_sched_scan_stop(ot_bus_sched_t *s) { s->scan_active = false; }

void ot_bus_sched_scan_progress(const ot_bus_sched_t *s, uint16_t *done, uint16_t *total)
{
    if (!s->scan_active) { *done = 0; *total = 0; return; }
    *total = (uint16_t)(s->scan_to - s->scan_from + 1u);
    *done  = (uint16_t)(s->scan_next - s->scan_from);
}

void ot_bus_sched_done(ot_bus_sched_t *s, const ot_bus_step_t *step,
                       uint32_t start_ms, uint32_t end_ms)
{
    s->started       = true;
    s->last_start_ms = start_ms;
    s->last_end_ms   = end_ms;

    if (!s->status_turn_done) {
        s->status_turn_done = true;
        return;
    }
    s->status_turn_done = false;

    if (s->scan_active && !step->is_write) {
        if (s->scan_next >= s->scan_to) {
            s->scan_active = false;   // the pass is finished
        } else {
            s->scan_next++;
        }
        return;
    }
    if (step->is_write) {
        // Cleared regardless of the outcome: retrying a failed write is the caller's
        // business, since the caller knows whether the value is still current. The bus
        // has no right to repeat a stale setpoint.
        s->write_pending = false;
        return;
    }
    if (s->poll_count > 0u) s->next = (uint8_t)((s->next + 1u) % s->poll_count);
}
