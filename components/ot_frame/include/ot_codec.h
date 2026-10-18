// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Codecs for the DATA-VALUE field. Every registry entity names exactly one of them
// All functions are pure and stateless.

#ifdef __cplusplus
extern "C" {
#endif

// f8.8 is SIGNED fixed point, divisor 256. The signedness is load-bearing: the
// outside temperature and the exhaust temperature can be negative, and an unsigned
// parse turns -1.0 into +255.996 completely silently.
float    ot_codec_f88_to_float(uint16_t raw);

// Saturates at the int16 bounds instead of overflowing: a setpoint calculation that
// has gone absurd should hit the ceiling, not change sign.
uint16_t ot_codec_float_to_f88(float value);

int16_t  ot_codec_s16(uint16_t raw);

uint8_t  ot_codec_u8_hb(uint16_t raw);
uint8_t  ot_codec_u8_lb(uint16_t raw);
int8_t   ot_codec_s8_hb(uint16_t raw);
int8_t   ot_codec_s8_lb(uint16_t raw);

// bit -- 0..7 within the selected byte.
bool     ot_codec_flag(uint16_t raw, bool high_byte, uint8_t bit);

#ifdef __cplusplus
}
#endif
