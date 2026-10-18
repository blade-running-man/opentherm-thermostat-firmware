// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_ha_render() and ot_ha_topic(): the six tokens of a generated discovery document filled in,
// and the three of the five HA rules that only the runtime can break:
//   rule 3 -- the node segment is the device id, so a context whose id is not bare hex is refused;
//   rule 4 -- configuration_url carries a scheme, or is absent;
//   rule 5 -- a value we do not know is omitted, never "".
// Rules 1 and 2 are properties of the generated shape and live in tools/tests/test_discovery.py.
#include <string.h>
#include <unity.h>

#include <string>

#include "ot_ha.h"
#include "ot_registry.h"

static char out[OT_HA_DOC_MAX];

void setUp(void) { memset(out, 0, sizeof out); }
void tearDown(void) {}

static ot_ha_ctx_t full(void)
{
    return (ot_ha_ctx_t){"opentherm/aabbccddeeff", "aabbccddeeff", "aa:bb:cc:dd:ee:ff",
                         "Котёл", "opentherm-thermostat", "1.0.0", "192.168.1.20"};
}

static ot_ha_ctx_t bare(void)
{
    return (ot_ha_ctx_t){"opentherm/aabbccddeeff", "aabbccddeeff", "", "t", "", "", ""};
}

// A hand-made document, so every expectation below is exact.
static const ot_ha_doc_t SAMPLE = {0, "number", "x", true, false, OT_HA_BOUNDS_FLOW,
                                   "{\"t\":\"{P}/x/state\",\"u\":\"{I}_x\",\"min\":{L},"
                                   "\"max\":{H},\"dev\":{D},\"o\":{O}}"};

static const ot_ha_doc_t *doc_named(const char *object_id)
{
    for (uint16_t i = 0; i < ot_ha_doc_count(); i++)
        if (strcmp(ot_ha_doc_at(i)->object_id, object_id) == 0)
            return ot_ha_doc_at(i);
    TEST_FAIL_MESSAGE(object_id);
    return NULL;
}

static void test_every_token_is_filled_exactly(void)
{
    const ot_ha_ctx_t c = full();
    const size_t n = ot_ha_render(&SAMPLE, &c, 400, 705, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(
        "{\"t\":\"opentherm/aabbccddeeff/x/state\",\"u\":\"aabbccddeeff_x\",\"min\":40,"
        "\"max\":70.5,\"dev\":{\"ids\":[\"aabbccddeeff\"],\"cns\":[[\"mac\",\"aa:bb:cc:dd:ee:ff\"]],"
        "\"name\":\"\\u041a\\u043e\\u0442\\u0451\\u043b\",\"mdl\":\"opentherm-thermostat\","
        "\"sw\":\"1.0.0\",\"cu\":\"http://192.168.1.20/\"},"
        "\"o\":{\"name\":\"opentherm-thermostat\",\"sw\":\"1.0.0\"}}",
        out);
    TEST_ASSERT_EQUAL(strlen(out), n);
}

// The token switch's default: branch, which the DO NOT comment beside it depends on. That comment
// says an unknown token is "copied as text, which the tests catch as a token left in a rendered
// document" -- and the test that catches it is on the GENERATOR side
// (tools/tests/test_discovery.py: no body may carry a token this renderer does not fill). What
// belongs here is the other half: that THIS renderer copies such a token out byte for byte instead
// of swallowing it, so a generator regression shows up as visible "{Q}" rather than as a document
// that is quietly short one key.
static void test_an_unrecognized_token_is_copied_out_literally(void)
{
    const ot_ha_doc_t odd = {0, "sensor", "x", false, false, OT_HA_BOUNDS_NONE,
                             "{\"t\":\"{Q}\",\"u\":\"{I}_x\"}"};
    const ot_ha_ctx_t c = bare();
    const size_t n = ot_ha_render(&odd, &c, 0, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"t\":\"{Q}\",\"u\":\"aabbccddeeff_x\"}", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
}

// Rules 4 and 5: no address, no MAC and no version are three ABSENT keys, not three "".
static void test_what_is_not_known_is_omitted_never_empty(void)
{
    const ot_ha_ctx_t c = bare();
    TEST_ASSERT_TRUE(ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"t\":\"opentherm/aabbccddeeff/x/state\",\"u\":\"aabbccddeeff_x\",\"min\":40,"
        "\"max\":70,\"dev\":{\"ids\":[\"aabbccddeeff\"],\"name\":\"t\"},"
        "\"o\":{\"name\":\"opentherm-thermostat\"}}",
        out);
}

// Rule 4 at its source: anything that is not a dotted quad is dropped, never sent to HA's URL
// validator, which would refuse the device block and every entity with it.
static void test_an_address_that_is_not_a_dotted_quad_is_omitted(void)
{
    const char *bad[] = {"192.168.1.20 ", "fe80::1", "host", "1.2.3", "1234567890123456"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ot_ha_ctx_t c = full();
        c.ip          = bad[i];
        TEST_ASSERT_TRUE(ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out) > 0);
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"cu\""), bad[i]);
    }
}

