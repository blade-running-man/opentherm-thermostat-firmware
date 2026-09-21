// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <math.h>

extern "C" {
#include "ot_sensor.h"
}

void setUp(void) {}
void tearDown(void) {}

#define STALE_AFTER_MS 900000u   // 15 minutes, the default STALE deadline

static ot_sensor_t make(void) {
    ot_sensor_t s;
    ot_sensor_init(&s, STALE_AFTER_MS);
    return s;
}

// --- the three states -------------------------------------------------------

// NEVER is a state of its own and not a synonym for STALE: a freshly flashed device has
// never had a measurement, and the loop tells the two apart in the panel.
static void test_never_before_any_value(void) {
    ot_sensor_t s = make();
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_NEVER, ot_sensor_state(&s));
    TEST_ASSERT_TRUE(isnan(ot_sensor_value(&s)));
    ot_sensor_tick(&s, 5000);
    ot_sensor_tick(&s, 10000);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_NEVER, ot_sensor_state(&s));
    TEST_ASSERT_TRUE(isnan(ot_sensor_value(&s)));
}

static void test_never_to_fresh(void) {
    ot_sensor_t s = make();
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 21.4f, 1000));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_FRESH, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_FLOAT(21.4f, ot_sensor_value(&s));
    TEST_ASSERT_EQUAL_UINT32(0, ot_sensor_overdue_ms(&s));
}

// The boundary is closed on the STALE side: overdue == stale_after is already stale.
static void test_fresh_to_stale_on_the_deadline(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    ot_sensor_tick(&s, 1000 + STALE_AFTER_MS - 1u);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_FRESH, ot_sensor_state(&s));
    ot_sensor_tick(&s, 1000 + STALE_AFTER_MS);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(STALE_AFTER_MS, ot_sensor_overdue_ms(&s));
}

// The last accepted value survives going stale: the loop decides what to do with it,
// ot_sensor does not erase it.
static void test_stale_keeps_the_last_value(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 19.5f, 1000);
    ot_sensor_tick(&s, 1000 + STALE_AFTER_MS * 3u);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_FLOAT(19.5f, ot_sensor_value(&s));
}

static void test_stale_to_fresh(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    ot_sensor_tick(&s, 1000 + STALE_AFTER_MS * 2u);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));

    TEST_ASSERT_TRUE(ot_sensor_update(&s, 22.0f, 1000 + STALE_AFTER_MS * 2u + 10u));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_FRESH, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(0, ot_sensor_overdue_ms(&s));
    TEST_ASSERT_EQUAL_FLOAT(22.0f, ot_sensor_value(&s));
}

// --- the outlier filter -----------------------------------------------------

// The two values a real DS18B20 emitted in someone else's tracker (prior-art-diyless).
static void test_the_ds18b20_pair_is_rejected(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 85.0f, 2000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, -127.0f, 3000));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, ot_sensor_value(&s));
}

// Out of range is rejected even with nothing to compare against -- the range check does
// not depend on a previous value, so a device that has just booted is protected too.
static void test_out_of_range_rejected_from_never(void) {
    ot_sensor_t s = make();
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 85.0f, 1000));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_NEVER, ot_sensor_state(&s));
    TEST_ASSERT_TRUE(isnan(ot_sensor_value(&s)));
}

static void test_range_bounds_are_inclusive(void) {
    ot_sensor_t s = make();
    TEST_ASSERT_TRUE(ot_sensor_update(&s, -40.0f, 1000));
    ot_sensor_t t = make();
    TEST_ASSERT_TRUE(ot_sensor_update(&t, 60.0f, 1000));

    ot_sensor_t u = make();
    TEST_ASSERT_FALSE(ot_sensor_update(&u, -40.1f, 1000));
    TEST_ASSERT_FALSE(ot_sensor_update(&u, 60.1f, 1000));
}

// An out-of-range value must never be let through by the jump escape hatch: four +85 in
// a row are still four rejections. The escape exists for a sensor that MOVED, not for a
// sensor that is broken.
static void test_the_escape_hatch_does_not_open_for_out_of_range(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    for (int i = 0; i < 6; ++i)
        TEST_ASSERT_FALSE(ot_sensor_update(&s, 85.0f, 2000u + (uint32_t)i * 1000u));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, ot_sensor_value(&s));
}

