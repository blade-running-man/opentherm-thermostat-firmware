// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

extern "C" {
#include "ot_bus_sched.h"
}

void setUp(void) {}
void tearDown(void) {}

static ot_bus_sched_t make(void) {
    static const uint8_t poll[] = { 25, 26, 28 };
    ot_bus_sched_t s;
    ot_bus_sched_init(&s, poll, 3);
    return s;
}

// The first conversation waits for nothing: the device has only just booted, and silence
// longer than five seconds is interpreted by the boiler as a demand for heat.
static void test_first_step_talks_immediately(void) {
    ot_bus_sched_t s = make();
    ot_bus_step_t st = ot_bus_sched_step(&s, 0);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
}

// ID 0 is mandatory EVERY cycle: it carries ch_enable and brings back the boiler flags.
static void test_status_first_and_every_cycle(void) {
    ot_bus_sched_t s = make();
    uint32_t t = 0;
    for (int cycle = 0; cycle < 4; ++cycle) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
        TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
        // the next slot is an element of the ring
        st = ot_bus_sched_step(&s, t);
        TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
        TEST_ASSERT_NOT_EQUAL(0, st.data_id);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
}

// The ring is walked in order and closes on itself.
static void test_poll_ring_wraps(void) {
    ot_bus_sched_t s = make();
    uint8_t seen[6];
    uint32_t t = 0;
    for (int i = 0; i < 6; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);   // ID 0
        ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
        st = ot_bus_sched_step(&s, t);                 // an element of the ring
        seen[i] = st.data_id;
        ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    }
    TEST_ASSERT_EQUAL_UINT8(25, seen[0]);
    TEST_ASSERT_EQUAL_UINT8(26, seen[1]);
    TEST_ASSERT_EQUAL_UINT8(28, seen[2]);
    TEST_ASSERT_EQUAL_UINT8(25, seen[3]);
}

// The pause between conversations is no shorter than 100 ms even when the period has expired.
static void test_min_gap_is_respected_after_a_slow_response(void) {
    ot_bus_sched_t s = make();
    ot_bus_step_t st = ot_bus_sched_step(&s, 0);
    // The slave answered on the 868th millisecond -- as slow as it gets, but legal.
    ot_bus_sched_done(&s, &st, 0, 868);
    st = ot_bus_sched_step(&s, 950);
    TEST_ASSERT_EQUAL_INT(OT_BUS_WAIT, st.verb);
    TEST_ASSERT_EQUAL_UINT32(18, st.delay_ms);   // 868 + 100 - 950
    st = ot_bus_sched_step(&s, 968);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
}

// And the 1150 ms deadline is still not violated: 968 < 1150.
static void test_worst_case_still_meets_the_deadline(void) {
    ot_bus_sched_t s = make();
    ot_bus_step_t st = ot_bus_sched_step(&s, 0);
    ot_bus_sched_done(&s, &st, 0, 868);
    st = ot_bus_sched_step(&s, 968);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
    TEST_ASSERT_FALSE(st.overdue);
}

// A fast response does not speed up the cadence: 950 ms from the START of the previous one.
static void test_fast_response_does_not_speed_the_cadence(void) {
    ot_bus_sched_t s = make();
    ot_bus_step_t st = ot_bus_sched_step(&s, 0);
    ot_bus_sched_done(&s, &st, 0, 60);
    TEST_ASSERT_EQUAL_INT(OT_BUS_WAIT, ot_bus_sched_step(&s, 200).verb);
    TEST_ASSERT_EQUAL_UINT32(750, ot_bus_sched_step(&s, 200).delay_ms);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, ot_bus_sched_step(&s, 950).verb);
}

// A pending write displaces an element of the ring, but NOT the mandatory ID 0.
static void test_write_takes_the_ring_slot_not_the_status_slot(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_write(&s, 1, 0x2A80);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
    TEST_ASSERT_FALSE(st.is_write);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;

    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_TRUE(st.is_write);
    TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x2A80, st.value);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;

    st = ot_bus_sched_step(&s, t);          // ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);          // the ring continued from 25
    TEST_ASSERT_FALSE(st.is_write);
    TEST_ASSERT_EQUAL_UINT8(25, st.data_id);
}

