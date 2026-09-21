// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_onewire_decode.h"

// Dallas/Maxim CRC-8: reflected polynomial 0x8C (i.e. x^8 + x^5 + x^4 + 1), init 0x00, no
// final xor. Bit-at-a-time rather than a 256-byte table: this runs a handful of times every
// few seconds, and the table would cost more RAM than the loop costs cycles.
uint8_t ot_onewire_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            // The reflected poly means we shift RIGHT and xor 0x8C on a set low bit.
            crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

bool ot_onewire_scratchpad_crc_ok(const uint8_t scratchpad[9])
{
    return ot_onewire_crc8(scratchpad, 8) == scratchpad[8];
}

int16_t ot_onewire_temp_raw(uint8_t lsb, uint8_t msb)
{
    // Two's-complement 16-bit count in 1/16 C units. Build it in a uint16_t and reinterpret,
    // rather than sign-extending by hand, so the negative range is exact and portable.
    uint16_t u = (uint16_t)(((uint16_t)msb << 8) | lsb);
    return (int16_t)u;
}

float ot_onewire_temp_c(const uint8_t scratchpad[9])
{
    return ot_onewire_temp_raw(scratchpad[0], scratchpad[1]) / 16.0f;
}
