// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The two documents the device HANDS OUT while it is being set up: the list of networks it
// heard (GET /api/wifi/scan) and the state document (GET /api/provision, a.k.a. /api/status).
//
// Both are rendered into a fixed-size buffer, and both contain strings that were not chosen by
// the owner: the SSID of a neighbour's router is an outsider's text inside JSON. Two
// properties common to the whole suite:
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

char g_out[4096];

bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != nullptr;
}

}  // namespace

// --- GET /api/wifi/scan ----------------------------------------------------------------------

void test_the_scan_is_a_bare_array(void) {
  const ot_wire_network_t list[] = {
      {"Kitchen", -52, true},
      {"Guest", -70, false},
  };
  const size_t n = ot_wire_render_scan(list, 2, g_out, sizeof g_out);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Kitchen\",\"rssi\":-52,\"secure\":true},"
      "{\"ssid\":\"Guest\",\"rssi\":-70,\"secure\":false}]",
      g_out);
}

void test_an_empty_scan_is_an_empty_array_and_not_an_error(void) {
  // A sweep that heard nothing is a true answer. The page keeps manual entry available for it.
  TEST_ASSERT_TRUE(ot_wire_render_scan(nullptr, 0, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING("[]", g_out);
}

void test_duplicate_names_are_not_filtered_by_the_device(void) {
  // One sweep returns a record per BSSID, so a mesh answers several times under one name. Which
  // to show is a presentation question and mergeScan() has more information than this does.
  const ot_wire_network_t list[] = {{"Mesh", -40, true}, {"Mesh", -80, true}};
  TEST_ASSERT_TRUE(ot_wire_render_scan(list, 2, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Mesh\",\"rssi\":-40,\"secure\":true},"
      "{\"ssid\":\"Mesh\",\"rssi\":-80,\"secure\":true}]",
      g_out);
}

void test_a_hidden_network_is_reported_with_an_empty_name(void) {
  const ot_wire_network_t list[] = {{"", -60, true}};
  TEST_ASSERT_TRUE(ot_wire_render_scan(list, 1, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"\",\"rssi\":-60,\"secure\":true}]", g_out);
}

void test_a_scan_that_does_not_fit_reports_zero(void) {
  const ot_wire_network_t list[] = {{"Kitchen", -52, true}};
  char                          small[16];
  TEST_ASSERT_EQUAL_size_t(0, ot_wire_render_scan(list, 1, small, sizeof small));
}

void test_an_ssid_with_json_in_it_cannot_break_the_document(void) {
  // An SSID is chosen by whoever set up the router, and a neighbour's router can be named
  // anything at all. It is rendered through the escaper for that reason and no other.
  ot_wire_network_t list[1] = {};
  snprintf(list[0].ssid, sizeof list[0].ssid, "%s", "\"},{\"ssid\":\"evil");
  list[0].rssi   = -50;
  list[0].secure = true;
  TEST_ASSERT_TRUE(ot_wire_render_scan(list, 1, g_out, sizeof g_out) > 0);
  // Exactly one record, whatever the name claims.
  const char *first = strstr(g_out, "\"rssi\"");
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NULL(strstr(first + 1, "\"rssi\""));
}

// --- GET /api/provision ----------------------------------------------------------------------

namespace {
ot_wire_provision_t connected_status(void) {
  ot_wire_provision_t s = {};
  s.state                     = OT_PROV_CONNECTED;
  s.mode                      = OT_PROV_MODE_STATION;
  s.failure                   = OT_PROV_FAIL_NONE;
  s.provisioned               = true;
  s.has_credentials           = true;
  s.has_known_good            = true;
  s.connected                 = true;
  s.window_remaining_ms       = 0;
  s.fallback_in_ms            = OT_PROV_NEVER;
  snprintf(s.ssid, sizeof s.ssid, "%s", "Kitchen");
  snprintf(s.ap_ssid, sizeof s.ap_ssid, "%s", "comfoair-3f2a");
  snprintf(s.ip, sizeof s.ip, "%s", "192.168.1.42");
  return s;
}

ot_wire_mqtt_t broker_stats(void) {
  ot_wire_mqtt_t m = {};
  m.configured           = true;
  m.connected            = true;
  m.published            = 1234;
  m.commands             = 7;
  m.rejected             = 0;
  m.reconnects           = 2;
  m.last_connack         = 0;
  return m;
}

// The `mqtt` member's object, lifted out as a document of its own. Lifted rather than matched by
// substring because what is INSIDE it is then read with the same reader the firmware uses -- and
// ot_json_check() stops at the first nested value (ot_json.cpp:377-381), so the block
// can only be validated as a document once it stands alone. The block holds no object and no array,
// so its first closing brace is its last.
std::string mqtt_block(const char *doc) {
  const char *key = strstr(doc, "\"mqtt\":");
  if (key == nullptr)
    return std::string();
  const char *open = strchr(key, '{');
  const char *close = open != nullptr ? strchr(open, '}') : nullptr;
  if (close == nullptr)
    return std::string();
  return std::string(open, (size_t)(close - open) + 1);
}

// The same document with the block cut out, so the OUTER object can be checked too. Without this
// the outer fields are only ever seen through ot_json_check()'s NESTED answer, which stops
// at the block and says nothing about the bytes after it.
std::string without_mqtt_block(const char *doc) {
  const char *key = strstr(doc, ",\"mqtt\":");
  if (key == nullptr)
    return std::string(doc);
  return std::string(doc, (size_t)(key - doc)) + "}";
}
}  // namespace

void test_the_status_document_says_where_the_device_is(void) {
  const ot_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"state\":\"connected\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"mode\":\"station\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"provisioned\":true"));
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_ssid\":\"Kitchen\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ip\":\"192.168.1.42\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ap_ssid\":\"comfoair-3f2a\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"failure\":\"none\""));
}

