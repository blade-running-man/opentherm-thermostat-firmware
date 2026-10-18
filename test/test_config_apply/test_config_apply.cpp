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
// came from there was moved byte for byte, not rewritten. Cut again at 628 lines:
// the broker-pair cases -- the mqtt_host/mqtt_password pairing and the empty-host refinement
// -- went to their own sibling suite, test/test_config_apply_broker/, along the same directory seam.
//
// The helpers below are this directory's OWN copy of exactly the ones its tests call. DO NOT
// hoist them into a shared directory: the moment two suites share a helper, "it grew a capability
// for suite B" becomes a way to quietly weaken suite A (CLAUDE.md, Tests). And a directory named
// test/test_config_common/ would itself be collected as a suite by `test_filter = test_*`.
//
// This directory: applying a submitted document -- all or nothing, the Wi-Fi pair, absent
// against empty, the two storage policies for the two passwords, a read-only store and a NULL.
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

// A hardware random number generator that has not started yet, or an mbedtls call that ran out
// of memory. The point is that this returns false rather than half a digest.
static bool broken_kdf(const char *, const uint8_t *, size_t, uint32_t, uint8_t *, size_t) {
  return false;
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

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static ot_config_t fresh(void) {
  ot_config_t cfg{};
  ot_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

// --- applying a submitted document ------------------------------------------------------------

void test_a_document_with_one_bad_field_changes_nothing_at_all(void) {
  // The owner fixes the port and saves again -- and if the good half of the first attempt had
  // been applied, the second attempt lands on a document that is no longer the one they were
  // looking at. All or nothing, checked by comparing the whole struct.
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);

  ot_config_patch_t p{};
  p.mqtt_host     = "broker.lan";
  p.device_name   = "Kitchen";
  p.has_mqtt_port = true;
  p.mqtt_port     = 70000;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PORT, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &cfg, sizeof cfg,
                                   "a rejected document was applied in part");
}

void test_changing_the_network_without_retyping_its_password_is_refused(void) {
  // The one that strands devices. The settings page renders a stored PSK as the sentinel, so a
  // form where only the SSID was edited submits a NEW network with the OLD password attached.
  // Storing that pair is the atomicity failure arriving through the front door instead of
  // through a power cut, and the device leaves the house for good.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.wifi_ssid = "OldNet";
  first.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t second{};
  second.wifi_ssid = "NewNet";
  second.wifi_psk  = OT_SECRET_SENTINEL;
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_WIFI_PAIR,
                            ot_config_apply(&cfg, &second, nullptr),
                            "a new SSID was stored with the previous network's password");
  TEST_ASSERT_EQUAL(6, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_MEMORY("OldNet", cfg.wifi.ssid, 6);
}

void test_saving_an_unrelated_setting_leaves_the_stored_network_untouched(void) {
  // The other half of the same rule, and the reason it cannot simply refuse every sentinel:
  // the page submits the WHOLE document, so changing the broker address re-submits the SSID
  // unchanged with the PSK as the sentinel. Refusing that would make every other setting
  // unsavable once a network is stored.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.wifi_ssid = "OldNet";
  first.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t second{};
  second.wifi_ssid = "OldNet";
  second.wifi_psk  = OT_SECRET_SENTINEL;
  second.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &second, nullptr));
  TEST_ASSERT_EQUAL(12, cfg.wifi.psk_len);
  TEST_ASSERT_EQUAL_MEMORY("old-password", cfg.wifi.psk, 12);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
}

void test_half_a_wifi_pair_is_refused_whichever_half_it_is(void) {
  // Neither half can be applied on its own without inventing the other, and the invented one
  // is always the stored one -- which is the exact pairing this component exists to prevent.
  ot_config_t cfg = fresh();

  ot_config_patch_t ssid_only{};
  ssid_only.wifi_ssid = "SomeNet";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_WIFI_PAIR, ot_config_apply(&cfg, &ssid_only, nullptr));

  ot_config_patch_t psk_only{};
  psk_only.wifi_psk = "some-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_WIFI_PAIR, ot_config_apply(&cfg, &psk_only, nullptr));
}

