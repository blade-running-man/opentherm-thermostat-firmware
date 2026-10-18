// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What survives a power cut, and what must never be allowed to.
//
// Every case here is a failure scenario. The happy path of a settings page gets exercised by
// hand on every build; what does not is the power cut between two NVS writes, the 32-character
// SSID that only one household in a hundred has, the corrupt record that locks the owner out
// of their own device, and the OTA that renames a key and sends everybody back to setup.
// Those are the cases below, and each of them enforces a rule stated in ot_config.h
// -- they are invariants, not preferences.
//
// This suite reaches only the PURE half of ot_config. ot_config_nvs.c is the thin
// layer that opens namespaces and moves bytes; it is device-only on purpose, because a suite
// that tested a fake NVS would be testing the fake. What can be decided without flash is
// decided here, which is nearly all of it.
//
// Cut out of test/test_config/: that one file had reached 1549 lines against the
// 600-line ceiling of a suite (CLAUDE.md, File ceiling), and a suite can only be cut along
// directories, because PlatformIO links every .cpp of a directory into one binary. Every test that
// came from there was moved byte for byte, not rewritten.
//
// The helpers below are this directory's OWN copy of exactly the ones its tests call. DO NOT
// hoist them into a shared directory: the moment two suites share a helper, "it grew a capability
// for suite B" becomes a way to quietly weaken suite A (CLAUDE.md, Tests). And a directory named
// test/test_config_common/ would itself be collected as a suite by `test_filter = test_*`.
//
// This directory: the names things are stored under, the two reset tiers that erase them by
// namespace, and the schema number that says which layout the flash holds. They are one contract
// -- a renamed key, a field filed under the wrong namespace and a layout change without a bump
// each send a working device back to its setup page -- so a change to the stored layout is
// reviewed against all three in one file. None of the shared helpers is needed here.
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "ot_config.h"

void setUp(void) {}
void tearDown(void) {}

// --- the names are a contract ------------------------------------------------------------

