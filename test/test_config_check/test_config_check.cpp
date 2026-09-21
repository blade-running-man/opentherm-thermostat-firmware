// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What survives a power cut, and what must never be allowed to.
//
// Every case here is a failure scenario. The happy path of a settings page gets exercised by
// hand on every build; what does not is the power cut between two NVS writes, the 32-character
// SSID that only one household in a hundred has, the corrupt record that locks the owner out
// of their own device, and the OTA that renames a key and sends everybody back to setup.
// Those are the cases below, and each of them enforces a rule stated in ot_config.h and the
// header comment below -- they are invariants, not preferences.
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
// This directory: validation on the way in -- every ot_config_check_*() and the sentence each
// refusal produces. The section is moved whole, so it also carries the earlier cases for the
// time zone that were filed under this heading when they were written.
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "ot_config.h"

// A stand-in for PBKDF2, and NOT a key derivation function -- it is fast, it is reversible,
// and it would be a security defect on a device. It exists because mbedtls is on the ESP32 and
// not on this host, and because without an injected hash the record format, the salt handling
// and the answer to a wrong password could not be tested at all. Its one required property is
// that it is a function of all four inputs: every case below turns on one of them changing.
static bool fake_kdf(const char *password, const uint8_t *salt, size_t salt_len,
                     uint32_t iterations, uint8_t *out, size_t out_len) {
  uint32_t h = 2166136261u ^ iterations;
  for (const char *p = password; *p != '\0'; p++)
    h = (h ^ (uint8_t)*p) * 16777619u;
  for (size_t i = 0; i < salt_len; i++)
    h = (h ^ salt[i]) * 16777619u;
  for (size_t i = 0; i < out_len; i++) {
    h = (h ^ (uint32_t)i) * 16777619u;
    out[i] = (uint8_t)(h >> 24);
  }
  return true;
}

static const uint8_t SALT_A[OT_CONFIG_SALT_LEN] = {1, 2,  3,  4,  5,  6,  7,  8,
                                                         9, 10, 11, 12, 13, 14, 15, 16};

static ot_config_hash_ctx_t hash_with(const uint8_t *salt, ot_config_kdf_t kdf) {
  ot_config_hash_ctx_t ctx{};
  ctx.kdf        = kdf;
  ctx.salt       = salt;
  ctx.iterations = 0;  // 0 means the compiled-in count; see OT_CONFIG_KDF_ITERATIONS
  return ctx;
}

// The checkers take a length because an SSID is not a C string where it comes from. Spelling
// the cast at forty call sites would bury what each case is actually about.
static ot_config_err_t ssid_check(const char *s) {
  return ot_config_check_ssid((const uint8_t *)s, strlen(s));
}
static ot_config_err_t psk_check(const char *s) {
  return ot_config_check_psk((const uint8_t *)s, strlen(s));
}

static void repeat(char *out, char c, size_t n) {
  memset(out, c, n);
  out[n] = '\0';
}

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static ot_config_t fresh(void) {
  ot_config_t cfg{};
  ot_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

// --- validation on the way in -------------------------------------------------------------

void test_a_thirty_two_character_ssid_is_legal_and_a_thirty_three_character_one_is_not(void) {
  // 802.11 caps an SSID at 32 octets, and wifi_sta_config_t.ssid is exactly 32 with no length
  // field beside it (esp_wifi_types_generic.h:559-593). So 32 is the legal maximum AND the
  // buffer, and the obvious `sizeof dst - 1` truncates a real network's name to 31 characters,
  // after which the device never associates and nothing in the symptom says why.
  char s[64];
  repeat(s, 'x', 32);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ssid_check(s));
  repeat(s, 'x', 33);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_SSID, ssid_check(s));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ssid_check("a"));
}

void test_an_empty_ssid_is_not_a_network(void) {
  // An earlier design stored a zeroed blob on `Forget Wi-Fi` and then spent a boot trying
  // to associate with "" (wifi_component.cpp:655-663; ap_guard.cpp:143-153 is the fix, not the
  // bug). An empty SSID is refused here so that state cannot be created at all.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_SSID, ssid_check(""));
}