// A second write displaces the first unexecuted one: a stale setpoint is worse than a lost one.
static void test_second_write_replaces_the_first(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_write(&s, 1, 0x1400);
    ot_bus_sched_write(&s, 1, 0x2A80);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_TRUE(st.is_write);
    TEST_ASSERT_EQUAL_HEX16(0x2A80, st.value);
}

// A write occupies a content slot but does NOT SPEND a position in the ring: after it the
// ring continues from the element it was standing on, not from the one after.
//
// The difference from test_write_takes_the_ring_slot_not_the_status_slot: there the position
// is still at zero, and "continued from 25" is equally true both when the position did not
// move and when it was reset. Here the ring is moved to 26 in advance, so the assertion
// discriminates.
static void test_a_write_does_not_cost_a_ring_position(void) {
    ot_bus_sched_t s = make();
    uint32_t t = 0;

    ot_bus_step_t st = ot_bus_sched_step(&s, t);            // ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(25, st.data_id);                // the ring stands at 26
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;

    ot_bus_sched_write(&s, 2, 0x0000);

    st = ot_bus_sched_step(&s, t);                          // ID 0 -- the write does not touch it
    TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
    TEST_ASSERT_FALSE(st.is_write);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;

    st = ot_bus_sched_step(&s, t);                          // the write comes BEFORE the content one
    TEST_ASSERT_TRUE(st.is_write);
    TEST_ASSERT_EQUAL_UINT8(2, st.data_id);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;

    st = ot_bus_sched_step(&s, t);                          // ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(26, st.data_id);                // from the place it was standing on
}

// The master's introduction (ID 2, 124, 126) goes out as THREE writes, one at a time: the
// queue holds one, and the next would displace an unexecuted one. What is checked is exactly
// the form in which bus_task() executes it: the next write is queued only once the previous
// one has gone out, the sign of that being write_pending cleared after ot_bus_sched_done().
//
// The value of the test is in the last line: three writes cost the ring not a single
// position, so the polling starts from the first element, not the fourth.
static void test_three_writes_in_a_row_cost_the_ring_nothing(void) {
    static const uint8_t  ids[]    = { 2, 124, 126 };
    static const uint16_t values[] = { 0x0000, 0x0233, 0x0101 };

    ot_bus_sched_t s = make();
    uint32_t t = 0;
    unsigned queued = 0;
    uint8_t seen_id[8];
    bool    seen_w[8];

    for (unsigned i = 0; i < 8; ++i) {
        if (!s.write_pending && queued < 3) {
            ot_bus_sched_write(&s, ids[queued], values[queued]);
            queued++;
        }
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        while (st.verb == OT_BUS_WAIT) { t += st.delay_ms; st = ot_bus_sched_step(&s, t); }
        seen_id[i] = st.data_id;
        seen_w[i]  = st.is_write;
        ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    }

    // The status alternates, and the writes go out one at a time and in the order queued.
    const uint8_t expect_id[] = { 0, 2, 0, 124, 0, 126, 0, 25 };
    const bool    expect_w[]  = { false, true, false, true, false, true, false, false };
    for (unsigned i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT8(expect_id[i], seen_id[i]);
        TEST_ASSERT_EQUAL_INT(expect_w[i] ? 1 : 0, seen_w[i] ? 1 : 0);
    }
    TEST_ASSERT_EQUAL_UINT(3, queued);
}