void test_the_stored_names_are_pinned_because_renaming_one_costs_the_owner_their_network(void) {
  // An OTA that renames a key does not fail: it silently finds nothing under the new name,
  // and the device that was on the owner's network for a year comes up on its own access
  // point asking to be set up. The literals below are the contract. Changing one is allowed;
  // changing one WITHOUT a migration and a schema bump is what this test forbids.
  TEST_ASSERT_EQUAL_STRING("cfg_wifi", ot_config_ns_name(OT_CONFIG_NS_WIFI));
  TEST_ASSERT_EQUAL_STRING("cfg_owner", ot_config_ns_name(OT_CONFIG_NS_OWNER));
  TEST_ASSERT_EQUAL_STRING("cfg_app", ot_config_ns_name(OT_CONFIG_NS_APP));

  TEST_ASSERT_EQUAL_STRING("sta", OT_CONFIG_KEY_STA);
  TEST_ASSERT_EQUAL_STRING("sta_good", OT_CONFIG_KEY_STA_GOOD);
  TEST_ASSERT_EQUAL_STRING("schema", OT_CONFIG_KEY_SCHEMA);
  // The two facts ot_prov_boot_t needs back after a power cut. Renaming one of these does
  // not lose a network, it loses a WAY BACK: `win_closed` forgotten means a device offers the
  // first-run fifteen minutes for ever instead of five, and `no_addr` forgotten means the
  // station that associates and is never given an address reproduces that state at every boot
  // with no access point to reach it through (ot_provision.h, address_never_arrived).
  TEST_ASSERT_EQUAL_STRING("win_closed", OT_CONFIG_KEY_WINDOW_CLOSED);
  TEST_ASSERT_EQUAL_STRING("no_addr", OT_CONFIG_KEY_NO_ADDRESS);

  struct {
    ot_config_field_t field;
    const char             *name;
    const char             *key;
  } const expected[] = {
      {OT_CONFIG_F_WIFI_SSID, "wifi_ssid", "sta"},
      {OT_CONFIG_F_WIFI_PSK, "wifi_psk", "sta"},
      {OT_CONFIG_F_MQTT_HOST, "mqtt_host", "mqtt_host"},
      {OT_CONFIG_F_MQTT_PORT, "mqtt_port", "mqtt_port"},
      {OT_CONFIG_F_MQTT_USER, "mqtt_user", "mqtt_user"},
      {OT_CONFIG_F_MQTT_PASSWORD, "mqtt_password", "mqtt_password"},
      {OT_CONFIG_F_TOPIC_PREFIX, "topic_prefix", "topic_prefix"},
      {OT_CONFIG_F_HA_DISCOVERY, "ha_discovery", "ha_discovery"},
      {OT_CONFIG_F_DEVICE_NAME, "device_name", "device_name"},
      // The one row where the stored thing is not the named thing: what is kept is a one-way
      // record, and calling its key `ui_password` would invite a reader to believe otherwise.
      {OT_CONFIG_F_UI_PASSWORD, "ui_password", "ui_pw_hash"},
      // The NVS key is abbreviated and the API name is not. The cap is 15 characters including
      // nothing -- see the test below -- and `ntp_server` would fit, but the pair is kept
      // deliberately asymmetric so that this table stays the one place the two are related.
      {OT_CONFIG_F_TZ, "tz", "tz"},
      {OT_CONFIG_F_NTP_SERVER, "ntp_server", "ntp"},
      // Renamed in schema 2, with the migration the comment above demands: the old `cl_dhw_en` is
      // carried into `dhw_en` and then erased.
      {OT_CONFIG_F_DHW_ENABLE, "dhw_enable", "dhw_en"},
      // The executor's settings: abbreviated keys and full
      // API names, for the reason the NTP row gives. `_dc` is tenths of a degree.
      {OT_CONFIG_F_CONTROL_MODE, "control_mode", "ctl_mode"},
      {OT_CONFIG_F_HEATING_SEASON, "heating_season", "season"},
      {OT_CONFIG_F_WATCHDOG_S, "watchdog_s", "wd_s"},
      {OT_CONFIG_F_FAILSAFE_SETPOINT, "failsafe_setpoint_dc", "fs_sp"},
      {OT_CONFIG_F_FAILSAFE_ROOM_TARGET, "failsafe_room_target_dc", "fs_room"},
      {OT_CONFIG_F_FAILSAFE_HEAT_DAYS, "failsafe_heat_days", "fs_days"},
      {OT_CONFIG_F_FAILSAFE_MIN_CYCLE, "failsafe_min_cycle_s", "fs_cycle"},
      {OT_CONFIG_F_FLOW_MIN, "flow_min_dc", "flow_min"},
      {OT_CONFIG_F_FLOW_MAX, "flow_max_dc", "flow_max"},
      {OT_CONFIG_F_LOCAL_CH_ENABLE, "local_ch_enable", "loc_ch"},
      {OT_CONFIG_F_LOCAL_CH_SETPOINT, "local_ch_setpoint_dc", "loc_ch_sp"},
      {OT_CONFIG_F_DHW_SETPOINT, "dhw_setpoint_dc", "dhw_sp"},
      // The MQTT room-source slot. NS_APP, like the executor's
      // settings above -- a soft reset does not throw away a slot the owner wired up in Home
      // Assistant just because they typed the Wi-Fi password wrong.
      {OT_CONFIG_F_ROOM_MQTT_ENABLE, "room_mqtt_enable", "room_en"},
      {OT_CONFIG_F_ROOM_MQTT_ROLE, "room_mqtt_role", "room_role"},
      {OT_CONFIG_F_ROOM_MQTT_STALE_S, "room_mqtt_stale_s", "room_stale"},
      {OT_CONFIG_F_ROOM_MQTT_HA_FORWARDED, "room_mqtt_ha_forwarded", "room_ha_fwd"},
  };
  TEST_ASSERT_EQUAL_size_t(OT_CONFIG_F_COUNT, sizeof expected / sizeof expected[0]);

  for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
    const ot_config_field_info_t *info = ot_config_field(expected[i].field);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_STRING(expected[i].name, info->name);
    TEST_ASSERT_EQUAL_STRING(expected[i].key, info->key);
  }
}