void test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_byte_for_byte(void) {
  // Both extremes are legal, both are what a serious router hands out, and both are what a
  // `- 1` for a terminator quietly destroys. Compared with memcmp against the exact lengths,
  // because strlen over either of these is the bug being guarded against.
  ot_config_t cfg = fresh();
  char              ssid[64];
  char              psk[80];
  repeat(ssid, 'S', 32);
  repeat(psk, 'f', 64);

  ot_config_patch_t p{};
  p.wifi_ssid = ssid;
  p.wifi_psk  = psk;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(32, cfg.wifi.ssid_len, "a 32-character SSID lost a byte");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(64, cfg.wifi.psk_len, "a 64-character key lost a byte");
  TEST_ASSERT_EQUAL_MEMORY(ssid, cfg.wifi.ssid, 32);
  TEST_ASSERT_EQUAL_MEMORY(psk, cfg.wifi.psk, 64);
}

void test_forgetting_the_network_clears_both_halves_at_once(void) {
  // The only way the settings page can express "this device is not on a network any more", and
  // it has to clear both halves in the same act for the same reason they are stored in one.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.wifi_ssid = "OldNet";
  p.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));

  ot_config_patch_t forget{};
  forget.wifi_ssid = "";
  forget.wifi_psk  = "";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &forget, nullptr));
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, cfg.wifi.psk_len,
                                  "the network was forgotten and its password was kept");
}

void test_an_absent_key_keeps_and_an_empty_key_clears(void) {
  // Absent and empty are different, and the whole sentinel scheme in ot_secrets exists
  // because they were once the same: one save of an unrelated setting wiped the broker
  // credentials.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  p.mqtt_user = "sensor";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));

  ot_config_patch_t absent{};  // every pointer NULL
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &absent, nullptr));
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("sensor", cfg.mqtt_user);

  ot_config_patch_t cleared{};
  cleared.mqtt_user = "";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &cleared, nullptr));
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
}

void test_the_broker_password_is_stored_so_it_can_be_used_and_the_ui_password_is_not(void) {
  // Two storage policies, one disclosure policy. MQTT authenticates
  // with the plaintext, so a hash there is not a hardening measure, it is a broker that never
  // connects -- which is exactly why storing a hash was rejected.
  ot_config_t                cfg  = fresh();
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  ot_config_patch_t          p{};
  p.mqtt_password = "broker-secret";
  p.ui_password   = "ui-secret-value";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, &hash));

  TEST_ASSERT_EQUAL_STRING_MESSAGE("broker-secret", cfg.mqtt_password,
                                   "the broker password was mangled; MQTT will not authenticate");
  TEST_ASSERT_FALSE_MESSAGE(contains_bytes(cfg.ui_pw_hash, sizeof cfg.ui_pw_hash, "ui-secret-value"),
                            "the UI password was stored in the clear");
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(cfg.ui_pw_hash, "ui-secret-value",
                                                             fake_kdf));
}

void test_a_ui_password_with_nowhere_to_hash_it_is_refused_rather_than_stored(void) {
  // The submission is fine and the device cannot honour it. Its own error code because the
  // owner did nothing wrong and retyping will not help -- and because the alternative branch,
  // the one where a missing hash context falls through to storing the plaintext, is a
  // one-line mistake that no other test in this file would catch.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.ui_password = "a-good-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NO_HASH, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));

  const ot_config_hash_ctx_t broken = hash_with(SALT_A, broken_kdf);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NO_HASH, ot_config_apply(&cfg, &p, &broken));
  TEST_ASSERT_FALSE_MESSAGE(ot_config_password_set(&cfg),
                            "a failed derivation left something behind that looks like a password");
}

