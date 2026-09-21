// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_mqtt_decide() and ot_mqtt_handle(): what an arriving message means, and what
// an accepted command makes happen -- with the executor and the bus replaced by recording fakes, so
// every call and every argument is visible. test_ot_mqtt_control drives the REAL ot_control.
#include <math.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>
#include <string>

// Every pure component this suite links is named by an include below: PlatformIO's library finder
// reaches a library through a suite's own #include, not through another library's header.
#include "ot_api.h"
#include "ot_command.h"
#include "ot_control.h"
#include "ot_mqtt.h"
#include "ot_registry.h"
#include "ot_state.h"

#define PFX "opentherm/aabbccddeeff"
#define HA_STATUS "homeassistant/status"

static ot_mqtt_in_t d(const char *topic, const char *payload, bool retained = false)
{
    return ot_mqtt_decide(PFX, HA_STATUS, topic, strlen(topic), payload, strlen(payload), retained);
}

// --- the fakes ------------------------------------------------------------------------------------

static ot_control_cfg_t cfg;
static int              applied, apply_result, writes;
static ot_origin_t      seen_origin;
static ot_control_cmd_t seen_cmd;
static int16_t          seen_value;
static uint8_t          w_id;
static uint16_t         w_raw;

static void fx_cfg(ot_control_cfg_t *out) { *out = cfg; }
static int  fx_apply(ot_origin_t origin, ot_control_cmd_t cmd, int16_t value)
{
    applied++;
    seen_origin = origin;
    seen_cmd    = cmd;
    seen_value  = value;
    return apply_result;
}
static void fx_write(uint8_t id, uint16_t raw)
{
    writes++;
    w_id  = id;
    w_raw = raw;
}
static const ot_mqtt_effects_t FX = {fx_cfg, fx_apply, fx_write};

static ot_control_cfg_t cfg_for(ot_control_mode_t mode)
{
    ot_control_cfg_t c;
    memset(&c, 0, sizeof c);
    c.mode                    = mode;
    c.heating_season          = true;
    c.watchdog_s              = 900;
    c.failsafe_setpoint_dc    = 450;
    c.failsafe_room_target_dc = 180;
    c.failsafe_heat_days      = 3;
    c.failsafe_min_cycle_s    = 600;
    c.flow_min_dc             = 400;
    c.flow_max_dc             = 700;
    c.local_ch_setpoint_dc    = 450;
    c.dhw_enable              = true;
    return c;
}

void setUp(void)
{
    ot_state_reset();
    cfg     = cfg_for(OT_CONTROL_MODE_HA);
    applied = apply_result = writes = 0;
}
void tearDown(void) {}

// --- decide ---------------------------------------------------------------------------------------

static void test_a_command_topic_names_its_key_and_its_number(void)
{
    const ot_mqtt_in_t in = d(PFX "/ch_setpoint/set", "45.5");
    TEST_ASSERT_EQUAL(OT_MQTT_COMMAND, in.kind);
    TEST_ASSERT_EQUAL_STRING("ch_setpoint", in.key);
    TEST_ASSERT_EQUAL_FLOAT(45.5f, in.value);
    TEST_ASSERT_NULL(in.reason);
}

// Retained means replayed, and a replayed command would feed the watchdog on every
// reconnect. Refused whatever it says -- the payload is not even read.
static void test_a_retained_command_is_refused_whatever_it_says(void)
{
    for (const char *payload : {"1", "0", "45.5", "garbage", ""}) {
        const ot_mqtt_in_t in = d(PFX "/ch_enable/set", payload, true);
        TEST_ASSERT_EQUAL_MESSAGE(OT_MQTT_REJECT, in.kind, payload);
        TEST_ASSERT_EQUAL_STRING("a retained command is ignored", in.reason);
    }
}

static void test_the_payload_must_be_a_plain_number(void)
{
    for (const char *ok : {"0", "1", "-0.5", "45", "45.50", "007", "1234567890123456789012345678901"})
        TEST_ASSERT_EQUAL_MESSAGE(OT_MQTT_COMMAND, d(PFX "/ch_enable/set", ok).kind, ok);
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, d(PFX "/x/set", "-0.5").value);
    for (const char *bad : {"", "ON", "true", "1e3", " 1", "1 ", "+1", ".5", "5.", "-", "nan",
                            "inf", "0x10", "1,5", "--1", "12345678901234567890123456789012"}) {
        const ot_mqtt_in_t in = d(PFX "/ch_enable/set", bad);
        TEST_ASSERT_EQUAL_MESSAGE(OT_MQTT_REJECT, in.kind, bad);
        TEST_ASSERT_EQUAL_STRING("the payload is not a number", in.reason);
    }
}