void test_the_status_document_names_the_device_it_came_from(void) {
  // The integration keys its config entry on device_id and puts mac in Home Assistant's
  // device connections, which is what merges this device with the MQTT one onto a single
  // card. A status document without them is a device Home Assistant cannot name -- and
  // until this existed, the three values were assembled for the discovery payload and
  // nowhere else, so a client that does not speak MQTT could not read them at all.
  const ot_wire_provision_t s = connected_status();
  const ot_wire_device_t d = {"aabbccddeeff", "aa:bb:cc:dd:ee:ff", "1.4.0"};
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, &d, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"device_id\":\"aabbccddeeff\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"mac\":\"aa:bb:cc:dd:ee:ff\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"sw_version\":\"1.4.0\""));
}

void test_a_status_document_with_no_device_omits_it_rather_than_lying(void) {
  // NULL omits the three keys instead of rendering empty strings, exactly as a NULL mqtt
  // omits its block. An empty device_id would be a client keying an entry on nothing.
  const ot_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE(contains(g_out, "device_id"));
  TEST_ASSERT_FALSE(contains(g_out, "sw_version"));
}

void test_each_of_the_four_failures_has_its_own_word(void) {
  // The invariant, in the API where the owner can see it: "network not found", "wrong password",
  // "refused" and "no address" are four different things to do about it, and the retired
  // firmware threw the reason away and said one thing that meant none of them.
  struct {
    ot_prov_failure_t failure;
    const char             *word;
  } const expected[] = {
      {OT_PROV_FAIL_NONE, "none"},
      {OT_PROV_FAIL_NETWORK_ABSENT, "network-not-found"},
      {OT_PROV_FAIL_WRONG_PASSWORD, "wrong-password"},
      {OT_PROV_FAIL_REFUSED, "refused"},
      {OT_PROV_FAIL_NO_ADDRESS, "no-address"},
  };
  for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
    ot_wire_provision_t s = connected_status();
    s.failure                   = expected[i].failure;
    s.failure_reason            = 15;
    TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
    std::string want = std::string("\"failure\":\"") + expected[i].word + "\"";
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, want.c_str()), g_out);
    // The word is the one ot_provision decided. One spelling, so the REST projection and
    // the MQTT one cannot invent a second.
    TEST_ASSERT_EQUAL_STRING(expected[i].word, ot_prov_failure_name(expected[i].failure));
  }
}