// An empty ring is a legal configuration: ID 0 alone remains, and there will be no silence.
static void test_empty_poll_ring_still_talks(void) {
    ot_bus_sched_t s;
    ot_bus_sched_init(&s, NULL, 0);
    uint32_t t = 0;
    for (int i = 0; i < 4; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
        TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
}

// A missed deadline must be VISIBLE, not silently corrected.
static void test_overdue_is_reported(void) {
    ot_bus_sched_t s = make();
    ot_bus_step_t st = ot_bus_sched_step(&s, 0);
    ot_bus_sched_done(&s, &st, 0, 100);
    // The task overslept: the next step is asked for on the 2000th millisecond.
    st = ot_bus_sched_step(&s, 2000);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
    TEST_ASSERT_TRUE(st.overdue);
}

// The uint32 millisecond wraparound comes after 49 days and must be survived: the rule
// "nothing reboots" applies to time arithmetic too.
static void test_survives_millisecond_wraparound(void) {
    ot_bus_sched_t s = make();
    const uint32_t near_end = 0xFFFFFF00u;
    ot_bus_step_t st = ot_bus_sched_step(&s, near_end);
    ot_bus_sched_done(&s, &st, near_end, near_end + 100u);
    // 0xFFFFFF00 + 950 wraps around
    st = ot_bus_sched_step(&s, near_end + 950u);
    TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
    TEST_ASSERT_FALSE(st.overdue);
}


// --- the sweep over the Data-ID space -------------------------------------

// The sweep occupies the ring slot, and the mandatory status keeps going out every other time.
static void test_scan_takes_the_ring_slot_status_still_alternates(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 10, 12);
    uint32_t t = 0;
    uint8_t seen[6];
    for (int i = 0; i < 6; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        seen[i] = st.data_id;
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
    TEST_ASSERT_EQUAL_UINT8(0,  seen[0]);
    TEST_ASSERT_EQUAL_UINT8(10, seen[1]);
    TEST_ASSERT_EQUAL_UINT8(0,  seen[2]);
    TEST_ASSERT_EQUAL_UINT8(11, seen[3]);
    TEST_ASSERT_EQUAL_UINT8(0,  seen[4]);
    TEST_ASSERT_EQUAL_UINT8(12, seen[5]);
}

// On reaching the end of the range the sweep switches itself off and the poll ring returns.
static void test_scan_ends_and_the_ring_returns(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 10, 11);
    uint32_t t = 0;
    for (int i = 0; i < 4; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
    uint16_t done, total;
    ot_bus_sched_scan_progress(&s, &done, &total);
    TEST_ASSERT_EQUAL_UINT16(0, total);          // the sweep is finished
    ot_bus_step_t st = ot_bus_sched_step(&s, t); // the status
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(25, st.data_id);     // the first element of the ring
}

// A pending write outranks the sweep: diagnostics has no right to delay a setpoint.
static void test_write_wins_over_scan(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 10, 12);
    ot_bus_sched_write(&s, 1, 0x1400);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);   // the status
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_TRUE(st.is_write);
    TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);                 // the status
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(10, st.data_id);       // the sweep did not lose its place
}

// The sweep NEVER produces a write: the price of a mistake in the number is a changed boiler
// setting whose previous value nobody will ever learn.
static void test_scan_never_writes(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 0, 40);
    uint32_t t = 0;
    for (int i = 0; i < 60; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        TEST_ASSERT_FALSE(st.is_write);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
}

static void test_scan_progress_counts_up(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 0, 127);
    uint16_t done, total;
    ot_bus_sched_scan_progress(&s, &done, &total);
    TEST_ASSERT_EQUAL_UINT16(128, total);
    TEST_ASSERT_EQUAL_UINT16(0, done);
    uint32_t t = 0;
    for (int i = 0; i < 4; ++i) {
        ot_bus_step_t st = ot_bus_sched_step(&s, t);
        ot_bus_sched_done(&s, &st, t, t + 100);
        t += OT_BUS_PERIOD_MS;
    }
    ot_bus_sched_scan_progress(&s, &done, &total);
    TEST_ASSERT_EQUAL_UINT16(2, done);
}

// A reversed range is accepted rather than breaking the sweep.
static void test_scan_range_is_normalised(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_scan(&s, 20, 10);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);   // the status
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(10, st.data_id);
}

// --- an unsupported Data-ID dropping out of the ring ------------------------