void test_the_published_sentinel_cannot_become_the_password_of_an_unconfigured_device(void) {
  // OT_SECRET_SENTINEL is a published string. On a device with nothing stored it cannot
  // be an untouched field, so it is a value being submitted -- and storing it would give every
  // never-configured device the same password, printed in the source.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.ui_password                         = OT_SECRET_SENTINEL;
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_UI_PASSWORD, ot_config_apply(&cfg, &p, &hash));
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));

  ot_config_patch_t broker{};
  broker.mqtt_password = OT_SECRET_SENTINEL;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_BROKER_PASSWORD,
                    ot_config_apply(&cfg, &broker, nullptr));
}

void test_clearing_the_ui_password_is_allowed_because_the_owner_may_be_locked_out_of_it(void) {
  // The password is optional and the physical button can erase it. An authenticated owner
  // removing it through the API is the same decision reached from the other side; refusing it
  // would mean the only way to undo a password is a physical gesture on a device in a wall.
  ot_config_t                cfg  = fresh();
  const ot_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  ot_config_patch_t          set{};
  set.ui_password = "a-good-password";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &set, &hash));

  ot_config_patch_t clear{};
  clear.ui_password = "";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &clear, &hash));
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));
}

void test_a_store_that_holds_a_newer_schema_refuses_every_write(void) {
  // An OTA rollback lands here: the flash was written by a build that knew more than this
  // one. Writing into it would destroy what the newer build stored, and the owner who rolls
  // forward again finds their settings gone. So the store goes read-only -- the device keeps
  // ventilating, keeps its network, keeps its broker, and says why.
  ot_config_t cfg = fresh();
  cfg.read_only         = true;
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);

  ot_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_READ_ONLY, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

void test_a_null_document_is_refused_rather_than_reported_as_saved(void) {
  // A body that failed to parse arrives here as nothing at all, on the HTTP task. It must not
  // crash -- and it must not answer OK either, because OK is what the handler turns into 200 and
  // what ot_config_strerror() renders as the word "accepted". The owner would read Saved
  // over a request that stored nothing, close the page, and find the settings gone.
  //
  // A patch with every field absent is a different thing and still succeeds: the page submits
  // the whole document, and "nothing was changed" is a legitimate save. What is refused here is
  // the absence of a document, not the absence of changes.
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NO_DOCUMENT, ot_config_apply(&cfg, nullptr, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
  TEST_ASSERT_TRUE_MESSAGE(
      strcmp(ot_config_strerror(OT_CONFIG_OK),
             ot_config_strerror(OT_CONFIG_ERR_NO_DOCUMENT)) != 0,
      "a request that stored nothing is answered with the sentence for a request that did");

  // The other NULL is a programming error rather than a bad request, and it gets the same
  // answer for the same reason: there is nothing this call could have stored.
  ot_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_NO_DOCUMENT, ot_config_apply(nullptr, &p, nullptr));
}

// --- the executor's settings: bounds and commits -------------------------------
// The rules that span fields, on the merged document, are test/test_config_merge/.

// One of the executor's numbers into a patch by field id, so a test can walk all of them.
static void set_number(ot_config_patch_t *p, ot_config_field_t f, uint32_t v) {
  switch (f) {
  case OT_CONFIG_F_CONTROL_MODE: p->has_control_mode = true; p->control_mode = v; break;
  case OT_CONFIG_F_WATCHDOG_S: p->has_watchdog_s = true; p->watchdog_s = v; break;
  case OT_CONFIG_F_FAILSAFE_SETPOINT:
    p->has_failsafe_setpoint_dc = true; p->failsafe_setpoint_dc = v; break;
  case OT_CONFIG_F_FAILSAFE_ROOM_TARGET:
    p->has_failsafe_room_target_dc = true; p->failsafe_room_target_dc = v; break;
  case OT_CONFIG_F_FAILSAFE_HEAT_DAYS:
    p->has_failsafe_heat_days = true; p->failsafe_heat_days = v; break;
  case OT_CONFIG_F_FAILSAFE_MIN_CYCLE:
    p->has_failsafe_min_cycle_s = true; p->failsafe_min_cycle_s = v; break;
  case OT_CONFIG_F_FLOW_MIN: p->has_flow_min_dc = true; p->flow_min_dc = v; break;
  case OT_CONFIG_F_FLOW_MAX: p->has_flow_max_dc = true; p->flow_max_dc = v; break;
  case OT_CONFIG_F_LOCAL_CH_SETPOINT:
    p->has_local_ch_setpoint_dc = true; p->local_ch_setpoint_dc = v; break;
  case OT_CONFIG_F_DHW_SETPOINT: p->has_dhw_setpoint_dc = true; p->dhw_setpoint_dc = v; break;
  case OT_CONFIG_F_ROOM_MQTT_ROLE: p->has_room_mqtt_role = true; p->room_mqtt_role = v; break;
  case OT_CONFIG_F_ROOM_MQTT_STALE_S:
    p->has_room_mqtt_stale_s = true; p->room_mqtt_stale_s = v; break;
  default: TEST_FAIL_MESSAGE("not one of the executor's numbers");
  }
}

