// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The pure, framework-free half of the DS18B20 read: the Dallas/Maxim CRC-8 and the
// temperature decode. It touches no GPIO, no RMT and no ESP-IDF header, so it is compiled
// and tested on the host (test_ot_onewire) exactly as the device runs it -- the timing-
// critical 1-Wire transport lives in ot_onewire.c, which is NOT host-built.
//
// DO NOT add an ESP-IDF include here or a REQUIRES to its CMakeLists: the moment this
// component reaches driver/ or esp_timer.h, PlatformIO -- which compiles ALL the sources of
// a library it pulls in -- can no longer link the host suite (the ot_sensor rake).

#ifdef __cplusplus
extern "C" {
#endif

// Dallas/Maxim CRC-8, polynomial x^8 + x^5 + x^4 + 1 taken in its reflected form 0x8C, init
// 0x00. This is the CRC the DS18B20 puts in scratchpad byte 8 (over bytes 0..7) and in ROM
// byte 7 (over bytes 0..6); computing the CRC over the whole nine/eight bytes yields 0 when
// the frame is intact. `len` bytes starting at `data`.
uint8_t ot_onewire_crc8(const uint8_t *data, int len);

// True when the 9-byte scratchpad is self-consistent: byte 8 equals the CRC-8 of bytes 0..7.
// The single guard against a half-read frame; a false here means "no trustworthy value",
// never "the sensor read 0 C".
bool ot_onewire_scratchpad_crc_ok(const uint8_t scratchpad[9]);

// The raw signed temperature register: scratchpad byte 0 (LSB) and byte 1 (MSB) as a 16-bit
// two's-complement count in units of 1/16 C. Exact and float-free, so the negative and edge
// cases (-55 C .. +125 C) are pinned on the host without a delta. lsb/msb passed directly so
// a test needs no scratchpad array.
int16_t ot_onewire_temp_raw(uint8_t lsb, uint8_t msb);

// The scratchpad temperature in degrees Celsius: ot_onewire_temp_raw(byte0, byte1) / 16.0.
// The only place a float appears; callers that log "%.2f C" use this.
float ot_onewire_temp_c(const uint8_t scratchpad[9]);

#ifdef __cplusplus
}
#endif