void test_an_ssid_with_a_nul_in_it_is_refused_because_the_radio_cannot_express_it(void) {
  // wifi_sta_config_t has no ssid_len (esp_wifi_types_generic.h:559-593), so a name shorter
  // than 32 bytes is terminated -- which means an embedded NUL truncates the SSID at the
  // driver, and the device associates with a different network name than the one stored.
  // Refused here rather than discovered on a roof.
  const uint8_t with_nul[] = {'M', 'y', 0x00, 'N', 'e', 't'};
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_SSID, ot_config_check_ssid(with_nul, sizeof with_nul));
}

void test_a_sixty_four_character_psk_is_legal_and_must_be_hexadecimal(void) {
  // 64 characters is not a long passphrase, it is the 256-bit PSK written as hex, and
  // wpa_supplicant treats it that way by LENGTH ALONE: strlen(password) == 64 goes to
  // hexstr2bin, anything else to pbkdf2_sha1 (rsn_supp/wpa.c:2517-2524). When hexstr2bin
  // fails, that function simply returns -- no PMK is ever computed, no error surfaces, and
  // the device just never associates. A 64-character non-hex value is therefore refused on
  // the way in, because on the way out it is silence.
  char s[80];
  repeat(s, 'a', 64);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, psk_check(s));
  repeat(s, 'z', 64);
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_PSK, psk_check(s),
                            "a 64-character non-hex key was stored; it can never associate");
  repeat(s, 'z', 63);
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_OK, psk_check(s),
                            "63 characters is a passphrase, and any character is legal in one");
  repeat(s, 'a', 65);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PSK, psk_check(s));
}

void test_a_passphrase_shorter_than_eight_characters_is_not_one(void) {
  // WPA2 sets the floor at 8. esp_wifi accepts a shorter one and then never associates, which
  // reads to the owner exactly like a wrong password on a good network.
  char s[16];
  repeat(s, 'x', 7);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PSK, psk_check(s));
  repeat(s, 'x', 8);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, psk_check(s));
}

void test_an_open_network_has_no_key_and_that_is_not_an_error(void) {
  // A guest network with no password is a network. Refusing it here would make the device
  // impossible to put on one.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, psk_check(""));
}

void test_a_psk_with_a_nul_in_it_is_refused(void) {
  // Same reason as the SSID, and worse: wpa.c:2517 measures the password with strlen(), so a
  // 64-byte key with a NUL at byte 10 is not even taken as hex -- it is run through
  // pbkdf2_sha1 as a 10-character passphrase.
  const uint8_t with_nul[] = {'s', 'e', 'c', 'r', 'e', 't', 0x00, 'x', 'y', 'z'};
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PSK, ot_config_check_psk(with_nul, sizeof with_nul));
}

void test_a_port_outside_the_range_is_refused_rather_than_wrapped(void) {
  // The checker takes a uint32_t on purpose. A JSON body carrying 70000 into a uint16_t
  // becomes 4464, and the device then quietly talks to a port the owner never typed --
  // wrapping is how "validated" turns into "accepted something else".
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_port(1883));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_port(1));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_port(65535));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PORT, ot_config_check_port(0));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PORT, ot_config_check_port(65536));
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_PORT, ot_config_check_port(70000),
                            "70000 was accepted; stored as a uint16_t it is port 4464");
}

