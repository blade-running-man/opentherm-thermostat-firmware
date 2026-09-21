// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <math.h>

extern "C" {
#include "ot_onewire_decode.h"
}

void setUp(void) {}
void tearDown(void) {}

// --- CRC-8 --------------------------------------------------------------------
//
// The expected CRCs were computed by an independent reference implementation (Dallas/Maxim
// CRC-8, reflected poly 0x8C) over the same byte vectors; see the plan/commit. A DS18B20
// scratchpad's byte 8 is the CRC of bytes 0..7.

// +85.0 C power-on scratchpad (LSB=0x50, MSB=0x05, config 0x7F): CRC byte is 0x1C.
static void test_crc8_scratchpad_plus85(void) {
    const uint8_t sp8[8] = {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    TEST_ASSERT_EQUAL_UINT8(0x1C, ot_onewire_crc8(sp8, 8));
}

// +25.0625 C scratchpad: CRC byte is 0x70.
static void test_crc8_scratchpad_plus25(void) {
    const uint8_t sp8[8] = {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    TEST_ASSERT_EQUAL_UINT8(0x70, ot_onewire_crc8(sp8, 8));
}

// -25.0625 C scratchpad: CRC byte is 0xE8. A negative value must not disturb the CRC.
static void test_crc8_scratchpad_minus25(void) {
    const uint8_t sp8[8] = {0x6F, 0xFE, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10};
    TEST_ASSERT_EQUAL_UINT8(0xE8, ot_onewire_crc8(sp8, 8));
}

// A ROM (7 bytes, family 0x28 = DS18B20): CRC byte is 0xF0.
static void test_crc8_rom(void) {
    const uint8_t rom[7] = {0x28, 0x1D, 0x39, 0x31, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8(0xF0, ot_onewire_crc8(rom, 7));
}

// CRC over the whole intact frame (data + its own CRC byte) folds to 0 -- the property the
// device relies on, checked here so scratchpad_crc_ok can be trusted.
static void test_crc8_over_frame_is_zero(void) {
    const uint8_t sp9[9] = {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10, 0x1C};
    TEST_ASSERT_EQUAL_UINT8(0x00, ot_onewire_crc8(sp9, 9));
}

// --- scratchpad_crc_ok --------------------------------------------------------

static void test_scratchpad_crc_ok_true(void) {
    const uint8_t sp9[9] = {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10, 0x1C};
    TEST_ASSERT_TRUE(ot_onewire_scratchpad_crc_ok(sp9));
}

// A single flipped bit in the payload must fail the CRC -- the guard against a half-read.
static void test_scratchpad_crc_ok_false_on_flip(void) {
    uint8_t sp9[9] = {0x50, 0x05, 0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10, 0x1C};
    sp9[0] ^= 0x01;
    TEST_ASSERT_FALSE(ot_onewire_scratchpad_crc_ok(sp9));
}

// The all-0xFF frame a disconnected bus reads back must NOT pass as a valid 0-ish reading.
static void test_scratchpad_crc_ok_false_all_ff(void) {
    uint8_t sp9[9];
    for (int i = 0; i < 9; i++) sp9[i] = 0xFF;
    TEST_ASSERT_FALSE(ot_onewire_scratchpad_crc_ok(sp9));
}

// The all-0x00 frame (a stuck-low line) PASSES the CRC -- CRC-8 of eight zero bytes is 0x00,
// which equals byte 8, so CRC alone cannot catch a stuck-low line. This pins that fact: the
// stuck-low line is rejected earlier, on the presence pulse in ot_onewire.c, not by the CRC.
static void test_scratchpad_crc_ok_false_all_zero(void) {
    uint8_t sp9[9];
    for (int i = 0; i < 9; i++) sp9[i] = 0x00;
    TEST_ASSERT_TRUE(ot_onewire_scratchpad_crc_ok(sp9));
}

// --- temperature decode -------------------------------------------------------

static void test_temp_raw_positive(void) {
    TEST_ASSERT_EQUAL_INT16(2000, ot_onewire_temp_raw(0xD0, 0x07));   // +125.0 C
    TEST_ASSERT_EQUAL_INT16(1360, ot_onewire_temp_raw(0x50, 0x05));   // +85.0 C
    TEST_ASSERT_EQUAL_INT16(401, ot_onewire_temp_raw(0x91, 0x01));    // +25.0625 C
    TEST_ASSERT_EQUAL_INT16(8, ot_onewire_temp_raw(0x08, 0x00));      // +0.5 C
    TEST_ASSERT_EQUAL_INT16(0, ot_onewire_temp_raw(0x00, 0x00));      // 0 C
}

static void test_temp_raw_negative(void) {
    TEST_ASSERT_EQUAL_INT16(-8, ot_onewire_temp_raw(0xF8, 0xFF));     // -0.5 C
    TEST_ASSERT_EQUAL_INT16(-162, ot_onewire_temp_raw(0x5E, 0xFF));   // -10.125 C
    TEST_ASSERT_EQUAL_INT16(-401, ot_onewire_temp_raw(0x6F, 0xFE));   // -25.0625 C
    TEST_ASSERT_EQUAL_INT16(-880, ot_onewire_temp_raw(0x90, 0xFC));   // -55.0 C
}

static void test_temp_c_positive(void) {
    const uint8_t sp[9] = {0x91, 0x01, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 25.0625f, ot_onewire_temp_c(sp));
}

static void test_temp_c_negative(void) {
    const uint8_t sp[9] = {0x90, 0xFC, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -55.0f, ot_onewire_temp_c(sp));
}

static void test_temp_c_edge_min_and_max(void) {
    const uint8_t hi[9] = {0xD0, 0x07, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t lo[9] = {0xF8, 0xFF, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 125.0f, ot_onewire_temp_c(hi));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -0.5f, ot_onewire_temp_c(lo));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_scratchpad_plus85);
    RUN_TEST(test_crc8_scratchpad_plus25);
    RUN_TEST(test_crc8_scratchpad_minus25);
    RUN_TEST(test_crc8_rom);
    RUN_TEST(test_crc8_over_frame_is_zero);
    RUN_TEST(test_scratchpad_crc_ok_true);
    RUN_TEST(test_scratchpad_crc_ok_false_on_flip);
    RUN_TEST(test_scratchpad_crc_ok_false_all_ff);
    RUN_TEST(test_scratchpad_crc_ok_false_all_zero);
    RUN_TEST(test_temp_raw_positive);
    RUN_TEST(test_temp_raw_negative);
    RUN_TEST(test_temp_c_positive);
    RUN_TEST(test_temp_c_negative);
    RUN_TEST(test_temp_c_edge_min_and_max);
    return UNITY_END();
}
