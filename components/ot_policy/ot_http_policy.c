// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_http_policy.h"

#include <string.h>
#include <strings.h>

// Exact match, never a prefix. "/api/configuration" must not inherit the bootstrap
// exemption that "/api/config" has, and getting this wrong is the usual way a rule like
// this leaks -- so it is a function with a name rather than a strncmp at each call site.
static bool path_is(const char *path, const char *expected)
{
    return path != NULL && strcmp(path, expected) == 0;
}

// A path UNDER a prefix, with something after it. The two command routes address one entity or one
// operation each, so "under" is what has to be asked -- but a prefix rule is how an exemption
// leaks, and this is written once with three properties rather than as a strncmp at each call
// site:
//
//   * the prefix must END IN A SLASH, so "/api/entities-secret" cannot inherit "/api/entities"'s
//     rule by sharing its first fourteen letters;
//   * something must FOLLOW it, so the bare stem and a trailing slash are not covered -- there is
//     no entity called "" and a rule that covered one would be a rule about a route, not a
//     resource;
//   * nothing is normalised. "/api/entities/../ota" matches, and that is safe rather than
//     overlooked: esp_http_server dispatched it to the entity handler on the same string, so the
//     only thing it can reach is an entity lookup that will not find it.
static bool path_under(const char *path, const char *prefix)
{
    if (path == NULL || prefix == NULL)
        return false;
    const size_t n = strlen(prefix);
    if (n == 0 || prefix[n - 1] != '/')
        return false;
    return strncmp(path, prefix, n) == 0 && path[n] != '\0';
}

// Changing the ventilation, as distinct from changing the device. The difference is what makes the
// exemption below defensible: a fan speed is what anybody who can walk up to the unit can already
// change, and an OTA image is not.
//
// NOT every operation under "/api/ops/", though the path cannot tell which. One that HALTS the
// conversation with the boiler is outside this exemption whatever its name, and that half of the
// decision is ot_http_check_op() at the bottom of this file -- it needs the operation table, which
// a path does not carry.
static bool path_is_a_command(const char *path)
{
    return path_under(path, "/api/entities/") || path_under(path, "/api/ops/");
}

ot_http_decision_t ot_http_check(ot_http_method_t method, const char *path,
                                             ot_http_ctx_t ctx)
{
    if (path == NULL)
        return OT_HTTP_FORBIDDEN;

    if (method == OT_HTTP_GET) {
        // Reading is open until the owner draws a boundary. Ventilation temperatures are
        // not worth locking someone out of their own device over, and on the access point
        // the setup page has to be reachable by definition.
        if (!ctx.password_set)
            return OT_HTTP_ALLOW;
        return ctx.authenticated ? OT_HTTP_ALLOW : OT_HTTP_UNAUTHORIZED;
    }

    if (method != OT_HTTP_POST)
        return OT_HTTP_FORBIDDEN;

    if (!ctx.provisioned) {
        // Radio proximity is not authorisation. The one thing an unprovisioned device must
        // accept is a network to join, or it is a brick; provisioning puts a clock on that
        // window, which is what the ESPHome firmware's design decision W12 was for.
        if (!path_is(path, "/api/provision"))
            return OT_HTTP_FORBIDDEN;

        // But "nobody owns this device yet" and "its router died" are different situations
        // wearing the same clothes. Both show an access point and no connection; only the
        // second one holds somebody's data. Once a password exists the device has an owner,
        // and re-homing it to another network is theirs to authorise -- otherwise a
        // neighbour waits for a power cut and takes the device out of the house.
        if (ctx.password_set)
            return ctx.authenticated ? OT_HTTP_ALLOW : OT_HTTP_UNAUTHORIZED;
        return OT_HTTP_ALLOW;
    }

    if (!ctx.password_set) {
        // Setting the first password is one of two writes that get through, or the device could
        // never be secured: every write needs a password and the password is a write.
        //
        // The other is CONTROLLING THE VENTILATION, and it is here because the password is
        // optional and off by default while a device without one is a supported
        // configuration. With only /api/config exempt, those two facts together meant every
        // command answered 403 on a device out of the box, until the owner set a password nothing
        // told them to set -- which is a feature that does not work rather than a device that is
        // secure. The owner chose this trade deliberately.
        //
        // WHAT IT COSTS, said plainly: anybody on the household LAN can change the ventilation.
        // That is the same thing anybody who can walk up to the unit's own screen can already do.
        //
        // The third is THE TICKET THAT OPENS THE SOCKET, and it is here for the reason that put
        // the commands here rather than for a new one. /ws carries nothing GET /api/state does not
        // already carry -- the same values of the same entities, pushed instead of polled -- and
        // reading is open while no password exists. Closing the ticket under the write rule would
        // mean a device in its default configuration (the password is optional and off; such a
        // device is supported) shows "no connection" on a page whose data it hands over to
        // a plain GET. It is a POST because it SPENDS something -- one slot in a table of four --
        // not because it changes the device.
        //
        // Everything else still fails CLOSED -- an OTA endpoint answering before anyone has
        // claimed the device is unauthenticated code execution on the LAN, and this firmware does
        // not verify image signatures. That is a different question from a fan speed.
        if (path_is(path, "/api/config") || path_is_a_command(path) ||
            path_is(path, "/api/ws-ticket"))
            return OT_HTTP_ALLOW;
        return OT_HTTP_FORBIDDEN;
    }

    return ctx.authenticated ? OT_HTTP_ALLOW : OT_HTTP_UNAUTHORIZED;
}

