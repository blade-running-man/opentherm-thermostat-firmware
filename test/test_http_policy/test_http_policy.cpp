// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Who may do what, and when.
//
// This is the security policy made executable, and it is a pure function on
// purpose: the rule has to be testable without a network stack, because "the device was
// reachable and unprotected for ninety seconds after a router outage" is exactly the class
// of defect that never shows up in a manual test.
//
// The shape is taken from ap_guard in the ESPHome firmware, which existed because the
// platform could not express it. Read the header comment of components/ap_guard/ap_guard.h
// in the old repository before changing any rule here -- it records why each one exists.
#include <unity.h>

#include "ot_http_policy.h"

static ot_http_ctx_t ctx(bool provisioned, bool password_set, bool authenticated) {
  ot_http_ctx_t c{};
  c.provisioned    = provisioned;
  c.password_set   = password_set;
  c.authenticated  = authenticated;
  return c;
}

void setUp(void) {}
void tearDown(void) {}

// --- reading ---------------------------------------------------------------------------------

void test_reading_is_open_when_no_password_is_set(void) {
  // The LAN is the trust boundary until the owner draws a tighter one. Reading ventilation
  // temperatures is not a capability worth locking a user out of their own device for.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_GET, "/api/state", ctx(true, false, false)));
}

void test_reading_requires_the_password_once_one_is_set(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_GET, "/api/state", ctx(true, true, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_GET, "/api/state", ctx(true, true, true)));
}

// --- writing ---------------------------------------------------------------------------------

void test_writing_is_refused_while_no_password_exists(void) {
  // A hole to guard: an OTA endpoint that answers before anyone has
  // claimed the device is unauthenticated code execution on the LAN, and the image is not
  // signed. Failing closed costs the owner one step and costs an attacker everything.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ota", ctx(true, false, false)));

  // /api/entities/fan USED TO BE ASSERTED HERE and is not any more. The rule changed on
  // By the owner's decision, and the reason is worth keeping next to the line that
  // moved: the UI password is optional and off by default, and a device without one is
  // supported, and with only /api/config exempt those two rules meant every ventilation
  // command answered 403 out of the box -- password-setting dead on arrival, waiting for a password
  // nothing asked the owner to set. Changing the ventilation is now exempt; changing the DEVICE
  // is not, which is why /api/ota above is unmoved. See the tests further down for both halves.
}

void test_writing_is_allowed_once_authenticated(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/ota", ctx(true, true, true)));
}

void test_setting_the_first_password_is_the_one_write_that_gets_through(void) {
  // Otherwise the device could never be secured: every write needs a password and the
  // password itself is a write. The exception is exactly one path and only while no
  // password exists -- afterwards it needs the old one like anything else.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/config",
                                        ctx(true, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_POST, "/api/config",
                                        ctx(true, true, false)));
}

// --- the access point ------------------------------------------------------------------------

void test_an_unprovisioned_device_refuses_every_write(void) {
  // Radio proximity is not authorisation. On the first-run AP anyone in range can reach the
  // device, so it is read-only there -- including the config write that is otherwise the
  // bootstrap exception, because that is the path that hands the device to a network.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ota", ctx(false, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/entities/fan",
                                        ctx(false, false, false)));
}

void test_provisioning_itself_is_the_exception_on_the_access_point(void) {
  // Wi-Fi credentials have to be accepted somehow, or the device is a brick. This is the
  // single path that works there, and provisioning puts a clock on the window it lives in.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/provision",
                                        ctx(false, false, false)));
}

void test_reading_still_works_on_the_access_point(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_GET, "/", ctx(false, false, false)));
}

// --- paths ------------------------------------------------------------------------------------

void test_an_unknown_method_is_refused_rather_than_defaulted(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_OTHER, "/api/state", ctx(true, true, true)));
}

void test_a_path_that_only_starts_like_the_exception_is_not_the_exception(void) {
  // "/api/configuration" must not inherit "/api/config"'s bootstrap exemption, and
  // "/api/provisioning-secrets" must not inherit the AP one. Prefix matching is how this
  // kind of rule is usually got wrong.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/configuration",
                                        ctx(true, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/provisioning",
                                        ctx(false, false, false)));
}

void test_a_null_path_is_refused(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_GET, nullptr, ctx(true, false, false)));
}


