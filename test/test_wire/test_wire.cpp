// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The settings document: what may be SENT to the device in POST /api/config and what it
// returns in GET /api/config.
//
// Every parsed case below is a body anyone can send to the open access point, and every render
// is a document a browser will read. Two properties run through the whole file and are worth
// naming once rather than in thirty comments:
//
//  * NO SECRET IS EVER RENDERED. Not the Wi-Fi key, not the broker password, not the UI password.
//    The projection turns each into a sentinel or an empty string and this suite asserts the
//    value itself never appears in the bytes.
//  * NO SUBMITTED VALUE IS EVER QUOTED IN AN ERROR. Errors name a FIELD. Everything this device
//    logs is served by GET /api/log and on the setup access point that is world-readable,
//    so a message that echoes what was typed publishes it.
//
// One directory -- one suite: PlatformIO links all the .cpp files of a directory into one
// binary, so two main()s in one directory would collide. Provisioning lives in
// test_wire_provision, the network scan and the status document in test_wire_status. Cut apart
// when the original suite went over the 600-line ceiling (CLAUDE.md, "File ceiling").

#include <unity.h>

#include <cstring>
#include <string>

#include "ot_json.h"
#include "ot_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_out[4096];

ot_wifi_t pair(const char *ssid, const char *psk) {
  ot_wifi_t w = {};
  w.ssid_len = (uint8_t)strlen(ssid);
  memcpy(w.ssid, ssid, w.ssid_len);
  w.psk_len = (uint8_t)strlen(psk);
  memcpy(w.psk, psk, w.psk_len);
  return w;
}

bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != nullptr;
}

}  // namespace

// --- POST /api/config ------------------------------------------------------------------------