void test_every_state_and_mode_has_a_word_and_none_of_them_is_null(void) {
  // A renderer that printed (null) for a state would put it into the document as a bare word and
  // the page would fail to parse a perfectly healthy device.
  const ot_prov_state_t states[] = {
      OT_PROV_UNCONFIGURED, OT_PROV_CONNECTING,  OT_PROV_CONNECTED,
      OT_PROV_TRIAL,        OT_PROV_RETRYING,    OT_PROV_FALLBACK_AP,
      OT_PROV_WINDOW_CLOSED};
  for (size_t i = 0; i < sizeof states / sizeof states[0]; i++) {
    const char *name = ot_wire_state_name(states[i]);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
  const ot_prov_mode_t modes[] = {OT_PROV_MODE_OFF, OT_PROV_MODE_ACCESS_POINT,
                                        OT_PROV_MODE_STATION, OT_PROV_MODE_AP_STA};
  for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const char *name = ot_wire_mode_name(modes[i]);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
}

void test_every_connack_code_has_its_own_word_and_refused_is_not_bad_password(void) {
  // The two an owner actually hits are 4 and 5, and they are DIFFERENT problems: 4 is a password
  // to retype, 5 is a broker ACL that will not let this client in whatever it types. A single
  // "bad credentials?" for both -- and for 1-3, which are not credentials at all -- is the same
  // lie about the cause that the MQTT_EVENT_ERROR branch exists to stop. MQTT 3.1.1 s3.2.2.3.
  //
  // THE WORDS THEMSELVES ARE THE ASSERTION. An earlier version of this test checked only that the
  // six were non-empty and pairwise distinct, which "a".."f" would have satisfied -- and the whole
  // of finding B4 is about WHICH words the owner reads. These are the spellings ot_wire.h
  // declares, and changing one here is meant to be as deliberate as changing the document.
  const char *const canonical[6] = {
      "no refusal",          // and NOT "accepted": a device that never contacted a broker is here
      "unacceptable protocol version",
      "client identifier rejected",
      "server unavailable",
      "bad username or password",   // 4: a password to retype
      "not authorized",             // 5: an ACL that will refuse whatever is typed
  };
  const char *seen[6];
  for (uint32_t code = 0; code <= 5; code++) {
    seen[code] = ot_wire_connack_name(code);
    TEST_ASSERT_NOT_NULL(seen[code]);
    TEST_ASSERT_TRUE(seen[code][0] != '\0');
    TEST_ASSERT_EQUAL_STRING(canonical[code], seen[code]);
  }
  for (uint32_t a = 0; a <= 5; a++)
    for (uint32_t b = a + 1; b <= 5; b++)
      TEST_ASSERT_FALSE_MESSAGE(strcmp(seen[a], seen[b]) == 0, seen[a]);
  // Named on their own so the two the owner meets cannot be collapsed by a later edit.
  TEST_ASSERT_FALSE(strcmp(ot_wire_connack_name(4), ot_wire_connack_name(5)) == 0);
  TEST_ASSERT_EQUAL_STRING("bad username or password", ot_wire_connack_name(4));
  TEST_ASSERT_EQUAL_STRING("not authorized", ot_wire_connack_name(5));

  // A code no broker should send is still answered, and with ONE spelling rather than a
  // fabricated one -- the same rule ot_wire_state_name follows for an unknown state.
  const char *unknown = ot_wire_connack_name(99);
  TEST_ASSERT_NOT_NULL(unknown);
  TEST_ASSERT_TRUE(unknown[0] != '\0');
  TEST_ASSERT_EQUAL_STRING("unknown refusal code", unknown);
  TEST_ASSERT_EQUAL_STRING(unknown, ot_wire_connack_name(0xFFFFFFFFu));
  for (uint32_t code = 0; code <= 5; code++)
    TEST_ASSERT_FALSE(strcmp(seen[code], unknown) == 0);
}

void test_never_is_rendered_as_null_and_zero_as_zero(void) {
  // Opposite answers. A window with 0 ms left is CLOSED; a window that never closes is the
  // supported configuration for a device sealed behind a front panel. A page that showed 0 for
  // both would count that device down to a deadline it does not have.
  ot_wire_provision_t s = connected_status();
  s.window_open               = true;
  s.window_remaining_ms       = OT_PROV_NEVER;
  s.fallback_in_ms            = OT_PROV_NEVER;
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"setup_window_remaining_ms\":null"));
  TEST_ASSERT_TRUE(contains(g_out, "\"fallback_ap_in_ms\":null"));

  s.window_remaining_ms = 0;
  s.fallback_in_ms      = 1000;
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"setup_window_remaining_ms\":0"));
  TEST_ASSERT_TRUE(contains(g_out, "\"fallback_ap_in_ms\":1000"));
}

