// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What survives a power cut, and what must never be allowed to.
//
// Every case here is a failure scenario. The happy path of a settings page gets exercised by
// hand on every build; what does not is the power cut between two NVS writes, the 32-character
// SSID that only one household in a hundred has, the corrupt record that locks the owner out
// of their own device, and the OTA that renames a key and sends everybody back to setup.
// Those are the cases below, and each of them enforces a projection rule stated in ot_config.h
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
// This directory: what a fresh device holds (ot_config_defaults()) and what a client is handed
// (ot_config_project()). They sit together because a field added to the document needs a default
// and a projection row in the same change, and the size assertion below is what makes it so.
#include <unity.h>

#include <cstddef>
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

static void repeat(char *out, char c, size_t n) {
  memset(out, c, n);
  out[n] = '\0';
}

// Does this blob contain that plaintext anywhere in it? Used to ask a question no strcmp can:
// "is the password ANYWHERE in the thing we are about to hand a client", including in a field
// nobody thought about when they added it.
static bool contains_bytes(const void *blob, size_t blob_len, const char *needle) {
  const size_t n = strlen(needle);
  if (n == 0 || n > blob_len)
    return false;
  const unsigned char *p = (const unsigned char *)blob;
  for (size_t i = 0; i + n <= blob_len; i++)
    if (memcmp(p + i, needle, n) == 0)
      return true;
  return false;
}

// Can a renderer measure this string without reading past the field it came out of? Asked with a
// BOUNDED memchr and never with strlen(), because strlen() is the very read this is testing for
// -- a test that had to commit the bug to detect it would be reporting its own crash. memchr is
// specified to stop at the first match, so this is defined for a one-byte "" as well as for an
// unterminated 254-byte array (C11 7.24.5.1).
static bool measurable(const char *s, size_t cap) {
  return s != nullptr && memchr(s, '\0', cap) != nullptr;
}

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static ot_config_t fresh(void) {
  ot_config_t cfg{};
  ot_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

// --- defaults ------------------------------------------------------------------------------

void test_a_fresh_device_has_no_network_no_password_and_a_broker_that_is_not_set(void) {
  ot_config_t cfg = fresh();
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL(0, cfg.wifi.psk_len);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_password);
  TEST_ASSERT_FALSE_MESSAGE(ot_config_password_set(&cfg),
                            "a device out of the box demanded a password nobody has set");
  TEST_ASSERT_FALSE(cfg.read_only);
  TEST_ASSERT_EQUAL_UINT16(1883, cfg.mqtt_port);
}

void test_the_default_topic_prefix_carries_the_device_id_so_two_units_do_not_collide(void) {
  // The identity is the full
  // MAC and never the name, because a rename that re-keys the topics leaves the old retained
  // discovery messages in the broker for ever and the owner sees two devices, one of them dead
  // and undeletable.
  ot_config_t cfg = fresh();
  TEST_ASSERT_EQUAL_STRING("opentherm/a1b2c3d4e5f6", cfg.topic_prefix);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_prefix(cfg.topic_prefix));
}

void test_a_device_that_cannot_read_its_own_mac_still_boots(void) {
  // A worse default -- two devices in one house would share a topic tree -- and still better
  // than refusing to start over it. What must not happen is "opentherm/(null)" or a prefix that
  // fails the very validator this component publishes.
  ot_config_t cfg{};
  ot_config_defaults(&cfg, nullptr);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_name(cfg.device_name));
  TEST_ASSERT_FALSE(contains_bytes(cfg.topic_prefix, strlen(cfg.topic_prefix), "null"));

  ot_config_defaults(&cfg, "not-hex");
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_FALSE(contains_bytes(cfg.topic_prefix, strlen(cfg.topic_prefix), "not-hex"));
}

void test_the_default_name_matches_the_access_point_the_owner_saw_on_their_phone(void) {
  // ot_net.c names the access point after the last two MAC bytes, so a device the owner
  // joined as `opentherm-e5f6` announces itself in Home Assistant with the same four digits.
  // The name is display only -- this is about the owner recognising their own device
  // among two, not about identity.
  ot_config_t cfg = fresh();
  TEST_ASSERT_TRUE(contains_bytes(cfg.device_name, strlen(cfg.device_name), "e5f6"));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_name(cfg.device_name));
}

