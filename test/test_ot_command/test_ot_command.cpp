// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <string.h>
#include <unity.h>

#include "ot_command.h"
#include "ot_registry.h"
#include "ot_state.h"

void setUp(void) { ot_state_reset(); }
void tearDown(void) {}

static void test_a_writable_setpoint_encodes_as_f88(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("dhw_setpoint", 55.0f, &f));
    TEST_ASSERT_EQUAL_UINT8(56, f.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x3700, f.raw);   // 55.0 * 256
}

static void test_an_unknown_key_is_refused(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_UNKNOWN_KEY, ot_command_encode("no_such", 1.0f, &f));
}

static void test_a_read_only_entity_is_refused(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_NOT_WRITABLE,
                      ot_command_encode("flow_temperature", 50.0f, &f));
}

// The bounds are taken from the table until the boiler has reported its own.
static void test_a_value_outside_the_table_range_is_refused(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", 95.0f, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", 5.0f, &f));
}

// And once it has reported them -- from those. ID 48 = 0x4128: ceiling 65, floor 40, as on
// a live boiler. 70 lies inside the table's 30..80, but above what the boiler allows.
static void test_the_boiler_own_bounds_win_over_the_table(void)
{
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4128, 1000);

    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", 70.0f, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", 35.0f, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("dhw_setpoint", 60.0f, &f));
}

static void test_the_bounds_themselves_are_accepted(void)
{
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4128, 1000);

    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("dhw_setpoint", 40.0f, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("dhw_setpoint", 65.0f, &f));
}

// The boiler answered unknown-dataid twice -- writing there is pointless, and saying so is
// more honest than queueing a frame nobody will accept.
static void test_an_unsupported_data_id_is_refused(void)
{
    ot_state_apply_dataid(57, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(57, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);

    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_UNSUPPORTED_BY_BOILER,
                      ot_command_encode("max_ch_setpoint", 60.0f, &f));
}

// NaN used to pass the range check STRAIGHT THROUGH: `value < lo` and `value > hi` are both
// false for a non-number, so "the range is passed", and then ot_codec_float_to_f88(NaN) hands
// back 0 -- a flow setpoint of 0 °C, queued silently. From REST this is unreachable
// (ot_json_f32 rejects NaN), but a caller feeding it a COMPUTED
// float -- a division by zero or an uninitialised sensor from MQTT -- gives
// exactly NaN.
static void test_a_nan_value_is_refused(void)
{
    ot_command_frame_t f = {.data_id = 0xEE, .raw = 0xBEEF};
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", NAN, &f));
    // On a refusal `*out` is left untouched -- that is the ot_command.h contract.
    TEST_ASSERT_EQUAL_UINT8(0xEE, f.data_id);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, f.raw);
}

// Infinity is of the same nature: it passes `value < lo` from the minus side and `value > hi`
// from the plus side through only one branch each, so one of the two is caught by the range
// and the other is not. Both must be rejected BEFORE the codec.
static void test_an_infinite_value_is_refused(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", INFINITY, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE, ot_command_encode("dhw_setpoint", -INFINITY, &f));
}

// The codec has nothing to do with it: for u16 the conversion (uint16_t)(NaN + 0.5f) is
// undefined behaviour, not "zero". The check must stand BEFORE the switch on the codec, once.
static void test_a_nan_value_is_refused_for_every_writable_codec(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE,
                      ot_command_encode("master_product_version", NAN, &f));
    TEST_ASSERT_EQUAL(OT_CMD_OUT_OF_RANGE,
                      ot_command_encode("master_product_version", INFINITY, &f));
}

// The second half of the same hole. ot_state_bounds() hands back false when the table
// declared min/max as NaN, and before this fix ot_command.c in that case did not check the
// range AT ALL -- silently leaning on the fact that writable entities without bounds do not
// exist. Now the support is named: the absence of bounds on a writable entity is a refusal,
// and this test keeps the registry such that the refusal does not fire on any existing entity.
//
// Every writable entity WITH A DATA-ID. A synthetic row (data_id -1: ch_enable, dhw_enable,
// heating_season) is never encoded into a frame -- see the next test -- and its bounds are
// ot_control's (0/1, or the flow bounds of the configuration), not the table's.
static void test_every_writable_entity_declares_bounds(void)
{
    const uint16_t n = ot_registry_count();
    for (uint16_t i = 0; i < n; i++) {
        const ot_entity_t *e = ot_registry_at(i);
        // Synthetic rows are bounded by ot_control, not by table bounds.
        if (!e->writable || e->data_id < 0)
            continue;
        float lo = 0, hi = 0;
        TEST_ASSERT_TRUE_MESSAGE(ot_state_bounds(e->key, &lo, &hi), e->key);
        TEST_ASSERT_FALSE_MESSAGE(isnan(lo) || isnan(hi), e->key);
    }
}

