// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <string.h>
#include <unity.h>

#include "ot_registry.h"

void setUp(void) {}
void tearDown(void) {}

static void test_the_registry_is_not_empty(void)
{
    TEST_ASSERT_GREATER_THAN_UINT16(0, ot_registry_count());
}

static void test_lookup_by_key_finds_a_known_entity(void)
{
    const ot_entity_t *e = ot_registry_by_key("flow_temperature");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT8(25, e->data_id);
    TEST_ASSERT_EQUAL(OT_CODEC_F88, e->codec);
}

static void test_lookup_by_key_returns_null_for_a_stranger(void)
{
    TEST_ASSERT_NULL(ot_registry_by_key("no_such_entity"));
    TEST_ASSERT_NULL(ot_registry_by_key(""));
    TEST_ASSERT_NULL(ot_registry_by_key(NULL));
}

static void test_index_of_agrees_with_at(void)
{
    const int idx = ot_registry_index_of("flow_temperature");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_STRING("flow_temperature", ot_registry_at((uint16_t)idx)->key);
}

static void test_index_of_returns_minus_one_for_a_stranger(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ot_registry_index_of("no_such_entity"));
}

static void test_at_returns_null_outside_the_registry(void)
{
    TEST_ASSERT_NULL(ot_registry_at(ot_registry_count()));
}

// One Data-ID carries SEVERAL entities: for ID 5 that is six flags plus the
// manufacturer's code. The walk must hand out all of them, otherwise half the registry
// will never be filled.
static void test_one_data_id_yields_every_entity_that_reads_it(void)
{
    uint16_t seen = 0;
    for (int i = ot_registry_first_for_id(5); i >= 0; i = ot_registry_next_for_id(5, i))
        seen++;
    TEST_ASSERT_EQUAL_UINT16(7, seen);
}

static void test_an_absent_data_id_yields_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(-1, ot_registry_first_for_id(8));
}

static void test_the_poll_ring_excludes_status(void)
{
    for (uint16_t i = 0; i < ot_registry_poll_count(); i++)
        TEST_ASSERT_TRUE(ot_registry_poll_at(i) != 0);
}

static void test_the_poll_ring_fits_the_scheduler(void)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(32, ot_registry_poll_count());
}

// --- Synthetic rows ----------------------------------------------------------------

static const char *const EXECUTOR_KEYS[] = {
    "ch_enable",     "dhw_enable",            "heating_season",      "control_mode",
    "control_state", "ch_setpoint_effective", "ch_enable_effective", "failsafe_count",
    "last_failsafe_duration_s",
};

// Every value a uint8_t holds, not only 0..127: the iterators are called with a frame's Data-ID
// byte, and a synthetic row matched by ANY of them would be decoded from the wire. 255 is what
// -1 becomes when it is truncated to a byte.
static void test_a_synthetic_row_is_never_yielded_for_any_data_id(void)
{
    uint16_t yielded = 0;
    for (unsigned id = 0; id <= 255; id++) {
        for (int i = ot_registry_first_for_id((uint8_t)id); i >= 0;
             i = ot_registry_next_for_id((uint8_t)id, i)) {
            const ot_entity_t *e = ot_registry_at((uint16_t)i);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)id, e->data_id, e->key);
            yielded++;
        }
    }
    // Every real row exactly once, and nothing else.
    uint16_t real = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++)
        if (ot_registry_at(i)->data_id >= 0)
            real++;
    TEST_ASSERT_EQUAL_UINT16(real, yielded);
    TEST_ASSERT_TRUE(real < ot_registry_count());
}

static void test_the_executor_rows_are_synthetic(void)
{
    for (const char *k : EXECUTOR_KEYS) {
        const ot_entity_t *e = ot_registry_by_key(k);
        TEST_ASSERT_NOT_NULL_MESSAGE(e, k);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, e->data_id, k);
        TEST_ASSERT_EQUAL_MESSAGE(OT_CODEC_NONE, e->codec, k);
        TEST_ASSERT_FALSE_MESSAGE(e->readable, k);
    }
}

// A writable synthetic row is a command to the executor, never a frame: it names one, has no
// codec, and has no table bounds (ot_control bounds it). Until ot_command routes
// it, ot_command_encode() refuses it at the range check -- no bounds, OT_CMD_OUT_OF_RANGE.
static void test_every_writable_synthetic_row_names_a_command(void)
{
    uint16_t seen = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        if (e->data_id >= 0 || !e->writable)
            continue;
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, e->control, e->key);
        TEST_ASSERT_EQUAL_MESSAGE(OT_CODEC_NONE, e->codec, e->key);
        TEST_ASSERT_TRUE_MESSAGE(isnan(e->min_value) && isnan(e->max_value), e->key);
        seen++;
    }
    TEST_ASSERT_EQUAL_UINT16(3, seen);
}