void test_a_fresh_device_is_local_out_of_season_and_holds_the_documented_numbers(void) {
  // Pinned as literals. heating_season false is the one that matters: a freshly
  // flashed device never asks the boiler for heat, and with
  // local_ch_enable false it does not ask even once the season is switched on.
  ot_config_t cfg = fresh();
  TEST_ASSERT_EQUAL_UINT16(0, cfg.control_mode);
  TEST_ASSERT_FALSE(cfg.heating_season);
  TEST_ASSERT_EQUAL_UINT16(900, cfg.watchdog_s);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(180, cfg.failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT16(3, cfg.failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT16(600, cfg.failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_UINT16(400, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(700, cfg.flow_max_dc);
  TEST_ASSERT_FALSE(cfg.local_ch_enable);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_TRUE(cfg.dhw_enable);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, cfg.dhw_setpoint_dc, "a fresh device has written no DHW setpoint");
  // The MQTT room-source slot: off, role "room" (not ambient -- a slot the owner turns on
  // is assumed to be the thing they meant to steer with), a 15-minute stale window, and not yet
  // marked as HA's forwarded reading.
  TEST_ASSERT_FALSE(cfg.room_mqtt_enable);
  TEST_ASSERT_EQUAL_UINT16(1, cfg.room_mqtt_role);
  TEST_ASSERT_EQUAL_UINT16(900, cfg.room_mqtt_stale_s);
  TEST_ASSERT_FALSE(cfg.room_mqtt_ha_forwarded);
  // And the defaults obey the rules they will be held to, or the first save of an unrelated
  // setting would be refused over numbers the owner never touched.
  TEST_ASSERT_EQUAL(OT_CONFIG_OK,
                    ot_config_check_flow(cfg.flow_min_dc, cfg.flow_max_dc, cfg.failsafe_setpoint_dc));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_mode(cfg.control_mode, cfg.mqtt_host));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK,
                    ot_config_check_range(OT_CONFIG_F_LOCAL_CH_SETPOINT, cfg.local_ch_setpoint_dc));
}

// --- the projection: what a client is allowed to see -----------------------------------------

// The projection's layout as this test understands it. It exists to FAIL: a field added to
// ot_config_public_t and not to this list changes one size and not the other, and the
// assertion below sends whoever added it back here to say what the new field may hold. Without
// it, "including the field added after the test was written" is a claim nothing enforces --
// which is what review found the previous version of this test quietly asserting.
struct projection_as_this_test_knows_it {
  char        ssid[OT_CONFIG_SSID_MAX + 1];
  const char *psk;
  const char *mqtt_host;
  uint16_t    mqtt_port;
  const char *mqtt_user;
  const char *mqtt_password;
  const char *topic_prefix;
  bool        ha_discovery;
  const char *device_name;
  const char *tz;
  const char *ntp_server;
  bool        dhw_enable;
  // The executor's settings: none is a secret, and every one is copied as it is stored.
  uint16_t    control_mode;
  bool        heating_season;
  uint16_t    watchdog_s;
  uint16_t    failsafe_setpoint_dc;
  uint16_t    failsafe_room_target_dc;
  uint16_t    failsafe_heat_days;
  uint16_t    failsafe_min_cycle_s;
  uint16_t    flow_min_dc;
  uint16_t    flow_max_dc;
  bool        local_ch_enable;
  uint16_t    local_ch_setpoint_dc;
  uint16_t    dhw_setpoint_dc;
  // The MQTT room-source slot: none is a secret, and every one is copied as it is stored.
  bool        room_mqtt_enable;
  uint16_t    room_mqtt_role;
  uint16_t    room_mqtt_stale_s;
  bool        room_mqtt_ha_forwarded;
  const char *ui_password;
  bool        ui_password_set;
  bool        read_only;
};

// Is this plaintext anywhere a client can reach it? BOTH halves are needed and neither is
// enough. The struct's own bytes catch an ARRAY field added later, like the SSID. Following the
// pointers catches the leak that actually happens -- `const char *api_token = cfg->token;` --
// which a scan of the struct cannot see at all, because what sits in the struct is eight bytes
// of address and the password is at the other end of it.
static bool projection_exposes(const ot_config_public_t *pub, const char *needle) {
  const char *const reached[] = {pub->ssid,          pub->psk,          pub->mqtt_host,
                                 pub->mqtt_user,     pub->mqtt_password, pub->topic_prefix,
                                 pub->device_name,   pub->ui_password,
                                 pub->tz,            pub->ntp_server};
  for (size_t i = 0; i < sizeof reached / sizeof reached[0]; i++)
    if (reached[i] != nullptr && contains_bytes(reached[i], strlen(reached[i]), needle))
      return true;
  return contains_bytes(pub, sizeof *pub, needle);
}

void test_no_stored_secret_appears_anywhere_in_what_a_client_is_handed(void) {
  // One disclosure policy over two storage policies. The broker
  // password is kept recoverably because MQTT needs the plaintext; the UI password is kept as
  // a one-way record; NEITHER is ever returned.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      sizeof(struct projection_as_this_test_knows_it), sizeof(ot_config_public_t),
      "the projection grew a field; say here what a client may see of it");

  ot_config_t cfg = fresh();
  ot_config_patch_t p{};
  p.wifi_ssid     = "HomeNet";
  p.wifi_psk      = "the-wifi-password";
  p.mqtt_password = "the-broker-password";
  p.ui_password   = "the-ui-password";
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, &hash));

  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);

  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-wifi-password"),
                            "the Wi-Fi key is in the projection");
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-broker-password"),
                            "the broker password is in the projection");
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-ui-password"),
                            "the UI password is in the projection");
  // The stored RECORD is not the password and is still not a client's business: it carries the
  // salt and the digest, which are exactly what an offline guess needs.
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, cfg.ui_pw_hash),
                            "the stored password record is in the projection");
  TEST_ASSERT_EQUAL_STRING(OT_SECRET_SENTINEL, pub.psk);
  TEST_ASSERT_EQUAL_STRING(OT_SECRET_SENTINEL, pub.mqtt_password);
  TEST_ASSERT_EQUAL_STRING(OT_SECRET_SENTINEL, pub.ui_password);
  // The SSID is not a secret: the owner has to see which network the device thinks it is on,
  // and it is broadcast in every beacon in the house anyway.
  TEST_ASSERT_EQUAL_STRING("HomeNet", pub.ssid);
}

