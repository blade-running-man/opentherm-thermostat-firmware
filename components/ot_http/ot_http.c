// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_http.h"

#include <string.h>

#include "ot_http_internal.h"

#include "ot_net.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "http";

static httpd_handle_t s_server;

// One document does not fit a stack frame, and every route file renders into this one buffer
// rather than carrying its own: two static kilobyte buffers in one component is the same RAM
// spent twice on a device with 400 KB of it. Static rather than heap so the cost is visible in
// the map file and cannot fail at the worst moment.
//
// Sized against the documents, not guessed. The largest document rendered is the status
// document, a few hundred bytes -- test_wire pins its worst case against 4096 and asserts the
// headroom. The figure below is deliberately larger: the entity list is
// generated from the registry and it grows with it, and in the firmware this was carried over
// from it came within 23 bytes of a 16 KB buffer before anyone looked. DO NOT shrink it to fit
// the smaller document -- the number that matters is the one nobody has to
// remember to raise.
char ot_http_scratch[OT_HTTP_SCRATCH_SIZE];

// esp_timer_get_time() gives monotonic microseconds since start-up; in milliseconds the counter
// overflows after 49 days, and that does not matter: the ticket TTL, the age of a value and the
// keepalive are all computed as an unsigned DIFFERENCE, which survives the overflow.
uint32_t ot_http_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// req->uri is the whole request target the client sent -- "/app.js?v=2", and in
// absolute-form even "http://host/app.js". esp_http_server routes on the path component
// alone (httpd_uri.c:307-310) but hands the handler the raw string, so every comparison
// here has to strip the query first. Without this the policy's exact matches simply never
// fire on a client that appends a parameter, and /app.js?v=2 fell through to the SPA shell.
const char *ot_http_request_path(httpd_req_t *req, char *out, size_t cap)
{
    const char *uri = req->uri;
    // Absolute-form: skip past the authority so the path starts at its slash.
    if (strncmp(uri, "http://", 7) == 0) {
        const char *slash = strchr(uri + 7, '/');
        uri = slash != NULL ? slash : "/";
    }
    size_t n = 0;
    while (uri[n] && uri[n] != '?' && uri[n] != '#' && n + 1 < cap) {
        out[n] = uri[n];
        n++;
    }
    out[n] = '\0';
    // A trailing slash on anything but the root names the same resource; folding it keeps
    // "/api/config/" from quietly missing the rule written for "/api/config".
    if (n > 1 && out[n - 1] == '/')
        out[n - 1] = '\0';
    return out;
}

// The access gate -- ot_http_allowed() and its per-request checks -- moved to ot_http_gate.c at
// the 350-line ceiling (the Host-header allowlist was added there). The route table and the
// server lifecycle stay here.

// --- handlers ---------------------------------------------------------------------------

esp_err_t ot_http_send_json(httpd_req_t *req, size_t len)
{
    httpd_resp_set_type(req, "application/json");
    // The UI is served from this device, so there is no cross-origin case to support. Not
    // sending the header is the decision: ESPHome sent "*", which let any page in the
    // browser read this device's state.
    return httpd_resp_send(req, ot_http_scratch, len);
}

// Every write, whatever the path. Registering this is what makes the access policy real:
// without it a POST simply found no handler and the server answered 404, so "writing is
// refused until a password exists" was true by accident rather than by rule -- and it would
// have stopped being true the moment the first write handler appeared.
static esp_err_t write_gate(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    // The policy let it through; there is simply nothing behind it yet. Every write that exists
    // -- /api/config, /api/provision, /api/ops/<name>, /api/ws-ticket and /api/entities/<key> --
    // is registered above this gate; /api/ota is not yet implemented. 501 says that honestly instead
    // of pretending the request was applied.
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"not implemented yet\"}");
    return ESP_OK;
}

// --- lifecycle --------------------------------------------------------------------------

