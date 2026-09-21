// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <unity.h>

#include "ot_registry.h"
#include "ot_state.h"

void setUp(void) { ot_state_reset(); }
void tearDown(void) {}

static void test_a_fresh_state_knows_nothing(void)
{
    ot_value_t v;
    TEST_ASSERT_TRUE(ot_state_get("flow_temperature", &v));
    TEST_ASSERT_EQUAL(OT_AVAIL_UNKNOWN, v.availability);
}

static void test_read_ack_lands_decoded(void)
{
    // 0x2B00 is 43.00 in f8.8, the way a live boiler answered on ID 25.
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);

    ot_value_t v;
    TEST_ASSERT_TRUE(ot_state_get("flow_temperature", &v));
    TEST_ASSERT_EQUAL(OT_AVAIL_OK, v.availability);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 43.0f, v.number);
    TEST_ASSERT_EQUAL_UINT32(1000, v.updated_ms);
}

// The main danger here: DATA-INVALID and UNKNOWN-DATAID
// both arrive with the value 0x0000. Merging them into one means showing "it is 0 °C
// outside" where there is no outside sensor at all.
static void test_data_invalid_is_not_a_value(void)
{
    ot_state_apply_dataid(27, OT_MSG_DATA_INVALID, 0x0000, 1000);

    ot_value_t v;
    TEST_ASSERT_TRUE(ot_state_get("outside_temperature", &v));
    TEST_ASSERT_EQUAL(OT_AVAIL_INVALID, v.availability);
    TEST_ASSERT_TRUE(isnan(v.number));
}

static void test_unknown_dataid_is_not_data_invalid(void)
{
    ot_state_apply_dataid(27, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(27, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);

    ot_value_t v;
    TEST_ASSERT_TRUE(ot_state_get("outside_temperature", &v));
    TEST_ASSERT_EQUAL(OT_AVAIL_UNSUPPORTED, v.availability);
}

// A single unknown-dataid may be a corrupted frame that passed the parity check.
static void test_one_unknown_dataid_does_not_condemn_an_entity(void)
{
    ot_state_apply_dataid(27, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);

    ot_value_t v;
    ot_state_get("outside_temperature", &v);
    TEST_ASSERT_NOT_EQUAL(OT_AVAIL_UNSUPPORTED, v.availability);
    TEST_ASSERT_FALSE(ot_state_is_unsupported(27));
}

static void test_a_good_answer_clears_the_unknown_streak(void)
{
    ot_state_apply_dataid(25, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 2000);
    ot_state_apply_dataid(25, OT_MSG_UNKNOWN_DATAID, 0x0000, 3000);

    TEST_ASSERT_FALSE(ot_state_is_unsupported(25));
}

static void test_one_data_id_fills_every_entity_that_reads_it(void)
{
    // ID 0, low byte: bit 0 fault, bit 1 central heating, bit 3 flame.
    ot_state_apply_dataid(0, OT_MSG_READ_ACK, 0x000A, 1000);

    ot_value_t fault, ch, flame;
    ot_state_get("fault", &fault);
    ot_state_get("ch_active", &ch);
    ot_state_get("flame", &flame);
    TEST_ASSERT_FALSE(fault.boolean);
    TEST_ASSERT_TRUE(ch.boolean);
    TEST_ASSERT_TRUE(flame.boolean);
}

static void test_bounds_arrive_from_another_data_id(void)
{
    // ID 48 = 0x4128: ceiling 65, floor 40 -- the way a live boiler answered.
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4128, 1000);

    float lo = 0, hi = 0;
    TEST_ASSERT_TRUE(ot_state_bounds("dhw_setpoint", &lo, &hi));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.0f, hi);
}

static void test_bounds_fall_back_to_the_table_until_read(void)
{
    float lo = 0, hi = 0;
    TEST_ASSERT_TRUE(ot_state_bounds("dhw_setpoint", &lo, &hi));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, lo);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, hi);
}

static void test_a_dirty_bit_is_raised_and_cleared_per_consumer(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);

    TEST_ASSERT_TRUE(ot_state_take_dirty(OT_CONSUMER_MQTT,
                                         ot_registry_index_of("flow_temperature")));
    TEST_ASSERT_FALSE(ot_state_take_dirty(OT_CONSUMER_MQTT,
                                          ot_registry_index_of("flow_temperature")));
    // The second consumer did not lose its own mark.
    TEST_ASSERT_TRUE(ot_state_take_dirty(OT_CONSUMER_WEB,
                                         ot_registry_index_of("flow_temperature")));
}

static void test_an_unchanged_value_does_not_dirty_anyone(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    (void)ot_state_take_dirty(OT_CONSUMER_MQTT, ot_registry_index_of("flow_temperature"));

    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 2000);

    TEST_ASSERT_FALSE(ot_state_take_dirty(OT_CONSUMER_MQTT,
                                          ot_registry_index_of("flow_temperature")));
}