void test_the_projection_is_the_layout_this_test_reviewed_member_by_member(void) {
  // sizeof alone is blind to a member that fits in padding, and the executor members left some -- six
  // bytes before ui_password, six at the tail. So every member's offset is compared too: a
  // reordering, a changed type or a POINTER added anywhere fails here. What still passes is a
  // scalar dropped into a padding hole: no offset and no size can see it.
  // It also cannot carry a secret, which is what this mirror exists to make somebody look at, and
  // closing every hole would mean reordering the public header for the sake of a test.
#define SAME_OFFSET(m)                                                                          \
  TEST_ASSERT_EQUAL_size_t_MESSAGE(offsetof(struct projection_as_this_test_knows_it, m),         \
                                   offsetof(ot_config_public_t, m), #m)
  SAME_OFFSET(ssid);
  SAME_OFFSET(psk);
  SAME_OFFSET(mqtt_host);
  SAME_OFFSET(mqtt_port);
  SAME_OFFSET(mqtt_user);
  SAME_OFFSET(mqtt_password);
  SAME_OFFSET(topic_prefix);
  SAME_OFFSET(ha_discovery);
  SAME_OFFSET(device_name);
  SAME_OFFSET(tz);
  SAME_OFFSET(ntp_server);
  SAME_OFFSET(dhw_enable);
  SAME_OFFSET(control_mode);
  SAME_OFFSET(heating_season);
  SAME_OFFSET(watchdog_s);
  SAME_OFFSET(failsafe_setpoint_dc);
  SAME_OFFSET(failsafe_room_target_dc);
  SAME_OFFSET(failsafe_heat_days);
  SAME_OFFSET(failsafe_min_cycle_s);
  SAME_OFFSET(flow_min_dc);
  SAME_OFFSET(flow_max_dc);
  SAME_OFFSET(local_ch_enable);
  SAME_OFFSET(local_ch_setpoint_dc);
  SAME_OFFSET(dhw_setpoint_dc);
  SAME_OFFSET(room_mqtt_enable);
  SAME_OFFSET(room_mqtt_role);
  SAME_OFFSET(room_mqtt_stale_s);
  SAME_OFFSET(room_mqtt_ha_forwarded);
  SAME_OFFSET(ui_password);
  SAME_OFFSET(ui_password_set);
  SAME_OFFSET(read_only);
#undef SAME_OFFSET
  TEST_ASSERT_EQUAL_size_t(sizeof(struct projection_as_this_test_knows_it), sizeof(ot_config_public_t));
}

void test_an_unset_secret_reads_back_empty_so_the_page_can_say_nothing_is_set(void) {
  ot_config_t        cfg = fresh();
  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_EQUAL_STRING("", pub.psk);
  TEST_ASSERT_EQUAL_STRING("", pub.mqtt_password);
  TEST_ASSERT_EQUAL_STRING("", pub.ui_password);
  TEST_ASSERT_FALSE(pub.ui_password_set);
}

void test_password_set_is_derived_from_the_record_and_is_never_a_key_of_its_own(void) {
  // A flag in its own key can be written while the record is not -- a power cut between two
  // NVS writes -- and the device then demands a password that does not exist. Locked, by
  // nobody, for ever, on a device behind a front panel. So there is no flag: the answer is the
  // record being non-empty, computed in one place that both the projection and the access
  // policy (ot_http_policy.h, ctx.password_set) read.
  ot_config_t cfg = fresh();
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));

  ot_config_patch_t p{};
  p.ui_password                         = "a-good-password";
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, &hash));
  TEST_ASSERT_TRUE(ot_config_password_set(&cfg));

  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(pub.ui_password_set);

  // And erasing the record is the whole of erasing the password -- there is no second thing to
  // forget to clear, which is what makes the soft reset a complete answer.
  cfg.ui_pw_hash[0] = '\0';
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));
}