void test_a_full_settings_submission_is_decoded(void) {
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const char *const body =
      "{\"mqtt_host\":\"broker.local\",\"mqtt_port\":1883,\"mqtt_user\":\"comfo\","
      "\"mqtt_password\":\"brokerpass\",\"topic_prefix\":\"comfoair\",\"ha_discovery\":true,"
      "\"device_name\":\"Loft\",\"ui_password\":\"letmein12\"}";
  const ot_wire_result_t r = ot_wire_parse_config(body, &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("broker.local", patch.mqtt_host);
  TEST_ASSERT_TRUE(patch.has_mqtt_port);
  TEST_ASSERT_EQUAL_UINT32(1883, patch.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("comfo", patch.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("brokerpass", patch.mqtt_password);
  TEST_ASSERT_EQUAL_STRING("comfoair", patch.topic_prefix);
  TEST_ASSERT_TRUE(patch.has_ha_discovery);
  TEST_ASSERT_TRUE(patch.ha_discovery);
  TEST_ASSERT_EQUAL_STRING("Loft", patch.device_name);
  TEST_ASSERT_EQUAL_STRING("letmein12", patch.ui_password);
}

void test_an_absent_field_is_a_null_pointer_and_an_empty_one_is_an_empty_string(void) {
  // The distinction the whole sentinel scheme is built on. NULL leaves the stored value alone;
  // "" clears it. A parser that wrote "" for an absent key would wipe the broker password every
  // time somebody saved an unrelated setting.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  r =
      ot_wire_parse_config("{\"mqtt_user\":\"\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_NOT_NULL(patch.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("", patch.mqtt_user);
  TEST_ASSERT_NULL(patch.mqtt_host);
  TEST_ASSERT_NULL(patch.mqtt_password);
  TEST_ASSERT_NULL(patch.ui_password);
  TEST_ASSERT_FALSE(patch.has_mqtt_port);
  TEST_ASSERT_FALSE(patch.has_ha_discovery);
}

void test_an_empty_object_is_a_legitimate_save_that_changes_nothing(void) {
  // The settings page submits the whole document, so this only arrives from a hand-made request
  // -- but it has to succeed, because ot_config_apply() distinguishes "a patch with every
  // field absent" (fine) from "there was no document" (an error), and that distinction is only
  // available if `{}` parses.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_config("{}", &patch, &storage).status);
}

void test_a_body_that_is_not_an_object_is_not_read_as_an_empty_save(void) {
  // If it were, the owner would be told Saved over a request that stored nothing.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_BODY,
                    ot_wire_parse_config("[]", &patch, &storage).status);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_BODY,
                    ot_wire_parse_config("", &patch, &storage).status);
}

void test_the_network_cannot_be_set_through_the_settings_route(void) {
  // /api/config is not the route the network travels on, and the omission is the design
  // (web/src/api/client.ts, ConfigPatch). Refused BY NAME rather than ignored: silently dropping
  // a credential somebody typed into a form is worse than telling them where it belongs, because
  // the form then reports success over a network that was never stored.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  ssid =
      ot_wire_parse_config("{\"wifi_ssid\":\"Kitchen\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_WRONG_ROUTE, ssid.status);
  TEST_ASSERT_EQUAL_STRING("wifi_ssid", ssid.field);

  const ot_wire_result_t psk =
      ot_wire_parse_config("{\"wifi_psk\":\"hunter2hunter\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_WRONG_ROUTE, psk.status);
  TEST_ASSERT_EQUAL_STRING("wifi_psk", psk.field);

  // And the sentence names the door that works, rather than describing the value. A caller
  // reaches this by echoing back the whole GET document, which carries wifi_ssid because the
  // page has to show which network is configured -- so it is not a client doing anything
  // unreasonable, and "bad field" would send it looking at the value it typed.
  TEST_ASSERT_TRUE(contains(ot_wire_strerror(ssid), "/api/provision"));
}

void test_an_unknown_field_is_ignored_so_a_newer_page_can_still_save(void) {
  // The other side of the rule above. A page that has grown a field an older firmware does not
  // know about must not have every save refused -- that turns one new setting into a device that
  // cannot be configured at all.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  r      = ot_wire_parse_config(
      "{\"mqtt_host\":\"broker.local\",\"something_new\":42}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("broker.local", patch.mqtt_host);
}

void test_the_derived_fields_are_ignored_rather_than_taken_as_input(void) {
  // ui_password_set, read_only and wifi_known_good are computed by the device and appear in the
  // projection only. A stored "a password is set" flag that disagrees with the stored record is a
  // device locked by nobody, for ever (ot_config.h:257-260) -- so echoing them back has to
  // be harmless, and the page does echo the whole document.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  r = ot_wire_parse_config(
      "{\"ui_password_set\":true,\"read_only\":false,\"wifi_known_good\":true}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_NULL(patch.ui_password);
}

void test_a_field_of_the_wrong_type_names_itself(void) {
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  host =
      ot_wire_parse_config("{\"mqtt_host\":42}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD, host.status);
  TEST_ASSERT_EQUAL_STRING("mqtt_host", host.field);

  const ot_wire_result_t flag =
      ot_wire_parse_config("{\"ha_discovery\":\"yes\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD, flag.status);
  TEST_ASSERT_EQUAL_STRING("ha_discovery", flag.field);
}

void test_a_port_outside_the_range_is_refused_and_never_wrapped(void) {
  // ot_config_check_port takes a uint32_t precisely so 70000 can be REFUSED. A parser that
  // wrapped it to 4464 would store a broker address the owner never typed.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD,
                    ot_wire_parse_config("{\"mqtt_port\":4294967296}", &patch, &storage)
                        .status);
  // In range for the reader and out of range for the validator: this is the layer that says
  // "a number", and ot_config_apply() is the one that says "a port".
  TEST_ASSERT_EQUAL(OT_WIRE_OK,
                    ot_wire_parse_config("{\"mqtt_port\":70000}", &patch, &storage).status);
  TEST_ASSERT_EQUAL_UINT32(70000, patch.mqtt_port);
}

void test_a_value_too_long_for_the_field_is_refused_rather_than_truncated(void) {
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  std::string                   host(OT_CONFIG_HOST_MAX + 1, 'h');
  std::string                   doc = "{\"mqtt_host\":\"" + host + "\"}";
  const ot_wire_result_t  r   = ot_wire_parse_config(doc.c_str(), &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_EQUAL_STRING("mqtt_host", r.field);
}

void test_a_refused_submission_leaves_no_half_decoded_patch_behind(void) {
  // ALL OR NOTHING is ot_config_apply()'s contract, and it can only keep it if what it is
  // handed is all or nothing too. A patch carrying the good half of a refused document would be
  // applied by a handler that checked the status after passing it on.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  r      = ot_wire_parse_config(
      "{\"mqtt_host\":\"broker.local\",\"ha_discovery\":\"yes\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_NULL(patch.mqtt_host);
  TEST_ASSERT_FALSE(patch.has_ha_discovery);
}

// --- GET /api/config -------------------------------------------------------------------------

void test_the_config_document_carries_every_key_the_page_reads(void) {
  // The keys are ot_config's `name` column, which is what that table defines as "what the
  // API and the owner see". A key missing here is a field the page silently never shows; a key
  // spelled differently is a field that silently never saves.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);

  const size_t n = ot_wire_render_config(&pub, false, g_out, sizeof g_out);
  TEST_ASSERT_TRUE(n > 0);
  const char *const keys[] = {"\"wifi_ssid\"",   "\"wifi_psk\"",       "\"mqtt_host\"",
                              "\"mqtt_port\"",   "\"mqtt_user\"",      "\"mqtt_password\"",
                              "\"topic_prefix\"", "\"ha_discovery\"",  "\"device_name\"",
                              "\"ui_password\"", "\"ui_password_set\"", "\"read_only\"",
                              "\"wifi_known_good\""};
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, keys[i]), keys[i]);
}

void test_no_stored_secret_appears_in_the_config_document(void) {
  // The property that unblocks a single universal binary (CLAUDE.md). A stored
  // password is never returned by any projection; reads get a sentinel.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  const ot_wifi_t stored = pair("Kitchen", "WIFIKEYINTHECLEAR");
  cfg.wifi                     = stored;
  snprintf(cfg.mqtt_password, sizeof cfg.mqtt_password, "%s", "BROKERPASSINTHECLEAR");
  snprintf(cfg.ui_pw_hash, sizeof cfg.ui_pw_hash, "%s", "1$10000$aabb$ccdd");

  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, true, g_out, sizeof g_out) > 0);

  TEST_ASSERT_FALSE(contains(g_out, "WIFIKEYINTHECLEAR"));
  TEST_ASSERT_FALSE(contains(g_out, "BROKERPASSINTHECLEAR"));
  // Not the hash either. It is not the password, but it is what an offline attack is run
  // against, and there is no reason for a browser to hold it.
  TEST_ASSERT_FALSE(contains(g_out, "1$10000$aabb$ccdd"));
  // The SSID is not a secret -- it is in every beacon that network sends -- and the page needs
  // it to show which network is configured.
  TEST_ASSERT_TRUE(contains(g_out, "Kitchen"));
  // Each set secret reads back as the sentinel, which is what tells the page a value exists.
  TEST_ASSERT_TRUE(contains(g_out, OT_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password_set\":true"));
}

void test_an_unset_secret_reads_back_as_an_empty_string_not_as_the_sentinel(void) {
  // The two are what SecretField's placeholder distinguishes: "stored on the device" against
  // "not set". A device with no broker password that answered with the sentinel would tell the
  // owner a password exists that they can never have set.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"mqtt_password\":\"\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password\":\"\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password_set\":false"));
}

void test_the_known_good_flag_comes_from_the_machine_and_not_from_the_stored_ssid(void) {
  // An SSID stored beside a mistyped key has never produced an address: the trial that fails
  // goes to RETRYING with nothing restored, and a page that promises a rollback there tells the
  // owner to wait for a return that is not coming.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  cfg.wifi = pair("Kitchen", "hunter2hunter");
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);

  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_known_good\":false"));
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, true, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_known_good\":true"));
}