static void test_a_stranger_key_is_refused(void)
{
    ot_value_t v;
    TEST_ASSERT_FALSE(ot_state_get("no_such_entity", &v));
}

static void test_an_id_outside_the_registry_is_ignored(void)
{
    // A frame from a faulty slave has no right to bring the model down.
    ot_state_apply_dataid(200, OT_MSG_READ_ACK, 0x1234, 1000);
    ot_state_apply_dataid(8, OT_MSG_READ_ACK, 0x1234, 1000);
}

// --- ot_state_set_virtual ---------------------------------------

static bool take(ot_consumer_t c, const char *key)
{
    return ot_state_take_dirty(c, ot_registry_index_of(key));
}

static ot_value_t get(const char *key)
{
    ot_value_t v = {};
    TEST_ASSERT_TRUE_MESSAGE(ot_state_get(key, &v), key);
    return v;
}

// The value of a boiler entity may arrive only from the wire. "fault" is there because its
// Data-ID is 0, which a `> 0` in place of `>= 0` would let through.
static void test_set_virtual_refuses_a_boiler_entity(void)
{
    const char *const keys[] = {"flow_temperature", "fault", "ch_setpoint", "dhw_setpoint"};
    for (const char *k : keys) {
        TEST_ASSERT_FALSE_MESSAGE(ot_state_set_virtual(k, 1.0f, 1000), k);
        const ot_value_t v = get(k);
        TEST_ASSERT_EQUAL_MESSAGE(OT_AVAIL_UNKNOWN, v.availability, k);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, v.updated_ms, k);
        TEST_ASSERT_FALSE_MESSAGE(take(OT_CONSUMER_WEB, k), k);
    }
}

static void test_set_virtual_refuses_an_unknown_key(void)
{
    TEST_ASSERT_FALSE(ot_state_set_virtual("no_such_entity", 1.0f, 1000));
    TEST_ASSERT_FALSE(ot_state_set_virtual("", 1.0f, 1000));
    TEST_ASSERT_FALSE(ot_state_set_virtual(NULL, 1.0f, 1000));
}

static void test_set_virtual_lands_a_number(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_setpoint_effective", 45.5f, 1000));
    const ot_value_t v = get("ch_setpoint_effective");
    TEST_ASSERT_EQUAL(OT_AVAIL_OK, v.availability);
    TEST_ASSERT_EQUAL_FLOAT(45.5f, v.number);
    TEST_ASSERT_EQUAL_UINT32(1000, v.updated_ms);
}

// The same rule as ot_state_apply_dataid(): a mark on a CHANGE, a fresh timestamp always.
static void test_set_virtual_marks_dirty_only_on_a_change(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_setpoint_effective", 45.0f, 1000));
    TEST_ASSERT_TRUE(take(OT_CONSUMER_WEB, "ch_setpoint_effective"));
    TEST_ASSERT_TRUE(take(OT_CONSUMER_MQTT, "ch_setpoint_effective"));

    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_setpoint_effective", 45.0f, 2000));
    TEST_ASSERT_FALSE(take(OT_CONSUMER_WEB, "ch_setpoint_effective"));
    TEST_ASSERT_FALSE(take(OT_CONSUMER_MQTT, "ch_setpoint_effective"));
    TEST_ASSERT_EQUAL_UINT32(2000, get("ch_setpoint_effective").updated_ms);

    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_setpoint_effective", 46.0f, 3000));
    TEST_ASSERT_TRUE(take(OT_CONSUMER_MQTT, "ch_setpoint_effective"));
}

// `value != 0` -- not `> 0`, not `== 1`: whatever non-zero a producer hands in is on.
static void test_a_binary_is_value_not_zero(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_enable_effective", 2.0f, 1000));
    ot_value_t v = get("ch_enable_effective");
    TEST_ASSERT_TRUE(v.boolean);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v.number);
    TEST_ASSERT_TRUE(take(OT_CONSUMER_WEB, "ch_enable_effective"));

    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_enable_effective", -1.0f, 2000));
    TEST_ASSERT_TRUE(get("ch_enable_effective").boolean);
    TEST_ASSERT_FALSE(take(OT_CONSUMER_WEB, "ch_enable_effective"));

    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_enable_effective", 0.0f, 3000));
    v = get("ch_enable_effective");
    TEST_ASSERT_FALSE(v.boolean);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v.number);
    TEST_ASSERT_TRUE(take(OT_CONSUMER_WEB, "ch_enable_effective"));
}

static void test_a_switch_is_a_boolean_too(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("ch_enable", 1.0f, 1000));
    TEST_ASSERT_TRUE(get("ch_enable").boolean);
}

