// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"
#include "ot_frame.h"

// One OpenTherm conversation: send a frame, receive the response, observe the OpenTherm
// response timings.
//
// The component knows about bits and does not know WHAT to ask. What to ask and when is
// ot_bus's business. The separation is not cosmetic: thanks to it the polling schedule
// stays a pure function and is verified on the host, while what is left here is the
// glue that only a boiler can verify anyway.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OT_EXCHANGE_OK = 0,        // response received, parity checked out
    OT_EXCHANGE_TIMEOUT,       // slave stayed silent longer than 800 ms
    OT_EXCHANGE_FRAME_ERROR,   // Manchester, timings or the stop bit
    OT_EXCHANGE_PARITY_ERROR,  // frame assembled, parity did not check out
} ot_exchange_result_t;

typedef struct {
    // How many TIMES the input level changed over the whole reception period. The only
    // number that honestly answers "is there anything at all on the line": in_duty
    // takes two milliseconds at an arbitrary moment and does not distinguish silence
    // from activity, while this counter watches the entire response window, every
    // conversation. Zero across hundreds of conversations means the wire is dead, not
    // that the frame failed to come together.
    uint32_t rx_edges;
    // Result of the one-off check at initialization: does the input follow the output.
    // True means the two pins are shorted together or swapped on the board -- a working
    // adapter has NO loop between transmitter and receiver.
    bool     pins_shorted;
    uint32_t sent;
    uint32_t ok;
    uint32_t timeout;
    uint32_t frame_error;
    uint32_t parity_error;
} ot_master_stats_t;

// Configures the pins and the timer. Undoes the inversion here and only
// here: further up the stack the levels are LOGICAL, where true means "line active".
// Call once, before ot_bus_start(). Returns ESP_OK or a driver error.
esp_err_t ot_master_init(const board_t *board);

// One conversation. **BLOCKS the calling task for up to ~1.2 s.**
//
// May be called ONLY from the ot_bus task. There is no such thing as two masters on the
// bus: overlapping conversations lose both frames, and it will show up as rare
// inexplicable communication errors rather than as an obvious defect. There is
// deliberately neither a lock nor reentrancy protection here -- what protects this is
// not a primitive but the fact that there is a single caller.
//
// resp is filled in only on OT_EXCHANGE_OK; on any other outcome it is left untouched.
ot_exchange_result_t ot_master_exchange(const ot_frame_t *request, ot_frame_t *response);

// The fraction of samples on which the input was ACTIVE, in percent, over ~2 ms.
//
// Diagnostics, and the only kind that answers a question otherwise settled only with a
// multimeter: the idle state of the OpenTherm line is LOW, so with a
// connected, silent boiler this should read 0. A steady 100 means the input polarity is
// the opposite of what was assumed and ot_in_inverted must be flipped. A value in
// between means the line is switching, that is, frames are flowing.
//
// Safe from any task: reading the GPIO register changes nothing and does not disturb a
// conversation. During a conversation the value is meaningful exactly as far as peeking
// into the middle of a frame is meaningful -- that is, not very; ask while idle.
uint8_t     ot_master_input_duty(void);

// Holds the line in the given state. ONLY for checking the hardware with a multimeter:
// frames are not transmitted this way, and it may be called exclusively from the ot_bus
// task while it is not holding a conversation.
//
// The point is that a healthy adapter output stage cannot be told from a broken one
// with a probe at idle: a frame takes 34 ms once a second, and the needle does not see
// it. Slow switching makes the output's operation observable.
//
// DO NOT call from HTTP handlers: a single task owns the bus, and wedging into its
// conversation means corrupting a frame.
void        ot_master_drive_line(bool active);

void        ot_master_stats(ot_master_stats_t *out);
const char *ot_exchange_result_str(ot_exchange_result_t r);

#ifdef __cplusplus
}
#endif