void test_a_broker_host_is_a_host_and_not_a_url(void) {
  // The port is its own field, so a scheme in this one is a configuration the device can never
  // act on: it resolves nothing and connects to nothing. Refusing it at the form is a sentence
  // the owner can act on; accepting it is a support request that starts "it just says
  // disconnected".
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host("mqtt.lan"));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host("192.168.1.10"));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host("fd00::1"));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host("home-assistant_1.local"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_HOST, ot_config_check_host("mqtt://broker.lan"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_HOST, ot_config_check_host("broker lan"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_HOST, ot_config_check_host("broker\n.lan"));
}

void test_no_broker_is_a_supported_configuration(void) {
  // The device ventilates a house whether or not anyone runs a broker, and CLAUDE.md's rule
  // that nothing reboots because a peer is absent starts here: an empty host must be storable,
  // or "I do not use MQTT" is not expressible.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_user(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_broker_password(""));
}

void test_a_host_longer_than_dns_allows_is_refused(void) {
  // RFC 1035 2.3.4. The cap is the protocol's, not a round number: a smaller one would refuse
  // a name the owner's resolver answers, and that is a bug report nobody can act on.
  char s[512];
  repeat(s, 'a', OT_CONFIG_HOST_MAX);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host(s));
  repeat(s, 'a', OT_CONFIG_HOST_MAX + 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_HOST, ot_config_check_host(s));
}

void test_credentials_longer_than_the_buffer_are_refused_not_truncated(void) {
  // Truncation stores a password that is not the one the owner typed, and the broker then
  // refuses a credential the owner can see is correct on their screen.
  char s[512];
  repeat(s, 'u', OT_CONFIG_USER_MAX);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_user(s));
  repeat(s, 'u', OT_CONFIG_USER_MAX + 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_USER, ot_config_check_user(s));
  repeat(s, 'p', OT_CONFIG_BROKER_PASS_MAX);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_broker_password(s));
  repeat(s, 'p', OT_CONFIG_BROKER_PASS_MAX + 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_BROKER_PASSWORD, ot_config_check_broker_password(s));
}

void test_the_default_time_zone_is_a_posix_string_with_the_inverted_sign(void) {
  ot_config_t cfg;
  ot_config_defaults(&cfg, "a1b2c3d4e5f6");
  // MSK-3 is UTC+3. The sign in a POSIX TZ is inverted relative to the one everybody says out
  // loud, and this test exists because "fixing" it to MSK+3 lands the schedule six hours away
  // and looks like a bug in the schedule rather than in a string.
  TEST_ASSERT_EQUAL_STRING("MSK-3", cfg.tz);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", cfg.ntp_server);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_tz(cfg.tz));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_ntp(cfg.ntp_server));
  // Empty is legal for the broker and refused for the time server, and that asymmetry is the
  // whole reason ot_config_check_ntp() exists rather than a second call to check_host().
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NTP, ot_config_check_ntp(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NTP, ot_config_check_ntp(nullptr));
}

void test_a_time_zone_with_a_space_or_a_control_byte_is_refused(void) {
  // The string goes into the process environment verbatim. A POSIX TZ has no legal use for
  // either, and refusing them here is what keeps setenv() from carrying one.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_TZ, ot_config_check_tz("MSK 3"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_TZ, ot_config_check_tz("MSK\t-3"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_TZ, ot_config_check_tz(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_TZ, ot_config_check_tz(nullptr));
  // The long form with a DST rule is the shape that has to keep working.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_tz("CET-1CEST,M3.5.0,M10.5.0/3"));
}

void test_a_garbled_time_zone_is_repaired_to_the_default_rather_than_cleared(void) {
  // An empty TZ is UTC and an empty NTP server is no clock at all: either leaves the schedule
  // WRONG instead of ABSENT, and the result must never be "nothing".
  ot_config_t cfg;
  ot_config_defaults(&cfg, "a1b2c3d4e5f6");
  memset(cfg.tz, 'x', sizeof cfg.tz);          // unterminated
  memset(cfg.ntp_server, 0, sizeof cfg.ntp_server);
  const ot_config_repairs_t repairs = ot_config_sanitize(&cfg, "a1b2c3d4e5f6");
  TEST_ASSERT_EQUAL_STRING("MSK-3", cfg.tz);
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", cfg.ntp_server);
  TEST_ASSERT_TRUE(repairs & OT_CONFIG_REPAIRED(OT_CONFIG_F_TZ));
  TEST_ASSERT_TRUE(repairs & OT_CONFIG_REPAIRED(OT_CONFIG_F_NTP_SERVER));
}