// --- a device that already belongs to someone -------------------------------------------------
//
// The case with no test at all until now: NOT provisioned, but a password IS set. That is a
// device whose owner has claimed it and whose router has since died, sitting on its own
// access point with the owner's data still in it. The whole "radio proximity authorises
// nothing but handing it a network" argument was written for a device nobody owns yet, and
// it must not survive into this state -- otherwise a neighbour hands the device to their
// network and takes it out of the owner's house.

void test_provisioning_needs_the_password_once_one_exists(void) {
  TEST_ASSERT_EQUAL_MESSAGE(OT_HTTP_UNAUTHORIZED,
                            ot_http_check(OT_HTTP_POST, "/api/provision",
                                                ctx(false, true, false)),
                            "an owned device let a stranger re-home it");
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/provision",
                                        ctx(false, true, true)));
}

void test_an_unclaimed_device_still_accepts_a_network(void) {
  // The existing rule, restated so the fix above cannot quietly take it away: a device
  // nobody has claimed must accept credentials, or it is a brick.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/provision",
                                        ctx(false, false, false)));
}

void test_an_owned_device_on_its_own_access_point_still_refuses_everything_else(void) {
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ota", ctx(false, true, true)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/config", ctx(false, true, true)));
}

void test_reading_an_owned_device_on_its_access_point_needs_the_password(void) {
  // Otherwise the whole neighbourhood reads the house's ventilation, occupancy included,
  // for as long as the router is down.
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_GET, "/api/state", ctx(false, true, false)));
}


// --- controlling the ventilation on a device with no password ----------------------------------

void test_a_claimed_device_with_no_password_still_accepts_ventilation_commands(void) {
  // The UI password is OPTIONAL and off by default, and a device with no password is a
  // SUPPORTED configuration -- the unit sealed behind a front panel of a running ventilation unit.
  // Before this rule those two facts made password-setting dead on arrival: every command answered 403
  // until the owner set a password nothing told them to set.
  //
  // The trade is named rather than hidden: anybody on the household LAN can change the
  // ventilation. That is the same thing anybody who can walk up to the unit can already do, and it
  // is a different question from OTA -- which stays closed, because unauthenticated code execution
  // is not "changing the fan speed".
  const ot_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/entities/fan", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/ops/boost", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/config", claimed));
}

void test_everything_else_still_fails_closed_without_a_password(void) {
  // The exemption is for the two command routes and nothing else. An OTA endpoint answering before
  // anyone has claimed the device is unauthenticated code execution on the LAN, and this firmware
  // does not verify image signatures.
  const ot_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ota", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/provision", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/", claimed));
}

void test_the_command_prefixes_are_matched_as_prefixes_and_not_as_the_word_before_them(void) {
  // A prefix rule is how an exemption leaks, so the bare stems must NOT inherit it, and neither
  // must a path that merely starts with the same letters.
  const ot_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/entities", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/entities/", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ops", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/entities-secret", claimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/opsimulator", claimed));
}

void test_an_unclaimed_device_does_not_accept_commands_even_now(void) {
  // While an OPEN access point is on the air, anybody in radio range is "the client". The one
  // thing such a device accepts is a network to join. Handing the ventilation of a house to
  // whoever is standing outside it is not a trade anybody made.
  const ot_http_ctx_t unclaimed = {.provisioned = false, .password_set = false,
                                         .authenticated = false};
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/entities/fan", unclaimed));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ops/boost", unclaimed));
}

void test_once_a_password_exists_commands_need_it_like_everything_else(void) {
  // The exemption is for a device with NO password, not for the command routes. An owner who has
  // drawn a boundary gets it drawn around the ventilation too.
  const ot_http_ctx_t locked = {.provisioned = true, .password_set = true,
                                      .authenticated = false};
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_POST, "/api/entities/fan", locked));
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_POST, "/api/ops/boost", locked));

  const ot_http_ctx_t open = {.provisioned = true, .password_set = true,
                                    .authenticated = true};
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/ops/boost", open));
}

// --- the ticket the socket is opened with ------------------------------------------------------
//
// POST /api/ws-ticket is an ORDINARY handle under the general access policy. That is precisely
// the answer to acceptance criterion 7: the web interface has no privileged handle, and the
// socket authorisation is not an exception to the rules but lives by them.
//
// The handle follows the READ rule, not the write rule, and that is a decision, not an
// oversight. The ticket opens /ws, and /ws carries NOTHING beyond GET /api/state -- the same
// values of the same entities, only without polling. Closing the issuing behind the write rule
// would mean that on a device without a password
// (the password is optional and off by default; such a device is supported)
// the page would never receive live values, although it reads the same values freely with a
// request. Exactly this failure already happened with the command routes and was fixed.
//
// It is a POST because it SPENDS a resource -- a slot in the ticket table -- not because it
// changes anything in the device.

