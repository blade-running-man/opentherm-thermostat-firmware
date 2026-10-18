// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The access gate: the ONE door every request passes through before a handler runs.
//
// Split out of ot_http.c at the 350-line ceiling, along the seam that was already there -- the
// route table and the server lifecycle stay in ot_http.c, while the per-request checks (the
// Host-header allowlist, the Content-Type gate, the credential check and the policy verdict) live
// here. The public surface is unchanged: ot_http_allowed() is declared in ot_http_internal.h and
// every route file calls it exactly as before.

#include "ot_http.h"

#include "ot_http_internal.h"

#include "ot_auth.h"
#include "ot_net.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Ignores `stored` on purpose: the record lives in ot_net and is checked there, in
// constant time. This exists so the hash has no reason to travel.
static bool verify_against_device(const char *stored, const char *candidate)
{
    (void)stored;
    return ot_net_check_password(candidate);
}

static ot_http_method_t method_of(const httpd_req_t *req)
{
    switch (req->method) {
    case HTTP_GET:  return OT_HTTP_GET;
    case HTTP_POST: return OT_HTTP_POST;
    default:        return OT_HTTP_OTHER;
    }
}

// DNS-rebinding guard: refuse a request whose Host header is not an address this device
// answers to. Reads the header into a bounded stack buffer -- the same pattern as the Authorization
// and Content-Type reads below -- and hands the DECISION to ot_http_host_ok() in ot_policy, which is
// pure and host-tested. Applied to EVERY method: a rebinding page can point a GET at the device too,
// and checking all of them is simpler than reasoning about which are sinks. The legitimate SPA is
// served from and posts back to the device's own IP, so it always carries a matching Host; the
// captive-portal probes and the setup page are reached by the SoftAP IP, which ot_http_host_ok()
// admits in AP mode. Returns true (ALLOWED) on the fail-open cases -- see the function's contract.
static bool host_ok(httpd_req_t *req)
{
    char       host_buf[64];
    esp_err_t  hr   = httpd_req_get_hdr_value_str(req, "Host", host_buf, sizeof host_buf);
    const char *host = hr == ESP_OK ? host_buf : NULL;
    // A Host that does not fit is longer than any address this device answers to (an IPv4
    // a.b.c.d:port is at most ~21 chars), so ESP-IDF's ESP_ERR_HTTPD_RESULT_TRUNC (header >= the
    // buffer) is a FOREIGN host and must be REFUSED -- not conflated with ESP_ERR_NOT_FOUND, which
    // maps to host==NULL and fail-OPENS. Conflating them lets an attacker who registers a 64+ byte
    // domain get Host truncated -> treated as absent -> fail-open -> the rebinding gate bypassed.
    // Rebinding bypass: truncation is fail-CLOSED. (ot_http_host_ok's own too-long branch
    // stays as defense-in-depth for a long-but-untruncated host a caller might pass.)
    if (hr == ESP_ERR_HTTPD_RESULT_TRUNC)
        return false;
    // The device's own station address, asked fresh -- ot_net_ip_string() writes "" when there is
    // none, which is the fail-open case ot_http_host_ok() handles. ap_mode is "the access point is
    // on the air", which is exactly !provisioned (ot_net.h: the flag is false for as long as an open
    // AP is up, whatever NVS holds), and that also covers an owned device whose router has died.
    char sta_ip[16];
    ot_net_ip_string(sta_ip);
    const bool ap_mode = !ot_net_is_provisioned();
    return ot_http_host_ok(host, sta_ip, ap_mode);
}