void test_a_topic_prefix_may_not_contain_a_wildcard_or_start_at_the_brokers_own_tree(void) {
  // MQTT 3.1.1 4.7.1: `+` and `#` are subscription syntax and are not legal in a topic being
  // published to. A broker's answer to one is to drop the connection, which reads here as a
  // broker that keeps disconnecting for no reason. `$SYS` is the broker's own tree: publishing
  // into it is refused by every broker worth using and accepted quietly by the rest.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_prefix("opentherm/a1b2c3d4e5f6"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("opentherm/+"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("opentherm/#"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("$SYS/opentherm"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("comfo air"));
}

void test_a_topic_prefix_with_an_empty_level_is_refused(void) {
  // Topics are built as prefix + "/" + entity, so a trailing slash makes `opentherm//state`.
  // That is a legal MQTT topic with an empty level in it, which is the worst kind of wrong: it
  // works, it is subscribable, and it does not match the topic anybody typed by hand.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("opentherm/"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("/opentherm"));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix("opentherm//q"));
}

void test_a_device_without_a_topic_prefix_or_a_name_is_refused(void) {
  // Both are cleared to nothing by an empty submission under the general rule, and for these
  // two that rule has to lose: an empty prefix publishes to topics that start with a slash,
  // and an empty name is a nameless device in Home Assistant. The default is restored by
  // ot_config_sanitize(), which knows the device id; ot_config_apply() does not,
  // so it refuses instead of inventing one.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PREFIX, ot_config_check_prefix(""));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NAME, ot_config_check_name(""));
}

void test_a_device_name_is_bounded_and_printable(void) {
  char s[64];
  repeat(s, 'n', OT_CONFIG_NAME_MAX);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_name(s));
  repeat(s, 'n', OT_CONFIG_NAME_MAX + 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NAME, ot_config_check_name(s));
  // A pasted newline is the realistic case, and it ends up in a log line and an MQTT payload.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NAME, ot_config_check_name("Comfo\nAir"));
  // Non-ASCII is a name, not an error: the renderer escapes it (ot_api_escape_json).
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_name("Термостат"));
}

void test_a_ui_password_has_a_floor_because_the_kdf_is_the_only_thing_slowing_a_guess(void) {
  // The device answers as fast as the LAN allows and there is no lockout -- a lockout on a
  // device you cannot physically reach is a way to lose it. What stands between a guesser and
  // the device is the cost of one derivation, so the search space has to carry its own weight.
  char s[256];
  repeat(s, 'p', OT_CONFIG_UI_PASS_MIN - 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_UI_PASSWORD, ot_config_check_ui_password(s));
  repeat(s, 'p', OT_CONFIG_UI_PASS_MIN);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_ui_password(s));
  repeat(s, 'p', OT_CONFIG_UI_PASS_MAX);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_ui_password(s));
  repeat(s, 'p', OT_CONFIG_UI_PASS_MAX + 1);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_UI_PASSWORD, ot_config_check_ui_password(s));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_UI_PASSWORD, ot_config_check_ui_password("pass\tword"));
}

