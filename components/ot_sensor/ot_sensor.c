// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_sensor.h"

#include <math.h>
#include <string.h>

// A difference of moments that survives the uint32 overflow, the same one ot_bus_sched.c
// uses and for the same reason: subtraction in unsigned arithmetic wraps correctly, a
// comparison of the moments themselves does not.
static uint32_t since(uint32_t now, uint32_t then) { return now - then; }

// Saturating addition. Wrapping here would turn a sensor absent for 60 days back into a
// fresh one, which is precisely the failure the accumulator exists to prevent.
static uint32_t add_sat(uint32_t a, uint32_t b)
{
    return (a > UINT32_MAX - b) ? UINT32_MAX : a + b;
}

// The clock advance, shared by tick and update so that an update cannot forget it.
// The first call after init only seeds the reference: ot_sensor_init() takes no timestamp
// (there is nothing sensible for it to take -- the owner has not started its task yet),
// so there is no interval to measure until a second moment is known.
static void advance(ot_sensor_t *s, uint32_t now_ms)
{
    if (!s->tick_seeded) {
        s->tick_seeded  = true;
        s->last_tick_ms = now_ms;
        return;
    }
    s->overdue_ms   = add_sat(s->overdue_ms, since(now_ms, s->last_tick_ms));
    s->last_tick_ms = now_ms;
}

void ot_sensor_init(ot_sensor_t *s, uint32_t stale_after_ms)
{
    memset(s, 0, sizeof *s);
    s->stale_after_ms = stale_after_ms;
    s->value          = NAN;
}

void ot_sensor_tick(ot_sensor_t *s, uint32_t now_ms)
{
    advance(s, now_ms);
}

bool ot_sensor_update(ot_sensor_t *s, float celsius, uint32_t now_ms)
{
    advance(s, now_ms);

    // isfinite() covers NaN and both infinities in one test, and it comes first: the
    // comparisons below are all false for NaN, so a NaN would otherwise slip through the
    // range check unnoticed.
    if (!isfinite(celsius)) return false;
    if (celsius < OT_SENSOR_MIN_C || celsius > OT_SENSOR_MAX_C) return false;

    if (s->have_value && fabsf(celsius - s->value) > OT_SENSOR_MAX_JUMP_C) {
        if (s->reject_streak < OT_SENSOR_MAX_JUMP_REJECTS) {
            s->reject_streak++;
            return false;
        }
        // The escape hatch: this is the fourth jump in a row, so the sensor has most
        // likely been moved or replaced and the value it insists on is the truth. Fall
        // through to acceptance -- see the header for why refusing for ever is worse.
    }

    s->value         = celsius;
    s->have_value    = true;
    s->overdue_ms    = 0;
    s->reject_streak = 0;
    return true;
}

ot_sensor_state_t ot_sensor_state(const ot_sensor_t *s)
{
    if (!s->have_value) return OT_SENSOR_NEVER;
    return (s->overdue_ms >= s->stale_after_ms) ? OT_SENSOR_STALE : OT_SENSOR_FRESH;
}

float ot_sensor_value(const ot_sensor_t *s) { return s->have_value ? s->value : NAN; }

uint32_t ot_sensor_overdue_ms(const ot_sensor_t *s) { return s->overdue_ms; }
