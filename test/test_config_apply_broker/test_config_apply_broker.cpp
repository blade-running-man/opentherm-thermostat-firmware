// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The broker host and its password are a pair. Everything here is about the pairing of
// mqtt_host and mqtt_password: a new host may not carry the old, kept password (that would
// exfiltrate a cleartext secret to a possibly attacker-chosen broker), a legit migration brings a
// fresh password with it, an unrelated save keeps the pair intact, and emptying the host disables
// MQTT rather than repointing it.
//
// Cut out of test/test_config_apply/: that file had reached 628 lines against the
// 600-line ceiling of a suite (CLAUDE.md, File ceiling), and a suite can only be cut along
// directories, because PlatformIO links every .cpp of a directory into one binary. The broker-pair
// cases were the clean seam and were moved byte for byte, not rewritten. The Wi-Fi and
// other config-apply cases stayed in test/test_config_apply/.
//
// The helpers below are this directory's OWN copy of exactly the ones its tests call. DO NOT hoist
// them into a shared directory: the moment two suites share a helper, "it grew a capability for
// suite B" becomes a way to quietly weaken suite A (CLAUDE.md, Tests). And a directory named
// test/test_config_common/ would itself be collected as a suite by `test_filter = test_*`.
#include <unity.h>

#include <cstring>

#include "ot_config.h"

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static ot_config_t fresh(void) {
  ot_config_t cfg{};
  ot_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

void test_changing_the_broker_without_retyping_its_password_is_refused(void) {
  // The broker analogue of the Wi-Fi pairing rule. The settings page renders a stored
  // broker password as the sentinel, so a form where only mqtt_host was edited submits a NEW
  // broker with the OLD password KEPT. The MQTT client would then send that password in
  // cleartext to the new -- possibly attacker-chosen -- host, exfiltrating it. Refused whole,
  // and nothing moves (all or nothing).
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.mqtt_host     = "broker.lan";
  first.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);

  ot_config_patch_t second{};
  second.mqtt_host = "evil.example";  // mqtt_password omitted -> KEEP
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_ERR_BROKER_PAIR,
                            ot_config_apply(&cfg, &second, nullptr),
                            "a new broker host was accepted with the previous broker's password");
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &cfg, sizeof cfg,
                                   "a refused broker-pair document was applied in part");
}

void test_changing_the_broker_with_the_sentinel_password_is_refused(void) {
  // The same refusal, reached the way the SPA actually sends it: the untouched password box
  // comes back as OT_SECRET_SENTINEL rather than absent. ot_secret_decide() maps the sentinel
  // over a stored secret to KEEP, so this must land on the same guard as the omitted-field case.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.mqtt_host     = "broker.lan";
  first.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t second{};
  second.mqtt_host     = "evil.example";
  second.mqtt_password = OT_SECRET_SENTINEL;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_BROKER_PAIR, ot_config_apply(&cfg, &second, nullptr));
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
}

void test_saving_an_unrelated_setting_leaves_the_stored_broker_password_untouched(void) {
  // The other half of the broker-pair rule, and the reason it cannot simply refuse every kept password: the
  // page submits the WHOLE document, so keeping the SAME host while editing anything else
  // re-submits the sentinel password unchanged. Refusing that would make every other setting
  // unsavable once a broker password is stored.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.mqtt_host     = "broker.lan";
  first.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t second{};
  second.mqtt_host = "broker.lan";  // same host, password omitted -> KEEP
  second.mqtt_user = "someone";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &second, nullptr));
  TEST_ASSERT_EQUAL_STRING_MESSAGE("broker-secret", cfg.mqtt_password,
                                   "the stored broker password was dropped by a legit save");
  TEST_ASSERT_EQUAL_STRING("someone", cfg.mqtt_user);
}

void test_changing_the_broker_with_a_fresh_password_is_a_legit_migration(void) {
  // A DIFFERENT host WITH a new password is the deliberate move to a new broker. broker is
  // OT_SECRET_STORE, not KEEP, so the guard does not fire and both the host and the password are
  // stored.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.mqtt_host     = "broker.lan";
  first.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t second{};
  second.mqtt_host     = "new.broker.lan";
  second.mqtt_password = "new-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &second, nullptr));
  TEST_ASSERT_EQUAL_STRING("new.broker.lan", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("new-secret", cfg.mqtt_password);
}

void test_changing_the_broker_with_no_password_ever_stored_is_allowed(void) {
  // Nothing to protect: a device with no broker password can point at any host freely. The guard
  // keys on a password actually being stored, so this must pass.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.mqtt_host = "some.broker.lan";  // password omitted, and none stored
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_STRING("some.broker.lan", cfg.mqtt_host);
}

void test_emptying_the_broker_host_disables_mqtt_and_keeps_the_password(void) {
  // A refinement of the broker-pair rule. Clearing mqtt_host to "" DISABLES MQTT (ot_config_check_host treats an
  // empty host as "I do not use a broker", and the client connects nowhere), so there is no new,
  // possibly attacker-chosen host to which the kept password could be exfiltrated. That is not a
  // repointing, so the pair guard must NOT fire -- refusing it would make disabling the broker
  // impossible once a password is stored. Reached both ways the SPA can send a kept password:
  // omitted, and the rendered sentinel.
  ot_config_t       cfg = fresh();
  ot_config_patch_t first{};
  first.mqtt_host     = "broker.lan";
  first.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &first, nullptr));

  ot_config_patch_t omitted{};
  omitted.mqtt_host = "";  // password omitted -> KEEP
  TEST_ASSERT_EQUAL_MESSAGE(OT_CONFIG_OK, ot_config_apply(&cfg, &omitted, nullptr),
                            "disabling the broker was refused as a repointing");
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("broker-secret", cfg.mqtt_password,
                                   "the kept password was dropped while disabling the broker");

  // Re-store a host so the sentinel case starts from the same place. A fresh password comes with
  // it -- re-pointing "" -> a host with a KEPT password would (correctly) hit the guard.
  ot_config_patch_t restore{};
  restore.mqtt_host     = "broker.lan";
  restore.mqtt_password = "broker-secret";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &restore, nullptr));

  ot_config_patch_t sentinel{};
  sentinel.mqtt_host     = "";
  sentinel.mqtt_password = OT_SECRET_SENTINEL;  // KEEP
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &sentinel, nullptr));
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("broker-secret", cfg.mqtt_password);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_changing_the_broker_without_retyping_its_password_is_refused);
  RUN_TEST(test_changing_the_broker_with_the_sentinel_password_is_refused);
  RUN_TEST(test_saving_an_unrelated_setting_leaves_the_stored_broker_password_untouched);
  RUN_TEST(test_changing_the_broker_with_a_fresh_password_is_a_legit_migration);
  RUN_TEST(test_changing_the_broker_with_no_password_ever_stored_is_allowed);
  RUN_TEST(test_emptying_the_broker_host_disables_mqtt_and_keeps_the_password);
  return UNITY_END();
}