void test_every_error_has_a_sentence_and_none_of_them_quotes_the_value(void) {
  // These strings go into an HTTP body AND into the log ring, and /api/log is readable by
  // whoever can reach the device. An error that echoes the rejected value
  // publishes a password the moment somebody mistypes one into the wrong field.
  // Every code until the fallback a code with no case falls through to, each its own sentence. The
  // loop is bounded by the fallback and NOT by a named last code: a hard bound was ERR_NO_DOCUMENT
  // ERR_TZ and ERR_NTP were left unasked by an earlier build, and a code appended after the
  // last one named here would go unasked the same way. The header has no count sentinel, and DO
  // NOT add one for this test -- it would be public API. ASCII only: ot_log_render() escapes every
  // other byte, and a sentence the log ring turns into ° is one nobody can grep for (CLAUDE.md,
  // Language).
  const char *const fallback = ot_config_strerror((ot_config_err_t)999);
  TEST_ASSERT_NOT_NULL(fallback);
  int i = OT_CONFIG_OK;
  for (; i < 256 && strcmp(ot_config_strerror((ot_config_err_t)i), fallback) != 0; i++) {
    const char *msg = ot_config_strerror((ot_config_err_t)i);
    TEST_ASSERT_TRUE(strlen(msg) > 0);
    for (const char *c = msg; *c != '\0'; c++)
      TEST_ASSERT_TRUE_MESSAGE((unsigned char)*c < 0x80, msg);
  }
  // The fallback begins right after the last code: never before the last one this test knows of,
  // and with no sentence hiding past a gap in the enum.
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(OT_CONFIG_ERR_READ_ONLY_FIELD, i, "a code with no sentence");
  for (int j = i; j < i + 32; j++)
    TEST_ASSERT_EQUAL_STRING(fallback, ot_config_strerror((ot_config_err_t)j));

  // The second half of the name, which used to go untested: real values are pushed through
  // apply() and the sentence it produced is searched for them. Asserting only that a message
  // exists says nothing about the property that matters, and the property that matters is the
  // one that publishes somebody's Wi-Fi key.
  ot_config_t cfg = fresh();
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);

  char long_host[512];
  repeat(long_host, 'h', OT_CONFIG_HOST_MAX + 1);
  ot_config_patch_t host{};
  host.mqtt_host                 = long_host;
  const ot_config_err_t eh = ot_config_apply(&cfg, &host, &hash);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_HOST, eh);
  TEST_ASSERT_NULL_MESSAGE(strstr(ot_config_strerror(eh), "hhhhhhhh"),
                           "the message quotes the address it rejected");

  // The realistic one: a password typed into a field it does not belong in. `hunter2` is
  // refused for being seven characters long, and the refusal is read by anyone with /api/log.
  ot_config_patch_t ui{};
  ui.ui_password                 = "hunter2";
  const ot_config_err_t eu = ot_config_apply(&cfg, &ui, &hash);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_UI_PASSWORD, eu);
  TEST_ASSERT_NULL_MESSAGE(strstr(ot_config_strerror(eu), "hunter2"),
                           "the message quotes the password it rejected");

  // And the one that costs the most: the network key, which arrives over an OPEN access point
  // in the first place and must not then be written into a public log ring as well.
  ot_config_patch_t net{};
  net.wifi_ssid                  = "HomeNet";
  net.wifi_psk                   = "wpa2key";
  const ot_config_err_t en = ot_config_apply(&cfg, &net, &hash);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PSK, en);
  TEST_ASSERT_NULL_MESSAGE(strstr(ot_config_strerror(en), "wpa2key"),
                           "the message quotes the network key it rejected");
  TEST_ASSERT_NULL(strstr(ot_config_strerror(en), "HomeNet"));
}

// --- the executor's numbers ----------------------------------------------------

// The bounds as LITERALS, not as the OT_CONFIG_*_MIN/MAX macros: a macro edited by accident moves
// the check and a test written against it together, and the owner's watchdog quietly changes
// meaning with every test still green.
struct number_bounds {
  ot_config_field_t field;
  uint32_t          lo, hi;
};
static const number_bounds BOUNDS[] = {
    {OT_CONFIG_F_CONTROL_MODE, 0, 1},
    {OT_CONFIG_F_WATCHDOG_S, 60, 7200},
    {OT_CONFIG_F_FAILSAFE_SETPOINT, 100, 900},
    {OT_CONFIG_F_FAILSAFE_ROOM_TARGET, 50, 300},
    {OT_CONFIG_F_FAILSAFE_HEAT_DAYS, 1, 30},
    {OT_CONFIG_F_FAILSAFE_MIN_CYCLE, 60, 3600},
    {OT_CONFIG_F_FLOW_MIN, 100, 900},
    {OT_CONFIG_F_FLOW_MAX, 100, 900},
    {OT_CONFIG_F_LOCAL_CH_SETPOINT, 100, 900},
    {OT_CONFIG_F_DHW_SETPOINT, 0, 900},
    // The MQTT room-source slot. role is 0 (ambient) or 1 (room); stale_s is the window
    // ot_sensor waits before calling the reading STALE, and 10 s is a floor against a slot that
    // never counts as fresh at all.
    {OT_CONFIG_F_ROOM_MQTT_ROLE, 0, 1},
    {OT_CONFIG_F_ROOM_MQTT_STALE_S, 10, 65535},
};

