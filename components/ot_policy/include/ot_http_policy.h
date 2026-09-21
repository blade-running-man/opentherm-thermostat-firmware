// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Who may do what, and when.
//
// Deliberately a pure function with no HTTP types in it, so the rule can be tested without
// a network stack. The old firmware's equivalent lived in components/ap_guard/ and could
// only be exercised on a device; read that component's header comment before changing
// anything here, because every rule below is one it paid for.
//
// The shape of the answer, in one sentence: reading is open until the owner sets a
// password, writing is refused until they do, and radio proximity to an UNCLAIMED device
// authorises nothing but handing it a network.
//
// Unclaimed, not merely unprovisioned. A device whose router has died looks identical from
// the outside -- an access point and no connection -- but it holds somebody's data, and
// handing it to another network is a decision only its owner may make.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OT_HTTP_GET,
    OT_HTTP_POST,
    // Anything else -- PUT, DELETE, PATCH, an unparsed method. Refused rather than mapped
    // onto the closest known verb.
    OT_HTTP_OTHER,
} ot_http_method_t;

typedef struct {
    // The device has Wi-Fi credentials and is on the owner's network. False means it is
    // serving its own access point, where anyone in radio range is "the client".
    bool provisioned;
    bool password_set;
    bool authenticated;
} ot_http_ctx_t;

typedef enum {
    OT_HTTP_ALLOW,
    // 401: a password exists and this request did not present it.
    OT_HTTP_UNAUTHORIZED,
    // 403: no credential could make this request acceptable in this state. Distinct from
    // 401 on purpose -- telling a client "authenticate" when no authentication exists yet
    // sends it round a loop it cannot leave.
    OT_HTTP_FORBIDDEN,
} ot_http_decision_t;

ot_http_decision_t ot_http_check(ot_http_method_t method, const char *path,
                                             ot_http_ctx_t ctx);

// The SECOND half of the decision on POST /api/ops/<name>, and it is about the OPERATION
// rather than the route.
//
// ot_http_check() judges by path, and "/api/ops/" is exempt on a device with no password by
// the same line that exempts writing a setpoint (the owner's decision). For an
// operation that HALTS the conversation with the boiler that is wrong, and the difference is
// not one of degree: a setpoint is bounded by the registry and will be overridden by the
// control loop's next decision, while the OpenTherm slave reads silence
// longer than five seconds as a short-circuited thermostat and go to HEAT DEMAND -- and
// nothing overrides that, because the loop's frames are exactly what is not being sent. A
// caller looping such an operation holds the boiler hot indefinitely.
//
// So: a password ALWAYS, even where an ordinary write does not need one. Not set -- 403, and
// no header can change that, because there is nothing to authenticate against.
//
// `halts_bus` comes from the operation table (ot_http_ops.c): the flag lives next to the
// operation it describes, and the RULE lives here, where it can be tested without a network
// stack. Nothing else is asked -- once a password exists, ot_http_check() has already demanded
// authentication under the ordinary write rule, so this function has no second question.
ot_http_decision_t ot_http_check_op(bool halts_bus, bool password_set);

// The Content-Type gate on a body-bearing write (the CSRF half). True iff
// `content_type` names JSON: "application/json", optionally with a "; charset=..." parameter
// tail and tolerant of surrounding whitespace. NULL or empty is NOT ok.
//
// WHY it is a security check, not politeness: the device serves its own UI and sends no
// Access-Control-Allow-Origin, so it answers no CORS preflight. Requiring application/json on a
// write with a body forces a preflight on any cross-origin caller -- a bare HTML <form> POST can
// only set the three "simple" content types (none of them JSON), and a cross-origin fetch()
// carrying JSON must preflight first -- so both are turned away before a handler runs. This is
// the CSRF half; the DNS-rebinding Host-allowlist is a separate change. The rule is
// pure and lives here so it is host-tested; ot_http.c only reads the header and, on a false,
// answers 415. The SPA already sends application/json on every body-bearing write, and
// POST /api/ws-ticket carries no body, so nothing legitimate is refused.
bool ot_http_content_type_ok(const char *content_type);

// The Host-header allowlist (the DNS-rebinding half). True iff `host` names an address
// this device is legitimately reachable as; false ONLY for a clearly-foreign, non-empty host --
// which is the rebinding signature.
//
// WHY it is the OTHER half of the CSRF/rebinding defence. The Content-Type gate above stops CROSS-origin writes by
// forcing a preflight the device denies. DNS rebinding defeats that: the attacker's page keeps its
// own origin but re-resolves that domain to the device's LAN IP, so the write is now SAME-origin
// and may freely carry Content-Type: application/json. What it CANNOT change is the Host header --
// the browser still sends `Host: attacker.example`, the name it navigated to. So a request whose
// Host is none of the addresses the device answers to is the rebinding case, and is refused before
// a handler runs. The device serves and the SPA posts back to the device's own address, so
// nothing legitimate carries a foreign Host.
//
// The allowlist, exactly:
//   * `sta_ip` -- the device's current station IP as a string, the normal case: the SPA is served
//     from and posts back to http://<device-ip>/. The `:port` suffix on `host` is stripped first.
//   * "192.168.4.1" -- the ESP-IDF SoftAP default -- but ONLY when `ap_mode`, i.e. the access
//     point is on the air. This matters in the AP+STA window; on a pure AP `sta_ip` is empty and
//     the fail-open below already covers it.
//   There is deliberately NO mDNS/.local name: no mDNS/esp_netif_set_hostname registers one (the
//   SPA's own comment in web/src/pages/settings/wifi.ts records that no code calls the component).
//   A router-published DHCP option-12 hostname (lwIP still sends one) is likewise not in the
//   allowlist and would be refused, so inventing a hostname here would only add an entry no
//   legitimate client reliably sends.
//
// FAIL-OPEN, and toward keeping the owner reachable, in exactly two ambiguous cases -- because a
// lockout of the owner is the worse failure than a rebinding window that other defenses still
// narrow:
//   * `host` NULL or empty -- some minimal clients omit the header; refusing them would lock out a
//     legitimate-but-terse client to stop an attacker who would simply set a real Host anyway.
//   * `sta_ip` NULL or empty -- the device does not yet know its own address (not connected, or
//     mid-DHCP); with nothing to compare against, the check cannot tell foreign from own, so it
//     must not guess "foreign".
bool ot_http_host_ok(const char *host, const char *sta_ip, bool ap_mode);

#ifdef __cplusplus
}
#endif
