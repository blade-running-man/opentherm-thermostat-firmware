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
// This directory: the one-way password record -- its format, its salt, the iteration count it
// carries, and what a record that does not parse is allowed to do, which is nothing.
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
static const uint8_t SALT_B[OT_CONFIG_SALT_LEN] = {99, 2,  3,  4,  5,  6,  7,  8,
                                                         9,  10, 11, 12, 13, 14, 15, 16};

static ot_config_hash_ctx_t hash_with(const uint8_t *salt, ot_config_kdf_t kdf) {
  ot_config_hash_ctx_t ctx{};
  ctx.kdf        = kdf;
  ctx.salt       = salt;
  ctx.iterations = 0;  // 0 means the compiled-in count; see OT_CONFIG_KDF_ITERATIONS
  return ctx;
}

// A stored password record assembled here rather than by ot_config_hash_password(), so a
// case can carry an iteration count the writer refuses to produce. Everything else about it is
// exactly what the writer would have written, which is the point: a record that fails to verify
// for a second reason proves nothing about the first.
static void record_at(uint32_t iterations, const char *password, char *out, size_t cap) {
  uint8_t digest[OT_CONFIG_DIGEST_LEN];
  fake_kdf(password, SALT_A, OT_CONFIG_SALT_LEN, iterations, digest, sizeof digest);
  char salt_hex[OT_CONFIG_SALT_LEN * 2 + 1];
  char digest_hex[OT_CONFIG_DIGEST_LEN * 2 + 1];
  for (size_t i = 0; i < OT_CONFIG_SALT_LEN; i++)
    snprintf(salt_hex + i * 2, 3, "%02x", SALT_A[i]);
  for (size_t i = 0; i < sizeof digest; i++)
    snprintf(digest_hex + i * 2, 3, "%02x", digest[i]);
  snprintf(out, cap, "1$%u$%s$%s", (unsigned)iterations, salt_hex, digest_hex);
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

// --- the password record -----------------------------------------------------------------------

void test_the_stored_record_is_not_the_password_and_carries_its_own_cost(void) {
  // `1$<iterations>$<salt>$<digest>`. The iteration count is IN the record so that raising it
  // later does not invalidate what is already stored -- a device whose owner cannot log in
  // after an update is the same loss as a lost password, arriving by a different road.
  char record[OT_CONFIG_HASH_MAX + 1];
  const ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(ot_config_hash_password("hunter2-and-more", &ctx, record, sizeof record));

  TEST_ASSERT_FALSE(contains_bytes(record, strlen(record), "hunter2-and-more"));
  TEST_ASSERT_EQUAL('1', record[0]);
  TEST_ASSERT_EQUAL('$', record[1]);
  TEST_ASSERT_TRUE(contains_bytes(record, strlen(record), "$10000$"));
  TEST_ASSERT_TRUE_MESSAGE(strlen(record) <= OT_CONFIG_HASH_MAX,
                           "the record does not fit the field it is stored in");
}

void test_the_right_password_verifies_and_a_wrong_one_does_not(void) {
  char record[OT_CONFIG_HASH_MAX + 1];
  const ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(ot_config_hash_password("correct-horse", &ctx, record, sizeof record));

  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(record, "correct-horse", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against(record, "correct-hors", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against(record, "", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against(record, nullptr, fake_kdf));
}

void test_two_devices_with_the_same_password_do_not_share_a_record(void) {
  // One recovered flash image would otherwise answer for every device whose owner picked the
  // same word. The salt is per stored password, not per device and not per build.
  char a[OT_CONFIG_HASH_MAX + 1];
  char b[OT_CONFIG_HASH_MAX + 1];
  const ot_config_hash_ctx_t ctx_a = hash_with(SALT_A, fake_kdf);
  const ot_config_hash_ctx_t ctx_b = hash_with(SALT_B, fake_kdf);
  TEST_ASSERT_TRUE(ot_config_hash_password("same-password", &ctx_a, a, sizeof a));
  TEST_ASSERT_TRUE(ot_config_hash_password("same-password", &ctx_b, b, sizeof b));
  TEST_ASSERT_TRUE_MESSAGE(strcmp(a, b) != 0, "the salt is not being used");
  // And each still verifies its own, which is what says the salt is carried, not just mixed in.
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(a, "same-password", fake_kdf));
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(b, "same-password", fake_kdf));
}

void test_a_record_written_at_another_iteration_count_still_verifies(void) {
  // The reason the count is in the record. A future build that raises it must still let in
  // everyone who set a password under the old one.
  ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  ctx.iterations                 = 4096;
  char record[OT_CONFIG_HASH_MAX + 1];
  TEST_ASSERT_TRUE(ot_config_hash_password("old-password", &ctx, record, sizeof record));
  TEST_ASSERT_TRUE(contains_bytes(record, strlen(record), "$4096$"));
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(record, "old-password", fake_kdf));
}