void test_the_socket_ticket_needs_the_password_once_one_exists(void) {
  // The owner drew a boundary -- it runs around the socket too. Otherwise the ticket would be
  // a way around the password: the socket hands out the same as the password-closed
  // GET /api/state.
  TEST_ASSERT_EQUAL_MESSAGE(OT_HTTP_UNAUTHORIZED,
                            ot_http_check(OT_HTTP_POST, "/api/ws-ticket",
                                          ctx(true, true, false)),
                            "the ticket was issued without a password on a device with one");
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket", ctx(true, true, true)));
}

void test_the_socket_ticket_is_issued_on_a_claimed_device_with_no_password(void) {
  // There is no password -- reading is open, and the socket is reading. A refusal here would
  // mean a page that shows "no connection" on a device the connection to which exists.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket", ctx(true, false, false)));
}

void test_the_socket_ticket_is_refused_on_the_access_point(void) {
  // While an OPEN access point is on the air, the client is anyone within range. The only
  // thing such a device accepts is a network to join. There are no live values on the setup
  // page and none are needed: it shows a list of networks, not the boiler.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket", ctx(false, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket", ctx(false, true, true)));
}

void test_a_path_that_only_starts_like_the_ticket_route_is_not_the_ticket_route(void) {
  // A whole match, never a prefix: otherwise "/api/ws-ticket-factory" inherits the relaxation
  // written for one single path.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket-factory",
                                  ctx(true, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/api/ws-ticket/renew",
                                  ctx(true, false, false)));
}

void test_the_policy_grants_the_socket_itself_nothing(void) {
  // "/ws" itself is described by NO rule in the policy, and that too is a decision. The browser
  // does not send Authorization on the WebSocket handshake, so the socket handler does not ask
  // the policy -- what closes it is the ticket issued by the handle above. What is pinned here
  // is the converse: "/ws" has no relaxations, and adding them here means opening the socket to
  // anyone who reached the device, even though the ticket has already done that work.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW, ot_http_check(OT_HTTP_GET, "/ws", ctx(true, false, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_UNAUTHORIZED,
                    ot_http_check(OT_HTTP_GET, "/ws", ctx(true, true, false)));
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN,
                    ot_http_check(OT_HTTP_POST, "/ws", ctx(true, false, false)));
}

// --- the operation that halts the conversation with the boiler ----------------------------------
//
// The second half of the decision on POST /api/ops/<name>. The first one is the route half, above:
// /api/ops/ is freed on a device without a password by the same line as a setpoint write. The
// a review showed that for the line test this is wrong, and the difference is not
// one of degree.

void test_an_operation_that_halts_the_bus_needs_a_password_even_where_writes_do_not(void) {
  // Silence from the master longer than five seconds is interpreted by the slave as a
  // shorted thermostat, and it goes into a DEMAND FOR HEAT. The line test halts the master, so
  // the loop
  //   while :; do curl -XPOST .../api/ops/linetest -d '{"duration_ms":30000}'; sleep 29; done
  // keeps the boiler hot indefinitely, and the executor is powerless: its frames do not go
  // out. A written setpoint cannot do that -- it is limited by the registry bounds and will be
  // overridden by the loop's next decision. Hence the password ALWAYS, and without a password a
  // 403 that no header will change.
  TEST_ASSERT_EQUAL(OT_HTTP_FORBIDDEN, ot_http_check_op(true, false));
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW, ot_http_check_op(true, true));
}

void test_an_operation_that_does_not_halt_the_bus_is_left_to_the_route_rule(void) {
  // The Data-ID sweep is NOT marked with the flag: it occupies a content slot, the mandatory
  // ID 0 keeps going out on every second step, the master does not fall silent. Marking it would
  // mean closing behind a password the very handle people reach into the device for the first
  // time.
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW, ot_http_check_op(false, false));
  TEST_ASSERT_EQUAL(OT_HTTP_ALLOW, ot_http_check_op(false, true));
}