void test_every_number_the_executor_reads_is_refused_one_step_outside_its_bounds(void) {
  for (size_t i = 0; i < sizeof BOUNDS / sizeof BOUNDS[0]; i++) {
    const ot_config_field_t f    = BOUNDS[i].field;
    const char             *name = ot_config_field(f)->name;
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_OK, ot_config_check_range(f, BOUNDS[i].lo), name);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_OK, ot_config_check_range(f, BOUNDS[i].hi), name);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_check_range(f, BOUNDS[i].hi + 1),
                              name);
    if (BOUNDS[i].lo > 0)
      TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_check_range(f, BOUNDS[i].lo - 1),
                                name);
    // Taken as uint32_t so that a value too big for the stored u16 is refused WHOLE. A checker
    // that narrowed first would see 65536 + lo as lo and accept it -- the port's 70000 -> 4464.
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_check_range(f, 65536u + BOUNDS[i].lo),
                              name);
  }
}

void test_every_value_that_becomes_id_1_sits_on_the_half_degree_grid(void) {
  // ot_control quantises the held CH setpoint to 5 dc and THEN keeps it inside [flow_min,
  // flow_max]. With a bound of 403 the two rules fight and the bound wins over the
  // rounding, so the four values that end up as ID 1 are multiples of 5 themselves.
  const ot_config_field_t grid[] = {OT_CONFIG_F_FAILSAFE_SETPOINT, OT_CONFIG_F_FLOW_MIN,
                                    OT_CONFIG_F_FLOW_MAX, OT_CONFIG_F_LOCAL_CH_SETPOINT};
  for (size_t i = 0; i < sizeof grid / sizeof grid[0]; i++) {
    const char *name = ot_config_field(grid[i])->name;
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_OK, ot_config_check_range(grid[i], 455), name);
    for (uint32_t off = 1; off < 5; off++)
      TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_check_range(grid[i], 455 + off),
                                name);
  }
  // And only those four. The room target is compared with a room reading and nothing quantises
  // it; a watchdog and a DHW setpoint are not ID 1 at all.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_FAILSAFE_ROOM_TARGET, 181));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_WATCHDOG_S, 901));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 551));
}

void test_a_dhw_setpoint_of_zero_means_unset_and_no_other_number_gets_that_pass(void) {
  // 0 is "nobody has written it" (the executor's dhw_setpoint_set), not a temperature.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 0));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_LOCAL_CH_SETPOINT, 0));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_FLOW_MIN, 0));
}

void test_the_store_takes_every_dhw_setpoint_a_boiler_may_report_it_accepts(void) {
  // The boiler's ID 48 bounds REPLACE the registry's 30..80 once read (ot_state.c:200-204), and
  // ot_command_check() and ot_control_apply() judge by them. A boiler reporting 25..85 is granted
  // 28 degrees there; a store that then refused 280 would persist nothing, and the value would be
  // gone after a reboot. So the store has no floor of its own, and the ceiling of ID 1.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 1));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 280));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 850));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 900));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_DHW_SETPOINT, 901));
}

void test_a_field_that_is_not_one_of_the_executors_numbers_has_no_range_to_pass(void) {
  // Refused rather than waved through: a caller asking about the broker host here has made a
  // mistake, and the answer that stores nothing is the safe one.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_MQTT_HOST, 0));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_HEATING_SEASON, 1));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range(OT_CONFIG_F_COUNT, 1));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_check_range((ot_config_field_t)-1, 1));
}

