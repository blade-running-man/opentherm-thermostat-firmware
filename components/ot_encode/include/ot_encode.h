// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Rendering of an OpenTherm frame into a sequence of half-bits for transmission.
//
// Manchester (§3.4.1): '1' is an active->idle transition in the middle of the bit,
// '0' is idle->active. That means the FIRST half of the bit equals the bit itself,
// and the second its negation. Thirty-four bits give 68 half-bits of 500 us: a frame
// takes 34 ms.
//
// The levels here are LOGICAL: true means "the line is active". The output inversion
// (spec §2.4) is removed by ot_master using the board field ot_out_inverted. DO NOT
// account for it here: then the host tests would start checking a property of one
// particular board, and the loopback test with ot_decode would stop agreeing.
//
// The function is pure, stateless and allocation-free. Safe from an ISR.

#ifdef __cplusplus
extern "C" {
#endif

#define OT_ENCODE_HALFBITS   68
#define OT_ENCODE_HALFBIT_US 500

// Fills out[] with 68 logical levels. payload is the thirty-two data bits (what
// ot_frame_encode returned); the start and stop bits are added here.
// out may not be NULL and must hold OT_ENCODE_HALFBITS elements.
void ot_encode_frame(uint32_t payload, bool out[OT_ENCODE_HALFBITS]);

#ifdef __cplusplus
}
#endif