// --- the Content-Type gate on a body-bearing write (CSRF half) -------------------------
//
// A state-changing request that carries a body must declare Content-Type: application/json.
// Requiring it forces a CORS preflight on any cross-origin caller, and this device answers no
// preflight (it sends no Access-Control-Allow-Origin), so a cross-origin fetch and a bare HTML
// form POST -- whose only content types are the three "simple" ones, none of them JSON -- are
// both refused before they reach a handler. The rule is pure so it can be tested here without a
// network stack; the header read is the only glue, and it lives in ot_http.c.

void test_content_type_json_is_accepted(void) {
  TEST_ASSERT_TRUE(ot_http_content_type_ok("application/json"));
}

void test_content_type_json_with_a_charset_tail_is_accepted(void) {
  // The media type may carry a parameter: this is exactly what fetch() sends by default.
  TEST_ASSERT_TRUE(ot_http_content_type_ok("application/json; charset=utf-8"));
}

void test_content_type_text_plain_is_not_json(void) {
  // "text/plain" is one of the simple content types a cross-origin form post can set with no
  // preflight, so it is precisely what this gate must refuse.
  TEST_ASSERT_FALSE(ot_http_content_type_ok("text/plain"));
}

void test_content_type_null_or_empty_is_not_json(void) {
  // A body with no declared type is the form-post shape the gate blocks: NOT ok.
  TEST_ASSERT_FALSE(ot_http_content_type_ok(nullptr));
  TEST_ASSERT_FALSE(ot_http_content_type_ok(""));
}

void test_content_type_tolerates_surrounding_whitespace(void) {
  // A client may pad the value; a leading or trailing space must not turn a JSON write away.
  TEST_ASSERT_TRUE(ot_http_content_type_ok(" application/json"));
  TEST_ASSERT_TRUE(ot_http_content_type_ok("application/json "));
}

void test_content_type_does_not_match_on_a_mere_prefix(void) {
  // "application/json-patch+json" starts like the exception but is a different media type; a
  // prefix match is how a rule like this leaks.
  TEST_ASSERT_FALSE(ot_http_content_type_ok("application/json-patch+json"));
}

// --- the Host-header allowlist (DNS-rebinding half) -------------------------------------
//
// The Content-Type gate above stops cross-origin writes; DNS rebinding gets around it by making
// the request same-origin (the attacker's domain re-resolves to the LAN IP). What the browser
// cannot forge is the Host header -- it still carries the name the page was loaded from. So a Host
// that is none of the addresses this device answers to is the rebinding case. The rule is pure and
// host-tested here; ot_http.c only reads the header and answers 403 on a false. It fails OPEN in two
// ambiguous cases, deliberately, because locking the owner out is the worse failure.

void test_host_matching_the_station_ip_is_accepted(void) {
  // The normal case: the SPA is served from and posts back to http://<device-ip>/.
  TEST_ASSERT_TRUE(ot_http_host_ok("192.168.1.50", "192.168.1.50", false));
}

void test_host_with_a_port_suffix_is_accepted(void) {
  // The Host header carries a :port when the client used a non-default one; it is stripped before
  // the address is compared, so "192.168.1.50:8080" is still the device's own address.
  TEST_ASSERT_TRUE(ot_http_host_ok("192.168.1.50:8080", "192.168.1.50", false));
}

void test_the_softap_ip_is_accepted_only_in_ap_mode(void) {
  // 192.168.4.1 is the ESP-IDF SoftAP default. It is a valid Host while the access point is on the
  // air, and not otherwise -- a rebinding page cannot claim it once the device is on the LAN.
  TEST_ASSERT_TRUE(ot_http_host_ok("192.168.4.1", "192.168.1.50", true));
  TEST_ASSERT_FALSE(ot_http_host_ok("192.168.4.1", "192.168.1.50", false));
}

void test_a_foreign_host_is_rejected(void) {
  // The rebinding signature: the device knows its own address, and the Host is neither it nor the
  // SoftAP IP. This is the one case the whole check exists to refuse.
  TEST_ASSERT_FALSE(ot_http_host_ok("attacker.example", "192.168.1.50", false));
  TEST_ASSERT_FALSE(ot_http_host_ok("attacker.example:80", "192.168.1.50", false));
  TEST_ASSERT_FALSE(ot_http_host_ok("192.168.1.51", "192.168.1.50", false));
}