void test_no_key_of_any_kind_reaches_the_status_document(void) {
  // There is no field to leak from -- ot_wire_provision_t has no PSK and neither does
  // ot_prov_t, whose static_assert says so. This asserts the absence rather than trusting
  // it, because the way a key gets into a status document is somebody adding a field "to help
  // with debugging".
  ot_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE(contains(g_out, "psk"));
  TEST_ASSERT_FALSE(contains(g_out, "password"));
}

void test_a_status_document_that_does_not_fit_reports_zero(void) {
  const ot_wire_provision_t s = connected_status();
  char                            small[24];
  TEST_ASSERT_EQUAL_size_t(0, ot_wire_render_provision(&s, NULL, NULL, small, sizeof small));
}

void test_the_status_document_carries_the_broker_counters(void) {
  // Finding H5: ot_mqtt_get_stats() existed and had no caller anywhere in the tree, so
  // every one of these numbers was reachable only with a serial cable. They are the whole
  // diagnosis of a broker connection -- "configured but never connected" and "connected and
  // publishing" are different faults with different answers.
  const ot_wire_provision_t s = connected_status();
  const ot_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);
  // A document in its own right, so the reader can be pointed at it. NESTED, not MALFORMED, is
  // what the whole document answers -- the block is the only nested value in it.
  TEST_ASSERT_EQUAL(OT_JSON_DOC_OK, ot_json_check(block.c_str()));
  TEST_ASSERT_EQUAL(OT_JSON_DOC_NESTED, ot_json_check(g_out));
  TEST_ASSERT_EQUAL(OT_JSON_DOC_OK, ot_json_check(without_mqtt_block(g_out).c_str()));

  bool     flag = false;
  uint32_t n    = 0;
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_bool(block.c_str(), "configured", &flag));
  TEST_ASSERT_TRUE(flag);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_bool(block.c_str(), "connected", &flag));
  TEST_ASSERT_TRUE(flag);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "published", &n));
  TEST_ASSERT_EQUAL_UINT32(1234, n);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "commands", &n));
  TEST_ASSERT_EQUAL_UINT32(7, n);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "rejected", &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "reconnects", &n));
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "last_connack", &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);

  // Counters only. The broker's password has no field here to arrive in, and this asserts the
  // absence rather than trusting it -- a diagnostic block is exactly the shape somebody later
  // adds "the host and the user, to help with debugging" to.
  //
  // NOTE why this is a search for `"password"` WITH ITS QUOTES and not the bare word: CONNACK
  // code 4 spells "bad username or password" into last_connack_reason, so the bare word appears
  // legitimately in this document. This test's fixture has last_connack 0, but the next person to
  // parameterise it over the codes would find a bare search failing for the right-looking wrong
  // reason. A key is what would leak; a key is what is searched for.
  TEST_ASSERT_FALSE(contains(g_out, "\"password\""));
  TEST_ASSERT_FALSE(contains(g_out, "mqtt_host"));
}