void test_a_document_that_does_not_fit_reports_zero_rather_than_a_short_one(void) {
  // The entity list once came twenty-three bytes from serving truncated
  // JSON with a 200 in front of it. The same rule here: a renderer that does not fit says so.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);
  char small[32];
  TEST_ASSERT_EQUAL_size_t(0, ot_wire_render_config(&pub, false, small, sizeof small));
}

void test_a_device_name_with_a_quote_in_it_survives_the_round_trip(void) {
  // Rendered by the escaper and read back by the reader in the same component. If the two
  // disagreed, a name with a quote in it would change every time the page was saved.
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  snprintf(cfg.device_name, sizeof cfg.device_name, "%s", "Anna\"s \\ loft");
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  char escaped_name[128];
  ot_json_escape("Anna\"s \\ loft", escaped_name, sizeof escaped_name);

  // The name comes back out of the rendered document byte for byte.
  char back[OT_CONFIG_NAME_MAX + 1];
  TEST_ASSERT_EQUAL(OT_JSON_FOUND,
                    ot_json_string(g_out, "device_name", back, sizeof back));
  TEST_ASSERT_EQUAL_STRING("Anna\"s \\ loft", back);

  // And it survives a save, which is what the page actually sends: a ConfigPatch, which omits
  // the two Wi-Fi keys by design (web/src/api/client.ts). The WHOLE GET document is deliberately
  // NOT a legal patch -- test_the_network_cannot_be_set_through_the_settings_route is the other
  // half of that rule.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const std::string submitted = std::string("{\"device_name\":\"") + escaped_name + "\"}";
  TEST_ASSERT_EQUAL(OT_WIRE_OK,
                    ot_wire_parse_config(submitted.c_str(), &patch, &storage).status);
  TEST_ASSERT_EQUAL_STRING("Anna\"s \\ loft", patch.device_name);
}

