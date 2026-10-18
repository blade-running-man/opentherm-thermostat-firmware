// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// POST /api/provision: the network and the key that are typed in on the open access point.
//
// The least protected body in the whole firmware -- it is sent by anyone within range of the
// open access point. Two properties common to the whole suite:
//
//  * NO SECRET IS EVER RENDERED. Not the Wi-Fi key, not the broker password, not the UI password.
//    The projection turns each into a sentinel or an empty string and this suite asserts the
//    value itself never appears in the bytes.
//  * NO SUBMITTED VALUE IS EVER QUOTED IN AN ERROR. Errors name a FIELD. Everything this device
//    logs is served by GET /api/log and on the setup access point that is world-readable,
//    so a message that echoes what was typed publishes it.
//
// A separate directory, not a separate file in test_wire: PlatformIO links all the .cpp files
// of a directory into one binary, and two main()s in one directory would collide.

#include <unity.h>

#include <cstring>
#include <string>

#include "ot_json.h"
#include "ot_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

ot_wifi_t pair(const char *ssid, const char *psk) {
  ot_wifi_t w = {};
  w.ssid_len = (uint8_t)strlen(ssid);
  memcpy(w.ssid, ssid, w.ssid_len);
  w.psk_len = (uint8_t)strlen(psk);
  memcpy(w.psk, psk, w.psk_len);
  return w;
}

std::string ssid_of(const ot_wifi_t &w) {
  return std::string((const char *)w.ssid, w.ssid_len);
}

std::string psk_of(const ot_wifi_t &w) {
  return std::string((const char *)w.psk, w.psk_len);
}

bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != nullptr;
}

}  // namespace

// --- POST /api/provision ---------------------------------------------------------------------

void test_a_network_and_a_key_are_taken_as_typed(void) {
  ot_wifi_t out = {};
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"hunter2hunter\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("Kitchen", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING("hunter2hunter", psk_of(out).c_str());
}

void test_an_empty_key_is_an_open_network_and_not_a_missing_one(void) {
  // ot_config_check_psk returns OK for length 0 for exactly this reason: half the guest
  // networks in the world have no key. The page says the same thing in its own words
  // (describeWifiKey, web/src/pages/settings/wifi.ts).
  ot_wifi_t out = {};
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Guest\",\"wifi_psk\":\"\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("Guest", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_UINT8(0, out.psk_len);
}

