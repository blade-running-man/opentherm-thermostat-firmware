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
// This directory: what came back out of the flash -- ot_config_sanitize(), and the one
// predicate it shares with ot_config_nvs_has_credentials() so that the two cannot disagree.
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

// --- what came back out of the flash --------------------------------------------------------

void test_a_good_document_is_left_exactly_as_it_was(void) {
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT32(0, r);
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

void test_a_document_of_pure_garbage_from_flash_comes_back_usable(void) {
  // The second half of "validate on the way in": the way in was checked, and the flash can hand
  // back something else anyway -- a short read, a bit flip, a blob written by another firmware.
  // What is pinned here is that the device still comes up with a document every validator
  // accepts, because the alternative is a boot that panics on its own settings, and settings
  // survive a reboot: that is a loop with no way in over the air.
  //
  // NOT pinned here, and it cannot be: that sanitize() never READS past a field while deciding
  // this. Every array is MAX+1 bytes, so an unterminated one measures too long either way and
  // the outcome is the same whether or not the terminator guard exists. See the DO NOT on
  // terminated() in ot_config_internal.h for what an AddressSanitizer run says instead.
  ot_config_t cfg;
  memset(&cfg, 0xff, sizeof cfg);
  // The two booleans are set to real booleans afterwards, and that is not the test being made
  // convenient: a _Bool holding 0xff is a bit pattern no C program may read, so a loader that
  // does nvs_get_blob() straight into this struct has already lost. ot_config_nvs_io.c reads
  // them as u8 and normalises; sanitize() is downstream of that and never sees one.
  cfg.read_only    = false;
  cfg.ha_discovery = true;
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r != 0);
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_host(cfg.mqtt_host));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_name(cfg.device_name));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_user(cfg.mqtt_user));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_check_broker_password(cfg.mqtt_password));
}

void test_a_wrecked_network_record_sends_the_device_to_its_own_access_point(void) {
  // Not a loss -- an access point is a way back, and the invariant that outranks everything
  // else here is that no state exists with no network, no access point and no way in.
  // A half-readable credential that cannot associate is worse than none:
  // it keeps the device looking configured while it is unreachable.
  ot_config_t cfg = fresh();
  memcpy(cfg.wifi.ssid, "HomeNet", 7);
  cfg.wifi.ssid_len = 200;  // impossible: the array is 32 bytes
  memcpy(cfg.wifi.psk, "12345678", 8);
  cfg.wifi.psk_len = 8;

  // The half of this the suite could not previously reach, and it is the half the name is about:
  // clearing the document is not what raises the access point. ot_prov_boot_t
  // .has_credentials is, and it is filled from ot_config_nvs_has_credentials(), which asks
  // ot_config_wifi_usable() about the very same record. Asked here so that the answer the
  // device acts on is pinned in the same test as the answer the document gets.
  TEST_ASSERT_FALSE_MESSAGE(ot_config_wifi_usable(&cfg.wifi),
                            "the device was told it has a network it is about to be denied");

  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_WIFI_SSID));
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, cfg.wifi.psk_len,
                                  "half a credential was kept; half a pair is not a credential");
}