// A synthetic row has no Data-ID, and (uint8_t)-1 is 255: without the guard a writable
// synthetic row would be asked about "ID 255" and, given bounds, queued as a frame to it. It is
// refused as not writable -- as a FRAME it is not; its writes go through ot_command_check().
// The loop also asserts that at least one such row IS writable, so that the refusal cannot be
// the registry's writable flag doing the work.
static void test_a_synthetic_row_is_never_encoded_into_a_frame(void)
{
    unsigned synthetic = 0, writable = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        if (e->data_id >= 0)
            continue;
        synthetic++;
        if (e->writable)
            writable++;
        ot_command_frame_t f = {.data_id = 0xEE, .raw = 0xBEEF};
        TEST_ASSERT_EQUAL_MESSAGE(OT_CMD_NOT_WRITABLE, ot_command_encode(e->key, 1.0f, &f),
                                  e->key);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEE, f.data_id, e->key);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xBEEF, f.raw, e->key);
    }
    TEST_ASSERT_TRUE(synthetic > 0);
    TEST_ASSERT_TRUE(writable > 0);
}

static void test_a_u16_entity_encodes_whole(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("master_product_version", 258.0f, &f));
    TEST_ASSERT_EQUAL_UINT8(126, f.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x0102, f.raw);
}

static void test_a_negative_room_temperature_survives_the_sign(void)
{
    // f8.8 is SIGNED. An unsigned parse would turn -1.0 into +255.996 completely silently.
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_OK, ot_command_encode("room_temperature", -1.0f, &f));
    TEST_ASSERT_EQUAL_HEX16(0xFF00, f.raw);
}

static void test_a_null_key_does_not_crash(void)
{
    ot_command_frame_t f;
    TEST_ASSERT_EQUAL(OT_CMD_UNKNOWN_KEY, ot_command_encode(NULL, 1.0f, &f));
}

// Every code has a message of its OWN. Non-empty is not enough: the fallback "unknown error" is
// non-empty too, and a code whose case was forgotten in the switch would pass as "has a
// message" while answering the client with a sentence that names nothing.
static void test_every_error_has_a_message(void)
{
    const ot_command_err_t all[] = {
        OT_CMD_OK, OT_CMD_UNKNOWN_KEY, OT_CMD_NOT_WRITABLE, OT_CMD_OUT_OF_RANGE,
        OT_CMD_UNSUPPORTED_BY_BOILER, OT_CMD_HALF_WORD_CODEC,
        OT_CMD_OWNED_BY_HA, OT_CMD_OWNED_BY_LOCAL, OT_CMD_SEASON_ON_IS_LOCAL,
    };
    const unsigned n = sizeof all / sizeof all[0];
    for (unsigned i = 0; i < n; i++) {
        const char *m = ot_command_strerror(all[i]);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_TRUE(strlen(m) > 0);
        TEST_ASSERT_NOT_EQUAL(0, strcmp(m, ot_command_strerror((ot_command_err_t)999)));
        for (unsigned j = 0; j < i; j++)
            TEST_ASSERT_NOT_EQUAL(0, strcmp(m, ot_command_strerror(all[j])));
    }
}

// Appended, not inserted: the existing codes keep their numbers, because a log line or a
// test that recorded a number must go on meaning the same refusal.
static void test_the_new_codes_are_appended(void)
{
    TEST_ASSERT_EQUAL_INT(5, OT_CMD_HALF_WORD_CODEC);
    TEST_ASSERT_EQUAL_INT(6, OT_CMD_OWNED_BY_HA);
    TEST_ASSERT_EQUAL_INT(7, OT_CMD_OWNED_BY_LOCAL);
    TEST_ASSERT_EQUAL_INT(8, OT_CMD_SEASON_ON_IS_LOCAL);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_writable_setpoint_encodes_as_f88);
    RUN_TEST(test_an_unknown_key_is_refused);
    RUN_TEST(test_a_read_only_entity_is_refused);
    RUN_TEST(test_a_value_outside_the_table_range_is_refused);
    RUN_TEST(test_the_boiler_own_bounds_win_over_the_table);
    RUN_TEST(test_the_bounds_themselves_are_accepted);
    RUN_TEST(test_an_unsupported_data_id_is_refused);
    RUN_TEST(test_a_nan_value_is_refused);
    RUN_TEST(test_an_infinite_value_is_refused);
    RUN_TEST(test_a_nan_value_is_refused_for_every_writable_codec);
    RUN_TEST(test_every_writable_entity_declares_bounds);
    RUN_TEST(test_a_synthetic_row_is_never_encoded_into_a_frame);
    RUN_TEST(test_a_u16_entity_encodes_whole);
    RUN_TEST(test_a_negative_room_temperature_survives_the_sign);
    RUN_TEST(test_a_null_key_does_not_crash);
    RUN_TEST(test_every_error_has_a_message);
    RUN_TEST(test_the_new_codes_are_appended);
    return UNITY_END();
}
