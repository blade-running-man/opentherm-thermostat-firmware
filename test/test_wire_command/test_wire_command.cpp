// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <string.h>
#include <unity.h>

#include "ot_wire.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_number_value_parses(void)
{
    ot_wire_value_t v;
    TEST_ASSERT_EQUAL(OT_WIRE_OK,
                      ot_wire_parse_entity_write("{\"value\":55.5}", &v));
    TEST_ASSERT_FALSE(v.is_bool);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.5f, v.number);
}

static void test_a_negative_value_parses(void)
{
    ot_wire_value_t v;
    TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_entity_write("{\"value\":-3.5}", &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.5f, v.number);
}

static void test_a_boolean_value_parses_as_boolean(void)
{
    // true and 1.0 are DIFFERENT answers. A flag entity that was sent a number is a
    // client error, not a reason to guess.
    ot_wire_value_t v;
    TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_entity_write("{\"value\":true}", &v));
    TEST_ASSERT_TRUE(v.is_bool);
    TEST_ASSERT_TRUE(v.boolean);
}

static void test_a_missing_value_key_is_refused(void)
{
    ot_wire_value_t v;
    TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK, ot_wire_parse_entity_write("{\"v\":1}", &v));
}

static void test_a_broken_document_is_refused(void)
{
    ot_wire_value_t v;
    TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK, ot_wire_parse_entity_write("{\"value\":", &v));
    TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK, ot_wire_parse_entity_write("", &v));
}

static void test_operation_parameters_parse_flat(void)
{
    // The operation name stands in the PATH, so the body is a flat object of parameters,
    // and the ot_json parser, which reads only flat objects, is fit as is.
    ot_wire_params_t p;
    TEST_ASSERT_EQUAL(OT_WIRE_OK,
                      ot_wire_parse_operation("{\"from\":0,\"to\":127}", &p));
    TEST_ASSERT_EQUAL_UINT8(2, p.count);
    float v = 0;
    TEST_ASSERT_TRUE(ot_wire_param(&p, "from", &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
    TEST_ASSERT_TRUE(ot_wire_param(&p, "to", &v));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 127.0f, v);
}

static void test_an_empty_operation_body_is_valid(void)
{
    // An operation without parameters is a legal case: the body `{}` means "run it as is".
    ot_wire_params_t p;
    TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_operation("{}", &p));
    TEST_ASSERT_EQUAL_UINT8(0, p.count);
}

static void test_an_absent_parameter_reports_absent(void)
{
    ot_wire_params_t p;
    TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_operation("{\"from\":1}", &p));
    float v = 0;
    TEST_ASSERT_FALSE(ot_wire_param(&p, "to", &v));
}

static void test_too_many_parameters_are_refused(void)
{
    ot_wire_params_t p;
    TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK,
        ot_wire_parse_operation("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5}", &p));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_number_value_parses);
    RUN_TEST(test_a_negative_value_parses);
    RUN_TEST(test_a_boolean_value_parses_as_boolean);
    RUN_TEST(test_a_missing_value_key_is_refused);
    RUN_TEST(test_a_broken_document_is_refused);
    RUN_TEST(test_operation_parameters_parse_flat);
    RUN_TEST(test_an_empty_operation_body_is_valid);
    RUN_TEST(test_an_absent_parameter_reports_absent);
    RUN_TEST(test_too_many_parameters_are_refused);
    return UNITY_END();
}
