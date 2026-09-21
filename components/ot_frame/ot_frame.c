// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_frame.h"

#define OT_PARITY_BIT   0x80000000u
#define OT_TYPE_SHIFT   28
#define OT_TYPE_MASK    0x7u
#define OT_SPARE_MASK   0x0F000000u
#define OT_ID_SHIFT     16
#define OT_ID_MASK      0xFFu
#define OT_VALUE_MASK   0xFFFFu

// Returns true when the number of ones is even. The xor fold is half the length of a
// loop over thirty-two bits and does not depend on __builtin_popcount, which a
// foreign compiler may not have.
static bool even_ones(uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1u) == 0u;
}

bool ot_frame_parity_ok(uint32_t raw) { return even_ones(raw); }

uint32_t ot_frame_encode(const ot_frame_t *f) {
    uint32_t raw = ((uint32_t)(f->type & OT_TYPE_MASK) << OT_TYPE_SHIFT)
                 | ((uint32_t)f->data_id << OT_ID_SHIFT)
                 | ((uint32_t)f->data_value & OT_VALUE_MASK);
    // SPARE is zeroed by construction: its four bits take no part in the expression
    // above.
    if (!even_ones(raw)) {
        raw |= OT_PARITY_BIT;
    }
    return raw;
}

bool ot_frame_decode(uint32_t raw, ot_frame_t *out) {
    // The first and only check: on wrong parity *out is left untouched, so that the
    // caller cannot accidentally make use of half a parse.
    if (!even_ones(raw)) {
        return false;
    }
    out->type       = (ot_msg_type_t)((raw >> OT_TYPE_SHIFT) & OT_TYPE_MASK);
    out->data_id    = (uint8_t)((raw >> OT_ID_SHIFT) & OT_ID_MASK);
    out->data_value = (uint16_t)(raw & OT_VALUE_MASK);
    (void)OT_SPARE_MASK;  // SPARE is ignored while parsing on purpose: the frame layout does not
                          // require zeros in this field from the slave.
    return true;
}