void test_a_refused_broker_is_visible_without_a_serial_cable(void) {
  // Finding B4, second half. The MQTT_EVENT_ERROR branch records the refusal and the log names
  // it; until this member existed the owner who mistyped a broker password still read
  // "broker unreachable" every ten seconds and had no way to see the word "not authorized".
  for (uint32_t code = 1; code <= 5; code++) {
    const ot_wire_provision_t s = connected_status();
    ot_wire_mqtt_t            m = broker_stats();
    m.connected                       = false;  // a refusal is not a connection
    m.last_connack                    = code;
    TEST_ASSERT_TRUE(ot_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

    const std::string block = mqtt_block(g_out);
    TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);

    uint32_t seen = 0;
    TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_u32(block.c_str(), "last_connack", &seen));
    TEST_ASSERT_EQUAL_UINT32(code, seen);

    // ASSERTED AGAINST THE FUNCTION, not against a copy of the words. ot_wire_connack_name()
    // is what the MQTT log line prints (ot_mqtt_client.c:126), and a literal here would let
    // the document and the log drift apart one edit at a time -- which is the invariant
    // "one spelling of each answer" lost.
    char reason[64];
    TEST_ASSERT_EQUAL(OT_JSON_FOUND,
                      ot_json_string(block.c_str(), "last_connack_reason", reason,
                                           sizeof reason));
    TEST_ASSERT_EQUAL_STRING(ot_wire_connack_name(code), reason);

    bool connected = true;
    TEST_ASSERT_EQUAL(OT_JSON_FOUND, ot_json_bool(block.c_str(), "connected", &connected));
    TEST_ASSERT_FALSE_MESSAGE(connected, block.c_str());
    // And the block still parses, refusal or not.
    TEST_ASSERT_EQUAL(OT_JSON_DOC_OK, ot_json_check(block.c_str()));
  }

  // Code 0 is not a refusal, and it must not read as one: a device that has never contacted a
  // broker is here too.
  const ot_wire_provision_t s = connected_status();
  const ot_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  char reason[64];
  TEST_ASSERT_EQUAL(OT_JSON_FOUND,
                    ot_json_string(mqtt_block(g_out).c_str(), "last_connack_reason", reason,
                                         sizeof reason));
  TEST_ASSERT_EQUAL_STRING(ot_wire_connack_name(0), reason);
}

