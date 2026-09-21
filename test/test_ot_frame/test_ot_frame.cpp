// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

extern "C" {
#include "ot_frame.h"
#include "ot_codec.h"
}

void setUp(void) {}
void tearDown(void) {}

// Parity: a correct word contains an even number of ones.
static void test_parity_zero_word_is_even(void) {
    TEST_ASSERT_TRUE(ot_frame_parity_ok(0x00000000u));
}

static void test_parity_single_bit_is_odd(void) {
    TEST_ASSERT_FALSE(ot_frame_parity_ok(0x00000001u));
}

static void test_parity_two_bits_is_even(void) {
    TEST_ASSERT_TRUE(ot_frame_parity_ok(0x00000003u));
}

static void test_parity_all_ones_is_even(void) {
    TEST_ASSERT_TRUE(ot_frame_parity_ok(0xFFFFFFFFu));
}

// Assembling READ-DATA for ID 0 with a zero value: zero ones, no parity needed.
static void test_encode_read_status_is_all_zero(void) {
    ot_frame_t f = { OT_MSG_READ_DATA, 0, 0 };
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, ot_frame_encode(&f));
}

// WRITE-DATA (001) for ID 1 with the value 0x1400 (20.0 in f8.8).
// Without parity the word is: 0001 0000 0000 0001 0001 0100 0000 0000 = 0x10011400.
// Ones in it: 1 + 1 + 1 + 1 = 4, even -- the parity bit stays zero.
static void test_encode_write_tset(void) {
    ot_frame_t f = { OT_MSG_WRITE_DATA, 1, 0x1400 };
    uint32_t raw = ot_frame_encode(&f);
    TEST_ASSERT_EQUAL_HEX32(0x10011400u, raw);
    TEST_ASSERT_TRUE(ot_frame_parity_ok(raw));
}

// A word with an odd number of ones must receive the parity bit.
static void test_encode_sets_parity_when_needed(void) {
    ot_frame_t f = { OT_MSG_READ_DATA, 1, 0 };   // without parity 0x00010000, one single one
    uint32_t raw = ot_frame_encode(&f);
    TEST_ASSERT_EQUAL_HEX32(0x80010000u, raw);
    TEST_ASSERT_TRUE(ot_frame_parity_ok(raw));
}

// SPARE is always zero, whatever the field in the structure was stuffed with.
static void test_encode_spare_is_always_zero(void) {
    ot_frame_t f = { OT_MSG_UNKNOWN_DATAID, 0xFF, 0xFFFF };
    uint32_t raw = ot_frame_encode(&f);
    TEST_ASSERT_EQUAL_HEX32(0u, raw & 0x0F000000u);
}

// A round trip over all message types and the edges of the values.
static void test_encode_decode_roundtrip(void) {
    for (int t = 0; t < 8; ++t) {
        const uint8_t  ids[]  = { 0, 1, 56, 127, 255 };
        const uint16_t vals[] = { 0x0000, 0x0001, 0x8000, 0x1400, 0xFFFF };
        for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
            for (unsigned j = 0; j < sizeof(vals) / sizeof(vals[0]); ++j) {
                ot_frame_t in = { (ot_msg_type_t)t, ids[i], vals[j] };
                ot_frame_t out = { OT_MSG_READ_DATA, 0, 0 };
                uint32_t raw = ot_frame_encode(&in);
                TEST_ASSERT_TRUE(ot_frame_parity_ok(raw));
                TEST_ASSERT_TRUE(ot_frame_decode(raw, &out));
                TEST_ASSERT_EQUAL_INT(in.type, out.type);
                TEST_ASSERT_EQUAL_UINT8(in.data_id, out.data_id);
                TEST_ASSERT_EQUAL_HEX16(in.data_value, out.data_value);
            }
        }
    }
}

// A single flipped bit must be noticed.
static void test_decode_rejects_flipped_bit(void) {
    ot_frame_t f = { OT_MSG_READ_ACK, 25, 0x2A80 };
    uint32_t raw = ot_frame_encode(&f);
    ot_frame_t out;
    TEST_ASSERT_FALSE(ot_frame_decode(raw ^ 0x00000040u, &out));
}

// On a wrong parity out is left untouched.
static void test_decode_leaves_out_untouched_on_failure(void) {
    ot_frame_t out = { OT_MSG_WRITE_ACK, 42, 0xBEEF };
    TEST_ASSERT_FALSE(ot_frame_decode(0x00000001u, &out));
    TEST_ASSERT_EQUAL_INT(OT_MSG_WRITE_ACK, out.type);
    TEST_ASSERT_EQUAL_UINT8(42, out.data_id);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, out.data_value);
}