// `online` re-publishes discovery; `offline` is not used -- it fires on HA's clean shutdown.
static void test_ha_online_asks_for_discovery_and_offline_means_nothing(void)
{
    TEST_ASSERT_EQUAL(OT_MQTT_HA_ONLINE, d("homeassistant/status", "online").kind);
    TEST_ASSERT_EQUAL(OT_MQTT_HA_ONLINE, d("homeassistant/status", "online", true).kind);
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d("homeassistant/status", "offline").kind);
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d("homeassistant/status", "Online").kind);
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d("homeassistant/status", "on").kind);
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d("homeassistant/statusx", "online").kind);
    // The match is `payload_len == strlen(ONLINE)`, and payload_len
    // is also memcmp's own length argument. A plain longer payload ("onlinex") is NOT enough to
    // pin this -- memcmp still reads the "online" literal's own trailing NUL, in bounds, and a
    // non-NUL 7th payload byte mismatches it regardless of `==` vs `>=`. What actually needs the
    // exact length is a 7-byte payload that is not a C string: "online" plus one extra byte that
    // is ALSO nul, matching the literal's footprint byte for byte ('o','n','l','i','n','e','\0').
    // Widen `==` to `>=` and this stops being distinguishable from "online" and is misread as
    // HA's birth; not reachable through strlen() on a NUL-terminated string, so built by hand.
    static const char birth_plus_nul[] = {'o', 'n', 'l', 'i', 'n', 'e', '\0'};
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, ot_mqtt_decide(PFX, HA_STATUS, HA_STATUS, strlen(HA_STATUS),
                                                      birth_plus_nul, sizeof birth_plus_nul, false)
                                         .kind);
}

// A broker is shared: somebody else's topic -- retained or not -- is IGNORED, never counted as a
// refusal. Only a well-formed <prefix>/<one level>/set is ours.
static void test_anything_outside_our_command_topics_is_ignored_silently(void)
{
    for (const char *t : {"someone/else/ch_enable/set", PFX "x/ch_enable/set", PFX "/ch_enable/state",
                          PFX "/a/b/set", PFX "//set", PFX "/set", PFX "/ch_enable/setx", PFX, "", PFX "kk/set",
                          PFX "/ch_enable_state",
                          "opentherm/aabbccddeef/ch_enable/set",
                          // The SAME LENGTH as ours, one character different: two thermostats on
                          // one broker. Only memcmp() catches this one -- a length test does not.
                          "opentherm/aabbccddeefe/ch_enable/set"}) {
        TEST_ASSERT_EQUAL_MESSAGE(OT_MQTT_IGNORE, d(t, "1").kind, t);
        TEST_ASSERT_EQUAL_MESSAGE(OT_MQTT_IGNORE, d(t, "1", true).kind, t);
    }
}

// esp-mqtt hands a pointer and a length into its own buffer: nothing past either is ours to read.
static void test_topic_and_payload_are_read_by_length_not_by_terminator(void)
{
    const char *topic   = PFX "/ch_enable/setXXXX";
    const char *payload = "1XYZ";
    const ot_mqtt_in_t in = ot_mqtt_decide(PFX, HA_STATUS, topic, strlen(topic) - 4, payload, 1, false);
    TEST_ASSERT_EQUAL(OT_MQTT_COMMAND, in.kind);
    TEST_ASSERT_EQUAL_STRING("ch_enable", in.key);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, in.value);
}

static void test_a_key_longer_than_the_buffer_is_ignored_not_truncated(void)
{
    const std::string key(OT_MQTT_KEY_MAX, 'k');
    const std::string topic = std::string(PFX "/") + key + "/set";
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d(topic.c_str(), "1").kind);
    const std::string fits = std::string(PFX "/") + key.substr(1) + "/set";
    TEST_ASSERT_EQUAL(OT_MQTT_COMMAND, d(fits.c_str(), "1").kind);
}

// A NUL inside the key is not a terminator here: the topic is read by length, and
// "<prefix>/ch_enable\0x/set" must not become a write to ch_enable -- which is what every C
// function downstream would make of it.
static void test_a_key_with_a_nul_inside_is_ignored(void)
{
    const char topic[] = PFX "/ch_enable\0x/set";
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE,
                      ot_mqtt_decide(PFX, HA_STATUS, topic, sizeof topic - 1, "1", 1, false).kind);
    // And in the payload it is not a number, as any other stray byte is not.
    const char payload[] = "1\0x";
    TEST_ASSERT_EQUAL(OT_MQTT_REJECT,
                      ot_mqtt_decide(PFX, HA_STATUS, PFX "/ch_enable/set",
                                     strlen(PFX "/ch_enable/set"), payload, sizeof payload - 1,
                                     false)
                          .kind);
}