void test_every_stored_name_is_short_enough_for_nvs_to_accept_it(void) {
  // NVS_KEY_NAME_MAX_SIZE is 16 INCLUDING the terminator (nvs.h:61) and namespaces share that
  // limit (nvs.h:62). A 16-character name is not truncated -- nvs_set_* returns
  // ESP_ERR_NVS_KEY_TOO_LONG -- and since nothing in this component may ESP_ERROR_CHECK a
  // store result, the write would fail silently and the field would simply never persist.
  // Asked over the whole table so a field added later cannot slip past a per-call-site check.
  for (int i = 0; i < OT_CONFIG_NS_COUNT; i++) {
    const char *ns = ot_config_ns_name((ot_config_ns_t)i);
    TEST_ASSERT_NOT_NULL(ns);
    TEST_ASSERT_TRUE_MESSAGE(strlen(ns) <= OT_CONFIG_NVS_NAME_MAX, ns);
    TEST_ASSERT_TRUE_MESSAGE(strlen(ns) > 0, "an empty namespace name is refused by nvs_open");
  }
  for (int i = 0; i < OT_CONFIG_F_COUNT; i++) {
    const ot_config_field_info_t *info = ot_config_field((ot_config_field_t)i);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE_MESSAGE(strlen(info->key) <= OT_CONFIG_NVS_NAME_MAX, info->key);
    TEST_ASSERT_TRUE_MESSAGE(strlen(info->key) > 0, "an empty key is refused by nvs_set_*");
  }
  TEST_ASSERT_TRUE(strlen(OT_CONFIG_KEY_STA) <= OT_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(OT_CONFIG_KEY_STA_GOOD) <= OT_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(OT_CONFIG_KEY_SCHEMA) <= OT_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(OT_CONFIG_KEY_WINDOW_CLOSED) <= OT_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(OT_CONFIG_KEY_NO_ADDRESS) <= OT_CONFIG_NVS_NAME_MAX);
  // And the schema key does not collide with a field already in cfg_app -- one key overwriting
  // the broker host is not a compile error, it is a device that stops reaching its broker.
  const char *const app_keys[] = {"mqtt_host", "mqtt_port", "mqtt_user", "mqtt_password",
                                  "topic_prefix", "ha_discovery", "device_name",
                                  "tz", "ntp", "dhw_en",
                                  "ctl_mode", "season", "wd_s", "fs_sp", "fs_room", "fs_days",
                                  "fs_cycle", "flow_min", "flow_max", "loc_ch", "loc_ch_sp",
                                  "dhw_sp", "room_en", "room_role", "room_stale", "room_ha_fwd"};
  for (size_t j = 0; j < sizeof app_keys / sizeof app_keys[0]; j++)
    TEST_ASSERT_TRUE_MESSAGE(strcmp(OT_CONFIG_KEY_SCHEMA, app_keys[j]) != 0, app_keys[j]);
}

void test_the_provisioning_flags_live_where_forgetting_the_network_forgets_them_too(void) {
  // They are in the Wi-Fi namespace, which is the one the SOFT reset erases. That is the
  // right tier and not an accident of filing: the soft gesture means "forget this network", and a
  // device that has forgotten its network is a device on its first run again -- it should get
  // the fifteen-minute first-run window, not the five that `win_closed` would still be asking for. Filing them
  // under cfg_app instead would leave a factory-reset device with a five-minute window and a
  // no-address escape armed against a network it no longer has.
  TEST_ASSERT_TRUE(ot_config_reset_erases(OT_CONFIG_RESET_SOFT,
                                                OT_CONFIG_NS_WIFI));
  TEST_ASSERT_TRUE(ot_config_reset_erases(OT_CONFIG_RESET_HARD,
                                                OT_CONFIG_NS_WIFI));
}