esp_err_t ot_http_start(const ot_http_config_t *config)
{
    if (s_server != NULL)
        return ESP_ERR_INVALID_STATE;
    if (config == NULL)
        return ESP_ERR_INVALID_ARG;

    static const httpd_uri_t routes[] = {
        {.uri = "/api/log",        .method = HTTP_GET,  .handler = ot_http_log_get},
        {.uri = "/api/status",     .method = HTTP_GET,  .handler = ot_http_status_get},
        {.uri = "/api/config",     .method = HTTP_GET,  .handler = ot_http_config_get},
        {.uri = "/api/config",     .method = HTTP_POST, .handler = ot_http_config_post},
        {.uri = "/api/wifi/scan",  .method = HTTP_GET,  .handler = ot_http_wifi_scan_get},
        {.uri = "/api/provision",  .method = HTTP_POST, .handler = ot_http_provision_post},
        {.uri = "/api/ot/raw",     .method = HTTP_GET,  .handler = ot_http_ot_raw_get},
        // The executor's live document. ABOVE /* GET, otherwise the SPA fallback intercepts it.
        {.uri = "/api/control",    .method = HTTP_GET,  .handler = ot_http_control_get},
        // Operations. ABOVE write_gate, otherwise the catch-all answers 501 before the request
        // reaches the handler. ONE entry for all the operations: the name is parsed from the tail
        // of the path and looked up in the ot_http_ops.c table, so a new operation needs no new
        // line here -- and cannot forget one. There is deliberately NO bare "/api/ops": there is
        // nothing to run without a name.
        //
        // The Data-ID sweep and the line test moved here; their former /api/ot/scan and
        // /api/ot/linetest are deleted. GET /api/ot/raw a line above stayed -- that is a read.
        {.uri = "/api/ops/*",      .method = HTTP_POST, .handler = ot_http_op_post},
        // The registry for reading. TWO entries for one handler rather than one with a
        // substitution: esp_http_server does not consider "/api/entities" a match for
        // "/api/entities/*" (httpd_uri_match_wildcard compares against the template without the
        // asterisk, that is, against "/api/entities/" with the slash), and without the bare entry
        // the entity list would answer 404. Both ABOVE /* GET, otherwise the SPA fallback
        // intercepts them both.
        {.uri = "/api/state",      .method = HTTP_GET,  .handler = ot_http_state_get},
        {.uri = "/api/entities",   .method = HTTP_GET,  .handler = ot_http_entities_get},
        {.uri = "/api/entities/*", .method = HTTP_GET,  .handler = ot_http_entities_get},
        // The write into an entity. ABOVE write_gate, otherwise the catch-all answers 501 before
        // the request reaches the handler. The template is the same as the GET's a line above,
        // and that is not a conflict: esp_http_server looks for a match on the pair "template +
        // method" (httpd_uri.c:157-166), so GET and POST on "/api/entities/*" are two different
        // table entries. There is deliberately NO bare "/api/entities" for writing: there is
        // nowhere to write without a key.
        {.uri = "/api/entities/*", .method = HTTP_POST, .handler = ot_http_entity_post},
        // The socket ticket. ABOVE write_gate, otherwise the catch-all answers 501 before the
        // request reaches the handler. An ordinary route under the ordinary policy: this is
        // precisely the answer to criterion 7 -- the page has no privileged endpoint.
        {.uri = "/api/ws-ticket",  .method = HTTP_POST, .handler = ot_http_ws_ticket_post},
        // The socket itself. ABOVE /* GET, otherwise the SPA fallback intercepts the handshake.
        //
        // The handler does NOT ask ot_http_allowed(), and that is not an omission: the browser
        // sends no Authorization on a WebSocket handshake, so what closes the socket is the
        // ticket, not the password. The ticket is checked by ws_pre_handshake_cb -- the server
        // answers the handshake ITSELF and it never gets as far as the handler
        // (httpd_uri.c:337-363).
        {.uri              = "/ws",
         .method           = HTTP_GET,
         .handler          = ot_http_ws_handler,
         .is_websocket     = true,
         .ws_pre_handshake_cb = ot_http_ws_pre_handshake},
        // /api/ota is not yet implemented. It must go ABOVE write_gate, otherwise the
        // catch-all answers 501 before the request reaches the handler.
        // Catch-all for writes, so the policy is applied to every one of them rather than
        // to the ones that happen to have a handler.
        {.uri = "/*",              .method = HTTP_POST,   .handler = write_gate},
        {.uri = "/*",              .method = HTTP_PUT,    .handler = write_gate},
        {.uri = "/*",              .method = HTTP_DELETE, .handler = write_gate},
        {.uri = "/*",              .method = HTTP_PATCH,  .handler = write_gate},
        // Last: the wildcard would otherwise shadow the API routes above.
        // Before the SPA fallback: a probe path answered with the shell is what breaks the
        // phone's captive-portal detection. Unknown paths fall through to the assets from here.
        {.uri = "/*",              .method = HTTP_GET,    .handler = ot_http_captive_probe_get},
    };

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    // NOT the default of 8. There are more routes than that, and the first registration past the
    // limit returns
    // ESP_ERR_HTTPD_HANDLERS_FULL (httpd_uri.c:196), which ESP_ERROR_CHECK turns into an
    // abort -- a device that reboots every second or two and never serves anything. Derived
    // from the array so adding a route cannot reintroduce it.
    cfg.max_uri_handlers = sizeof routes / sizeof routes[0];
    cfg.server_port      = config->port;
    // EVICTION IS ON, and that is deliberate. Without it the eighth connection is refused, that
    // is, a page opened from a second device simply does not load until the first one's
    // keep-alive expires. The long-lived /ws socket is protected not by turning eviction off but
    // by the fact that every successful send advances its LRU counter (ot_http_ws_push.c):
    // without that the counter of a one-way session is frozen for ever and it is always the
    // oldest, that is, the first victim.
    cfg.lru_purge_enable = true;
    // SEVEN CLIENT SESSIONS. The server's three service sockets go ON TOP of this number, not
    // inside it -- see OT_HTTP_MAX_CLIENTS in ot_http_internal.h, where this is derived from
    // esp_http_server.h:196 and httpd_main.c:438,500-503. The ceiling above is the LWIP budget
    // (10 = 7 + 3), so the number cannot be raised without raising LWIP_MAX_SOCKETS.
    cfg.max_open_sockets = OT_HTTP_MAX_CLIENTS;
    // NOT the default 4096. POST /api/config is the deepest call in this firmware: a 2 KB request
    // body, the config document and the patch storage, one on top of another. This was measured
    // to come within a few hundred bytes of the default, and a stack overflow on ESP-IDF is
    // a panic and a reboot -- on the one operation the owner performs standing in front of the
    // thermostat, and one that would then repeat. DO NOT lower it because a smaller document asks less of it
    // than the entity list will: the figure came from a measurement and only another measurement should
    // move it.
    cfg.stack_size = 8192;
    // Wildcard matching, so one handler can serve every client-side route.
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    const esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    // LOGGED, never ESP_ERROR_CHECK'd. The count above is derived from the array, so
    // ESP_ERR_HTTPD_HANDLERS_FULL cannot happen -- but that was true of the version that shipped
    // nine routes against a limit of eight, and it aborted on every boot. A device that serves
    // ten routes out of eleven is one the owner can reach to fix; a device that calls
    // abort() in app_main reboots every second or two for ever and cannot be reached at all
    // (decisions, "The firmware did not boot at all").
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) {
        const esp_err_t reg = httpd_register_uri_handler(s_server, &routes[i]);
        if (reg != ESP_OK)
            ESP_LOGE(TAG, "route %s did not register: %s -- it will answer 404", routes[i].uri,
                     esp_err_to_name(reg));
    }

    // After the registration: the push needs the server handle, not merely its existence.
    ot_http_ws_start(s_server);

    // Asked, not remembered -- the same call the policy makes on every request.
    ESP_LOGI(TAG, "listening on :%u (%s)", config->port,
             ot_net_is_provisioned() ? "network" : "access point");
    return ESP_OK;
}

esp_err_t ot_http_stop(void)
{
    if (s_server == NULL)
        return ESP_OK;
    // BEFORE httpd_stop(): the push tick must go quiet before the server handle stops being
    // valid, otherwise httpd_queue_work() gets garbage.
    ot_http_ws_stop();
    const esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}
