// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_decode.h"

// Phases of waiting for the next transition.
//
// After the leading edge and after a transition on a bit boundary the next transition
// can only be a bit middle, and only a half-bit away. After a bit middle it can be
// either a new middle (a full bit) or a boundary (a half-bit).
enum {
    PHASE_MID_ONLY = 0,
    PHASE_ANY      = 1,
};

#define IS_HALF(n) ((n) >= OT_DECODE_HALF_MIN && (n) <= OT_DECODE_HALF_MAX)
#define IS_FULL(n) ((n) >= OT_DECODE_FULL_MIN && (n) <= OT_DECODE_FULL_MAX)

void ot_decode_reset(ot_decode_t *d) {
    d->status     = OT_DECODE_IDLE;
    d->error      = OT_DECODE_ERR_NONE;
    d->armed      = false;
    d->last_level = false;
    d->since      = 0;
    d->phase      = PHASE_MID_ONLY;
    d->bits       = 0;
    d->shift      = 0;
}

static void fail(ot_decode_t *d, ot_decode_error_t e) {
    d->status = OT_DECODE_ERROR;
    d->error  = e;
}

// Records a bit and checks the framing. Returns false if the frame was rejected.
static bool record_bit(ot_decode_t *d, bool to_idle) {
    // Manchester coding: an active->idle transition is a one, idle->active is a zero.
    const bool bit = to_idle;
    d->shift = (d->shift << 1) | (uint64_t)(bit ? 1u : 0u);
    d->bits++;

    if (d->bits == OT_DECODE_BITS) {
        if (!bit) {
            fail(d, OT_DECODE_ERR_STOP_BIT);
            return false;
        }
        d->status = OT_DECODE_DONE;
    }
    return true;
}

ot_decode_status_t ot_decode_push(ot_decode_t *d, bool level) {
    if (d->status == OT_DECODE_DONE || d->status == OT_DECODE_ERROR) {
        return d->status;   // frozen until reset
    }

    if (d->status == OT_DECODE_IDLE) {
        // Until we have seen idle, any active level is the tail of someone else's
        // activity, not the beginning of a frame. Without this, a reset while the line
        // is active would take its very first sample for the leading edge.
        if (!level) {
            d->armed = true;
        } else if (d->armed) {
            d->status     = OT_DECODE_BUSY;
            d->phase      = PHASE_MID_ONLY;
            d->since      = 0;
            d->bits       = 0;
            d->shift      = 0;
            d->last_level = true;
            return d->status;
        }
        d->last_level = level;
        return d->status;
    }

    // BUSY
    d->since++;
    if (level == d->last_level) {
        if (d->since > OT_DECODE_FULL_MAX) {
            // The slave went silent in the middle of a frame. This is a failure, not
            // "almost a frame": the protocol requires rejecting the frame as a whole, partial
            // parsing is forbidden.
            fail(d, OT_DECODE_ERR_SILENCE);
        }
        return d->status;
    }

    const uint16_t n       = d->since;
    const bool     to_idle = (d->last_level && !level);
    d->since      = 0;
    d->last_level = level;

    if (d->phase == PHASE_MID_ONLY) {
        if (!IS_HALF(n)) {
            fail(d, OT_DECODE_ERR_TIMING);
            return d->status;
        }
        if (!record_bit(d, to_idle)) {
            return d->status;
        }
        d->phase = PHASE_ANY;
        return d->status;
    }

    // PHASE_ANY
    if (IS_FULL(n)) {
        if (!record_bit(d, to_idle)) {
            return d->status;
        }
        d->phase = PHASE_ANY;
    } else if (IS_HALF(n)) {
        // A transition on a bit boundary: the level returns so that the next middle
        // can go in the required direction. It carries no bit.
        d->phase = PHASE_MID_ONLY;
    } else {
        fail(d, OT_DECODE_ERR_TIMING);
    }
    return d->status;
}

uint32_t ot_decode_payload(const ot_decode_t *d) {
    // shift: bit 33 is the start bit, bits 32..1 are the data, bit 0 is the stop bit.
    return (uint32_t)((d->shift >> 1) & 0xFFFFFFFFu);
}

ot_decode_error_t ot_decode_error(const ot_decode_t *d) { return d->error; }
