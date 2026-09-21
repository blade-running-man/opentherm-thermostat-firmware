// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Reception of an OpenTherm frame by oversampling. The state machine is fed the line
// level exactly every OT_DECODE_SAMPLE_US microseconds; it returns a status.
//
// The component is pure: no timer, no GPIO, no allocations. The level arrives ALREADY
// normalised -- true means "the line is active". The adapter inversion is removed by
// ot_master using the board field ot_in_inverted. DO NOT account for it
// here: otherwise the host tests would start checking a property of one particular
// board.
//
// All functions are safe to call from an ISR: they do not block and do not allocate.
// An instance belongs to a single task; calling it from two contexts at once is a
// defect of the caller.

#ifdef __cplusplus
extern "C" {
#endif

#define OT_DECODE_SAMPLE_US 100
#define OT_DECODE_BITS      34   // start + 32 data + stop

// Windows for the intervals between transitions, in samples.
//
// The OpenTherm spec sets the bit period at 900..1150 us, that is, a half-bit of
// 450..575. A uniform sampler with step P counts either floor(T/P) or floor(T/P)+1
// samples inside an interval T -- THE PHASE ERROR IS ALREADY INSIDE THAT SPREAD.
// Hence:
//
//     half-bit   450..575  us / 100 -> {4, 5, 6}
//     full bit   900..1150 us / 100 -> {9, 10, 11, 12}
//
// DO NOT add another ±1 "for the phase error" on top of this: it is already accounted
// for, and the first edition of this file, counting it twice, ended up with {3..7} and
// {8..13}. Measured: with those windows a stream with a period of 800..875 us -- 12 %
// faster than the tolerance -- is accepted with a hundred percent success rate (3200
// frames out of 3200). With the windows below, 3197 out of 3200 are rejected.
//
// Between HALF_MAX=6 and FULL_MIN=9 two free positions remain, and that is the whole
// reason for the 100 us step. At 200 us the correct windows -- {2,3} and {4,5,6} --
// butt right up against each other: any interval falls into one of them, and there is
// nothing left to tell "too fast" apart. That same 800..875 us stream passes there
// entirely.
#define OT_DECODE_HALF_MIN  4
#define OT_DECODE_HALF_MAX  6
#define OT_DECODE_FULL_MIN  9
#define OT_DECODE_FULL_MAX  12

typedef enum {
    OT_DECODE_IDLE = 0,   // waiting for the leading edge
    OT_DECODE_BUSY,       // a frame is in progress
    OT_DECODE_DONE,       // 34 bits collected, the payload is available
    OT_DECODE_ERROR,      // the frame was rejected
} ot_decode_status_t;

typedef enum {
    OT_DECODE_ERR_NONE = 0,
    OT_DECODE_ERR_TIMING,     // the interval fell into no window
    OT_DECODE_ERR_STOP_BIT,   // the thirty-fourth bit turned out to be zero
    OT_DECODE_ERR_SILENCE,    // no transitions for longer than a full bit
} ot_decode_error_t;

// There is NO start-bit check here, and that is not an omission. The leading edge on
// which the state machine catches synchronisation is by construction the transition
// into the active half of the start bit, and the start bit equals one. A zero
// start bit would mean there is no edge at that place at all, and the state machine
// would simply have caught synchronisation later. Telling one from the other is
// impossible; this is caught by the stop bit and by the parity that ot_frame checks.
// DO NOT add `if (bits == 1 && !bit)` here: the branch is unreachable, and a test for
// it would be checking something other than what it claims.

typedef struct {
    ot_decode_status_t status;
    ot_decode_error_t  error;
    bool               armed;       // we have seen idle -- so the next edge is leading
    bool               last_level;
    uint16_t           since;       // samples since the previous transition
    uint8_t            phase;       // internal; see ot_decode.c
    uint8_t            bits;
    uint64_t           shift;       // the collected bits, most significant first
} ot_decode_t;

// Resets the state machine. Call before every wait for a reply. Calling it while the
// line is active is safe: the state machine will first wait for idle and only then
// begin a frame.
void ot_decode_reset(ot_decode_t *d);

// One sample. Returns the current status. After DONE or ERROR further calls change
// nothing -- ot_decode_reset is needed.
ot_decode_status_t ot_decode_push(ot_decode_t *d, bool level);

// The thirty-two data bits without the framing. Meaningful only on DONE.
uint32_t ot_decode_payload(const ot_decode_t *d);

ot_decode_error_t ot_decode_error(const ot_decode_t *d);

#ifdef __cplusplus
}
#endif