// The reserved type 3 does not turn into an error -- it is carried upwards.
static void test_decode_passes_reserved_type_through(void) {
    ot_frame_t f = { OT_MSG_RESERVED, 7, 0 };
    ot_frame_t out;
    TEST_ASSERT_TRUE(ot_frame_decode(ot_frame_encode(&f), &out));
    TEST_ASSERT_EQUAL_INT(OT_MSG_RESERVED, out.type);
}

// f8.8 is SIGNED fixed point, the divisor is 256.
static void test_f88_positive(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f,  ot_codec_f88_to_float(0x1400));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.5f,  ot_codec_f88_to_float(0x2A80));
}

// A negative outside temperature is exactly the place where an unsigned parse silently
// gives +255.996 instead of -0.004 and the thermometer in Home Assistant lies in winter.
static void test_f88_negative(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f,  ot_codec_f88_to_float(0xFF00));
    TEST_ASSERT_FLOAT_WITHIN(0.01f,  -12.5f, ot_codec_f88_to_float(0xF380));
}

static void test_f88_roundtrip(void) {
    const float xs[] = { 0.0f, 0.5f, 20.0f, 65.5f, -0.5f, -20.0f, 100.0f };
    for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i) {
        uint16_t raw = ot_codec_float_to_f88(xs[i]);
        TEST_ASSERT_FLOAT_WITHIN(0.004f, xs[i], ot_codec_f88_to_float(raw));
    }
}

// Saturation instead of overflow: 300 degrees must not turn into a negative value.
static void test_f88_saturates(void) {
    TEST_ASSERT_EQUAL_HEX16(0x7FFF, ot_codec_float_to_f88(1000.0f));
    TEST_ASSERT_EQUAL_HEX16(0x8000, ot_codec_float_to_f88(-1000.0f));
}

static void test_s16(void) {
    TEST_ASSERT_EQUAL_INT16(0,     ot_codec_s16(0x0000));
    TEST_ASSERT_EQUAL_INT16(-1,    ot_codec_s16(0xFFFF));
    TEST_ASSERT_EQUAL_INT16(32767, ot_codec_s16(0x7FFF));
}

static void test_bytes(void) {
    TEST_ASSERT_EQUAL_UINT8(0x12, ot_codec_u8_hb(0x1234));
    TEST_ASSERT_EQUAL_UINT8(0x34, ot_codec_u8_lb(0x1234));
    TEST_ASSERT_EQUAL_INT8(-1,    ot_codec_s8_hb(0xFF00));
    TEST_ASSERT_EQUAL_INT8(-2,    ot_codec_s8_lb(0x00FE));
}

// The flags of ID 0. The low byte of the slave's response: bit 0 -- fault,
// bit 1 -- central heating mode, bit 2 -- DHW mode, bit 3 -- flame.
static void test_flags(void) {
    const uint16_t status = 0x000A;   // low byte 0000 1010: bits 1 and 3
    TEST_ASSERT_FALSE(ot_codec_flag(status, false, 0));
    TEST_ASSERT_TRUE (ot_codec_flag(status, false, 1));
    TEST_ASSERT_FALSE(ot_codec_flag(status, false, 2));
    TEST_ASSERT_TRUE (ot_codec_flag(status, false, 3));
    TEST_ASSERT_FALSE(ot_codec_flag(status, true,  1));
}

static void test_flags_high_byte(void) {
    const uint16_t master = 0x0300;   // high byte 0000 0011: bits 0 and 1
    TEST_ASSERT_TRUE (ot_codec_flag(master, true, 0));
    TEST_ASSERT_TRUE (ot_codec_flag(master, true, 1));
    TEST_ASSERT_FALSE(ot_codec_flag(master, true, 2));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parity_zero_word_is_even);
    RUN_TEST(test_parity_single_bit_is_odd);
    RUN_TEST(test_parity_two_bits_is_even);
    RUN_TEST(test_parity_all_ones_is_even);
    RUN_TEST(test_encode_read_status_is_all_zero);
    RUN_TEST(test_encode_write_tset);
    RUN_TEST(test_encode_sets_parity_when_needed);
    RUN_TEST(test_encode_spare_is_always_zero);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_decode_rejects_flipped_bit);
    RUN_TEST(test_decode_leaves_out_untouched_on_failure);
    RUN_TEST(test_decode_passes_reserved_type_through);
    RUN_TEST(test_f88_positive);
    RUN_TEST(test_f88_negative);
    RUN_TEST(test_f88_roundtrip);
    RUN_TEST(test_f88_saturates);
    RUN_TEST(test_s16);
    RUN_TEST(test_bytes);
    RUN_TEST(test_flags);
    RUN_TEST(test_flags_high_byte);
    return UNITY_END();
}