// HERE WERE the tests for POST /api/entities/<key> and POST /api/ops/<name>. Both routes
// are typed by ot_command, which arrives together with the registry -- bring them
// back here at that same time, along with ot_wire_parse_entity_write() and
// ot_wire_parse_operation(). Among them was also the end-to-end test "POST body -> bytes on
// the bus": it was the only one checking the junction of ot_wire with ot_command, and
// without it nobody checks that junction.

// --- the executor's settings ----------------------------------------------------

namespace {

// The eight numbers a settings body may carry. The five values the executor owns are not here:
// they are refused by name, below.
const char *const NUMBERS[] = {"control_mode",         "watchdog_s",
                               "failsafe_setpoint_dc", "failsafe_room_target_dc",
                               "failsafe_heat_days",   "failsafe_min_cycle_s",
                               "flow_min_dc",          "flow_max_dc"};

}  // namespace

void test_the_executors_settings_are_decoded_as_whole_numbers(void) {
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const char *const body =
      "{\"control_mode\":1,\"watchdog_s\":901,"
      "\"failsafe_setpoint_dc\":452,\"failsafe_room_target_dc\":183,\"failsafe_heat_days\":4,"
      "\"failsafe_min_cycle_s\":605,\"flow_min_dc\":406,\"flow_max_dc\":707}";
  TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_config(body, &patch, &storage).status);
  TEST_ASSERT_TRUE(patch.has_control_mode);
  TEST_ASSERT_EQUAL_UINT32(1, patch.control_mode);
  TEST_ASSERT_TRUE(patch.has_watchdog_s);
  TEST_ASSERT_EQUAL_UINT32(901, patch.watchdog_s);
  TEST_ASSERT_TRUE(patch.has_failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(452, patch.failsafe_setpoint_dc);
  TEST_ASSERT_TRUE(patch.has_failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT32(183, patch.failsafe_room_target_dc);
  TEST_ASSERT_TRUE(patch.has_failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT32(4, patch.failsafe_heat_days);
  TEST_ASSERT_TRUE(patch.has_failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_UINT32(605, patch.failsafe_min_cycle_s);
  TEST_ASSERT_TRUE(patch.has_flow_min_dc);
  TEST_ASSERT_EQUAL_UINT32(406, patch.flow_min_dc);
  TEST_ASSERT_TRUE(patch.has_flow_max_dc);
  TEST_ASSERT_EQUAL_UINT32(707, patch.flow_max_dc);
}

void test_an_absent_setting_is_left_alone_and_never_read_as_zero(void) {
  // Absent is not zero and not false: a page that has not heard of heating_season must not switch
  // the season off by saving the broker.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(OT_WIRE_OK,
                    ot_wire_parse_config("{\"mqtt_host\":\"b\"}", &patch, &storage).status);
  TEST_ASSERT_FALSE(patch.has_control_mode || patch.has_heating_season || patch.has_watchdog_s ||
                    patch.has_failsafe_setpoint_dc || patch.has_failsafe_room_target_dc ||
                    patch.has_failsafe_heat_days || patch.has_failsafe_min_cycle_s ||
                    patch.has_flow_min_dc || patch.has_flow_max_dc);
}

void test_a_setting_that_is_not_a_whole_non_negative_number_names_itself(void) {
  // "450" is a string, -1 is not a count of anything, and 45.5 is a typo for a value in tenths --
  // each named, none guessed at. What a number may BE is ot_config_apply()'s answer, not this one.
  const char *const bad[] = {"\"450\"", "-1", "45.5", "true"};
  for (size_t i = 0; i < sizeof NUMBERS / sizeof NUMBERS[0]; i++) {
    for (size_t j = 0; j < sizeof bad / sizeof bad[0]; j++) {
      ot_config_patch_t       patch   = {};
      ot_wire_patch_storage_t storage = {};
      const std::string       body = std::string("{\"") + NUMBERS[i] + "\":" + bad[j] + "}";
      const ot_wire_result_t  r    = ot_wire_parse_config(body.c_str(), &patch, &storage);
      TEST_ASSERT_EQUAL_MESSAGE(OT_WIRE_BAD_FIELD, r.status, body.c_str());
      TEST_ASSERT_EQUAL_STRING(NUMBERS[i], r.field);
    }
  }
}

void test_a_number_too_big_for_its_field_reaches_apply_whole_so_it_can_be_refused(void) {
  // The parser says "a number"; ot_config_check_range() says whether it is a watchdog. Carried as
  // uint32_t so 70000 arrives as 70000 and is refused there, never as a wrapped 4464.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(OT_WIRE_OK,
                    ot_wire_parse_config("{\"watchdog_s\":70000}", &patch, &storage).status);
  TEST_ASSERT_EQUAL_UINT32(70000, patch.watchdog_s);
}

void test_a_number_past_32_bits_or_with_an_exponent_names_itself_rather_than_wrapping(void) {
  // 2^32 is the first number a uint32_t wraps to 0, and 4.5e2 is 450 written in a way a typo in
  // tenths also produces. Neither is guessed at: ot_json_u32 answers OUT_OF_RANGE and the field
  // is named, so ot_config_check_range() never sees a watchdog of 0 the owner did not type.
  const char *const bodies[] = {"{\"watchdog_s\":4294967296}", "{\"flow_min_dc\":4.5e2}"};
  const char *const fields[] = {"watchdog_s", "flow_min_dc"};
  for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
    ot_config_patch_t       patch   = {};
    ot_wire_patch_storage_t storage = {};
    const ot_wire_result_t  r = ot_wire_parse_config(bodies[i], &patch, &storage);
    TEST_ASSERT_EQUAL_MESSAGE(OT_WIRE_BAD_FIELD, r.status, bodies[i]);
    TEST_ASSERT_EQUAL_STRING(fields[i], r.field);
    TEST_ASSERT_FALSE(patch.has_watchdog_s || patch.has_flow_min_dc);
  }
}