void test_the_predicate_that_raises_the_access_point_is_the_one_that_clears_the_record(void) {
  // The one place two functions could build the state that must never occur: no network, no
  // access point, no way in.
  // ot_config_nvs_has_credentials() decides whether the device raises its own access
  // point; ot_config_sanitize() decides what the station is handed. When they disagree
  // about one stored blob the device is told it has a network, puts no access point on air, and
  // hands the station an empty record -- for ever, because nothing rewrites the blob.
  //
  // They disagreed. has_credentials() asked only whether the SSID length was 1..32, so one
  // flipped bit in the PSK length byte -- 0x08 becoming 0x88 -- was a present credential to the
  // provisioning state machine and a wrecked one to the document. So there is now ONE predicate,
  // and this table is what keeps it one: every record is run through both, and the answers must
  // match.
  char not_hex[80];
  repeat(not_hex, 'z', 64);
  char hex_key[80];
  repeat(hex_key, 'a', 64);
  static const char NUL_SSID[] = {'H', 'o', 'm', 'e', '\0', 'N', 'e', 't'};

  const struct {
    const char *what;
    const char *ssid;
    size_t      ssid_bytes;
    uint8_t     ssid_len;
    const char *psk;
    size_t      psk_bytes;
    uint8_t     psk_len;
    bool        usable;
  } records[] = {
      {"a network with a passphrase", "HomeNet", 7, 7, "secret12", 8, 8, true},
      {"an open network", "HomeNet", 7, 7, "", 0, 0, true},
      {"a 32-character SSID and a 64-character hex key", "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", 32,
       32, hex_key, 64, 64, true},
      // The reviewer's trigger, and the reason this test exists: one flipped bit in a length
      // byte. The pair is intact, the record is not, and the length-only test called it present.
      {"a PSK length of 136", "HomeNet", 7, 7, "secret12", 8, 136, false},
      {"a PSK shorter than WPA2 allows", "HomeNet", 7, 7, "secret1", 7, 7, false},
      // 64 bytes is the 256-bit key as hex by LENGTH ALONE (rsn_supp/wpa.c:2517-2524); when it is
      // not hex, no PMK is computed and the device silently never associates.
      {"a 64-byte key that is not hex", "HomeNet", 7, 7, not_hex, 64, 64, false},
      {"an SSID with a NUL inside it", NUL_SSID, 8, 8, "secret12", 8, 8, false},
      {"an SSID length past the end of the array", "HomeNet", 7, 200, "secret12", 8, 8, false},
      {"nothing stored at all", "", 0, 0, "", 0, 0, false},
  };

  for (size_t i = 0; i < sizeof records / sizeof records[0]; i++) {
    ot_wifi_t w{};
    memcpy(w.ssid, records[i].ssid, records[i].ssid_bytes);
    memcpy(w.psk, records[i].psk, records[i].psk_bytes);
    w.ssid_len = records[i].ssid_len;
    w.psk_len  = records[i].psk_len;

    TEST_ASSERT_EQUAL_MESSAGE(records[i].usable, ot_config_wifi_usable(&w), records[i].what);

    ot_config_t cfg = fresh();
    cfg.wifi              = w;
    ot_config_sanitize(&cfg, DEVICE_ID);
    const bool kept = cfg.wifi.ssid_len != 0;
    TEST_ASSERT_EQUAL_MESSAGE(ot_config_wifi_usable(&w), kept, records[i].what);
  }

  // NULL is not a record. The caller is a store that may have failed to read anything at all.
  TEST_ASSERT_FALSE(ot_config_wifi_usable(nullptr));
}

void test_a_password_record_that_cannot_be_read_unlocks_the_device_rather_than_sealing_it(void) {
  // The uncomfortable one, and it is deliberate. A corrupt record matches no password that
  // exists, so keeping it locks the owner out of a device that is visible on their network and
  // completely unusable -- with no way in over the air and no keypad on the thermostat between
  // them and the button. Clearing it leaves the device open on the LAN, which the owner can SEE and
  // fix. That is the no-way-in invariant applied to the password rather than to the
  // radio. DO NOT "harden" this into keeping the record.
  ot_config_t cfg = fresh();
  memcpy(cfg.ui_pw_hash, "1$10000$not-a-record", 21);
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));
}

void test_a_readable_password_record_is_kept(void) {
  // The other side of the previous case: sanitize must not throw away a record it merely does
  // not recognise the digest of. It has no password to check against and must not invent one.
  ot_config_t                cfg = fresh();
  const ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(ot_config_hash_password("a-good-password", &ctx, cfg.ui_pw_hash,
                                                 sizeof cfg.ui_pw_hash));
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_TRUE(ot_config_password_set(&cfg));
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(cfg.ui_pw_hash, "a-good-password",
                                                             fake_kdf));
}

void test_a_broker_port_of_zero_from_flash_becomes_the_default_and_is_reported(void) {
  // A zeroed key -- an interrupted write, or a field this build reads and an older one never
  // wrote. Zero is not a port, and handing it to esp-mqtt is a connection that never happens.
  ot_config_t cfg = fresh();
  cfg.mqtt_port         = 0;
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_PORT));
  TEST_ASSERT_EQUAL_UINT16(1883, cfg.mqtt_port);
}