static void test_the_prefix_and_the_name_are_escaped_inside_their_strings(void)
{
    ot_ha_ctx_t c = full();
    c.prefix      = "op\"x";
    c.name        = "a\\b\"c";
    TEST_ASSERT_TRUE(ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"t\":\"op\\\"x/x/state\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"a\\\\b\\\"c\""));
}

static void test_bounds_are_json_numbers_in_tenths(void)
{
    const ot_ha_ctx_t c = full();
    ot_ha_render(&SAMPLE, &c, -5, 455, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"min\":-0.5,\"max\":45.5,"));
    ot_ha_render(&SAMPLE, &c, -120, 0, out, sizeof out);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"min\":-12,\"max\":0,"));
}

// HA refuses a number whose min exceeds its max -- silently. Refused here, loudly (0).
static void test_a_bounded_document_with_min_above_max_is_refused(void)
{
    const ot_ha_ctx_t c = full();
    TEST_ASSERT_EQUAL(0, ot_ha_render(&SAMPLE, &c, 705, 700, out, sizeof out));
    TEST_ASSERT_TRUE(ot_ha_render(&SAMPLE, &c, 700, 700, out, sizeof out) > 0);
    ot_ha_doc_t unbounded = SAMPLE;
    unbounded.bounds      = OT_HA_BOUNDS_NONE;
    TEST_ASSERT_TRUE(ot_ha_render(&unbounded, &c, 705, 700, out, sizeof out) > 0);
}

static void test_a_document_that_does_not_fit_is_zero_never_truncated(void)
{
    const ot_ha_ctx_t c = full();
    char small[64];
    TEST_ASSERT_EQUAL(0, ot_ha_render(&SAMPLE, &c, 400, 700, small, sizeof small));
    TEST_ASSERT_EQUAL(0, ot_ha_topic(&SAMPLE, &c, small, 20));
}

// Rule 3: the node segment is the device id. A MAC with colons, capitals or a short id would be
// a discovery topic HA's TOPIC_MATCHER ignores -- so the whole context is refused.
static void test_a_device_id_that_is_not_twelve_lower_hex_is_refused(void)
{
    const char *bad[] = {"aa:bb:cc:dd:ee:ff", "AABBCCDDEEFF", "aabbccddeef", "aabbccddeeff0", "",
                         "aabbccddeefg"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ot_ha_ctx_t c = full();
        c.device_id   = bad[i];
        TEST_ASSERT_EQUAL_MESSAGE(0, ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out), bad[i]);
        TEST_ASSERT_EQUAL_MESSAGE(0, ot_ha_topic(&SAMPLE, &c, out, sizeof out), bad[i]);
    }
    TEST_ASSERT_EQUAL(0, ot_ha_render(&SAMPLE, NULL, 400, 700, out, sizeof out));
    TEST_ASSERT_EQUAL(0, ot_ha_topic(&SAMPLE, NULL, out, sizeof out));
    ot_ha_ctx_t c = full();
    c.prefix      = "";
    TEST_ASSERT_EQUAL(0, ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out));
}