void test_a_null_or_empty_host_is_allowed_fail_open(void) {
  // Some minimal clients omit the header. Refusing them would lock out a legitimate-but-terse
  // client to stop an attacker who would simply set a real Host -- so this fails OPEN.
  TEST_ASSERT_TRUE(ot_http_host_ok(nullptr, "192.168.1.50", false));
  TEST_ASSERT_TRUE(ot_http_host_ok("", "192.168.1.50", false));
}

void test_an_unknown_station_ip_allows_anything_fail_open(void) {
  // The device does not yet know its own address (not connected, or mid-DHCP). With nothing to
  // compare against, the check cannot tell foreign from own and must not guess "foreign" -- it
  // fails OPEN, which is also the state on a pure access point.
  TEST_ASSERT_TRUE(ot_http_host_ok("anything.example", nullptr, false));
  TEST_ASSERT_TRUE(ot_http_host_ok("anything.example", "", false));
  // Even a plainly foreign host is allowed while the address is unknown: better a brief window than
  // a device the owner cannot reach the moment before it has an IP.
  TEST_ASSERT_TRUE(ot_http_host_ok("attacker.example", "", true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reading_is_open_when_no_password_is_set);
  RUN_TEST(test_reading_requires_the_password_once_one_is_set);
  RUN_TEST(test_writing_is_refused_while_no_password_exists);
  RUN_TEST(test_writing_is_allowed_once_authenticated);
  RUN_TEST(test_setting_the_first_password_is_the_one_write_that_gets_through);
  RUN_TEST(test_an_unprovisioned_device_refuses_every_write);
  RUN_TEST(test_provisioning_itself_is_the_exception_on_the_access_point);
  RUN_TEST(test_reading_still_works_on_the_access_point);
  RUN_TEST(test_an_unknown_method_is_refused_rather_than_defaulted);
  RUN_TEST(test_a_path_that_only_starts_like_the_exception_is_not_the_exception);
  RUN_TEST(test_a_null_path_is_refused);
  RUN_TEST(test_provisioning_needs_the_password_once_one_exists);
  RUN_TEST(test_an_unclaimed_device_still_accepts_a_network);
  RUN_TEST(test_an_owned_device_on_its_own_access_point_still_refuses_everything_else);
  RUN_TEST(test_reading_an_owned_device_on_its_access_point_needs_the_password);
  RUN_TEST(test_a_claimed_device_with_no_password_still_accepts_ventilation_commands);
  RUN_TEST(test_everything_else_still_fails_closed_without_a_password);
  RUN_TEST(test_the_command_prefixes_are_matched_as_prefixes_and_not_as_the_word_before_them);
  RUN_TEST(test_an_unclaimed_device_does_not_accept_commands_even_now);
  RUN_TEST(test_once_a_password_exists_commands_need_it_like_everything_else);
  RUN_TEST(test_the_socket_ticket_needs_the_password_once_one_exists);
  RUN_TEST(test_the_socket_ticket_is_issued_on_a_claimed_device_with_no_password);
  RUN_TEST(test_the_socket_ticket_is_refused_on_the_access_point);
  RUN_TEST(test_a_path_that_only_starts_like_the_ticket_route_is_not_the_ticket_route);
  RUN_TEST(test_the_policy_grants_the_socket_itself_nothing);
  RUN_TEST(test_an_operation_that_halts_the_bus_needs_a_password_even_where_writes_do_not);
  RUN_TEST(test_an_operation_that_does_not_halt_the_bus_is_left_to_the_route_rule);
  RUN_TEST(test_content_type_json_is_accepted);
  RUN_TEST(test_content_type_json_with_a_charset_tail_is_accepted);
  RUN_TEST(test_content_type_text_plain_is_not_json);
  RUN_TEST(test_content_type_null_or_empty_is_not_json);
  RUN_TEST(test_content_type_tolerates_surrounding_whitespace);
  RUN_TEST(test_content_type_does_not_match_on_a_mere_prefix);
  RUN_TEST(test_host_matching_the_station_ip_is_accepted);
  RUN_TEST(test_host_with_a_port_suffix_is_accepted);
  RUN_TEST(test_the_softap_ip_is_accepted_only_in_ap_mode);
  RUN_TEST(test_a_foreign_host_is_rejected);
  RUN_TEST(test_a_null_or_empty_host_is_allowed_fail_open);
  RUN_TEST(test_an_unknown_station_ip_allows_anything_fail_open);
  return UNITY_END();
}