void test_sanitize_names_every_field_it_replaced_so_the_owner_can_be_told(void) {
  // A device that quietly loses settings is indistinguishable from one that reset itself,
  // and the owner concludes the firmware is unreliable. Nothing here logs -- a rejected value
  // may itself be a secret and the log ring is public -- so the bitmask is
  // how the fact travels.
  ot_config_t cfg = fresh();
  cfg.mqtt_port         = 0;
  memset(cfg.device_name, 'x', sizeof cfg.device_name);  // no terminator
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_PORT));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_DEVICE_NAME));
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_HOST));
}

void test_sanitize_does_not_take_the_broker_password_away_with_the_host(void) {
  // A repair is per field. Clearing a whole namespace because one value in it was unreadable
  // is a factory reset nobody asked for.
  ot_config_t cfg = fresh();
  memcpy(cfg.mqtt_host, "broker.lan", 11);
  memcpy(cfg.mqtt_password, "broker-secret", 14);
  cfg.mqtt_port = 0;
  ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("broker-secret", cfg.mqtt_password);
}

// --- the executor's numbers from flash ------------------------------------------

void test_a_number_from_flash_outside_its_bounds_goes_back_to_its_default_and_is_reported(void) {
  // ot_config_io_load_u16() takes whatever sixteen bits the key holds and validates nothing, so
  // this is where they are judged -- by the same checker ot_config_apply() runs on the way in.
  ot_config_t cfg             = fresh();
  cfg.control_mode            = 2;
  cfg.watchdog_s              = 0xffff;
  cfg.failsafe_room_target_dc = 49;
  cfg.failsafe_heat_days      = 0;
  cfg.failsafe_min_cycle_s    = 59;
  cfg.local_ch_setpoint_dc    = 901;
  cfg.dhw_setpoint_dc         = 901;
  cfg.room_mqtt_role          = 5;
  cfg.room_mqtt_stale_s       = 3;
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.control_mode);
  TEST_ASSERT_EQUAL_UINT16(900, cfg.watchdog_s);
  TEST_ASSERT_EQUAL_UINT16(180, cfg.failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT16(3, cfg.failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT16(600, cfg.failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.dhw_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(1, cfg.room_mqtt_role);
  TEST_ASSERT_EQUAL_UINT16(900, cfg.room_mqtt_stale_s);
  const ot_config_field_t repaired[] = {
      OT_CONFIG_F_CONTROL_MODE,       OT_CONFIG_F_WATCHDOG_S,        OT_CONFIG_F_FAILSAFE_ROOM_TARGET,
      OT_CONFIG_F_FAILSAFE_HEAT_DAYS, OT_CONFIG_F_FAILSAFE_MIN_CYCLE, OT_CONFIG_F_LOCAL_CH_SETPOINT,
      OT_CONFIG_F_DHW_SETPOINT,       OT_CONFIG_F_ROOM_MQTT_ROLE,     OT_CONFIG_F_ROOM_MQTT_STALE_S};
  for (size_t i = 0; i < sizeof repaired / sizeof repaired[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(r & OT_CONFIG_REPAIRED(repaired[i]), ot_config_field(repaired[i])->name);
  // A repair is per field: the flow band was legal and is neither touched nor reported.
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MIN));
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT));
  TEST_ASSERT_EQUAL_UINT16(400, cfg.flow_min_dc);
}

void test_one_flow_value_from_flash_outside_its_bounds_is_repaired_alone(void) {
  ot_config_t cfg          = fresh();
  cfg.flow_min_dc          = 450;
  cfg.flow_max_dc          = 901;
  cfg.failsafe_setpoint_dc = 0xffff;
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(700, cfg.flow_max_dc);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(450, cfg.flow_min_dc, "a legal lowest flow was thrown away");
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MAX));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT));
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MIN));
}