void test_an_absent_key_keeps_the_one_already_stored(void) {
  // The owner re-submitted the SAME network with some OTHER field edited; the form omits the
  // password field rather than sending a placeholder, which is the same rule ot_config_patch_t
  // states for every other string. The key is kept only because the SSID is UNCHANGED -- an
  // absent key against a DIFFERENT SSID is refused (the test below), because a kept key
  // belongs to the network it was stored for.
  const ot_wifi_t stored = pair("Old", "storedkey123");
  ot_wifi_t       out    = {};
  const ot_wire_result_t r =
      ot_wire_parse_provision("{\"wifi_ssid\":\"Old\"}", &stored, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("Old", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING("storedkey123", psk_of(out).c_str());
}

void test_an_absent_key_with_nothing_stored_is_an_open_network_attempt(void) {
  // KEEP of nothing. The correct reading, and it needs no special case -- a device with no
  // stored key that is told to join a network without one tries it open.
  ot_wifi_t out = {};
  const ot_wire_result_t r =
      ot_wire_parse_provision("{\"wifi_ssid\":\"Open\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_UINT8(0, out.psk_len);
}

void test_the_sentinel_with_a_key_stored_means_keep_it(void) {
  // The page rendered the sentinel for an untouched box and handed it straight back. This is the
  // ordinary case of a form submitted with only the SSID changed.
  const ot_wifi_t stored = pair("Kitchen", "storedkey123");
  ot_wifi_t       out    = {};
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" OT_SECRET_SENTINEL "\"}", &stored, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("storedkey123", psk_of(out).c_str());
}

void test_a_new_ssid_with_the_sentinel_keeps_no_stored_key(void) {
  // A KEPT password belongs to the network it was stored FOR. Pairing it with a
  // DIFFERENT SSID is the atomicity failure ot_config_apply() already refuses on /api/config: a
  // new network carrying the previous network's key, which strands the device. This route must
  // agree, or the two surfaces that write Wi-Fi credentials disagree about the same pairing.
  const ot_wifi_t stored = pair("Kitchen", "storedkey123");
  ot_wifi_t       out    = pair("original", "must-not-move");
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Bedroom\",\"wifi_psk\":\"" OT_SECRET_SENTINEL "\"}", &stored, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_REFUSED, r.status);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_WIFI_PAIR, r.err);
  // Nothing was written into the caller's record on the way to refusing.
  TEST_ASSERT_EQUAL_STRING("original", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING("must-not-move", psk_of(out).c_str());
}

void test_a_new_ssid_with_the_key_omitted_keeps_no_stored_key(void) {
  // The same refusal reached through the OTHER shape of KEEP: the password field is absent
  // rather than the sentinel. ot_secret_decide(has_stored, NULL) is also KEEP, so a new SSID
  // with no password submitted must land on the same guard -- not silently inherit the stored
  // key for a network it was never the key of.
  const ot_wifi_t stored = pair("Kitchen", "storedkey123");
  ot_wifi_t       out    = {};
  const ot_wire_result_t r =
      ot_wire_parse_provision("{\"wifi_ssid\":\"Bedroom\"}", &stored, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_REFUSED, r.status);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_WIFI_PAIR, r.err);
}

void test_the_same_ssid_with_the_sentinel_still_keeps_the_key(void) {
  // The regression guard for that rule: the legit "edited only some OTHER field" case, where the
  // SSID is unchanged. The stored key must still be kept, or the fix would break every ordinary
  // re-submission of the setup form.
  const ot_wifi_t stored = pair("Kitchen", "storedkey123");
  ot_wifi_t       out    = {};
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" OT_SECRET_SENTINEL "\"}", &stored, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("storedkey123", psk_of(out).c_str());
}

void test_the_sentinel_is_never_stored_as_a_key(void) {
  // THE CASE THIS FUNCTION EXISTS FOR. "__UNCHANGED__" is thirteen printable bytes and passes
  // ot_config_check_psk whole: at least eight characters, no NUL, not sixty-four so no hex
  // test. A handler that wrote it through would leave the device holding credentials it cannot
  // associate with -- and credentials existing is what stops the access point coming back up, so
  // the device disappears. ot_provision.c:443-446 calls that the likeliest catastrophe in
  // this project; this path reaches it with nobody having made a typo.
  ot_wifi_t out = pair("sentinel", "must-not-survive");
  const ot_wire_result_t r = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" OT_SECRET_SENTINEL "\"}", nullptr, &out);
  TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK, r.status);
  // And nothing was written into the caller's record on the way to refusing.
  TEST_ASSERT_EQUAL_STRING("sentinel", ssid_of(out).c_str());
}

void test_a_missing_network_name_is_refused(void) {
  ot_wifi_t out = {};
  TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK,
                        ot_wire_parse_provision("{\"wifi_psk\":\"hunter2hunter\"}", nullptr,
                                                      &out)
                            .status);
  TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK,
                        ot_wire_parse_provision("{\"wifi_ssid\":\"\"}", nullptr, &out).status);
}

void test_a_body_that_is_not_json_is_refused_as_a_body_not_as_a_field(void) {
  // The two are different answers to the owner: a bad body is a broken client, a bad field is
  // something they typed. Reporting the first as the second sends them back to a form that was
  // never the problem.
  ot_wifi_t out = {};
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_BODY,
                    ot_wire_parse_provision("not json", nullptr, &out).status);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_BODY,
                    ot_wire_parse_provision("", nullptr, &out).status);
  TEST_ASSERT_EQUAL(OT_WIRE_BAD_BODY,
                    ot_wire_parse_provision(nullptr, nullptr, &out).status);
}

