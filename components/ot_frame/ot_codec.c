// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_codec.h"

#include <math.h>

float ot_codec_f88_to_float(uint16_t raw) {
    return (float)(int16_t)raw / 256.0f;
}

uint16_t ot_codec_float_to_f88(float value) {
    // NaN has no representation in f8.8, and silently turning it into zero would mean
    // sending the boiler a setpoint of 0 degrees. Zero is chosen here deliberately as
    // "off", and the caller must not let it come to that; a NaN that reaches here is
    // already a defect further up the stack.
    if (!isfinite(value)) {
        return 0;
    }
    float scaled = value * 256.0f;
    if (scaled >  32767.0f) return 0x7FFF;
    if (scaled < -32768.0f) return 0x8000;
    return (uint16_t)(int16_t)lrintf(scaled);
}

int16_t ot_codec_s16(uint16_t raw) { return (int16_t)raw; }

uint8_t ot_codec_u8_hb(uint16_t raw) { return (uint8_t)(raw >> 8); }
uint8_t ot_codec_u8_lb(uint16_t raw) { return (uint8_t)(raw & 0xFFu); }
int8_t  ot_codec_s8_hb(uint16_t raw) { return (int8_t)(uint8_t)(raw >> 8); }
int8_t  ot_codec_s8_lb(uint16_t raw) { return (int8_t)(uint8_t)(raw & 0xFFu); }

bool ot_codec_flag(uint16_t raw, bool high_byte, uint8_t bit) {
    uint8_t byte = high_byte ? (uint8_t)(raw >> 8) : (uint8_t)(raw & 0xFFu);
    return ((byte >> (bit & 7u)) & 1u) != 0u;
}