void test_no_two_things_are_stored_under_one_name_in_the_wifi_namespace(void) {
  // Four keys share cfg_wifi now, and a collision between two of them is not a compile error --
  // it is a boolean overwriting half a credential, discovered on somebody's kitchen counter.
  const char *const keys[] = {
      OT_CONFIG_KEY_STA,
      OT_CONFIG_KEY_STA_GOOD,
      OT_CONFIG_KEY_WINDOW_CLOSED,
      OT_CONFIG_KEY_NO_ADDRESS,
  };
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    for (size_t j = i + 1; j < sizeof keys / sizeof keys[0]; j++)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(keys[i], keys[j]) != 0, keys[i]);
}

void test_the_wifi_pair_is_one_stored_record_and_the_table_says_so(void) {
  // The invariant, expressed as storage rather than as a comment: two fields, ONE key. NVS
  // gives no atomicity between keys, so a power cut between two writes leaves a new SSID with
  // an old password -- indistinguishable from a typo, and it strands the device just as
  // thoroughly. Splitting them "for symmetry with the API" would pass
  // every other test in this file and reintroduce that failure whole.
  const ot_config_field_info_t *ssid = ot_config_field(OT_CONFIG_F_WIFI_SSID);
  const ot_config_field_info_t *psk  = ot_config_field(OT_CONFIG_F_WIFI_PSK);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(ssid->key, psk->key,
                                   "the SSID and the PSK are stored under two keys again");
  TEST_ASSERT_EQUAL_STRING(OT_CONFIG_KEY_STA, ssid->key);
  TEST_ASSERT_EQUAL(OT_CONFIG_NS_WIFI, ssid->ns);
  TEST_ASSERT_EQUAL(OT_CONFIG_NS_WIFI, psk->ns);
}

void test_a_field_that_is_a_secret_is_one_that_ot_secrets_also_calls_a_secret(void) {
  // Two independent judgements about the same field, and they must agree. ot_secrets
  // redacts by the WORDS in a key, not by a list of key names, so a field named `mqtt_pw`
  // would look perfectly sensible in the table here and be published in full by every
  // projection. The disagreement is the bug; this is where it is caught.
  for (int i = 0; i < OT_CONFIG_F_COUNT; i++) {
    const ot_config_field_info_t *info = ot_config_field((ot_config_field_t)i);
    TEST_ASSERT_EQUAL_MESSAGE(info->secret, ot_secret_key(info->name), info->name);
  }
  TEST_ASSERT_TRUE(ot_config_field(OT_CONFIG_F_WIFI_PSK)->secret);
  TEST_ASSERT_TRUE(ot_config_field(OT_CONFIG_F_MQTT_PASSWORD)->secret);
  TEST_ASSERT_TRUE(ot_config_field(OT_CONFIG_F_UI_PASSWORD)->secret);
  TEST_ASSERT_FALSE(ot_config_field(OT_CONFIG_F_WIFI_SSID)->secret);
  TEST_ASSERT_FALSE(ot_config_field(OT_CONFIG_F_MQTT_USER)->secret);
}

void test_an_unknown_field_is_null_rather_than_a_crash(void) {
  // Field ids arrive from a URL. A caller that gets the entity list wrong should get a 404,
  // not a reboot of the thermostat.
  TEST_ASSERT_NULL(ot_config_field(OT_CONFIG_F_COUNT));
  TEST_ASSERT_NULL(ot_config_field((ot_config_field_t)-1));
  TEST_ASSERT_NULL(ot_config_ns_name(OT_CONFIG_NS_COUNT));
  TEST_ASSERT_NULL(ot_config_ns_name((ot_config_ns_t)-1));
}

void test_every_field_has_a_bit_of_its_own_in_the_repair_set(void) {
  // ot_config_repairs_t is a uint32_t with one bit per field id (OT_CONFIG_REPAIRED). A 33rd
  // field would shift a 1 off the end -- undefined in C, and in practice a repair reported under
  // another field's name or under none, which is a silent loss arriving through the bitmask.
  // The table is 25 rows (the two climate bits retired onto the erase list); the room
  // slot adds four more, to 29 -- still inside the 32 a uint32_t bitmask has.
  TEST_ASSERT_TRUE_MESSAGE(OT_CONFIG_F_COUNT <= 8 * sizeof(ot_config_repairs_t),
                           "more fields than ot_config_repairs_t has bits");
}