// The claim of the header, and the half the test above cannot make: an out-of-range
// rejection NEVER ADVANCES THE STREAK, "so no run of +85 can ever reach the escape hatch".
// Three +85 followed by an in-range value 9 K away -- if the range check had counted, the
// streak would stand at three and this fourth value would be let through as "the sensor
// was moved". It is a jump like any other and the counter is still at zero, so it is not.
static void test_out_of_range_rejections_do_not_arm_the_escape_hatch(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    for (int i = 0; i < 3; ++i)
        TEST_ASSERT_FALSE(ot_sensor_update(&s, 85.0f, 2000u + (uint32_t)i * 1000u));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 6000));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, ot_sensor_value(&s));
    // ... and the escape hatch itself still works from there: three of its own rejections.
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 7000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 8000));
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 30.0f, 9000));
}

// MQTT will bring worse than a broken one-wire bus: a payload that parses to NaN.
static void test_nan_and_infinity_rejected(void) {
    ot_sensor_t s = make();
    TEST_ASSERT_FALSE(ot_sensor_update(&s, NAN, 1000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, INFINITY, 2000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, -INFINITY, 3000));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_NEVER, ot_sensor_state(&s));
}

static void test_a_jump_over_five_degrees_is_rejected(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 27.0f, 2000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 15.0f, 3000));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, ot_sensor_value(&s));
    // Exactly five degrees is not a jump: the bound is "more than".
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 26.0f, 4000));
}

// The escape hatch. Without it a relocated or replaced sensor locks the loop out
// permanently, which is a worse failure than the one being filtered.
static void test_the_fourth_jump_in_a_row_is_accepted(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 2000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.1f, 3000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.2f, 4000));
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 30.3f, 5000));
    TEST_ASSERT_EQUAL_FLOAT(30.3f, ot_sensor_value(&s));
    TEST_ASSERT_EQUAL_UINT32(0, ot_sensor_overdue_ms(&s));
}

// "Three IN A ROW": one accepted value in between and the counting starts over, so a
// sensor that merely twitches never accumulates its way to an accepted outlier.
static void test_an_accepted_value_resets_the_reject_counter(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 2000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 3000));
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 21.2f, 4000));      // a normal reading

    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 5000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 6000));
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 30.0f, 7000));
    TEST_ASSERT_TRUE(ot_sensor_update(&s, 30.0f, 8000));
}

// A rejected value is not a measurement: it must not push the deadline forward, or a
// sensor stuck on +85 would read as FRESH for ever.
static void test_a_rejected_value_does_not_refresh(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    ot_sensor_update(&s, 85.0f, 1000 + STALE_AFTER_MS / 2u);
    ot_sensor_update(&s, 85.0f, 1000 + STALE_AFTER_MS);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(STALE_AFTER_MS, ot_sensor_overdue_ms(&s));
}

// The same rule down the NaN path, which the test above cannot reach: it uses 85.0, and
// the range check rejects that before the NaN guard is ever consulted. An MQTT payload
// that parses to NaN every minute -- an empty retained message, a sensor publishing
// "unknown" -- must not keep the sensor FRESH for ever.
static void test_a_nan_does_not_refresh_the_deadline(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    for (uint32_t t = 60000u; t <= STALE_AFTER_MS; t += 60000u)
        TEST_ASSERT_FALSE(ot_sensor_update(&s, NAN, 1000 + t));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(STALE_AFTER_MS, ot_sensor_overdue_ms(&s));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, ot_sensor_value(&s));
}

// --- time ------------------------------------------------------------------

// An update carries a timestamp, so it is a tick as well: the loop is not obliged to
// call both, and calling both must not count the interval twice.
static void test_update_is_also_a_tick(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 1000);
    TEST_ASSERT_FALSE(ot_sensor_update(&s, 85.0f, 61000));    // rejected, but time moved
    TEST_ASSERT_EQUAL_UINT32(60000, ot_sensor_overdue_ms(&s));
    ot_sensor_tick(&s, 61000);                                 // the same instant again
    TEST_ASSERT_EQUAL_UINT32(60000, ot_sensor_overdue_ms(&s));
}