void test_a_record_that_does_not_parse_lets_nobody_in(void) {
  // Garbage from a bit flip or from a firmware that wrote another format. It must not throw,
  // must not read past the end, and must not accidentally match. What HAPPENS to such a record
  // is decided by sanitize(), not here -- this only says it never authenticates anyone.
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("", "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against(nullptr, "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("garbage", "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("1$", "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("1$10000$zz$ff", "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("9$10000$aabb$ccdd", "anything", fake_kdf));
  TEST_ASSERT_FALSE(ot_config_check_ui_password_against("1$0$aabb$ccdd", "anything", fake_kdf));
}

void test_an_iteration_count_nobody_could_wait_for_is_not_a_record(void) {
  // The cost is carried IN the record, which is what lets a future build raise it -- and it is
  // therefore a number that arrives from flash, where a bit flip lives. At this file's own figure
  // of 100 ms per 10,000 iterations on a C6, 100,000,000 is about seventeen minutes of PBKDF2
  // per login attempt, run on the HTTP task, in front of an owner who is being asked for a
  // password. There is no lockout to stop them being asked again, so nothing bounds how
  // often that is paid.
  //
  // Built by hand with the RIGHT digest for that count. A record with a wrong digest would be
  // refused for the ordinary reason and would prove nothing about the count -- which is exactly
  // how this test passed before it was looked at twice.
  char record[OT_CONFIG_HASH_MAX + 1];
  record_at(OT_CONFIG_KDF_ITERATIONS, "a-good-password", record, sizeof record);
  TEST_ASSERT_TRUE_MESSAGE(
      ot_config_check_ui_password_against(record, "a-good-password", fake_kdf),
      "the hand-built record does not verify, so nothing below is about the iteration count");

  record_at(100000000u, "a-good-password", record, sizeof record);
  TEST_ASSERT_FALSE_MESSAGE(
      ot_config_check_ui_password_against(record, "a-good-password", fake_kdf),
      "a login was allowed to cost seventeen minutes of the HTTP task");

  // And it is not left in flash to be re-parsed at every attempt. sanitize() throws away a
  // record it cannot read, for the reason in test_a_password_record_that_cannot_be_read: the
  // device ends up open on the LAN, which the owner can see and fix.
  ot_config_t cfg = fresh();
  memcpy(cfg.ui_pw_hash, record, strlen(record) + 1);
  const ot_config_repairs_t r = ot_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & OT_CONFIG_REPAIRED(OT_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_FALSE(ot_config_password_set(&cfg));

  // The cap has to sit ABOVE the compiled-in count with room to spare, or raising that count in
  // a later build locks out everyone who set a password under the old one -- the same loss as a
  // forgotten password, arriving by a different road.
  TEST_ASSERT_TRUE(OT_CONFIG_KDF_ITERATIONS_MAX > OT_CONFIG_KDF_ITERATIONS);

  // Whatever this build can WRITE it must be able to READ BACK. A parser stricter than the
  // writer is a device that stores a password and then refuses it, with nobody to ask; so the
  // count at the cap round-trips, and one past the cap is refused at the WRITER, before anything
  // unreadable reaches the flash.
  ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  ctx.iterations                 = OT_CONFIG_KDF_ITERATIONS_MAX;
  TEST_ASSERT_TRUE(ot_config_hash_password("a-good-password", &ctx, record, sizeof record));
  TEST_ASSERT_TRUE(ot_config_check_ui_password_against(record, "a-good-password", fake_kdf));

  char guarded[OT_CONFIG_HASH_MAX + 1];
  memset(guarded, 'Z', sizeof guarded);
  ctx.iterations = OT_CONFIG_KDF_ITERATIONS_MAX + 1;
  TEST_ASSERT_FALSE_MESSAGE(
      ot_config_hash_password("a-good-password", &ctx, guarded, sizeof guarded),
      "a record was written at a count this build's own parser refuses");
  for (size_t i = 0; i < sizeof guarded; i++)
    TEST_ASSERT_EQUAL('Z', guarded[i]);
}

void test_a_record_is_never_written_into_a_buffer_it_does_not_fit(void) {
  char small[8];
  memset(small, 'Z', sizeof small);
  const ot_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_FALSE(ot_config_hash_password("a-good-password", &ctx, small, sizeof small));
  for (size_t i = 0; i < sizeof small; i++)
    TEST_ASSERT_EQUAL_MESSAGE('Z', small[i], "a failed hash wrote a partial record");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_stored_record_is_not_the_password_and_carries_its_own_cost);
  RUN_TEST(test_the_right_password_verifies_and_a_wrong_one_does_not);
  RUN_TEST(test_two_devices_with_the_same_password_do_not_share_a_record);
  RUN_TEST(test_a_record_written_at_another_iteration_count_still_verifies);
  RUN_TEST(test_a_record_that_does_not_parse_lets_nobody_in);
  RUN_TEST(test_an_iteration_count_nobody_could_wait_for_is_not_a_record);
  RUN_TEST(test_a_record_is_never_written_into_a_buffer_it_does_not_fit);
  return UNITY_END();
}