void test_no_broker_block_is_not_a_broker_reporting_zeros(void) {
  // Opposite answers, and the reason the parameter is a pointer rather than a value. Zeros say
  // "the broker connection was looked at and nothing has happened"; no member at all says nobody
  // was in a position to look. POST /api/provision's reply is the second case -- it is answered
  // before the radio has moved to the owner's network -- and a page shown "published": 0 there
  // would read a configured broker gone silent, which is the fault this member exists to expose.
  const ot_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(ot_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE_MESSAGE(contains(g_out, "\"mqtt\""), g_out);
  TEST_ASSERT_FALSE(contains(g_out, "last_connack"));
  TEST_ASSERT_FALSE(contains(g_out, "reconnects"));
  // Still one flat object, so nothing was left dangling where the member would have gone.
  TEST_ASSERT_EQUAL(OT_JSON_DOC_OK, ot_json_check(g_out));
}

void test_the_worst_case_status_document_still_fits_the_servers_scratch_buffer(void) {
  // ot_http.c:35 sizes ONE static buffer for every JSON document the server renders --
  // SCRATCH_SIZE, 32768 -- and ot_http.c:700-702 answers a renderer that reports 0 with a 500.
  // A member that pushed this document over would take the whole endpoint down, which is a worse
  // failure than the one it was added to fix. Measured rather than reasoned about, the way
  // test_ha.cpp:471 pins the publish buffer.

  ot_wire_provision_t s = connected_status();
  // Every string at its maximum, and non-ASCII: ot_json_escape turns each such character
  // into a six-byte \uXXXX, and an SSID is chosen by whoever set up the router next door.
  for (int i = 0; i < 16; i++)
    memcpy(s.ssid + i * 2, "\xd0\xaf", 2);  // CYRILLIC CAPITAL LETTER YA
  s.ssid[32] = '\0';
  TEST_ASSERT_EQUAL_size_t(32, strlen(s.ssid));
  memset(s.ap_ssid, 'a', sizeof s.ap_ssid - 1);
  s.ap_ssid[sizeof s.ap_ssid - 1] = '\0';
  snprintf(s.ip, sizeof s.ip, "%s", "255.255.255.255");
  // Not OT_PROV_NEVER: that renders as the four bytes "null", so the longest duration is
  // the largest number that is not it.
  s.window_remaining_ms = OT_PROV_NEVER - 1;
  s.fallback_in_ms      = OT_PROV_NEVER - 1;
  s.failure             = OT_PROV_FAIL_NETWORK_ABSENT;
  s.failure_reason      = 255;

  ot_wire_mqtt_t m = {};
  m.configured = m.connected = true;
  m.published = m.commands = m.rejected = m.reconnects = 4294967295u;

  // The identity block at ITS maximum as well. Added here and not merely to the happy-path test
  // because this is the only test that bounds the document, and three members measured nowhere
  // are three members that can quietly consume the headroom -- which is what this test exists to
  // prevent. device_id is twelve hex characters and mac is seventeen, both fixed; sw_version is
  // esp_app_desc_t.version, which is a 32-byte field.
  char version[32];
  memset(version, 'v', sizeof version - 1);
  version[sizeof version - 1] = '\0';
  const ot_wire_device_t d = {"ffffffffffff", "ff:ff:ff:ff:ff:ff", version};

  // Over every refusal spelling rather than a guessed longest one: which of the seven is longest
  // is not this test's business, and a new one added later must not quietly stop being measured.
  size_t largest = 0;
  const uint32_t codes[] = {0, 1, 2, 3, 4, 5, 99};
  for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
    m.last_connack     = codes[i];
    const size_t bytes = ot_wire_render_provision(&s, &m, &d, g_out, sizeof g_out);
    TEST_ASSERT_TRUE_MESSAGE(bytes > 0, ot_wire_connack_name(codes[i]));
    if (bytes > largest)
      largest = bytes;
  }
  // Measured at 669 bytes. TWO buffers bound this document and the SMALLER one is
  // the live bound: ot_http.c's scratch is 32768, but this test renders into g_out, which is
  // 4096 -- so a document that outgrew the server's buffer would have failed the `bytes > 0` check
  // above long before reaching 32768. Asserted against sizeof g_out for that reason: a ceiling the
  // test cannot reach is not a ceiling. The point is not the headroom either way, but that a member
  // added later cannot quietly consume it unnoticed.
  TEST_ASSERT_TRUE_MESSAGE(largest < sizeof g_out,
                           "the status document outgrew this test's render buffer");
  // A floor as well, so a renderer that stopped emitting the block would not pass this by
  // producing a comfortably small document.
  TEST_ASSERT_TRUE_MESSAGE(largest > 500, "the worst case is not being exercised any more");
}


int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_scan_is_a_bare_array);
  RUN_TEST(test_an_empty_scan_is_an_empty_array_and_not_an_error);
  RUN_TEST(test_duplicate_names_are_not_filtered_by_the_device);
  RUN_TEST(test_a_hidden_network_is_reported_with_an_empty_name);
  RUN_TEST(test_a_scan_that_does_not_fit_reports_zero);
  RUN_TEST(test_an_ssid_with_json_in_it_cannot_break_the_document);
  RUN_TEST(test_the_status_document_says_where_the_device_is);
  RUN_TEST(test_the_status_document_names_the_device_it_came_from);
  RUN_TEST(test_a_status_document_with_no_device_omits_it_rather_than_lying);
  RUN_TEST(test_each_of_the_four_failures_has_its_own_word);
  RUN_TEST(test_every_state_and_mode_has_a_word_and_none_of_them_is_null);
  RUN_TEST(test_every_connack_code_has_its_own_word_and_refused_is_not_bad_password);
  RUN_TEST(test_never_is_rendered_as_null_and_zero_as_zero);
  RUN_TEST(test_no_key_of_any_kind_reaches_the_status_document);
  RUN_TEST(test_a_status_document_that_does_not_fit_reports_zero);
  RUN_TEST(test_the_status_document_carries_the_broker_counters);
  RUN_TEST(test_a_refused_broker_is_visible_without_a_serial_cable);
  RUN_TEST(test_no_broker_block_is_not_a_broker_reporting_zeros);
  RUN_TEST(test_the_worst_case_status_document_still_fits_the_servers_scratch_buffer);
  return UNITY_END();
}