// --- the two-tier reset --------------------------------------------------------------------

void test_the_soft_reset_clears_the_network_and_the_password_and_keeps_the_broker(void) {
  // Five seconds AFTER boot. The UI password goes with the Wi-Fi
  // deliberately: otherwise a forgotten password on a device that is visible on the network
  // and completely unusable can only be fixed with a USB cable, and the device is a thermostat
  // screwed to a wall.
  TEST_ASSERT_TRUE(ot_config_reset_erases(OT_CONFIG_RESET_SOFT, OT_CONFIG_NS_WIFI));
  TEST_ASSERT_TRUE(ot_config_reset_erases(OT_CONFIG_RESET_SOFT, OT_CONFIG_NS_OWNER));
  TEST_ASSERT_FALSE_MESSAGE(
      ot_config_reset_erases(OT_CONFIG_RESET_SOFT, OT_CONFIG_NS_APP),
      "the soft gesture threw away the broker, which it must keep");
}

void test_the_hard_reset_erases_every_namespace_that_exists_now_or_later(void) {
  // Asked over the enum, not over a hand-written list, and that is the whole point. A key list
  // goes stale the first time a field is added -- and it can never name a key that an OLDER
  // firmware wrote and this build has not heard of. The named failure is a factory reset that
  // leaves the previous owner's broker password in the flash of a device that has been sold.
  for (int i = 0; i < OT_CONFIG_NS_COUNT; i++)
    TEST_ASSERT_TRUE_MESSAGE(
        ot_config_reset_erases(OT_CONFIG_RESET_HARD, (ot_config_ns_t)i),
        ot_config_ns_name((ot_config_ns_t)i));
}

void test_every_field_lands_on_the_side_of_the_soft_reset_that_d6_puts_it_on(void) {
  // The tier is carried by the NAMESPACE, so a field added to the wrong one changes what a
  // reset does without anyone touching the reset code. This is the test that notices.
  for (int i = 0; i < OT_CONFIG_F_COUNT; i++) {
    const ot_config_field_t       f    = (ot_config_field_t)i;
    const ot_config_field_info_t *info = ot_config_field(f);
    const bool soft_clears = ot_config_reset_erases(OT_CONFIG_RESET_SOFT, info->ns);
    const bool should_clear = (f == OT_CONFIG_F_WIFI_SSID || f == OT_CONFIG_F_WIFI_PSK ||
                               f == OT_CONFIG_F_UI_PASSWORD);
    TEST_ASSERT_EQUAL_MESSAGE(should_clear, soft_clears, info->name);
  }
}

void test_an_unknown_namespace_is_erased_by_neither_tier(void) {
  TEST_ASSERT_FALSE(ot_config_reset_erases(OT_CONFIG_RESET_HARD, OT_CONFIG_NS_COUNT));
  TEST_ASSERT_FALSE(ot_config_reset_erases(OT_CONFIG_RESET_SOFT, (ot_config_ns_t)-1));
}

// --- schema ---------------------------------------------------------------------------------

void test_the_schema_version_is_pinned(void) {
  // Bumped when the MEANING or the layout of a stored value changes, never for a field added
  // with a default a fresh read already produces. A bump without a migration is what sends an
  // owner back to the setup page after an update.
  // 2 in schema-v2: the DHW bit changed its key and the executor retired keys. An older
  // store holds schema 1 and must take the migration, not be read as current.
  TEST_ASSERT_EQUAL_UINT32(2u, OT_CONFIG_SCHEMA_VERSION);
  TEST_ASSERT_EQUAL(OT_CONFIG_SCHEMA_MIGRATE, ot_config_schema_check(1));
}