void test_a_setpoint_from_flash_off_the_half_degree_grid_is_damage_to_that_field(void) {
  // Every writer refuses a value off the grid (ot_config_check_range) and no schema before 2 stored
  // these keys, so one off it can only be damage -- the same damage as one out of bounds, with the
  // same answer: its own default. Each value below rounds to a point that is NOT its default (502
  // to 500, 648 to 650, 553 to 555, 597 to 595), so a sanitize that rounded fails here.
  ot_config_t cfg          = fresh();
  cfg.flow_min_dc          = 502;
  cfg.flow_max_dc          = 648;
  cfg.failsafe_setpoint_dc = 553;
  cfg.local_ch_setpoint_dc = 597;
  cfg.failsafe_room_target_dc = 181;  // on no grid, and needing none
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(400, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(700, cfg.flow_max_dc);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(450, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT16(181, cfg.failsafe_room_target_dc);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MIN));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MAX));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT));
  TEST_ASSERT_FALSE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_ROOM_TARGET));

  ot_config_t out = fresh();
  out.flow_max_dc = 902;
  ot_config_sanitize(&out, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(700, out.flow_max_dc);
}

void test_a_damaged_failsafe_in_a_legal_band_is_repaired_alone_and_the_band_survives(void) {
  // The code-quality review's probe: 400..450 is a legal band and 453 is damage to the failsafe
  // alone. Rounding it gave 455, outside the band, and the triple rule then threw the owner's band
  // away over a fault in another field. It lands on 450: its default brought into the band, and
  // the default already lies inside 400..450 on the grid, so into_band() leaves it where it is.
  ot_config_t cfg          = fresh();
  cfg.flow_max_dc          = 450;
  cfg.failsafe_setpoint_dc = 453;
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(400, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(450, cfg.flow_max_dc, "a legal band was thrown away");
  TEST_ASSERT_EQUAL_UINT16(450, cfg.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT), r);

  // A band that does not contain the default moves it to the nearest edge -- the rule the local
  // setpoint is held to -- and a failsafe out of bounds is the same damage with the same answer.
  const uint16_t damaged[] = {553, 0xffff};
  for (size_t i = 0; i < sizeof damaged / sizeof damaged[0]; i++) {
    ot_config_t high          = fresh();
    high.flow_min_dc          = 500;
    high.flow_max_dc          = 600;
    high.local_ch_setpoint_dc = 550;
    high.failsafe_setpoint_dc = damaged[i];
    const ot_config_repairs_t rh = ot_config_sanitize(&high, DEVICE_ID);
    TEST_ASSERT_EQUAL_UINT16(500, high.flow_min_dc);
    TEST_ASSERT_EQUAL_UINT16(600, high.flow_max_dc);
    TEST_ASSERT_EQUAL_UINT16(500, high.failsafe_setpoint_dc);
    TEST_ASSERT_EQUAL_UINT16(550, high.local_ch_setpoint_dc);
    TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT), rh);
  }
}

void test_a_flow_band_from_flash_with_no_room_for_its_setpoint_goes_back_whole(void) {
  // Each value legal, the triple not: which one flash damaged cannot be known, so the three go
  // back to the defaults together and all three are reported. The owner is told -- flow_min at
  // or above the boiler's parameter E is their duty, and a silent 40.0 is not.
  ot_config_t inverted = fresh();
  inverted.flow_min_dc = 600;
  inverted.flow_max_dc = 500;
  const ot_config_repairs_t r1 = ot_config_sanitize(&inverted, DEVICE_ID);
  ot_config_t outside          = fresh();
  outside.flow_min_dc          = 500;
  const ot_config_repairs_t r2 = ot_config_sanitize(&outside, DEVICE_ID);
  const ot_config_t *const docs[] = {&inverted, &outside};
  const ot_config_repairs_t reps[] = {r1, r2};
  for (size_t i = 0; i < 2; i++) {
    TEST_ASSERT_EQUAL_UINT16(400, docs[i]->flow_min_dc);
    TEST_ASSERT_EQUAL_UINT16(700, docs[i]->flow_max_dc);
    TEST_ASSERT_EQUAL_UINT16(450, docs[i]->failsafe_setpoint_dc);
    TEST_ASSERT_TRUE(reps[i] & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MIN));
    TEST_ASSERT_TRUE(reps[i] & OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MAX));
    TEST_ASSERT_TRUE(reps[i] & OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT));
  }
}