static void test_a_disabled_id_leaves_the_ring(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26, 27 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    ot_bus_sched_disable(&s, 26);

    // Walk enough steps for the ring to turn several times, and make sure 26 never came up.
    uint32_t now = 0;
    for (int i = 0; i < 40; i++) {
        ot_bus_step_t step = ot_bus_sched_step(&s, now);
        while (step.verb == OT_BUS_WAIT) {
            now += step.delay_ms;
            step = ot_bus_sched_step(&s, now);
        }
        TEST_ASSERT_TRUE(step.data_id != 26);
        ot_bus_sched_done(&s, &step, now, now + 40);
        now += 40;
    }
}

static void test_disabling_an_id_that_is_not_in_the_ring_is_harmless(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    ot_bus_sched_disable(&s, 99);

    TEST_ASSERT_EQUAL_UINT8(2, s.poll_count);
}

// The degenerate case that makes this verb dangerous in the first place: the boiler supported
// NOTHING from the ring. The bus is obliged to keep the conversation going with the mandatory
// ID 0 -- the boiler turns silence from the master into a demand for heat, and "there is nothing to
// ask" is not a reason to fall silent.
static void test_the_bus_keeps_talking_when_every_content_id_is_gone(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    ot_bus_sched_disable(&s, 25);
    ot_bus_sched_disable(&s, 26);
    TEST_ASSERT_EQUAL_UINT8(0, s.poll_count);

    uint32_t now = 0;
    for (int i = 0; i < 10; i++) {
        ot_bus_step_t step = ot_bus_sched_step(&s, now);
        while (step.verb == OT_BUS_WAIT) {
            now += step.delay_ms;
            step = ot_bus_sched_step(&s, now);
        }
        TEST_ASSERT_EQUAL_UINT8(0, step.data_id);
        ot_bus_sched_done(&s, &step, now, now + 40);
        now += 40;
    }
}

// One content slot IN THE SAME ORDER in which bus_task() executes it: the status step, the
// ring step, ot_bus_sched_done() -- and only AFTER done does the caller learn from the response
// that the identifier is unsupported and call disable(). The order carries load here: if
// disable is called before done, the defect does not reproduce.
static uint8_t ring_slot(ot_bus_sched_t *s, uint32_t *now)
{
    ot_bus_step_t st = ot_bus_sched_step(s, *now);
    while (st.verb == OT_BUS_WAIT) { *now += st.delay_ms; st = ot_bus_sched_step(s, *now); }
    TEST_ASSERT_EQUAL_UINT8(0, st.data_id);              // the mandatory status
    ot_bus_sched_done(s, &st, *now, *now + 40); *now += 40;

    st = ot_bus_sched_step(s, *now);
    while (st.verb == OT_BUS_WAIT) { *now += st.delay_ms; st = ot_bus_sched_step(s, *now); }
    const uint8_t asked = st.data_id;
    ot_bus_sched_done(s, &st, *now, *now + 40); *now += 40;
    return asked;
}

// A defect found on live hardware (ESP32-C6, Intergas Kombi Kompakt HRE).
// By the time disable() is called the position in the ring has ALREADY been advanced by the
// done step, and compacting the array moves the successor of the departed one into a slot the
// position has already passed. The successor loses a whole turn of the ring -- about a minute.
static void test_the_successor_of_a_disabled_id_is_asked_next(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26, 27, 28 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    uint32_t now = 0;
    TEST_ASSERT_EQUAL_UINT8(25, ring_slot(&s, &now));
    TEST_ASSERT_EQUAL_UINT8(26, ring_slot(&s, &now));
    ot_bus_sched_disable(&s, 26);                        // the boiler answered unknown-dataid
    TEST_ASSERT_EQUAL_UINT8(27, ring_slot(&s, &now));    // the successor, not a turn later
}