void test_a_retired_key_is_on_the_erase_list_and_never_stored_under_again(void) {
  // NVS saves per key and erases only by namespace, so a retired key stays in the flash until it
  // is erased by name -- at every boot of a writable store and at every save, whatever the schema
  // number (ot_config_nvs.c), because cl_auto and cl_man_ch were retired with no bump. An OTA rollback
  // to an older build would read a stale one back as live (cl_man_ch = true is manual heat); a later field
  // reusing the name would inherit its value. The list is pinned literal by literal because the
  // device erases exactly what ot_config_retired_key() lists -- a name dropped from it is a key
  // left behind on every upgraded device.
  const char *const retired[] = {"cl_dhw_en", "cl_auto", "cl_man_ch"};
  size_t            n         = 0;
  while (ot_config_retired_key(n) != nullptr)
    n++;
  TEST_ASSERT_EQUAL_size_t(sizeof retired / sizeof retired[0], n);
  TEST_ASSERT_NULL(ot_config_retired_key((size_t)-1));
  TEST_ASSERT_EQUAL_STRING("cl_dhw_en", OT_CONFIG_KEY_V1_DHW_EN);
  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL_STRING(retired[i], ot_config_retired_key(i));
    TEST_ASSERT_TRUE_MESSAGE(strlen(retired[i]) <= OT_CONFIG_NVS_NAME_MAX, retired[i]);
    TEST_ASSERT_TRUE_MESSAGE(strcmp(OT_CONFIG_KEY_SCHEMA, retired[i]) != 0, retired[i]);
    for (int f = 0; f < OT_CONFIG_F_COUNT; f++)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(ot_config_field((ot_config_field_t)f)->key, retired[i]) != 0,
                               retired[i]);
  }
}

void test_a_fresh_device_and_an_older_one_take_the_same_path(void) {
  // A device with nothing stored reads 0. Making that a third answer would mean three code
  // paths where two suffice: migration from nothing finds nothing, which is exactly right -- a
  // first boot and an upgrade from an older schema take the one path that has been tested.
  TEST_ASSERT_EQUAL(OT_CONFIG_SCHEMA_MIGRATE, ot_config_schema_check(0));
  TEST_ASSERT_EQUAL(OT_CONFIG_SCHEMA_CURRENT,
                    ot_config_schema_check(OT_CONFIG_SCHEMA_VERSION));
}

void test_a_schema_from_the_future_is_read_only_rather_than_erased(void) {
  // An OTA rollback. Both obvious answers are wrong: erasing destroys a working owner's
  // configuration to fix a problem they do not have, and refusing to boot is a brick screwed to
  // a wall. Read-only keeps the thermostat driving the boiler and leaves the fifteen-second
  // gesture as the way out.
  TEST_ASSERT_EQUAL(OT_CONFIG_SCHEMA_FUTURE,
                    ot_config_schema_check(OT_CONFIG_SCHEMA_VERSION + 1));
  TEST_ASSERT_EQUAL(OT_CONFIG_SCHEMA_FUTURE, ot_config_schema_check(0xffffffffu));
}