void test_the_lowest_flow_is_strictly_below_the_highest(void) {
  // Equal bounds leave exactly one legal setpoint, and an inverted pair leaves none; either is a
  // band the executor cannot quantise a held setpoint into.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_flow(699, 700, 700));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FLOW, ot_config_check_flow(700, 700, 700));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FLOW, ot_config_check_flow(701, 700, 700));
  // An inverted pair is reported as the pair, not as the setpoint that cannot fit inside it.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FLOW, ot_config_check_flow(700, 400, 1000));
}

void test_the_failsafe_setpoint_lies_inside_the_flow_band_both_ends_included(void) {
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_flow(400, 700, 400));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_flow(400, 700, 700));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FAILSAFE, ot_config_check_flow(400, 700, 399));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FAILSAFE, ot_config_check_flow(400, 700, 701));
}

void test_home_assistant_mode_needs_a_broker_and_local_mode_does_not(void) {
  // A device in Home Assistant mode with nowhere to hear Home Assistant from sits in
  // ha_waiting with CH down until the watchdog puts it into the failsafe.
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_MODE_NEEDS_BROKER, ot_config_check_mode(1, ""));
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_MODE_NEEDS_BROKER, ot_config_check_mode(1, nullptr));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_mode(1, "broker.lan"));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_mode(0, ""));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_mode(0, nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_thirty_two_character_ssid_is_legal_and_a_thirty_three_character_one_is_not);
  RUN_TEST(test_an_empty_ssid_is_not_a_network);
  RUN_TEST(test_an_ssid_with_a_nul_in_it_is_refused_because_the_radio_cannot_express_it);
  RUN_TEST(test_a_sixty_four_character_psk_is_legal_and_must_be_hexadecimal);
  RUN_TEST(test_a_passphrase_shorter_than_eight_characters_is_not_one);
  RUN_TEST(test_an_open_network_has_no_key_and_that_is_not_an_error);
  RUN_TEST(test_a_psk_with_a_nul_in_it_is_refused);
  RUN_TEST(test_a_port_outside_the_range_is_refused_rather_than_wrapped);
  RUN_TEST(test_a_broker_host_is_a_host_and_not_a_url);
  RUN_TEST(test_no_broker_is_a_supported_configuration);
  RUN_TEST(test_a_host_longer_than_dns_allows_is_refused);
  RUN_TEST(test_credentials_longer_than_the_buffer_are_refused_not_truncated);
  RUN_TEST(test_the_default_time_zone_is_a_posix_string_with_the_inverted_sign);
  RUN_TEST(test_a_time_zone_with_a_space_or_a_control_byte_is_refused);
  RUN_TEST(test_a_garbled_time_zone_is_repaired_to_the_default_rather_than_cleared);
  RUN_TEST(test_a_topic_prefix_may_not_contain_a_wildcard_or_start_at_the_brokers_own_tree);
  RUN_TEST(test_a_topic_prefix_with_an_empty_level_is_refused);
  RUN_TEST(test_a_device_without_a_topic_prefix_or_a_name_is_refused);
  RUN_TEST(test_a_device_name_is_bounded_and_printable);
  RUN_TEST(test_a_ui_password_has_a_floor_because_the_kdf_is_the_only_thing_slowing_a_guess);
  RUN_TEST(test_every_error_has_a_sentence_and_none_of_them_quotes_the_value);

  RUN_TEST(test_every_number_the_executor_reads_is_refused_one_step_outside_its_bounds);
  RUN_TEST(test_every_value_that_becomes_id_1_sits_on_the_half_degree_grid);
  RUN_TEST(test_a_dhw_setpoint_of_zero_means_unset_and_no_other_number_gets_that_pass);
  RUN_TEST(test_the_store_takes_every_dhw_setpoint_a_boiler_may_report_it_accepts);
  RUN_TEST(test_a_field_that_is_not_one_of_the_executors_numbers_has_no_range_to_pass);
  RUN_TEST(test_the_lowest_flow_is_strictly_below_the_highest);
  RUN_TEST(test_the_failsafe_setpoint_lies_inside_the_flow_band_both_ends_included);
  RUN_TEST(test_home_assistant_mode_needs_a_broker_and_local_mode_does_not);
  return UNITY_END();
}