// The room reading topic is exact -- neither a level shorter nor a level longer matches it, so
// `<prefix>/room/foo/state` (a level this component does not serve) falls through to IGNORE
// rather than being mistaken for the one topic HA is asked to publish.
static void test_room_state_accepted(void)
{
    const ot_mqtt_in_t in = d(PFX "/room/state", "21.5");
    TEST_ASSERT_EQUAL(OT_MQTT_ROOM, in.kind);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, in.value);
    TEST_ASSERT_NULL(in.reason);
}

// A retained reading would replay on every reconnect and look exactly as fresh as one just
// published -- the same reasoning as a retained command, but for the room source that
// is about to steer the failsafe: a stale retained value must not masquerade as fresh.
static void test_room_state_retained_rejected(void)
{
    const ot_mqtt_in_t in = d(PFX "/room/state", "21.5", true);
    TEST_ASSERT_EQUAL(OT_MQTT_REJECT, in.kind);
    TEST_ASSERT_EQUAL_STRING("a retained room reading is refused", in.reason);
}

static void test_room_state_non_numeric_rejected(void)
{
    const ot_mqtt_in_t in = d(PFX "/room/state", "warm");
    TEST_ASSERT_EQUAL(OT_MQTT_REJECT, in.kind);
    TEST_ASSERT_EQUAL_STRING("the payload is not a number", in.reason);
}

static void test_room_wrong_topic_ignored(void)
{
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, d(PFX "/room/foo/state", "21.5").kind);
}

static void test_null_arguments_are_ignored(void)
{
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, ot_mqtt_decide(NULL, HA_STATUS, "a/b/set", 7, "1", 1, false).kind);
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, ot_mqtt_decide(PFX, HA_STATUS, NULL, 7, "1", 1, false).kind);
    // The LENGTH of the topic, not one byte less: a short count would make this a topic that is
    // not ours, and the NULL payload below would never be reached.
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE,
                      ot_mqtt_decide(PFX, HA_STATUS, PFX "/x/set", strlen(PFX "/x/set"), NULL, 1,
                                     false).kind);
    // No birth topic given: HA's `online` is nobody's, and a command still is ours.
    TEST_ASSERT_EQUAL(OT_MQTT_IGNORE, ot_mqtt_decide(PFX, NULL, HA_STATUS, 20, "online", 6, false).kind);
    TEST_ASSERT_EQUAL(OT_MQTT_COMMAND,
                      ot_mqtt_decide(PFX, NULL, PFX "/x/set", strlen(PFX "/x/set"), "1", 1,
                                     false).kind);
}

// --- handle ---------------------------------------------------------------------------------------

static void test_a_control_command_goes_to_the_executor_with_origin_ha(void)
{
    const ot_mqtt_in_t      in = d(PFX "/ch_setpoint/set", "55");
    const ot_mqtt_verdict_t v  = ot_mqtt_handle(&in, &FX);
    TEST_ASSERT_TRUE(v.accepted);
    TEST_ASSERT_NULL(v.reason);
    TEST_ASSERT_EQUAL(1, applied);
    TEST_ASSERT_EQUAL(OT_ORIGIN_HA, seen_origin);
    TEST_ASSERT_EQUAL(OT_CONTROL_CMD_CH_SETPOINT, seen_cmd);
    TEST_ASSERT_EQUAL(550, seen_value);
    TEST_ASSERT_EQUAL(0, writes);
}

// The executor's answer is the final one: the early one may predate a mode flip.
static void test_the_executors_refusal_is_the_final_answer(void)
{
    apply_result = OT_CONTROL_OWNED_BY_LOCAL;
    const ot_mqtt_in_t      in = d(PFX "/ch_enable/set", "1");
    const ot_mqtt_verdict_t v  = ot_mqtt_handle(&in, &FX);
    TEST_ASSERT_FALSE(v.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OWNED_BY_LOCAL), v.reason);
    apply_result = OT_CONTROL_SEASON_ON_IS_LOCAL;
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_SEASON_ON_IS_LOCAL),
                             ot_mqtt_handle(&in, &FX).reason);
    apply_result = OT_CONTROL_OUT_OF_RANGE;
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OUT_OF_RANGE), ot_mqtt_handle(&in, &FX).reason);
    apply_result = 0x41;   // the task layer's own code (ot_thermostat.h): the store refused it
    TEST_ASSERT_EQUAL_STRING("accepted but not carried out by the executor",
                             ot_mqtt_handle(&in, &FX).reason);
}