void test_a_null_for_a_value_the_executor_owns_is_still_refused_by_name(void) {
  // null is legal JSON and no type the probe asks for. It is refused by PRESENCE like the others:
  // a reader that took null for "absent" would answer Saved over a body that tried to write it.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const ot_wire_result_t  r =
      ot_wire_parse_config("{\"dhw_enable\":null}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_REFUSED, r.status);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_READ_ONLY_FIELD, r.err);
  TEST_ASSERT_EQUAL_STRING("dhw_enable", r.field);
}

void test_a_value_the_executor_owns_is_refused_by_name_from_a_settings_body(void) {
  // These five are written through the entity path, so that one
  // value has one validator. Refused BY NAME rather than ignored, for the reason the Wi-Fi pair is:
  // a settings page that reported Saved over a hot-water switch it silently dropped is lying.
  // Presence is what is refused, whatever the type -- a string there is no less a write attempt.
  const char *const owned[]  = {"local_ch_enable", "local_ch_setpoint_dc", "dhw_enable",
                                "dhw_setpoint_dc", "heating_season"};
  const char *const values[] = {"true", "450", "false", "\"550\"", "true"};
  for (size_t i = 0; i < sizeof owned / sizeof owned[0]; i++) {
    ot_config_patch_t       patch   = {};
    ot_wire_patch_storage_t storage = {};
    const std::string body = std::string("{\"mqtt_host\":\"b\",\"") + owned[i] + "\":" + values[i] + "}";
    const ot_wire_result_t r = ot_wire_parse_config(body.c_str(), &patch, &storage);
    TEST_ASSERT_EQUAL_MESSAGE(OT_WIRE_REFUSED, r.status, owned[i]);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_READ_ONLY_FIELD, r.err, owned[i]);
    TEST_ASSERT_EQUAL_STRING(owned[i], r.field);
    // All or nothing, as for every refusal: the good broker host beside it did not land either.
    TEST_ASSERT_NULL(patch.mqtt_host);
    TEST_ASSERT_FALSE(patch.has_dhw_enable || patch.has_local_ch_enable ||
                      patch.has_local_ch_setpoint_dc || patch.has_dhw_setpoint_dc ||
                      patch.has_heating_season);
    // And the sentence names the door that works.
    TEST_ASSERT_TRUE(contains(ot_wire_strerror(r), "/api/entities/"));
  }
}