void test_a_local_setpoint_from_flash_outside_its_band_moves_to_the_nearest_edge(void) {
  // The probe, from flash: local 850 under a band that tops out at 600 is legal
  // field by field and was reported by nobody. ot_control would clamp it to 60.0 on the bus while
  // the page showed 85.0.
  ot_config_t cfg          = fresh();
  cfg.flow_max_dc          = 600;
  cfg.local_ch_setpoint_dc = 850;
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT),
                           ot_config_sanitize(&cfg, DEVICE_ID));
  TEST_ASSERT_EQUAL_UINT16(600, cfg.local_ch_setpoint_dc);
  ot_config_t low          = fresh();
  low.flow_min_dc          = 500;
  low.failsafe_setpoint_dc = 500;
  low.local_ch_setpoint_dc = 450;
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT),
                           ot_config_sanitize(&low, DEVICE_ID));
  TEST_ASSERT_EQUAL_UINT16(500, low.local_ch_setpoint_dc);
  // AFTER the triple: a band sanitize has just reset is the band the setpoint is moved into.
  ot_config_t inv          = fresh();
  inv.flow_min_dc          = 600;
  inv.flow_max_dc          = 500;
  inv.local_ch_setpoint_dc = 800;
  ot_config_sanitize(&inv, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT16(700, inv.local_ch_setpoint_dc);
}

void test_home_assistant_mode_from_flash_with_no_broker_falls_back_to_local(void) {
  // The rule the way in enforces, asked again of what came back. It is reached for real:
  // a broker address the flash damaged is cleared a few lines earlier in sanitize(), and a device
  // left in HA mode with no broker can never hear the command that ends ha_waiting.
  ot_config_t cfg  = fresh();
  cfg.control_mode = 1;
  memset(cfg.mqtt_host, 'x', sizeof cfg.mqtt_host);  // no terminator: cleared as damaged
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_UINT16(0, cfg.control_mode);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_HOST));
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_CONTROL_MODE));

  ot_config_t kept = fresh();
  kept.control_mode = 1;
  snprintf(kept.mqtt_host, sizeof kept.mqtt_host, "%s", "broker.lan");
  TEST_ASSERT_EQUAL_UINT32(0, ot_config_sanitize(&kept, DEVICE_ID));
  TEST_ASSERT_EQUAL_UINT16(1, kept.control_mode);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_good_document_is_left_exactly_as_it_was);
  RUN_TEST(test_a_document_of_pure_garbage_from_flash_comes_back_usable);
  RUN_TEST(test_a_wrecked_network_record_sends_the_device_to_its_own_access_point);
  RUN_TEST(test_the_predicate_that_raises_the_access_point_is_the_one_that_clears_the_record);
  RUN_TEST(test_a_password_record_that_cannot_be_read_unlocks_the_device_rather_than_sealing_it);
  RUN_TEST(test_a_readable_password_record_is_kept);
  RUN_TEST(test_a_broker_port_of_zero_from_flash_becomes_the_default_and_is_reported);
  RUN_TEST(test_sanitize_names_every_field_it_replaced_so_the_owner_can_be_told);
  RUN_TEST(test_sanitize_does_not_take_the_broker_password_away_with_the_host);

  RUN_TEST(test_a_number_from_flash_outside_its_bounds_goes_back_to_its_default_and_is_reported);
  RUN_TEST(test_one_flow_value_from_flash_outside_its_bounds_is_repaired_alone);
  RUN_TEST(test_a_setpoint_from_flash_off_the_half_degree_grid_is_damage_to_that_field);
  RUN_TEST(test_a_damaged_failsafe_in_a_legal_band_is_repaired_alone_and_the_band_survives);
  RUN_TEST(test_a_flow_band_from_flash_with_no_room_for_its_setpoint_goes_back_whole);
  RUN_TEST(test_a_local_setpoint_from_flash_outside_its_band_moves_to_the_nearest_edge);
  RUN_TEST(test_home_assistant_mode_from_flash_with_no_broker_falls_back_to_local);
  return UNITY_END();
}