static void test_the_discovery_topic_is_prefix_component_node_object_config(void)
{
    const ot_ha_ctx_t c = full();
    ot_ha_topic(doc_named("heating_season_off"), &c, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/aabbccddeeff/heating_season_off/config", out);
}

// Rule 3 over every generated document, and every topic fits the glue's buffer.
static void test_every_discovery_topic_is_made_of_characters_ha_accepts(void)
{
    const ot_ha_ctx_t c = full();
    for (uint16_t i = 0; i < ot_ha_doc_count(); i++) {
        char t[OT_HA_TOPIC_MAX];
        TEST_ASSERT_TRUE(ot_ha_topic(ot_ha_doc_at(i), &c, t, sizeof t) > 0);
        for (const char *p = t; *p; p++)
            TEST_ASSERT_TRUE_MESSAGE((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                                         *p == '_' || *p == '-' || *p == '/',
                                     t);
    }
}

// The longest context the store allows: a 64-byte prefix of quotes (each escaped to two bytes),
// a 48-byte name of two-byte letters (each six bytes escaped), a 31-byte version. Every document
// must still fit OT_HA_DOC_MAX, with no token left and no "" in it.
static void test_every_document_fits_the_publish_buffer_in_the_worst_case(void)
{
    std::string prefix(63, '"');
    prefix = "p" + prefix;
    std::string name;
    for (int i = 0; i < 24; i++)
        name += "ж";
    ot_ha_ctx_t c = full();
    c.prefix      = prefix.c_str();
    c.name        = name.c_str();
    c.sw_version  = "fw-1234567890-abcdef-dirty-123";
    c.ip          = "255.255.255.255";
    for (uint16_t i = 0; i < ot_ha_doc_count(); i++) {
        const ot_ha_doc_t *d = ot_ha_doc_at(i);
        TEST_ASSERT_TRUE_MESSAGE(ot_ha_render(d, &c, -1000, 1000, out, sizeof out) > 0,
                                 d->object_id);
        TEST_ASSERT_NULL_MESSAGE(strstr(out, "\"\""), d->object_id);
        for (const char *tok : {"{P}", "{I}", "{D}", "{O}", "{L}", "{H}"})
            TEST_ASSERT_NULL_MESSAGE(strstr(out, tok), d->object_id);
    }
}

// The C expander against a second statement of the six rules, on a real generated document:
// the Python reference (render_discovery.expand) does the same plain substitutions.
static void test_a_generated_document_expands_like_the_python_reference(void)
{
    const ot_ha_doc_t *d = doc_named("ch_setpoint");
    std::string want(d->body);
    const ot_ha_ctx_t c = full();
    const std::pair<std::string, std::string> subs[] = {
        {"{P}", c.prefix},
        {"{I}", c.device_id},
        {"{D}", "{\"ids\":[\"aabbccddeeff\"],\"cns\":[[\"mac\",\"aa:bb:cc:dd:ee:ff\"]],"
                "\"name\":\"\\u041a\\u043e\\u0442\\u0451\\u043b\",\"mdl\":\"opentherm-thermostat\","
                "\"sw\":\"1.0.0\",\"cu\":\"http://192.168.1.20/\"}"},
        {"{O}", "{\"name\":\"opentherm-thermostat\",\"sw\":\"1.0.0\"}"},
        {"{L}", "40"},
        {"{H}", "70"},
    };
    for (const auto &s : subs)
        for (size_t at; (at = want.find(s.first)) != std::string::npos;)
            want.replace(at, s.first.size(), s.second);
    ot_ha_render(d, &c, 400, 700, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(want.c_str(), out);
}

static void test_a_field_at_the_escape_limit_renders_and_one_past_it_is_refused(void)
{
    char name[66];
    ot_ha_ctx_t c = bare();

    memset(name, 'n', 64);
    name[64] = '\0';
    c.name = name;
    TEST_ASSERT_NOT_EQUAL(0, ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out));

    name[64] = 'n';
    name[65] = '\0';
    c.name = name;
    TEST_ASSERT_EQUAL(0, ot_ha_render(&SAMPLE, &c, 400, 700, out, sizeof out));
}

static void test_every_document_names_a_registry_entity_it_is_keyed_by(void)
{
    TEST_ASSERT_EQUAL(OT_HA_DOC_COUNT, ot_ha_doc_count());
    TEST_ASSERT_NULL(ot_ha_doc_at(OT_HA_DOC_COUNT));
    for (uint16_t i = 0; i < ot_ha_doc_count(); i++) {
        const ot_ha_doc_t *d = ot_ha_doc_at(i);
        const ot_entity_t *e = ot_registry_at(d->entity);
        TEST_ASSERT_NOT_NULL(e);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(d->object_id, e->key, strlen(e->key)), e->key);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_token_is_filled_exactly);
    RUN_TEST(test_an_unrecognized_token_is_copied_out_literally);
    RUN_TEST(test_what_is_not_known_is_omitted_never_empty);
    RUN_TEST(test_an_address_that_is_not_a_dotted_quad_is_omitted);
    RUN_TEST(test_the_prefix_and_the_name_are_escaped_inside_their_strings);
    RUN_TEST(test_bounds_are_json_numbers_in_tenths);
    RUN_TEST(test_a_bounded_document_with_min_above_max_is_refused);
    RUN_TEST(test_a_document_that_does_not_fit_is_zero_never_truncated);
    RUN_TEST(test_a_device_id_that_is_not_twelve_lower_hex_is_refused);
    RUN_TEST(test_the_discovery_topic_is_prefix_component_node_object_config);
    RUN_TEST(test_every_discovery_topic_is_made_of_characters_ha_accepts);
    RUN_TEST(test_every_document_fits_the_publish_buffer_in_the_worst_case);
    RUN_TEST(test_a_generated_document_expands_like_the_python_reference);
    RUN_TEST(test_a_field_at_the_escape_limit_renders_and_one_past_it_is_refused);
    RUN_TEST(test_every_document_names_a_registry_entity_it_is_keyed_by);
    return UNITY_END();
}