// The CSRF half. See the contract in ot_http_policy.h for WHY a body-bearing write is
// required to be application/json. The match is case-insensitive on the media type (a client may
// send "Application/JSON") and tolerates a leading/trailing space and a "; charset=..." tail --
// but it is NOT a bare prefix: "application/json-patch+json" is a different media type and must
// not slip through on the first sixteen letters.
bool ot_http_content_type_ok(const char *content_type)
{
    if (content_type == NULL)
        return false;
    // A client may pad the value: "Content-Type:  application/json".
    while (*content_type == ' ' || *content_type == '\t')
        content_type++;
    static const char json[] = "application/json";
    const size_t n = sizeof json - 1;
    if (strncasecmp(content_type, json, n) != 0)
        return false;
    // What follows the media type must END it -- string end, a parameter (';'), or trailing
    // whitespace -- so the prefix "application/json" cannot annex "application/json-patch+json".
    const char c = content_type[n];
    return c == '\0' || c == ';' || c == ' ' || c == '\t';
}

// The DNS-rebinding half. See the contract in ot_http_policy.h for WHY the Host header is
// the one thing a rebinding page cannot forge, and for the exact allowlist and the two deliberate
// fail-open cases.
bool ot_http_host_ok(const char *host, const char *sta_ip, bool ap_mode)
{
    // FAIL-OPEN: a client that sends no Host is not the attack -- a rebinding page sets a real one.
    // Refusing here would only lock out a terse-but-legitimate client.
    if (host == NULL || host[0] == '\0')
        return true;

    // Strip an optional ":port" so "192.168.1.50:8080" compares as the bare address. The port is
    // taken as the LAST ':' followed by digits, which leaves a bracketed IPv6 literal ("[::1]")
    // untouched -- it then matches no IPv4 sta_ip and is refused, and the device is reached by its
    // IPv4 address (ot_net_ip_string is a 16-byte IPv4 buffer), so no legitimate access is lost.
    char        host_no_port[64];
    const char *colon = strrchr(host, ':');
    size_t      len   = strlen(host);
    if (colon != NULL && colon[1] != '\0') {
        bool all_digits = true;
        for (const char *p = colon + 1; *p != '\0'; p++)
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
        if (all_digits)
            len = (size_t)(colon - host);
    }
    if (len >= sizeof host_no_port)
        // Longer than any address this device answers to. A real Host is short; this is not one,
        // and it is not NULL/empty, so it falls to the foreign rule below -- refuse. Defense in
        // depth: the ot_http glue already refuses a Host too long to fit its read buffer
        // (ESP_ERR_HTTPD_RESULT_TRUNC), so this branch fires only for a long-but-untruncated host.
        return false;
    memcpy(host_no_port, host, len);
    host_no_port[len] = '\0';

    // The normal case: the device's own station address, from which the SPA is served and to which
    // it posts back.
    if (sta_ip != NULL && sta_ip[0] != '\0' && strcmp(host_no_port, sta_ip) == 0)
        return true;

    // The ESP-IDF SoftAP default, valid only while the access point is on the air. In the AP+STA
    // window sta_ip is non-empty, so this is the line that admits a client still on the AP.
    if (ap_mode && strcmp(host_no_port, "192.168.4.1") == 0)
        return true;

    // FAIL-OPEN: the device does not yet know its own address, so it cannot tell foreign from own
    // and must not guess "foreign". This is also the state on a pure access point.
    if (sta_ip == NULL || sta_ip[0] == '\0')
        return true;

    // A non-empty Host that is none of the above, on a device that knows its address: rebinding.
    return false;
}

ot_http_decision_t ot_http_check_op(bool halts_bus, bool password_set)
{
    // The route rule already answered for everything else, and answering twice is how two
    // answers start to disagree.
    if (!halts_bus)
        return OT_HTTP_ALLOW;

    // FORBIDDEN and not UNAUTHORIZED: with no password on the device there is no credential
    // that could make this request acceptable, and telling the client "authenticate" would
    // send it round a loop it cannot leave (ot_http_policy.h). Once a password exists,
    // ot_http_check() has already turned an unauthenticated POST away with a 401, so reaching
    // here with password_set means the caller presented it.
    return password_set ? OT_HTTP_ALLOW : OT_HTTP_FORBIDDEN;
}