void test_heating_season_is_refused_even_as_false_or_as_a_mistyped_number(void) {
  // Home Assistant may only turn the season OFF. A settings page loaded while the
  // season was on and saved after HA had turned it off would send `true` back and heat behind HA's
  // back -- `true` is in the test above. Here the two a flag reader would let through: `false`,
  // which looks harmless, and a wrongly typed `1`. Refused by PRESENCE, never read as a flag
  // first, so no body can move the season.
  const char *const values[] = {"false", "1"};
  for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
    ot_config_patch_t       patch   = {};
    ot_wire_patch_storage_t storage = {};
    const std::string body = std::string("{\"mqtt_host\":\"b\",\"heating_season\":") + values[i] + "}";
    const ot_wire_result_t r = ot_wire_parse_config(body.c_str(), &patch, &storage);
    TEST_ASSERT_EQUAL_MESSAGE(OT_WIRE_REFUSED, r.status, body.c_str());
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_READ_ONLY_FIELD, r.err, body.c_str());
    TEST_ASSERT_EQUAL_STRING("heating_season", r.field);
    TEST_ASSERT_NULL(patch.mqtt_host);
    TEST_ASSERT_FALSE(patch.has_heating_season);
    // The sentence lists the season among the entity keys that do take it.
    TEST_ASSERT_TRUE(contains(ot_wire_strerror(r), "heating_season"));
  }
}

void test_the_config_document_renders_every_setting_of_the_executor(void) {
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  cfg.control_mode            = 1;
  cfg.heating_season          = true;
  cfg.watchdog_s              = 901;
  cfg.failsafe_setpoint_dc    = 451;
  cfg.failsafe_room_target_dc = 181;
  cfg.failsafe_heat_days      = 4;
  cfg.failsafe_min_cycle_s    = 601;
  cfg.flow_min_dc             = 401;
  cfg.flow_max_dc             = 701;
  cfg.local_ch_enable         = true;
  cfg.local_ch_setpoint_dc    = 452;
  cfg.dhw_enable              = false;
  cfg.dhw_setpoint_dc         = 550;
  ot_config_public_t pub;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  const char *const members[] = {
      "\"control_mode\":1",           "\"heating_season\":true",       "\"watchdog_s\":901",
      "\"failsafe_setpoint_dc\":451", "\"failsafe_room_target_dc\":181", "\"failsafe_heat_days\":4",
      "\"failsafe_min_cycle_s\":601", "\"flow_min_dc\":401",           "\"flow_max_dc\":701",
      "\"local_ch_enable\":true",     "\"local_ch_setpoint_dc\":452",  "\"dhw_enable\":false",
      "\"dhw_setpoint_dc\":550"};
  for (size_t i = 0; i < sizeof members / sizeof members[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, members[i]), members[i]);
  // Still one flat object the reader can walk: every member above is reachable through it.
  uint32_t mode = 0;
  TEST_ASSERT_EQUAL(OT_JSON_DOC_OK, ot_json_check(g_out));
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(g_out, "dhw_setpoint_dc", &mode));
  TEST_ASSERT_EQUAL_UINT32(550, mode);

  // The three flags once more with the season off. Above, two of them were true, and one rendered
  // from the other was the same bytes.
  cfg.heating_season = false;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"heating_season\":false"));
  TEST_ASSERT_TRUE(contains(g_out, "\"local_ch_enable\":true"));
  TEST_ASSERT_TRUE(contains(g_out, "\"dhw_enable\":false"));
}

// --- the room-source config ---------------------------------------------------