// The early answer refuses before the executor is asked, with the command layer's own words.
static void test_the_command_layer_refuses_before_the_executor_is_asked(void)
{
    cfg                  = cfg_for(OT_CONTROL_MODE_LOCAL);
    ot_mqtt_in_t      in = d(PFX "/ch_enable/set", "1");
    ot_mqtt_verdict_t v  = ot_mqtt_handle(&in, &FX);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OWNED_BY_LOCAL), v.reason);
    cfg = cfg_for(OT_CONTROL_MODE_HA);
    in  = d(PFX "/ch_setpoint/set", "99");
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OUT_OF_RANGE), ot_mqtt_handle(&in, &FX).reason);
    in = d(PFX "/no_such_key/set", "1");
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_UNKNOWN_KEY), ot_mqtt_handle(&in, &FX).reason);
    in = d(PFX "/control_mode/set", "1");   // not writable over any transport
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_NOT_WRITABLE), ot_mqtt_handle(&in, &FX).reason);
    TEST_ASSERT_EQUAL(0, applied);
    TEST_ASSERT_EQUAL(0, writes);
}

// NOT parity with POST /api/entities: Home Assistant drives the executor,
// and a raw OpenTherm frame is not its to write. The refusal is ot_command_check()'s -- what this
// suite pins is that the transport CARRIES it and never decides one of its own -- and fx->write is
// not reached. The FRAME branch of ot_mqtt_handle() stays for the day Q6 is answered the other way.
static void test_a_frame_entity_is_refused_for_home_assistant(void)
{
    const ot_mqtt_in_t      in = d(PFX "/max_ch_setpoint/set", "60");
    const ot_mqtt_verdict_t v  = ot_mqtt_handle(&in, &FX);
    TEST_ASSERT_FALSE(v.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_NOT_FOR_HA), v.reason);
    TEST_ASSERT_EQUAL(0, writes);
    TEST_ASSERT_EQUAL(0, applied);
}

static void test_a_message_that_is_not_a_command_does_nothing(void)
{
    for (const ot_mqtt_in_t &in : {d("homeassistant/status", "online"), d(PFX "/x/set", "1", true),
                                  d("other/x/set", "1")}) {
        const ot_mqtt_verdict_t v = ot_mqtt_handle(&in, &FX);
        TEST_ASSERT_FALSE(v.accepted);
        TEST_ASSERT_NULL(v.reason);
    }
    const ot_mqtt_in_t in = d(PFX "/ch_enable/set", "1");
    TEST_ASSERT_FALSE(ot_mqtt_handle(NULL, &FX).accepted);
    TEST_ASSERT_FALSE(ot_mqtt_handle(&in, NULL).accepted);
    TEST_ASSERT_EQUAL(0, applied + writes);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_command_topic_names_its_key_and_its_number);
    RUN_TEST(test_a_retained_command_is_refused_whatever_it_says);
    RUN_TEST(test_the_payload_must_be_a_plain_number);
    RUN_TEST(test_ha_online_asks_for_discovery_and_offline_means_nothing);
    RUN_TEST(test_anything_outside_our_command_topics_is_ignored_silently);
    RUN_TEST(test_topic_and_payload_are_read_by_length_not_by_terminator);
    RUN_TEST(test_a_key_longer_than_the_buffer_is_ignored_not_truncated);
    RUN_TEST(test_a_key_with_a_nul_inside_is_ignored);
    RUN_TEST(test_room_state_accepted);
    RUN_TEST(test_room_state_retained_rejected);
    RUN_TEST(test_room_state_non_numeric_rejected);
    RUN_TEST(test_room_wrong_topic_ignored);
    RUN_TEST(test_null_arguments_are_ignored);
    RUN_TEST(test_a_control_command_goes_to_the_executor_with_origin_ha);
    RUN_TEST(test_the_executors_refusal_is_the_final_answer);
    RUN_TEST(test_the_command_layer_refuses_before_the_executor_is_asked);
    RUN_TEST(test_a_frame_entity_is_refused_for_home_assistant);
    RUN_TEST(test_a_message_that_is_not_a_command_does_nothing);
    return UNITY_END();
}