// The dropping out of the LAST element of the ring: the position has already wrapped to zero,
// the compaction moves nothing, and the correction would be superfluous here. What is checked
// is that it did not fire.
static void test_disabling_the_last_ring_element_keeps_the_wrap(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26, 27 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    uint32_t now = 0;
    TEST_ASSERT_EQUAL_UINT8(25, ring_slot(&s, &now));
    TEST_ASSERT_EQUAL_UINT8(26, ring_slot(&s, &now));
    TEST_ASSERT_EQUAL_UINT8(27, ring_slot(&s, &now));
    ot_bus_sched_disable(&s, 27);
    TEST_ASSERT_EQUAL_UINT8(25, ring_slot(&s, &now));    // the start of the ring, nothing lost
    TEST_ASSERT_EQUAL_UINT8(26, ring_slot(&s, &now));
}

// The shape of an observation from hardware: several drop-outs in a row within one turn. The
// ring is a shortened list of real Data-IDs; the boiler declared 15, 28 and 49 unsupported, and
// it was exactly their successors 17, 33 and 56 that disappeared from the log for a turn.
static void test_no_supported_id_is_skipped_when_several_drop_out(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 15, 17, 28, 33, 49, 56 };
    ot_bus_sched_init(&s, poll, sizeof poll);

    static const uint8_t unsupported[] = { 15, 28, 49 };
    static const uint8_t expect[]      = { 15, 17, 28, 33, 49, 56 };

    uint32_t now = 0;
    for (unsigned i = 0; i < sizeof expect; i++) {
        const uint8_t asked = ring_slot(&s, &now);
        TEST_ASSERT_EQUAL_UINT8(expect[i], asked);
        for (unsigned u = 0; u < sizeof unsupported; u++)
            if (unsupported[u] == asked) ot_bus_sched_disable(&s, asked);
    }
    TEST_ASSERT_EQUAL_UINT8(3, s.poll_count);
    TEST_ASSERT_EQUAL_UINT8(17, ring_slot(&s, &now));    // the ring closed on what was left
}

static void test_disable_does_not_disturb_the_scan(void)
{
    ot_bus_sched_t s;
    const uint8_t poll[] = { 25, 26 };
    ot_bus_sched_init(&s, poll, sizeof poll);
    ot_bus_sched_scan(&s, 0, 10);

    ot_bus_sched_disable(&s, 25);

    uint16_t done = 0, total = 0;
    ot_bus_sched_scan_progress(&s, &done, &total);
    TEST_ASSERT_EQUAL_UINT16(11, total);
}

// The master status high byte. Until this point the ID 0 request went out with DATA-VALUE
// 0x0000, so the device had never once asked the boiler for heat.

// A booted device must not demand heat: the byte is zero until a caller sets it.
static void test_status_high_byte_is_zero_until_someone_sets_it(void) {
    ot_bus_sched_t s = make();
    TEST_ASSERT_EQUAL_HEX16(0x0000, ot_bus_sched_step(&s, 0).value);
}

// The bit positions are the OpenTherm master-status ones, and they are named so that no caller writes a bare 0x01.
static void test_status_bit_constants_follow_the_spec(void) {
    TEST_ASSERT_EQUAL_HEX8(0x01, OT_STATUS_CH_ENABLE);
    TEST_ASSERT_EQUAL_HEX8(0x02, OT_STATUS_DHW_ENABLE);
    TEST_ASSERT_EQUAL_HEX8(0x04, OT_STATUS_COOLING);
    TEST_ASSERT_EQUAL_HEX8(0x08, OT_STATUS_OTC_ACTIVE);
    TEST_ASSERT_EQUAL_HEX8(0x10, OT_STATUS_CH2_ENABLE);
}

// Branch one of two: the mandatory every-odd-step ID 0. The byte travels in the HIGH half
// of the request, the frame stays a READ-DATA (the protocol), and the value takes
// effect on the VERY NEXT ID 0 slot -- no latch, no cycle of delay.
static void test_status_rides_the_mandatory_id_0_slot(void) {
    ot_bus_sched_t s = make();
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);   // ID 0, nothing set yet
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    ot_bus_sched_set_status(&s, OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE);
    st = ot_bus_sched_step(&s, t);                 // a ring read, not ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x0300, st.value);
    TEST_ASSERT_FALSE(st.is_write);
    ot_bus_sched_set_status(&s, 0);                // lowering the bits is symmetric
    TEST_ASSERT_EQUAL_HEX16(0x0000, ot_bus_sched_step(&s, t).value);
}

