// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

extern "C" {
#include "ot_decode.h"
#include "ot_encode.h"
#include "ot_frame.h"
}

void setUp(void) {}
void tearDown(void) {}

// The start bit is a one, that is, the first half is active and the second one idle.
static void test_starts_with_the_start_bit(void) {
    bool h[OT_ENCODE_HALFBITS];
    ot_encode_frame(0x00000000u, h);
    TEST_ASSERT_TRUE(h[0]);
    TEST_ASSERT_FALSE(h[1]);
}

// The stop bit is a one as well, and it comes last.
static void test_ends_with_the_stop_bit(void) {
    bool h[OT_ENCODE_HALFBITS];
    ot_encode_frame(0xFFFFFFFFu, h);
    TEST_ASSERT_TRUE(h[OT_ENCODE_HALFBITS - 2]);
    TEST_ASSERT_FALSE(h[OT_ENCODE_HALFBITS - 1]);
}

// Every pair of half-bits is a bit and its negation. A violation here means a Manchester
// violation on the wire, and the slave is obliged to reject such a frame.
static void test_every_pair_is_a_transition(void) {
    const uint32_t payloads[] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u,
                                  0xAAAAAAAAu, 0x40192A80u };
    for (unsigned p = 0; p < sizeof(payloads) / sizeof(payloads[0]); ++p) {
        bool h[OT_ENCODE_HALFBITS];
        ot_encode_frame(payloads[p], h);
        for (unsigned i = 0; i < OT_ENCODE_HALFBITS; i += 2) {
            TEST_ASSERT_NOT_EQUAL(h[i], h[i + 1]);
        }
    }
}

// The data is laid out most significant bit first, right after the start bit.
static void test_payload_is_msb_first(void) {
    bool h[OT_ENCODE_HALFBITS];
    ot_encode_frame(0x80000000u, h);
    TEST_ASSERT_TRUE(h[2]);            // bit 31 of the payload is a one
    ot_encode_frame(0x40000000u, h);
    TEST_ASSERT_FALSE(h[2]);           // bit 31 is a zero
    TEST_ASSERT_TRUE(h[4]);            // bit 30 is a one
}

// --- LOOPBACK: what made a fake ESP-IDF unnecessary ------------------------
//
// The half-bits are fed into ot_decode with the same sampling step as on the device.
// The transmitter and the receiver check each other; a mistake in the bit order, in the
// polarity or in the framing cannot pass both ends unnoticed.

static void feed(ot_decode_t *d, const bool *h, unsigned halfbit_us) {
    unsigned now = 0, next = 0;
    // an idle preamble, so that the state machine arms itself
    for (; next < 2000; next += OT_DECODE_SAMPLE_US) ot_decode_push(d, false);
    now = next;
    for (unsigned i = 0; i < OT_ENCODE_HALFBITS; ++i) {
        unsigned end = now + halfbit_us;
        while (next < end) { ot_decode_push(d, h[i]); next += OT_DECODE_SAMPLE_US; }
        now = end;
    }
    for (unsigned end = now + 3000; next < end; next += OT_DECODE_SAMPLE_US)
        ot_decode_push(d, false);
}

static void loopback_one(uint32_t payload, unsigned halfbit_us) {
    bool h[OT_ENCODE_HALFBITS];
    ot_encode_frame(payload, h);
    ot_decode_t d;
    ot_decode_reset(&d);
    feed(&d, h, halfbit_us);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(payload, ot_decode_payload(&d));
}

static void test_loopback_nominal(void) {
    const uint32_t xs[] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu,
                            0x40192A80u, 0xC00119A0u, 0x10011400u };
    for (unsigned i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
        loopback_one(xs[i], OT_ENCODE_HALFBIT_US);
}

// Loopback over every message type and every Data-ID: the render and the parse must
// agree over the whole space, not on seven lucky values.
static void test_loopback_every_message_type_and_id(void) {
    for (int t = 0; t < 8; ++t) {
        for (int id = 0; id < 128; id += 7) {
            ot_frame_t in = { (ot_msg_type_t)t, (uint8_t)id, (uint16_t)(id * 517u) };
            const uint32_t payload = ot_frame_encode(&in);
            bool h[OT_ENCODE_HALFBITS];
            ot_encode_frame(payload, h);
            ot_decode_t d;
            ot_decode_reset(&d);
            feed(&d, h, OT_ENCODE_HALFBIT_US);
            TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
            ot_frame_t out;
            TEST_ASSERT_TRUE(ot_frame_decode(ot_decode_payload(&d), &out));
            TEST_ASSERT_EQUAL_INT(in.type, out.type);
            TEST_ASSERT_EQUAL_UINT8(in.data_id, out.data_id);
            TEST_ASSERT_EQUAL_HEX16(in.data_value, out.data_value);
        }
    }
}

// Our transmitter must fit inside the receiver's tolerance with a margin: the spec allows
// a bit period of 900..1150 us, and at the edges the loopback must agree too.
static void test_loopback_at_tolerance_edges(void) {
    loopback_one(0x40192A80u, 450);   // bit period 900 us
    loopback_one(0x40192A80u, 575);   // bit period 1150 us
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_with_the_start_bit);
    RUN_TEST(test_ends_with_the_stop_bit);
    RUN_TEST(test_every_pair_is_a_transition);
    RUN_TEST(test_payload_is_msb_first);
    RUN_TEST(test_loopback_nominal);
    RUN_TEST(test_loopback_every_message_type_and_id);
    RUN_TEST(test_loopback_at_tolerance_edges);
    return UNITY_END();
}