// The numbers of ot_control_cmd_t. test_ot_command pins them against ot_control.h itself.
static void test_the_control_numbers(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, ot_registry_by_key("ch_enable")->control);
    TEST_ASSERT_EQUAL_UINT8(2, ot_registry_by_key("ch_setpoint")->control);
    TEST_ASSERT_EQUAL_UINT8(3, ot_registry_by_key("dhw_enable")->control);
    TEST_ASSERT_EQUAL_UINT8(4, ot_registry_by_key("dhw_setpoint")->control);
    TEST_ASSERT_EQUAL_UINT8(5, ot_registry_by_key("heating_season")->control);
    TEST_ASSERT_EQUAL_UINT8(0, ot_registry_by_key("flow_temperature")->control);
    TEST_ASSERT_EQUAL_UINT8(0, ot_registry_by_key("fault")->control);
}

static void assert_option(const ot_entity_t *e, unsigned k, const char *want)
{
    size_t len = 999;
    const char *o = ot_registry_option(e, k, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(o, want);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(strlen(want), len, want);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(o, want, len), want);
}

static void test_an_enum_names_its_options_in_order(void)
{
    const ot_entity_t *m = ot_registry_by_key("control_mode");
    TEST_ASSERT_EQUAL(OT_KIND_ENUM, m->kind);
    TEST_ASSERT_EQUAL_UINT8(2, ot_registry_option_count(m));
    assert_option(m, 0, "local");
    assert_option(m, 1, "ha");
    size_t len = 0;
    TEST_ASSERT_NULL(ot_registry_option(m, 2, &len));

    const ot_entity_t *s = ot_registry_by_key("control_state");
    TEST_ASSERT_EQUAL_UINT8(6, ot_registry_option_count(s));
    assert_option(s, 0, "season_off");
    assert_option(s, 3, "ha_waiting");
    assert_option(s, 5, "ha");
    TEST_ASSERT_NULL(ot_registry_option(s, 6, &len));
}

static void test_a_non_enum_has_no_options(void)
{
    const ot_entity_t *e = ot_registry_by_key("flow_temperature");
    size_t len = 0;
    TEST_ASSERT_NULL(e->options);
    TEST_ASSERT_EQUAL_UINT8(0, ot_registry_option_count(e));
    TEST_ASSERT_NULL(ot_registry_option(e, 0, &len));
    TEST_ASSERT_EQUAL_UINT8(0, ot_registry_option_count(NULL));
    TEST_ASSERT_NULL(ot_registry_option(NULL, 0, &len));
}

// By kind, not by codec: a synthetic switch has OT_CODEC_NONE, and "boolean means FLAG" would
// print its value as a number.
static void test_booleans_are_told_by_kind(void)
{
    TEST_ASSERT_TRUE(ot_registry_is_boolean(ot_registry_by_key("fault")));
    TEST_ASSERT_TRUE(ot_registry_is_boolean(ot_registry_by_key("ch_enable")));
    TEST_ASSERT_TRUE(ot_registry_is_boolean(ot_registry_by_key("ch_enable_effective")));
    TEST_ASSERT_FALSE(ot_registry_is_boolean(ot_registry_by_key("flow_temperature")));
    TEST_ASSERT_FALSE(ot_registry_is_boolean(ot_registry_by_key("ch_setpoint")));
    TEST_ASSERT_FALSE(ot_registry_is_boolean(ot_registry_by_key("control_mode")));
    TEST_ASSERT_FALSE(ot_registry_is_boolean(NULL));
}

static void test_the_poll_ring_holds_only_real_data_ids(void)
{
    for (uint16_t i = 0; i < ot_registry_poll_count(); i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(1, ot_registry_poll_at(i));
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(127, ot_registry_poll_at(i));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_registry_is_not_empty);
    RUN_TEST(test_lookup_by_key_finds_a_known_entity);
    RUN_TEST(test_lookup_by_key_returns_null_for_a_stranger);
    RUN_TEST(test_index_of_agrees_with_at);
    RUN_TEST(test_index_of_returns_minus_one_for_a_stranger);
    RUN_TEST(test_at_returns_null_outside_the_registry);
    RUN_TEST(test_one_data_id_yields_every_entity_that_reads_it);
    RUN_TEST(test_an_absent_data_id_yields_nothing);
    RUN_TEST(test_the_poll_ring_excludes_status);
    RUN_TEST(test_the_poll_ring_fits_the_scheduler);
    RUN_TEST(test_a_synthetic_row_is_never_yielded_for_any_data_id);
    RUN_TEST(test_the_executor_rows_are_synthetic);
    RUN_TEST(test_every_writable_synthetic_row_names_a_command);
    RUN_TEST(test_the_control_numbers);
    RUN_TEST(test_an_enum_names_its_options_in_order);
    RUN_TEST(test_a_non_enum_has_no_options);
    RUN_TEST(test_booleans_are_told_by_kind);
    RUN_TEST(test_the_poll_ring_holds_only_real_data_ids);
    return UNITY_END();
}