void test_room_mqtt_fields_are_decoded_and_a_wrong_type_names_itself(void) {
  // Without this decode the four fields, already defaulted in ot_config, can never be anything
  // else, and the MQTT room source can never be turned on.
  ot_config_patch_t       patch   = {};
  ot_wire_patch_storage_t storage = {};
  const char *const body = "{\"room_mqtt_enable\":true,\"room_mqtt_role\":1,"
                           "\"room_mqtt_stale_s\":600,\"room_mqtt_ha_forwarded\":true}";
  TEST_ASSERT_EQUAL(OT_WIRE_OK, ot_wire_parse_config(body, &patch, &storage).status);
  TEST_ASSERT_TRUE(patch.has_room_mqtt_enable && patch.room_mqtt_enable);
  TEST_ASSERT_TRUE(patch.has_room_mqtt_role && patch.room_mqtt_role == 1);
  TEST_ASSERT_TRUE(patch.has_room_mqtt_stale_s && patch.room_mqtt_stale_s == 600);
  TEST_ASSERT_TRUE(patch.has_room_mqtt_ha_forwarded && patch.room_mqtt_ha_forwarded);
  const ot_wire_result_t bad =
      ot_wire_parse_config("{\"room_mqtt_enable\":\"yes\"}", &patch, &storage);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_FIELD, bad.status);
  TEST_ASSERT_EQUAL_STRING("room_mqtt_enable", bad.field);
}

void test_the_config_document_renders_the_room_mqtt_fields(void) {
  ot_config_t cfg;
  ot_config_defaults(&cfg, "aabbccddeeff");
  cfg.room_mqtt_enable = true; cfg.room_mqtt_role = 1;
  cfg.room_mqtt_stale_s = 600; cfg.room_mqtt_ha_forwarded = true;
  ot_config_public_t pub; ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(ot_wire_render_config(&pub, false, g_out, sizeof g_out) > 0);
  const char *const members[] = {"\"room_mqtt_enable\":true", "\"room_mqtt_role\":1",
                                 "\"room_mqtt_stale_s\":600", "\"room_mqtt_ha_forwarded\":true"};
  for (size_t i = 0; i < sizeof members / sizeof members[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, members[i]), members[i]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_full_settings_submission_is_decoded);
  RUN_TEST(test_an_absent_field_is_a_null_pointer_and_an_empty_one_is_an_empty_string);
  RUN_TEST(test_an_empty_object_is_a_legitimate_save_that_changes_nothing);
  RUN_TEST(test_a_body_that_is_not_an_object_is_not_read_as_an_empty_save);
  RUN_TEST(test_the_network_cannot_be_set_through_the_settings_route);
  RUN_TEST(test_an_unknown_field_is_ignored_so_a_newer_page_can_still_save);
  RUN_TEST(test_the_derived_fields_are_ignored_rather_than_taken_as_input);
  RUN_TEST(test_a_field_of_the_wrong_type_names_itself);
  RUN_TEST(test_a_port_outside_the_range_is_refused_and_never_wrapped);
  RUN_TEST(test_a_value_too_long_for_the_field_is_refused_rather_than_truncated);
  RUN_TEST(test_a_refused_submission_leaves_no_half_decoded_patch_behind);
  RUN_TEST(test_the_config_document_carries_every_key_the_page_reads);
  RUN_TEST(test_no_stored_secret_appears_in_the_config_document);
  RUN_TEST(test_an_unset_secret_reads_back_as_an_empty_string_not_as_the_sentinel);
  RUN_TEST(test_the_known_good_flag_comes_from_the_machine_and_not_from_the_stored_ssid);
  RUN_TEST(test_a_document_that_does_not_fit_reports_zero_rather_than_a_short_one);
  RUN_TEST(test_a_device_name_with_a_quote_in_it_survives_the_round_trip);
  RUN_TEST(test_the_executors_settings_are_decoded_as_whole_numbers);
  RUN_TEST(test_an_absent_setting_is_left_alone_and_never_read_as_zero);
  RUN_TEST(test_a_setting_that_is_not_a_whole_non_negative_number_names_itself);
  RUN_TEST(test_a_number_too_big_for_its_field_reaches_apply_whole_so_it_can_be_refused);
  RUN_TEST(test_a_number_past_32_bits_or_with_an_exponent_names_itself_rather_than_wrapping);
  RUN_TEST(test_a_null_for_a_value_the_executor_owns_is_still_refused_by_name);
  RUN_TEST(test_a_value_the_executor_owns_is_refused_by_name_from_a_settings_body);
  RUN_TEST(test_heating_season_is_refused_even_as_false_or_as_a_mistyped_number);
  RUN_TEST(test_the_config_document_renders_every_setting_of_the_executor);
  RUN_TEST(test_room_mqtt_fields_are_decoded_and_a_wrong_type_names_itself);
  RUN_TEST(test_the_config_document_renders_the_room_mqtt_fields);
  return UNITY_END();
}