// Every handler goes through this. There is no second door: a handler that forgets to call
// it is the bug this arrangement exists to make obvious.
bool ot_http_allowed(httpd_req_t *req)
{
    char path[128];
    ot_http_request_path(req, path, sizeof path);

    // Somebody is talking to us. On the access point this extends the setup window: it closing
    // while the owner is in the next room reading the password off their router is the most
    // annoying failure in the design, and it is not an attack.
    ot_net_note_client();

    // DNS-rebinding guard: a foreign Host is the one thing a rebinding page cannot hide,
    // so it is refused before any handler runs -- 403, the same shape as the policy's forbidden.
    // FIRST, so the check is applied to reads and writes alike.
    if (!host_ok(req)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"forbidden\"}");
        return false;
    }

    // CSRF guard: a state-changing request that carries a body must declare
    // Content-Type: application/json, else 415 before any handler runs. This forces a CORS
    // preflight on a cross-origin caller, which the device denies (no Access-Control-Allow-Origin,
    // ot_http_send_json), so a cross-origin fetch and a bare HTML form POST cannot drive a write.
    // The DECISION is ot_http_content_type_ok() in ot_policy, host-tested; only the header read is
    // here. GET/HEAD skipped (a read is no CSRF sink); a body-less POST too, so ws-ticket is fine.
    if (req->method != HTTP_GET && req->method != HTTP_HEAD && req->content_len > 0) {
        char        ct_buf[128];
        const char *ct =
            httpd_req_get_hdr_value_str(req, "Content-Type", ct_buf, sizeof ct_buf) == ESP_OK
                ? ct_buf
                : NULL;
        if (!ot_http_content_type_ok(ct)) {
            httpd_resp_set_status(req, "415 Unsupported Media Type");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"unsupported media type\"}");
            return false;
        }
    }

    // Generous but bounded: "Basic " plus base64 of a 128-byte password and a name. A header
    // longer than this is not a credential, and reading it into a fixed buffer is the whole
    // reason ot_auth parses rather than trusting.
    char        header_buf[512];
    const char *header =
        httpd_req_get_hdr_value_str(req, "Authorization", header_buf, sizeof header_buf) == ESP_OK
            ? header_buf
            : NULL;

    const bool password_set = ot_net_password_set();
    // The stored record NEVER reaches this layer. ot_auth wants a `stored` string only so
    // it can tell "nothing is set" from "something is"; the comparison itself goes through the
    // verifier below, which hands the candidate to ot_net and gets back a yes or no. A
    // caller that could read the record could also log it, and the log is served by /api/log.
    const ot_auth_outcome_t auth =
        ot_auth_check(header, password_set ? "set" : "", verify_against_device);

    ot_http_ctx_t ctx = {
        // NOT "are there credentials in NVS". While an OPEN access point is on the air the
        // device counts as unclaimed whatever the flash holds, because saving credentials
        // otherwise hands POST /api/config to whoever is still on that access point -- the
        // same passer-by who supplied them.
        .provisioned   = ot_net_is_provisioned(),
        .password_set  = password_set,
        // GRANTED and nothing else. ot_auth.h states the rule outright: "a caller bridging
        // this into its `authenticated` field must map only GRANTED to true", because
        // OT_AUTH_NO_PASSWORD means there was no question to answer -- told that a stranger
        // on the open access point had authenticated, the policy would hand them the device.
        // DO NOT write OT_AUTH_OK here: that is a value of ot_auth_parse_t, the OTHER
        // enum in that header, and it compiles because both are zero. It would keep working until
        // somebody adds a case to either enum.
        .authenticated = auth.result == OT_AUTH_GRANTED,
    };

    // A wrong password costs a fixed wait. A delay and not a lockout: locking out is a denial
    // of service against the owner, who is the person most likely to mistype.
    if (password_set && header != NULL && auth.result != OT_AUTH_GRANTED && auth.delay_ms > 0)
        vTaskDelay(pdMS_TO_TICKS(auth.delay_ms));

    switch (ot_http_check(method_of(req), path, ctx)) {
    case OT_HTTP_ALLOW:
        return true;
    case OT_HTTP_UNAUTHORIZED:
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"opentherm\"");
        httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
        return false;
    case OT_HTTP_FORBIDDEN:
    default:
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"forbidden\"}");
        return false;
    }
}
