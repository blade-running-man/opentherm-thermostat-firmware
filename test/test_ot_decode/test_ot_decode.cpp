// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

extern "C" {
#include "ot_decode.h"
}

void setUp(void) {}
void tearDown(void) {}

// --- the sample stream generator ----------------------------------------------

// The samples are taken on an absolute time grid, not by integer division of the duration.
// DO NOT bring back the division `us / OT_DECODE_SAMPLE_US`: with it a half-bit of 550 us
// gives exactly the same five samples as 500, and the test for the upper edge of the
// tolerance silently stops checking anything. The accumulator, in contrast, gives an
// alternation of 5-6-5-6 -- exactly that fractional number of samples for whose sake the
// windows are widened by ±1.
static ot_decode_t *g_d;
static unsigned     g_now_us;    // the model time
static unsigned     g_next_us;   // the time of the next sample

static void gen_reset(ot_decode_t *d) { g_d = d; g_now_us = 0; g_next_us = 0; }

static void hold(bool level, unsigned us) {
    unsigned end = g_now_us + us;
    while (g_next_us < end) {
        ot_decode_push(g_d, level);
        g_next_us += OT_DECODE_SAMPLE_US;
    }
    g_now_us = end;
}

// Draws one frame in Manchester and feeds it to the state machine.
// Manchester: '1' is an active→idle transition in the middle of the bit, '0' is idle→active.
// So the first half of the bit equals the bit itself: active for 1, idle for 0.
static void push_frame_bits(uint64_t bits34, unsigned bit_us) {
    hold(false, 2000);                          // an idle preamble
    for (int i = OT_DECODE_BITS - 1; i >= 0; --i) {
        bool bit = (bits34 >> i) & 1u;
        hold(bit,  bit_us / 2);
        hold(!bit, bit_us - bit_us / 2);
    }
    hold(false, 2000);                          // an idle tail
}

static uint64_t wrap(uint32_t payload) {
    return (1ULL << 33) | ((uint64_t)payload << 1) | 1ULL;
}

static void push_frame(uint32_t payload, unsigned bit_us) {
    push_frame_bits(wrap(payload), bit_us);
}

// --- the tests ----------------------------------------------------------------

static void test_reset_starts_idle(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_IDLE, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_NONE, ot_decode_error(&d));
}

static void test_idle_line_stays_idle(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    hold(false, 50000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_IDLE, d.status);
}

static void test_nominal_frame_all_zero_payload(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x00000000u, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, ot_decode_payload(&d));
}

// The alternation 0101... is the case where there is NEVER a transition on a bit boundary.
static void test_alternating_payload(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x55555555u, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x55555555u, ot_decode_payload(&d));
}

// All ones is the case where there is a transition on the bit boundary EVERY time.
static void test_all_ones_payload(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0xFFFFFFFFu, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, ot_decode_payload(&d));
}

static void test_realistic_read_ack(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 1000);   // READ-ACK, ID 25, 42.5 in f8.8
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
}

// The tolerance edges: -10 % and +15 % must be accepted.
static void test_accepts_slow_edge_of_tolerance(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 1100);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
}

static void test_accepts_fast_edge_of_tolerance(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 900);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
}

// And this one is no longer a frame, and accepting it is worse than rejecting it.
static void test_rejects_far_too_slow(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 1600);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_TIMING, ot_decode_error(&d));
}

// 800 us per bit is only 11 % faster than the lower edge of the tolerance, and it is exactly
// for this case that the sampling step is 100 us and not 200. The half-bit gives 4 samples
// and lands in the window; what catches the stream is the full bit -- 8 samples, between
// HALF_MAX and FULL_MIN.
static void test_rejects_slightly_too_fast(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 800);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_TIMING, ot_decode_error(&d));
}

// 400 us per bit -- the half-bit is exactly two samples at any phase, and two is below
// OT_DECODE_HALF_MIN. DO NOT put 500 here: a half-bit of 250 us gives sometimes two samples
// and sometimes three, and three lands in the window -- the test would become
// non-deterministic.
static void test_rejects_far_too_fast(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 400);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_TIMING, ot_decode_error(&d));
}

static void test_rejects_zero_stop_bit(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame_bits((1ULL << 33) | (0x12345678ULL << 1) | 0ULL, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_STOP_BIT, ot_decode_error(&d));
}

// The slave began a frame and fell silent -- this is not "almost a frame", it is a failure.
static void test_rejects_truncated_frame(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    hold(false, 2000);
    for (int i = 0; i < 10; ++i) {          // ten bits and then silence
        bool bit = (i % 2) == 0;
        hold(bit,  500);
        hold(!bit, 500);
    }
    hold(false, 5000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_SILENCE, ot_decode_error(&d));
}

// A Manchester violation: a half-bit followed by another half-bit of the same level,
// that is, a transition that must not be there.
static void test_rejects_manchester_violation(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    hold(false, 2000);
    hold(true,  500);    // the start bit, first half
    hold(false, 500);    // the start bit, middle -- bit 1
    hold(true,  200);    // a transition after 200 us -- outside both windows
    hold(false, 500);
    hold(false, 3000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERROR, d.status);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_ERR_TIMING, ot_decode_error(&d));
}

// A reset while the line is active must not take it for the beginning of a frame.
static void test_reset_while_line_active(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    hold(true, 3000);              // the line is already active
    TEST_ASSERT_EQUAL_INT(OT_DECODE_IDLE, d.status);
    push_frame(0x40192A80u, 1000);       // and now an honest frame
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
}

// After DONE the state machine is frozen until a reset.
static void test_done_is_sticky(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    push_frame(0x00000000u, 1000);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
}

// Two frames in a row with a reset in between -- the ordinary work of ot_bus.
static void test_two_frames_with_reset(void) {
    ot_decode_t d;
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0x40192A80u, 1000);
    TEST_ASSERT_EQUAL_HEX32(0x40192A80u, ot_decode_payload(&d));
    ot_decode_reset(&d);
    gen_reset(&d);
    push_frame(0xC00119A0u, 1000);
    TEST_ASSERT_EQUAL_INT(OT_DECODE_DONE, d.status);
    TEST_ASSERT_EQUAL_HEX32(0xC00119A0u, ot_decode_payload(&d));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_starts_idle);
    RUN_TEST(test_idle_line_stays_idle);
    RUN_TEST(test_nominal_frame_all_zero_payload);
    RUN_TEST(test_alternating_payload);
    RUN_TEST(test_all_ones_payload);
    RUN_TEST(test_realistic_read_ack);
    RUN_TEST(test_accepts_slow_edge_of_tolerance);
    RUN_TEST(test_accepts_fast_edge_of_tolerance);
    RUN_TEST(test_rejects_far_too_slow);
    RUN_TEST(test_rejects_slightly_too_fast);
    RUN_TEST(test_rejects_far_too_fast);
    RUN_TEST(test_rejects_zero_stop_bit);
    RUN_TEST(test_rejects_truncated_frame);
    RUN_TEST(test_rejects_manchester_violation);
    RUN_TEST(test_reset_while_line_active);
    RUN_TEST(test_done_is_sticky);
    RUN_TEST(test_two_frames_with_reset);
    return UNITY_END();
}