void test_a_key_that_no_radio_could_use_is_refused_before_it_is_stored(void) {
  // Validated on the way IN, which is the whole first rule of ot_config: a stored value
  // that is only discovered to be impossible at the next boot is one the surface that could have
  // fixed it is no longer up to fix.
  ot_wifi_t out = {};
  // Seven characters: below the WPA2 floor. esp_wifi would take it and never associate.
  const ot_wire_result_t short_key = ot_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"1234567\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_REFUSED, short_key.status);
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_PSK, short_key.err);

  // Sixty-four characters that are not hex. wpa_supplicant reads a 64-character key as a raw
  // PSK in hex and silently never associates when it is not.
  std::string not_hex(64, 'z');
  std::string doc = "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" + not_hex + "\"}";
  TEST_ASSERT_EQUAL(OT_WIRE_REFUSED,
                    ot_wire_parse_provision(doc.c_str(), nullptr, &out).status);
}

void test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_whole(void) {
  // Both extremes are LEGAL -- 802.11 caps an SSID at 32 octets, and 64 characters is the
  // 256-bit PSK written as hex. A reader that reserved a byte for a terminator would truncate
  // each by one and the device would never associate, with a symptom nobody can deduce.
  std::string ssid(32, 'S');
  std::string psk(64, 'a');
  std::string doc = "{\"wifi_ssid\":\"" + ssid + "\",\"wifi_psk\":\"" + psk + "\"}";
  ot_wifi_t out = {};
  const ot_wire_result_t r = ot_wire_parse_provision(doc.c_str(), nullptr, &out);
  TEST_ASSERT_EQUAL(OT_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_UINT8(32, out.ssid_len);
  TEST_ASSERT_EQUAL_UINT8(64, out.psk_len);
  TEST_ASSERT_EQUAL_STRING(ssid.c_str(), ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING(psk.c_str(), psk_of(out).c_str());
}

void test_an_over_long_field_is_refused_rather_than_cut_down_to_a_legal_one(void) {
  std::string ssid(33, 'S');
  std::string doc = "{\"wifi_ssid\":\"" + ssid + "\"}";
  ot_wifi_t out = {};
  TEST_ASSERT_NOT_EQUAL(OT_WIRE_OK,
                        ot_wire_parse_provision(doc.c_str(), nullptr, &out).status);
}

void test_no_error_message_from_provisioning_contains_a_submitted_value(void) {
  // The message goes into an HTTP body and into the log ring, and /api/log hands the ring to
  // whoever can reach the device -- which on the setup access point is everybody.
  const char *const bodies[] = {
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"1234567\"}",
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" OT_SECRET_SENTINEL "\"}",
      "{\"wifi_ssid\":\"SECRETNAME\",\"wifi_psk\":123}",
      "rubbish",
  };
  for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
    ot_wifi_t              out = {};
    const ot_wire_result_t r   = ot_wire_parse_provision(bodies[i], nullptr, &out);
    const char                  *msg = ot_wire_strerror(r);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "1234567"), msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "SECRETNAME"), msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "Kitchen"), msg);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_network_and_a_key_are_taken_as_typed);
  RUN_TEST(test_an_empty_key_is_an_open_network_and_not_a_missing_one);
  RUN_TEST(test_an_absent_key_keeps_the_one_already_stored);
  RUN_TEST(test_an_absent_key_with_nothing_stored_is_an_open_network_attempt);
  RUN_TEST(test_the_sentinel_with_a_key_stored_means_keep_it);
  RUN_TEST(test_a_new_ssid_with_the_sentinel_keeps_no_stored_key);
  RUN_TEST(test_a_new_ssid_with_the_key_omitted_keeps_no_stored_key);
  RUN_TEST(test_the_same_ssid_with_the_sentinel_still_keeps_the_key);
  RUN_TEST(test_the_sentinel_is_never_stored_as_a_key);
  RUN_TEST(test_a_missing_network_name_is_refused);
  RUN_TEST(test_a_body_that_is_not_json_is_refused_as_a_body_not_as_a_field);
  RUN_TEST(test_a_key_that_no_radio_could_use_is_refused_before_it_is_stored);
  RUN_TEST(test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_whole);
  RUN_TEST(test_an_over_long_field_is_refused_rather_than_cut_down_to_a_legal_one);
  RUN_TEST(test_no_error_message_from_provisioning_contains_a_submitted_value);
  return UNITY_END();
}