void test_every_refusal_has_a_name_of_its_own(void) {
  // The page matches on these names to point at a field (POST /api/config's `field`), so they are
  // pinned literal by literal. Every code has one: a code appended without a case is a compile
  // error of the host build (-Werror=switch, [env:native]), and one given a sentence but no name
  // fails the sweep below instead of reaching the page as "rejected".
  struct {
    ot_config_err_t err;
    const char     *name;
  } const expected[] = {
      {OT_CONFIG_OK, "ok"},
      {OT_CONFIG_ERR_SSID, "ssid"},
      {OT_CONFIG_ERR_PSK, "psk"},
      {OT_CONFIG_ERR_WIFI_PAIR, "wifi-pair"},
      {OT_CONFIG_ERR_HOST, "mqtt-host"},
      {OT_CONFIG_ERR_PORT, "mqtt-port"},
      {OT_CONFIG_ERR_USER, "mqtt-user"},
      {OT_CONFIG_ERR_BROKER_PASSWORD, "mqtt-password"},
      {OT_CONFIG_ERR_PREFIX, "topic-prefix"},
      {OT_CONFIG_ERR_NAME, "device-name"},
      {OT_CONFIG_ERR_UI_PASSWORD, "ui-password"},
      {OT_CONFIG_ERR_NO_HASH, "no-hash"},
      {OT_CONFIG_ERR_READ_ONLY, "read-only"},
      {OT_CONFIG_ERR_NO_DOCUMENT, "no-document"},
      {OT_CONFIG_ERR_TZ, "tz"},
      {OT_CONFIG_ERR_NTP, "ntp-server"},
      {OT_CONFIG_ERR_FLOW, "flow"},
      {OT_CONFIG_ERR_FAILSAFE, "failsafe"},
      {OT_CONFIG_ERR_MODE_NEEDS_BROKER, "mode-needs-broker"},
      {OT_CONFIG_ERR_RANGE, "range"},
      {OT_CONFIG_ERR_READ_ONLY_FIELD, "read-only-field"},
      {OT_CONFIG_ERR_BROKER_PAIR, "broker-pair"},
  };
  const size_t n = sizeof expected / sizeof expected[0];
  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i].name, ot_config_err_name(expected[i].err));
    for (size_t j = 0; j < i; j++)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(expected[i].name, expected[j].name) != 0, expected[i].name);
  }
  // Swept over values, not over the table: a table counted against the last enumerator does not
  // move when a code is appended after it. A code is one strerror has a sentence for; it must
  // have a name exactly then, and the table must pin every one of them.
  const char *const fallback = "the settings were refused";
  size_t            named    = 0;
  char              msg[48];
  // Below 32: the enum's representable range is 0..31, and casting past it is UB in C++17.
  for (int e = 0; e < 32; e++) {
    const bool has_sentence = strcmp(ot_config_strerror((ot_config_err_t)e), fallback) != 0;
    const bool has_name     = strcmp(ot_config_err_name((ot_config_err_t)e), "rejected") != 0;
    snprintf(msg, sizeof msg, "code %d: a sentence and a name, or neither", e);
    TEST_ASSERT_EQUAL_MESSAGE(has_sentence, has_name, msg);
    named += has_name;
  }
  TEST_ASSERT_EQUAL_size_t_MESSAGE(n, named, "a named code without a row in the table above");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_stored_names_are_pinned_because_renaming_one_costs_the_owner_their_network);
  RUN_TEST(test_every_stored_name_is_short_enough_for_nvs_to_accept_it);
  RUN_TEST(test_the_provisioning_flags_live_where_forgetting_the_network_forgets_them_too);
  RUN_TEST(test_no_two_things_are_stored_under_one_name_in_the_wifi_namespace);
  RUN_TEST(test_the_wifi_pair_is_one_stored_record_and_the_table_says_so);
  RUN_TEST(test_a_field_that_is_a_secret_is_one_that_ot_secrets_also_calls_a_secret);
  RUN_TEST(test_an_unknown_field_is_null_rather_than_a_crash);
  RUN_TEST(test_every_field_has_a_bit_of_its_own_in_the_repair_set);

  RUN_TEST(test_the_soft_reset_clears_the_network_and_the_password_and_keeps_the_broker);
  RUN_TEST(test_the_hard_reset_erases_every_namespace_that_exists_now_or_later);
  RUN_TEST(test_every_field_lands_on_the_side_of_the_soft_reset_that_d6_puts_it_on);
  RUN_TEST(test_an_unknown_namespace_is_erased_by_neither_tier);

  RUN_TEST(test_the_schema_version_is_pinned);
  RUN_TEST(test_a_retired_key_is_on_the_erase_list_and_never_stored_under_again);
  RUN_TEST(test_a_fresh_device_and_an_older_one_take_the_same_path);
  RUN_TEST(test_a_schema_from_the_future_is_read_only_rather_than_erased);
  RUN_TEST(test_every_refusal_has_a_name_of_its_own);
  return UNITY_END();
}
