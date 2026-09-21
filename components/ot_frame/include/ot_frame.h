// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The OpenTherm 2.2 frame. Thirty-four bits on the line: a start '1',
// thirty-two data bits, a stop '1'. Only the thirty-two middle ones live here --
// the framing belongs to ot_decode.
//
//   bit 31      parity
//   bits 30..28 MSG-TYPE
//   bits 27..24 SPARE, must be zeros when transmitting
//   bits 23..16 DATA-ID
//   bits 15..0  DATA-VALUE
//
// All functions are pure, stateless and allocation-free. Safe from any context,
// including an ISR.

#ifdef __cplusplus
extern "C" {
#endif

// The value 3 is reserved by the OpenTherm spec; it is accepted while parsing
// and passed on to the caller, because silently turning it into an error would mean
// losing the only sign that the slave is not behaving according to the protocol.
typedef enum {
    OT_MSG_READ_DATA      = 0,
    OT_MSG_WRITE_DATA     = 1,
    OT_MSG_INVALID_DATA   = 2,
    OT_MSG_RESERVED       = 3,
    OT_MSG_READ_ACK       = 4,
    OT_MSG_WRITE_ACK      = 5,
    OT_MSG_DATA_INVALID   = 6,
    OT_MSG_UNKNOWN_DATAID = 7,
} ot_msg_type_t;

typedef struct {
    ot_msg_type_t type;
    uint8_t       data_id;
    uint16_t      data_value;
} ot_frame_t;

// The parity is EVEN over all thirty-two bits: a correct word contains an
// even number of ones.
//
// DO NOT "fix" this to odd on the strength of opentherm_library: there the helper
// function is called parity and is labelled "odd" in a comment, while
// isValidResponse rejects a frame when the number of ones is odd, that is, it
// implements even parity. The name lies, the behaviour is right. What is fixed here
// is the behaviour.
bool ot_frame_parity_ok(uint32_t raw);

// Assembles a word with a correct parity bit. The SPARE field is always zero.
uint32_t ot_frame_encode(const ot_frame_t *f);

// Parses a word. Returns false and does not touch *out if the parity is wrong.
// out may not be NULL.
bool ot_frame_decode(uint32_t raw, ot_frame_t *out);

#ifdef __cplusplus
}
#endif