// THE accumulator test. Overdue time is ACCUMULATED, never computed as now - last_ok: uint32
// milliseconds wrap after 49.7 days, the hard STALE limit is 24 hours, and a naive
// subtraction across the wrap reads as "the sensor is fresh" -- in January, with nobody
// watching. The clock here is driven straight through UINT32_MAX.
static void test_overdue_accumulates_across_the_wrap(void) {
    ot_sensor_t s = make();
    const uint32_t start = 0xFFFF0000u;
    ot_sensor_update(&s, 21.0f, start);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_FRESH, ot_sensor_state(&s));

    // 30 hours of ticking, one minute at a time, straight over the wrap.
    uint32_t now = start;
    for (unsigned i = 0; i < 30u * 60u; ++i) {
        now += 60000u;
        ot_sensor_tick(&s, now);
    }
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(30u * 3600u * 1000u, ot_sensor_overdue_ms(&s));
    // and it is past the 24 h hard limit, which is the whole point
    TEST_ASSERT_TRUE(ot_sensor_overdue_ms(&s) >= 86400000u);
}

// An update that lands after the wrap is still an update: the accumulator is cleared,
// not recomputed from a comparison of two moments on opposite sides of the wrap.
static void test_an_update_across_the_wrap_clears_the_accumulator(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 0xFFFF0000u);
    for (unsigned i = 0; i < 40u; ++i) ot_sensor_tick(&s, 0xFFFF0000u + (i + 1u) * 60000u);
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));

    TEST_ASSERT_TRUE(ot_sensor_update(&s, 21.5f, 0xFFFF0000u + 41u * 60000u));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_FRESH, ot_sensor_state(&s));
    TEST_ASSERT_EQUAL_UINT32(0, ot_sensor_overdue_ms(&s));
}

// The accumulator saturates instead of wrapping: a sensor absent for 60 days must not
// come back round to "fresh".
static void test_the_accumulator_saturates(void) {
    ot_sensor_t s = make();
    ot_sensor_update(&s, 21.0f, 0);
    uint32_t now = 0;
    for (unsigned i = 0; i < 100u; ++i) {   // 100 * ~14 days
        now += 0x4C4B4000u;                 // 1 280 000 000 ms, ~14.8 days
        ot_sensor_tick(&s, now);
    }
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, ot_sensor_overdue_ms(&s));
    TEST_ASSERT_EQUAL_INT(OT_SENSOR_STALE, ot_sensor_state(&s));
}

// The first tick after init only seeds the reference moment: init takes no timestamp,
// so there is nothing to measure the first interval from.
static void test_the_first_tick_seeds_the_reference(void) {
    ot_sensor_t s = make();
    ot_sensor_tick(&s, 5000000u);
    TEST_ASSERT_EQUAL_UINT32(0, ot_sensor_overdue_ms(&s));
    ot_sensor_tick(&s, 5001000u);
    TEST_ASSERT_EQUAL_UINT32(1000, ot_sensor_overdue_ms(&s));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_never_before_any_value);
    RUN_TEST(test_never_to_fresh);
    RUN_TEST(test_fresh_to_stale_on_the_deadline);
    RUN_TEST(test_stale_keeps_the_last_value);
    RUN_TEST(test_stale_to_fresh);
    RUN_TEST(test_the_ds18b20_pair_is_rejected);
    RUN_TEST(test_out_of_range_rejected_from_never);
    RUN_TEST(test_range_bounds_are_inclusive);
    RUN_TEST(test_the_escape_hatch_does_not_open_for_out_of_range);
    RUN_TEST(test_out_of_range_rejections_do_not_arm_the_escape_hatch);
    RUN_TEST(test_nan_and_infinity_rejected);
    RUN_TEST(test_a_jump_over_five_degrees_is_rejected);
    RUN_TEST(test_the_fourth_jump_in_a_row_is_accepted);
    RUN_TEST(test_an_accepted_value_resets_the_reject_counter);
    RUN_TEST(test_a_rejected_value_does_not_refresh);
    RUN_TEST(test_a_nan_does_not_refresh_the_deadline);
    RUN_TEST(test_update_is_also_a_tick);
    RUN_TEST(test_overdue_accumulates_across_the_wrap);
    RUN_TEST(test_an_update_across_the_wrap_clears_the_accumulator);
    RUN_TEST(test_the_accumulator_saturates);
    RUN_TEST(test_the_first_tick_seeds_the_reference);
    return UNITY_END();
}
