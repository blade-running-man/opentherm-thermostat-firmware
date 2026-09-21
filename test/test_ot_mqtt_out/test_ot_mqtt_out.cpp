// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What goes out over MQTT (ot_mqtt_out.c): the topic tree, the state payloads -- ot_api's one
// spelling, as HA reads a bare payload -- the owner topic, control_state's
// attributes, and the publisher's two pieces of bookkeeping.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

// Every pure component this suite links is named by an include below: PlatformIO's library finder
// reaches a library through a suite's own #include, not through another library's header.
#include "ot_api.h"
#include "ot_command.h"
#include "ot_control.h"
#include "ot_mqtt.h"
#include "ot_registry.h"
#include "ot_state.h"

#define PFX "opentherm/aabbccddeeff"

static char out[OT_MQTT_TOPIC_MAX];

void setUp(void)
{
    ot_state_reset();
    memset(out, 0, sizeof out);
}
void tearDown(void) {}

static size_t payload(const char *key)
{
    return ot_mqtt_state_payload((uint16_t)ot_registry_index_of(key), out, sizeof out);
}

static void test_the_topic_tree_is_flat_and_keyed_by_the_entity_key(void)
{
    ot_mqtt_topic(PFX, "ch_enable", "state", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(PFX "/ch_enable/state", out);
    ot_mqtt_topic(PFX, "+", "set", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(PFX "/+/set", out);
    ot_mqtt_topic(PFX, "control", "owner", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING(PFX "/control/owner", out);
    TEST_ASSERT_EQUAL(strlen(PFX "/status"), ot_mqtt_topic(PFX, NULL, "status", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING(PFX "/status", out);
}

static void test_a_topic_that_does_not_fit_is_zero_and_empty(void)
{
    char small[10];
    TEST_ASSERT_EQUAL(0, ot_mqtt_topic(PFX, "ch_enable", "state", small, sizeof small));
    TEST_ASSERT_EQUAL_STRING("", small);
    TEST_ASSERT_EQUAL(0, ot_mqtt_topic(NULL, "k", "state", out, sizeof out));
}

// `n >= cap` is the correct snprintf-truncation test (n is the
// length snprintf WOULD have written, excluding the NUL, so n == cap means the string was cut a
// byte short of fitting). A buffer sized to the exact formatted length -- no room for the NUL --
// must still be refused; `n > cap` would let it through and report a length one byte longer than
// what the (silently truncated) buffer actually holds.
static void test_the_exact_truncation_boundary_is_still_refused(void)
{
    const char  *full  = PFX "/ch_enable/state";
    const size_t exact = strlen(full);
    char         buf[OT_MQTT_TOPIC_MAX];
    memset(buf, 'z', sizeof buf);
    TEST_ASSERT_EQUAL(0, ot_mqtt_topic(PFX, "ch_enable", "state", buf, exact));
    TEST_ASSERT_EQUAL_STRING("", buf);

    char      attrs[80];
    const int full_n = snprintf(attrs, sizeof attrs, "{\"reason\":\"%s\",\"cause\":\"%s\"}",
                                ot_control_reason_name(OT_CONTROL_REASON_FS_BLIND),
                                ot_control_reason_name(OT_CONTROL_REASON_WATCHDOG));
    TEST_ASSERT_TRUE(full_n > 0);
    memset(attrs, 'z', sizeof attrs);
    TEST_ASSERT_EQUAL(0, ot_mqtt_attributes(OT_CONTROL_REASON_FS_BLIND, OT_CONTROL_REASON_WATCHDOG,
                                            attrs, (size_t)full_n));
    TEST_ASSERT_EQUAL_STRING("", attrs);
}

// The spelling of /api/state and /ws, as HA reads a bare payload.
static void test_a_state_payload_is_ot_apis_value_as_ha_reads_it(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);   // 43.0 °C
    TEST_ASSERT_EQUAL(5, payload("flow_temperature"));
    TEST_ASSERT_EQUAL_STRING("43.00", out);
    ot_state_set_virtual("ch_enable", 1.0f, 1000);
    payload("ch_enable");
    TEST_ASSERT_EQUAL_STRING("true", out);
    ot_state_set_virtual("ch_enable", 0.0f, 1000);
    payload("ch_enable");
    TEST_ASSERT_EQUAL_STRING("false", out);
    ot_state_set_virtual("control_state", 4.0f, 1000);
    payload("control_state");
    TEST_ASSERT_EQUAL_STRING("failsafe", out);   // the option, without its quotes
}

// No data is HA's "None" -- never "", whose retained publication DELETES the value on the broker.
static void test_no_value_is_none_never_empty(void)
{
    TEST_ASSERT_EQUAL(4, payload("flow_temperature"));
    TEST_ASSERT_EQUAL_STRING("None", out);
    ot_state_apply_dataid(27, OT_MSG_DATA_INVALID, 0, 1000);
    payload("outside_temperature");
    TEST_ASSERT_EQUAL_STRING("None", out);
    for (uint16_t i = 0; i < ot_registry_count(); i++)
        TEST_ASSERT_TRUE_MESSAGE(ot_mqtt_state_payload(i, out, sizeof out) > 0,
                                 ot_registry_at(i)->key);
}

// The buffer's EXACT edge, not merely a buffer far too small: the rule is `len + 1 > cap`, so
// "43.00" needs six bytes and is refused by five -- leaving the buffer empty, never half a number,
// which as a retained payload would be a wrong value on the broker for as long as nobody looks.
static void test_a_payload_outside_the_registry_or_the_buffer_is_zero(void)
{
    TEST_ASSERT_EQUAL(0, ot_mqtt_state_payload(OT_ENTITY_COUNT, out, sizeof out));
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);   // 43.00: five bytes
    const uint16_t i = (uint16_t)ot_registry_index_of("flow_temperature");
    char           small[8];
    memset(small, 'x', sizeof small);
    TEST_ASSERT_EQUAL(5, ot_mqtt_state_payload(i, small, 6));
    TEST_ASSERT_EQUAL_STRING("43.00", small);
    memset(small, 'x', sizeof small);
    TEST_ASSERT_EQUAL(0, ot_mqtt_state_payload(i, small, 5));
    TEST_ASSERT_EQUAL_STRING("", small);
    TEST_ASSERT_EQUAL(0, ot_mqtt_state_payload(i, small, 1));
    TEST_ASSERT_EQUAL(0, ot_mqtt_state_payload(i, NULL, sizeof small));
}

// The control entities are available only while HA owns them.
static void test_the_owner_topic_says_whether_ha_owns_the_controls(void)
{
    TEST_ASSERT_EQUAL_STRING("online", ot_mqtt_owner_payload(OT_CONTROL_MODE_HA));
    TEST_ASSERT_EQUAL_STRING("offline", ot_mqtt_owner_payload(OT_CONTROL_MODE_LOCAL));
    TEST_ASSERT_EQUAL_STRING("offline", ot_mqtt_owner_payload((ot_control_mode_t)7));
}

static void test_control_states_attributes_carry_its_reason_and_cause(void)
{
    char buf[80];
    ot_mqtt_attributes(OT_CONTROL_REASON_FS_BLIND, OT_CONTROL_REASON_WATCHDOG, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("{\"reason\":\"fs_blind\",\"cause\":\"watchdog\"}", buf);
    TEST_ASSERT_EQUAL(0, ot_mqtt_attributes(OT_CONTROL_REASON_NONE, OT_CONTROL_REASON_NONE, buf, 10));
}

static void test_owed_all_owes_exactly_every_registry_entity(void)
{
    ot_mqtt_owed_t o;
    ot_mqtt_owed_all(&o);
    int n = 0, last = -1;
    for (int i = ot_mqtt_owed_next(&o, -1); i >= 0; i = ot_mqtt_owed_next(&o, i)) {
        TEST_ASSERT_EQUAL(last + 1, i);
        last = i;
        n++;
    }
    TEST_ASSERT_EQUAL(OT_ENTITY_COUNT, n);
    // No bit past the registry: the publisher would render an entity that does not exist.
    const int spare = OT_MQTT_OWED_WORDS * 32 - OT_ENTITY_COUNT;
    if (spare > 0)
        TEST_ASSERT_EQUAL_HEX32(0, o.w[OT_MQTT_OWED_WORDS - 1] >> (32 - spare));
}

static void test_owed_set_clear_and_next(void)
{
    ot_mqtt_owed_t o;
    memset(&o, 0, sizeof o);
    TEST_ASSERT_EQUAL(-1, ot_mqtt_owed_next(&o, -1));
    ot_mqtt_owed_set(&o, 33);
    ot_mqtt_owed_set(&o, 0);
    ot_mqtt_owed_set(&o, OT_ENTITY_COUNT - 1);
    ot_mqtt_owed_set(&o, OT_ENTITY_COUNT);   // ignored
    ot_mqtt_owed_set(&o, -1);                // ignored
    TEST_ASSERT_EQUAL(0, ot_mqtt_owed_next(&o, -1));
    TEST_ASSERT_EQUAL(33, ot_mqtt_owed_next(&o, 0));
    TEST_ASSERT_EQUAL(OT_ENTITY_COUNT - 1, ot_mqtt_owed_next(&o, 33));
    TEST_ASSERT_EQUAL(-1, ot_mqtt_owed_next(&o, OT_ENTITY_COUNT - 1));
    ot_mqtt_owed_clear(&o, 33);
    TEST_ASSERT_EQUAL(OT_ENTITY_COUNT - 1, ot_mqtt_owed_next(&o, 0));
    ot_mqtt_owed_clear(&o, OT_ENTITY_COUNT);   // ignored, and does not touch the last bit
    TEST_ASSERT_EQUAL(OT_ENTITY_COUNT - 1, ot_mqtt_owed_next(&o, 0));
}

// `index < OT_ENTITY_COUNT` is the only thing standing between a
// stray index and a write outside `w[]` -- OT_MQTT_OWED_WORDS is sized to OT_ENTITY_COUNT, not to
// whatever a caller might pass. This pins the guard as far as a plain assertion safely can: an
// index past OT_ENTITY_COUNT but still inside the array's own bit capacity must leave every word
// untouched. An index far enough outside the array to actually overrun it is undefined behaviour
// this suite cannot pin without a sanitizer -- [env:native] builds with none (platformio.ini).
static void test_owed_set_and_clear_never_touch_a_bit_beyond_the_entity_count(void)
{
    const int last_addressable = OT_MQTT_OWED_WORDS * 32 - 1;
    // If OT_ENTITY_COUNT ever becomes an exact multiple of 32 this index stops being "beyond
    // the count" and the test below would pass vacuously -- fail loudly instead of silently.
    TEST_ASSERT_TRUE(last_addressable >= OT_ENTITY_COUNT);

    ot_mqtt_owed_t o;
    memset(&o, 0, sizeof o);
    ot_mqtt_owed_set(&o, last_addressable);
    TEST_ASSERT_EQUAL_HEX32(0, o.w[last_addressable / 32]);
    TEST_ASSERT_EQUAL(-1, ot_mqtt_owed_next(&o, OT_ENTITY_COUNT - 1));

    memset(&o, 0xFF, sizeof o);
    ot_mqtt_owed_clear(&o, last_addressable);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, o.w[last_addressable / 32]);
}

static void test_quiet_speaks_once_a_period_and_counts_what_it_held_back(void)
{
    ot_mqtt_quiet_t q;
    memset(&q, 0, sizeof q);
    uint32_t held = 99;
    TEST_ASSERT_TRUE(ot_mqtt_quiet(&q, 1000, 60000, &held));
    TEST_ASSERT_EQUAL(0, held);
    TEST_ASSERT_FALSE(ot_mqtt_quiet(&q, 1001, 60000, &held));
    TEST_ASSERT_FALSE(ot_mqtt_quiet(&q, 30000, 60000, &held));
    TEST_ASSERT_FALSE(ot_mqtt_quiet(&q, 60999, 60000, &held));
    TEST_ASSERT_TRUE(ot_mqtt_quiet(&q, 61000, 60000, &held));   // exactly one period later
    TEST_ASSERT_EQUAL(3, held);
    TEST_ASSERT_TRUE(ot_mqtt_quiet(&q, 121000, 60000, &held));
    TEST_ASSERT_EQUAL(0, held);   // the count starts again after every line
}

// A 32-bit millisecond clock wraps after 49.7 days; this one does not.
static void test_quiet_keeps_its_period_past_the_32_bit_wrap(void)
{
    ot_mqtt_quiet_t q;
    memset(&q, 0, sizeof q);
    const uint64_t t = 0xFFFFFFF0ull;
    TEST_ASSERT_TRUE(ot_mqtt_quiet(&q, t, 60000, NULL));
    TEST_ASSERT_FALSE(ot_mqtt_quiet(&q, t + 0x20, 60000, NULL));
    TEST_ASSERT_TRUE(ot_mqtt_quiet(&q, t + 60000, 60000, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_topic_tree_is_flat_and_keyed_by_the_entity_key);
    RUN_TEST(test_a_topic_that_does_not_fit_is_zero_and_empty);
    RUN_TEST(test_the_exact_truncation_boundary_is_still_refused);
    RUN_TEST(test_a_state_payload_is_ot_apis_value_as_ha_reads_it);
    RUN_TEST(test_no_value_is_none_never_empty);
    RUN_TEST(test_a_payload_outside_the_registry_or_the_buffer_is_zero);
    RUN_TEST(test_the_owner_topic_says_whether_ha_owns_the_controls);
    RUN_TEST(test_control_states_attributes_carry_its_reason_and_cause);
    RUN_TEST(test_owed_all_owes_exactly_every_registry_entity);
    RUN_TEST(test_owed_set_clear_and_next);
    RUN_TEST(test_owed_set_and_clear_never_touch_a_bit_beyond_the_entity_count);
    RUN_TEST(test_quiet_speaks_once_a_period_and_counts_what_it_held_back);
    RUN_TEST(test_quiet_keeps_its_period_past_the_32_bit_wrap);
    return UNITY_END();
}