void test_every_number_is_checked_against_its_own_bounds_and_a_refusal_changes_nothing(void) {
  // One past the top of each, beside a good broker host that must not land either. Walked field
  // by field because each has its own line in ot_config_apply()'s table, and a line left out is
  // a number stored unchecked.
  const ot_config_field_t numbers[] = {
      OT_CONFIG_F_CONTROL_MODE,       OT_CONFIG_F_WATCHDOG_S,         OT_CONFIG_F_FAILSAFE_SETPOINT,
      OT_CONFIG_F_FAILSAFE_ROOM_TARGET, OT_CONFIG_F_FAILSAFE_HEAT_DAYS, OT_CONFIG_F_FAILSAFE_MIN_CYCLE,
      OT_CONFIG_F_FLOW_MIN,           OT_CONFIG_F_FLOW_MAX,           OT_CONFIG_F_LOCAL_CH_SETPOINT,
      OT_CONFIG_F_DHW_SETPOINT,       OT_CONFIG_F_ROOM_MQTT_ROLE,     OT_CONFIG_F_ROOM_MQTT_STALE_S};
  const uint32_t too_big[] = {2, 7201, 901, 301, 31, 3601, 901, 901, 901, 901, 2, 65536};
  // And the four that become ID 1 once more, one tenth off the half-degree grid inside the band.
  const ot_config_field_t grid[] = {OT_CONFIG_F_FAILSAFE_SETPOINT, OT_CONFIG_F_FLOW_MIN,
                                    OT_CONFIG_F_FLOW_MAX, OT_CONFIG_F_LOCAL_CH_SETPOINT};
  for (size_t i = 0; i < sizeof grid / sizeof grid[0]; i++) {
    ot_config_t cfg = fresh();
    ot_config_t before;
    memcpy(&before, &cfg, sizeof before);
    ot_config_patch_t p{};
    set_number(&p, grid[i], 451);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_apply(&cfg, &p, nullptr),
                              ot_config_field(grid[i])->name);
    TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
  }
  for (size_t i = 0; i < sizeof numbers / sizeof numbers[0]; i++) {
    ot_config_t cfg = fresh();
    ot_config_t before;
    memcpy(&before, &cfg, sizeof before);
    ot_config_patch_t p{};
    p.mqtt_host = "broker.lan";
    set_number(&p, numbers[i], too_big[i]);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_RANGE, ot_config_apply(&cfg, &p, nullptr),
                              ot_config_field(numbers[i])->name);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &cfg, sizeof cfg, ot_config_field(numbers[i])->name);
  }
}