static void test_an_enum_stores_its_option_index(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("control_state", 4.0f, 1000));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, get("control_state").number);

    // Six options are 0..5. Every refusal leaves the stored index alone.
    TEST_ASSERT_FALSE(ot_state_set_virtual("control_state", 6.0f, 2000));
    TEST_ASSERT_FALSE(ot_state_set_virtual("control_state", 1.5f, 2000));
    TEST_ASSERT_FALSE(ot_state_set_virtual("control_state", -1.0f, 2000));
    TEST_ASSERT_EQUAL_FLOAT(4.0f, get("control_state").number);
    TEST_ASSERT_EQUAL_UINT32(1000, get("control_state").updated_ms);

    TEST_ASSERT_TRUE(ot_state_set_virtual("control_state", 5.0f, 3000));
    TEST_ASSERT_TRUE(ot_state_set_virtual("control_state", 0.0f, 3000));
    TEST_ASSERT_FALSE(ot_state_set_virtual("control_mode", 2.0f, 3000));
    TEST_ASSERT_TRUE(ot_state_set_virtual("control_mode", 1.0f, 3000));
}

// Refused, not stored: /ws prints a synthetic row's number with %.2f, and NaN prints as "nan",
// which is not JSON -- one such value would break every frame for every client.
static void test_set_virtual_refuses_a_non_finite_number(void)
{
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (float x : bad) {
        TEST_ASSERT_FALSE(ot_state_set_virtual("ch_setpoint_effective", x, 1000));
        TEST_ASSERT_FALSE(ot_state_set_virtual("ch_enable_effective", x, 1000));
    }
    TEST_ASSERT_EQUAL(OT_AVAIL_UNKNOWN, get("ch_setpoint_effective").availability);
    TEST_ASSERT_EQUAL(OT_AVAIL_UNKNOWN, get("ch_enable_effective").availability);
}

// The other half of the same wall: nothing from the wire lands on a synthetic row.
static void test_the_wire_never_writes_a_synthetic_row(void)
{
    ot_state_set_virtual("ch_enable", 1.0f, 1000);
    ot_state_set_virtual("ch_setpoint_effective", 45.0f, 1000);
    (void)take(OT_CONSUMER_WEB, "ch_enable");
    (void)take(OT_CONSUMER_WEB, "ch_setpoint_effective");

    for (unsigned id = 0; id <= 255; id++)
        ot_state_apply_dataid((uint8_t)id, OT_MSG_DATA_INVALID, 0x0000, 5000);

    const ot_value_t sw = get("ch_enable");
    TEST_ASSERT_EQUAL(OT_AVAIL_OK, sw.availability);
    TEST_ASSERT_TRUE(sw.boolean);
    TEST_ASSERT_EQUAL_UINT32(1000, sw.updated_ms);
    TEST_ASSERT_FALSE(take(OT_CONSUMER_WEB, "ch_enable"));
    const ot_value_t sp = get("ch_setpoint_effective");
    TEST_ASSERT_EQUAL(OT_AVAIL_OK, sp.availability);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, sp.number);
    TEST_ASSERT_FALSE(take(OT_CONSUMER_WEB, "ch_setpoint_effective"));
}

// store() compares the boolean as well as the number: a flag's number is NaN on both sides, so
// a flipped bit is a change ONLY through the boolean.
static void test_a_flipped_flag_is_a_change(void)
{
    ot_state_apply_dataid(0, OT_MSG_READ_ACK, 0x0000, 1000);
    (void)take(OT_CONSUMER_WEB, "ch_active");
    ot_state_apply_dataid(0, OT_MSG_READ_ACK, 0x0002, 2000);
    TEST_ASSERT_TRUE(take(OT_CONSUMER_WEB, "ch_active"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_state_knows_nothing);
    RUN_TEST(test_read_ack_lands_decoded);
    RUN_TEST(test_data_invalid_is_not_a_value);
    RUN_TEST(test_unknown_dataid_is_not_data_invalid);
    RUN_TEST(test_one_unknown_dataid_does_not_condemn_an_entity);
    RUN_TEST(test_a_good_answer_clears_the_unknown_streak);
    RUN_TEST(test_one_data_id_fills_every_entity_that_reads_it);
    RUN_TEST(test_bounds_arrive_from_another_data_id);
    RUN_TEST(test_bounds_fall_back_to_the_table_until_read);
    RUN_TEST(test_a_dirty_bit_is_raised_and_cleared_per_consumer);
    RUN_TEST(test_an_unchanged_value_does_not_dirty_anyone);
    RUN_TEST(test_a_stranger_key_is_refused);
    RUN_TEST(test_an_id_outside_the_registry_is_ignored);
    RUN_TEST(test_set_virtual_refuses_a_boiler_entity);
    RUN_TEST(test_set_virtual_refuses_an_unknown_key);
    RUN_TEST(test_set_virtual_lands_a_number);
    RUN_TEST(test_set_virtual_marks_dirty_only_on_a_change);
    RUN_TEST(test_a_binary_is_value_not_zero);
    RUN_TEST(test_a_switch_is_a_boolean_too);
    RUN_TEST(test_an_enum_stores_its_option_index);
    RUN_TEST(test_set_virtual_refuses_a_non_finite_number);
    RUN_TEST(test_the_wire_never_writes_a_synthetic_row);
    RUN_TEST(test_a_flipped_flag_is_a_change);
    return UNITY_END();
}