// Branch two of two: with the ring empty the meaningful slot falls back to ID 0 as well.
// Leaving that branch at 0x0000 asks for heat every other conversation, and not one
// timing test would notice.
static void test_status_rides_the_empty_ring_fallback(void) {
    ot_bus_sched_t s;
    ot_bus_sched_init(&s, NULL, 0);
    ot_bus_sched_set_status(&s, OT_STATUS_CH_ENABLE);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);   // the odd-step ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);                 // the fallback ID 0
    TEST_ASSERT_EQUAL_UINT8(0, st.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x0100, st.value);
    TEST_ASSERT_FALSE(st.is_write);
}

// The byte belongs to ID 0 alone: a queued write carries the caller's own value, and
// stamping the status over it would send a flow setpoint of 0x03xx to the boiler.
static void test_status_does_not_touch_a_queued_write(void) {
    ot_bus_sched_t s = make();
    ot_bus_sched_set_status(&s, OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE);
    ot_bus_sched_write(&s, 1, 0x2A80);
    uint32_t t = 0;
    ot_bus_step_t st = ot_bus_sched_step(&s, t);   // ID 0
    ot_bus_sched_done(&s, &st, t, t + 100); t += OT_BUS_PERIOD_MS;
    st = ot_bus_sched_step(&s, t);
    TEST_ASSERT_TRUE(st.is_write);
    TEST_ASSERT_EQUAL_HEX16(0x2A80, st.value);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_first_step_talks_immediately);
    RUN_TEST(test_status_first_and_every_cycle);
    RUN_TEST(test_poll_ring_wraps);
    RUN_TEST(test_min_gap_is_respected_after_a_slow_response);
    RUN_TEST(test_worst_case_still_meets_the_deadline);
    RUN_TEST(test_fast_response_does_not_speed_the_cadence);
    RUN_TEST(test_write_takes_the_ring_slot_not_the_status_slot);
    RUN_TEST(test_second_write_replaces_the_first);
    RUN_TEST(test_a_write_does_not_cost_a_ring_position);
    RUN_TEST(test_three_writes_in_a_row_cost_the_ring_nothing);
    RUN_TEST(test_empty_poll_ring_still_talks);
    RUN_TEST(test_overdue_is_reported);
    RUN_TEST(test_survives_millisecond_wraparound);
    RUN_TEST(test_scan_takes_the_ring_slot_status_still_alternates);
    RUN_TEST(test_scan_ends_and_the_ring_returns);
    RUN_TEST(test_write_wins_over_scan);
    RUN_TEST(test_scan_never_writes);
    RUN_TEST(test_scan_progress_counts_up);
    RUN_TEST(test_scan_range_is_normalised);
    RUN_TEST(test_a_disabled_id_leaves_the_ring);
    RUN_TEST(test_disabling_an_id_that_is_not_in_the_ring_is_harmless);
    RUN_TEST(test_the_bus_keeps_talking_when_every_content_id_is_gone);
    RUN_TEST(test_the_successor_of_a_disabled_id_is_asked_next);
    RUN_TEST(test_disabling_the_last_ring_element_keeps_the_wrap);
    RUN_TEST(test_no_supported_id_is_skipped_when_several_drop_out);
    RUN_TEST(test_disable_does_not_disturb_the_scan);
    RUN_TEST(test_status_high_byte_is_zero_until_someone_sets_it);
    RUN_TEST(test_status_bit_constants_follow_the_spec);
    RUN_TEST(test_status_rides_the_mandatory_id_0_slot);
    RUN_TEST(test_status_rides_the_empty_ring_fallback);
    RUN_TEST(test_status_does_not_touch_a_queued_write);
    return UNITY_END();
}