void test_the_executor_persists_its_own_fields_through_apply_within_their_bounds(void) {
  // The executor persists an accepted LOCAL command with a patch it builds in C. Refusing
  // these four in a settings BODY is ot_wire's job (test_wire), so apply has to take them -- and
  // still bounds them, because a caller in C is not a validator.
  ot_config_t cfg = fresh();
  ot_config_patch_t p{};
  p.has_local_ch_enable      = true;
  p.local_ch_enable          = true;
  p.has_local_ch_setpoint_dc = true;
  p.local_ch_setpoint_dc     = 455;
  p.has_dhw_enable           = true;
  p.dhw_enable               = false;
  p.has_dhw_setpoint_dc      = true;
  p.dhw_setpoint_dc          = 550;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_TRUE(cfg.local_ch_enable);
  TEST_ASSERT_EQUAL_UINT16(455, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_FALSE(cfg.dhw_enable);
  TEST_ASSERT_EQUAL_UINT16(550, cfg.dhw_setpoint_dc);

  ot_config_patch_t unset{};
  unset.has_dhw_setpoint_dc = true;
  unset.dhw_setpoint_dc     = 0;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &unset, nullptr));
  TEST_ASSERT_EQUAL_UINT16(0, cfg.dhw_setpoint_dc);
}

void test_every_setting_of_an_accepted_document_lands_in_its_own_field(void) {
  // Distinct values again, for the reason the projection test gives: two uint16_t side by side
  // are one typo apart, and a swapped commit is a flow bound stored as a watchdog.
  ot_config_t cfg = fresh();
  ot_config_patch_t p{};
  p.mqtt_host                   = "broker.lan";
  p.has_control_mode            = true;
  p.control_mode                = 1;
  p.has_heating_season          = true;
  p.heating_season              = true;
  p.has_watchdog_s              = true;
  p.watchdog_s                  = 901;
  p.has_failsafe_setpoint_dc    = true;
  p.failsafe_setpoint_dc        = 455;
  p.has_failsafe_room_target_dc = true;
  p.failsafe_room_target_dc     = 181;
  p.has_failsafe_heat_days      = true;
  p.failsafe_heat_days          = 4;
  p.has_failsafe_min_cycle_s    = true;
  p.failsafe_min_cycle_s        = 601;
  p.has_flow_min_dc             = true;
  p.flow_min_dc                 = 405;
  p.has_flow_max_dc             = true;
  p.flow_max_dc                 = 705;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_UINT16(1, cfg.control_mode);
  TEST_ASSERT_TRUE(cfg.heating_season);
  TEST_ASSERT_EQUAL_UINT16(901, cfg.watchdog_s);
  TEST_ASSERT_EQUAL_UINT16(455, cfg.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(181, cfg.failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT16(4, cfg.failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT16(601, cfg.failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_UINT16(405, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(705, cfg.flow_max_dc);

  // Absent is not false: a document without heating_season leaves the season where it is.
  ot_config_patch_t other{};
  other.device_name = "Hall";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &other, nullptr));
  TEST_ASSERT_TRUE(cfg.heating_season);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_document_with_one_bad_field_changes_nothing_at_all);
  RUN_TEST(test_changing_the_network_without_retyping_its_password_is_refused);
  RUN_TEST(test_saving_an_unrelated_setting_leaves_the_stored_network_untouched);
  RUN_TEST(test_half_a_wifi_pair_is_refused_whichever_half_it_is);
  RUN_TEST(test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_byte_for_byte);
  RUN_TEST(test_forgetting_the_network_clears_both_halves_at_once);
  RUN_TEST(test_an_absent_key_keeps_and_an_empty_key_clears);
  RUN_TEST(test_the_broker_password_is_stored_so_it_can_be_used_and_the_ui_password_is_not);
  RUN_TEST(test_a_ui_password_with_nowhere_to_hash_it_is_refused_rather_than_stored);
  RUN_TEST(test_the_published_sentinel_cannot_become_the_password_of_an_unconfigured_device);
  RUN_TEST(test_clearing_the_ui_password_is_allowed_because_the_owner_may_be_locked_out_of_it);
  RUN_TEST(test_a_store_that_holds_a_newer_schema_refuses_every_write);
  RUN_TEST(test_a_null_document_is_refused_rather_than_reported_as_saved);

  RUN_TEST(test_every_number_is_checked_against_its_own_bounds_and_a_refusal_changes_nothing);
  RUN_TEST(test_the_executor_persists_its_own_fields_through_apply_within_their_bounds);
  RUN_TEST(test_every_setting_of_an_accepted_document_lands_in_its_own_field);
  return UNITY_END();
}