void test_a_thirty_two_character_ssid_survives_the_projection_whole(void) {
  // The record is not NUL-terminated and has no room to be. A projection that copies with
  // `sizeof - 1` shows the owner 31 characters of their own network name and, worse, invites
  // the same mistake in the code that hands it to esp_wifi.
  ot_config_t cfg = fresh();
  char              s[64];
  repeat(s, 'S', 32);
  ot_config_patch_t p{};
  p.wifi_ssid = s;
  p.wifi_psk  = "12345678";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));

  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(32, strlen(pub.ssid), "the SSID lost a character to a terminator");
  TEST_ASSERT_EQUAL_STRING(s, pub.ssid);
}

void test_the_projection_hands_out_nothing_a_renderer_can_run_off_the_end_of(void) {
  // The projection defends the SSID length against a document that has not been sanitized -- its
  // own comment says so -- and then hands out four char arrays as `const char *` for a renderer
  // to strlen(). The same 0xff document that the terminated() DO NOT in ot_config_internal.h was
  // measured against reaches the same end through this function: with `read_only` true, so that
  // no byte after mqtt_host is zero, strlen(pub.mqtt_host) reports
  // `READ of size 743, 0 bytes after 840-byte region` under AddressSanitizer -- on the HTTP task,
  // over a heap object, from a pointer a client asked for.
  //
  // Both parts of the trigger are states this component exists to survive: read_only is a store
  // written by a newer build, and an array with no NUL in it is a short read or a bit flip.
  ot_config_t cfg;
  memset(&cfg, 0xff, sizeof cfg);
  // Real booleans, for the reason given in test_a_document_of_pure_garbage_from_flash: a _Bool
  // holding 0xff is a bit pattern no C program may read.
  cfg.ha_discovery = true;
  cfg.read_only    = true;

  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);

  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.mqtt_host, sizeof cfg.mqtt_host), "mqtt_host");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.mqtt_user, sizeof cfg.mqtt_user), "mqtt_user");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.topic_prefix, sizeof cfg.topic_prefix), "topic_prefix");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.device_name, sizeof cfg.device_name), "device_name");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.tz, sizeof cfg.tz), "tz");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.ntp_server, sizeof cfg.ntp_server), "ntp_server");
  // The other three are asked too, because the property is that EVERY string leaving here can be
  // measured -- not that four of them can. These three reach it another way: the secrets are
  // redacted to published literals, and the SSID is copied under the clamp above.
  TEST_ASSERT_TRUE(measurable(pub.psk, sizeof OT_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(measurable(pub.mqtt_password, sizeof OT_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(measurable(pub.ui_password, sizeof OT_SECRET_SENTINEL));
  TEST_ASSERT_EQUAL_size_t(OT_CONFIG_SSID_MAX, strlen(pub.ssid));

  // What is shown for a field the flash could not answer for is NOTHING, never the bytes: an
  // owner reading an empty broker address learns something true, and a truncated one does not.
  TEST_ASSERT_EQUAL_STRING("", pub.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("", pub.device_name);
}

void test_the_executors_settings_reach_the_client_as_they_are_stored(void) {
  // Twelve distinct values, so a projection that dropped one or copied it into its neighbour --
  // two uint16_t side by side are one typo apart -- shows here and not as a watchdog the owner
  // reads on the page and the device is not using.
  ot_config_t cfg             = fresh();
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
  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_EQUAL_UINT16(1, pub.control_mode);
  TEST_ASSERT_TRUE(pub.heating_season);
  TEST_ASSERT_EQUAL_UINT16(901, pub.watchdog_s);
  TEST_ASSERT_EQUAL_UINT16(451, pub.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(181, pub.failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT16(4, pub.failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT16(601, pub.failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_UINT16(401, pub.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(701, pub.flow_max_dc);
  TEST_ASSERT_TRUE(pub.local_ch_enable);
  TEST_ASSERT_EQUAL_UINT16(452, pub.local_ch_setpoint_dc);
  TEST_ASSERT_FALSE(pub.dhw_enable);
  TEST_ASSERT_EQUAL_UINT16(550, pub.dhw_setpoint_dc);
  // The three flags once more with the season off. Above, two of them were true, and one copied
  // from the other was the same byte (see the wire twin of this test).
  cfg.heating_season = false;
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_FALSE(pub.heating_season);
  TEST_ASSERT_TRUE(pub.local_ch_enable);
  TEST_ASSERT_FALSE(pub.dhw_enable);
}

void test_the_room_mqtt_slot_round_trips_through_apply_and_project(void) {
  // The patch a settings page or ot_thermostat_room_init's caller would send: every field set,
  // away from its default, so a projection that dropped one or copied it into its neighbour --
  // enable and ha_forwarded are both bool, role and stale_s both uint16_t -- shows here.
  ot_config_t cfg = fresh();
  ot_config_patch_t p{};
  p.has_room_mqtt_enable      = true;
  p.room_mqtt_enable          = true;
  p.has_room_mqtt_role        = true;
  p.room_mqtt_role            = 0;  // ambient
  p.has_room_mqtt_stale_s     = true;
  p.room_mqtt_stale_s         = 120;
  p.has_room_mqtt_ha_forwarded = true;
  p.room_mqtt_ha_forwarded    = true;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_TRUE(cfg.room_mqtt_enable);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.room_mqtt_role);
  TEST_ASSERT_EQUAL_UINT16(120, cfg.room_mqtt_stale_s);
  TEST_ASSERT_TRUE(cfg.room_mqtt_ha_forwarded);

  ot_config_public_t pub{};
  ot_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(pub.room_mqtt_enable);
  TEST_ASSERT_EQUAL_UINT16(0, pub.room_mqtt_role);
  TEST_ASSERT_EQUAL_UINT16(120, pub.room_mqtt_stale_s);
  TEST_ASSERT_TRUE(pub.room_mqtt_ha_forwarded);

  // Absent is not false/zero: a document that touches nothing else leaves the slot as it was.
  ot_config_patch_t other{};
  other.device_name = "Hall";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &other, nullptr));
  TEST_ASSERT_TRUE(cfg.room_mqtt_enable);
  TEST_ASSERT_EQUAL_UINT16(120, cfg.room_mqtt_stale_s);
}

void test_a_room_mqtt_role_or_stale_outside_its_bounds_is_refused_and_changes_nothing(void) {
  // The two literal cases the design calls out: role 2 (neither ambient nor room) and a stale
  // window of 0 (a slot that could never be called fresh, which is not what "stale window" means).
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);

  ot_config_patch_t bad_role{};
  bad_role.has_room_mqtt_role = true;
  bad_role.room_mqtt_role     = 2;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_apply(&cfg, &bad_role, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);

  ot_config_patch_t bad_stale{};
  bad_stale.has_room_mqtt_stale_s = true;
  bad_stale.room_mqtt_stale_s     = 0;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_apply(&cfg, &bad_stale, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_device_has_no_network_no_password_and_a_broker_that_is_not_set);
  RUN_TEST(test_the_default_topic_prefix_carries_the_device_id_so_two_units_do_not_collide);
  RUN_TEST(test_a_device_that_cannot_read_its_own_mac_still_boots);
  RUN_TEST(test_the_default_name_matches_the_access_point_the_owner_saw_on_their_phone);
  RUN_TEST(test_a_fresh_device_is_local_out_of_season_and_holds_the_documented_numbers);
  RUN_TEST(test_the_executors_settings_reach_the_client_as_they_are_stored);

  RUN_TEST(test_no_stored_secret_appears_anywhere_in_what_a_client_is_handed);
  RUN_TEST(test_the_projection_is_the_layout_this_test_reviewed_member_by_member);
  RUN_TEST(test_an_unset_secret_reads_back_empty_so_the_page_can_say_nothing_is_set);
  RUN_TEST(test_password_set_is_derived_from_the_record_and_is_never_a_key_of_its_own);
  RUN_TEST(test_a_thirty_two_character_ssid_survives_the_projection_whole);
  RUN_TEST(test_the_projection_hands_out_nothing_a_renderer_can_run_off_the_end_of);
  RUN_TEST(test_the_room_mqtt_slot_round_trips_through_apply_and_project);
  RUN_TEST(test_a_room_mqtt_role_or_stale_outside_its_bounds_is_refused_and_changes_nothing);
  return UNITY_END();
}
