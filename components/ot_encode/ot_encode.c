// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_encode.h"

void ot_encode_frame(uint32_t payload, bool out[OT_ENCODE_HALFBITS])
{
    // Thirty-four bits: a start '1', thirty-two data, a stop '1' (§4.2).
    const uint64_t bits = (1ULL << 33) | ((uint64_t)payload << 1) | 1ULL;

    for (unsigned i = 0; i < 34u; ++i) {
        // Most significant first: bit 33 is the start bit.
        const bool bit = ((bits >> (33u - i)) & 1u) != 0u;
        // §3.4.1: '1' is an active->idle transition, so the first half is active.
        // '0' is an idle->active transition, so the first half is idle.
        // In other words, the first half EQUALS the bit, the second its negation.
        out[2u * i]      = bit;
        out[2u * i + 1u] = !bit;
    }
}
