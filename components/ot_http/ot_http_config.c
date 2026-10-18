// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The routes by which the device is configured: configuration, network scanning, provisioning,
// status, the log. Separated from ot_http.c not by volume but by addressee: these six work on an
// UNCONFIGURED device, behind an open access point, and the access policy for them is decided by
// ot_policy. The registry routes work on a configured one and live in a third file.
//
// DO NOT merge them back into ot_http.c: it was already 1039 lines long against a ceiling of 350,
// and that is exactly why nobody read it end to end.

#include "ot_http_internal.h"

#include <stdio.h>
#include <string.h>

#include "ot_config.h"
#include "ot_log.h"
#include "ot_mqtt_link.h"
#include "ot_net.h"
#include "ot_wire.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"

// Who this device is, for any document that reports it. ONE builder, because the two callers
// below would otherwise be two chances for the identity to be spelled differently -- and it is
// spelled a third time by the MQTT client, from these same two helpers -- which is
// why they, and not a formatted string, are the source.
//
// The two buffers are the CALLER'S: ot_wire_device_t borrows its strings and does not copy
// them, so returning a struct pointing at this function's own stack would be a dangling read.
static ot_wire_device_t this_device(char id_out[OT_CONFIG_DEVICE_ID_LEN + 1],
                                          char mac_out[18])
{
    ot_net_device_id(id_out);
    ot_net_mac_string(mac_out);
    const esp_app_desc_t *app = esp_app_get_description();
    return (ot_wire_device_t){
        .device_id  = id_out,
        .mac        = mac_out,
        .sw_version = app != NULL ? app->version : "unknown",
    };
}

// --- GET /api/log ------------------------------------------------------------------------

esp_err_t ot_http_log_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;
    const size_t len = ot_log_render(ot_http_scratch, sizeof ot_http_scratch);
    if (len == 0) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"log does not fit\"}");
        return ESP_OK;
    }
    return ot_http_send_json(req, len);
}

// --- refusals ------------------------------------------------------------------------------

// read_body() and send_error() LIVED HERE until they were split out. A comment here long
// predicted a third caller, and POST /api/entities/<key> became it: they are now
// ot_http_read_body() and ot_http_send_error() in ot_http_body.c, and the body limit is
// OT_HTTP_BODY_MAX. DO NOT make a local copy here: two limits on the body size are two answers to
// one question, and they will diverge silently.

// ot_http_send_error() plus the field that was rejected. A page that is told only "bad
// request" leaves the owner to guess which of eight boxes it meant.
static esp_err_t send_wire_error(httpd_req_t *req, const char *status,
                                 ot_wire_result_t result)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[320];
    if (result.field != NULL)
        snprintf(body, sizeof body, "{\"error\":\"%s\",\"field\":\"%s\"}",
                 ot_wire_strerror(result), result.field);
    else
        snprintf(body, sizeof body, "{\"error\":\"%s\"}", ot_wire_strerror(result));
    return httpd_resp_sendstr(req, body);
}

esp_err_t ot_http_wifi_scan_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    static ot_net_scan_entry_t entries[OT_NET_SCAN_MAX];
    size_t                           count = 0;
    if (ot_net_scan(entries, OT_NET_SCAN_MAX, &count) != ESP_OK)
        return ot_http_send_error(req, "503 Service Unavailable", "scan unavailable");

    // Rendered by ot_wire, which is where the shape of this array is pinned by tests --
    // including the one that matters here: an SSID is chosen by whoever set up the router next
    // door, so a neighbour can broadcast a name containing JSON at this device.
    const size_t used = ot_wire_render_scan(entries, count, ot_http_scratch, sizeof ot_http_scratch);
    if (used == 0)
        return ot_http_send_error(req, "500 Internal Server Error", "scan does not fit");
    return ot_http_send_json(req, used);
}

esp_err_t ot_http_provision_post(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    char body[OT_HTTP_BODY_MAX];
    if (!ot_http_read_body(req, body, sizeof body))
        return ESP_OK;

    // Decoded inside ot_net, because resolving "keep the key already stored" means reading
    // the stored key -- and a handler that could read it could also log it, into a ring that
    // GET /api/log serves to anybody. What comes back is why, never what.
    ot_wire_result_t  refusal = {0};
    const esp_err_t         accepted = ot_net_provision_body(body, &refusal);

    if (accepted == ESP_ERR_INVALID_ARG)
        return send_wire_error(req,
                               refusal.status == OT_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               refusal);
    if (accepted != ESP_OK)
        return ot_http_send_error(req, "503 Service Unavailable", "busy");

    // ANSWER FIRST, and ot_net_provision_body() has already returned without connecting so
    // that this line can be reached at all. The station moves to the router's channel and the
    // access point follows it, so the phone that sent this is about to lose us -- if the reply
    // had waited for the connection, everything having worked would look like a failure and the
    // owner would press the button again.
    ot_net_status_t st;
    ot_net_status(&st);
    // NULL, and deliberately not this device's broker counters: the radio has not moved to the
    // owner's network yet, so nothing here has been in a position to reach a broker. Zeros would
    // read as a configured broker that has gone silent, which is the opposite answer
    // (ot_wire.h, ot_wire_render_provision).
    // The identity, unlike the counters, IS knowable here: it comes from the MAC and the app
    // descriptor, neither of which waits for a network. A client that has just handed this device
    // its credentials is the client most in need of knowing which device it just configured.
    char                         id_buf[OT_CONFIG_DEVICE_ID_LEN + 1];
    char                         mac_buf[18];
    const ot_wire_device_t device = this_device(id_buf, mac_buf);
    const size_t                 used =
        ot_wire_render_provision(&st, NULL, &device, ot_http_scratch, sizeof ot_http_scratch);
    if (used == 0)
        return ot_http_send_error(req, "500 Internal Server Error", "status does not fit");
    return ot_http_send_json(req, used);
}

esp_err_t ot_http_config_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    // ot_config_public_t, never ot_config_t: the projection is where each stored
    // secret has already become the sentinel or "", so this renderer cannot leak one by
    // forgetting to. `known_good` comes from the provisioning machine under the same lock --
    // the page cannot describe the rollback truthfully without it.
    ot_config_public_t pub;
    bool                     known_good = false;
    ot_net_config_snapshot(&pub, &known_good);

    const size_t used = ot_wire_render_config(&pub, known_good, ot_http_scratch,
                                              sizeof ot_http_scratch);
    if (used == 0)
        return ot_http_send_error(req, "500 Internal Server Error", "config does not fit");
    return ot_http_send_json(req, used);
}

esp_err_t ot_http_config_post(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    char body[OT_HTTP_BODY_MAX];
    if (!ot_http_read_body(req, body, sizeof body))
        return ESP_OK;

    // STATIC, and it has to be: the strings a patch points at have to outlive the parse, and this
    // is the better part of a kilobyte on a task with a fixed stack. It holds the broker password
    // and the owner's new UI password in the clear until the next parse overwrites them, so it is
    // also a buffer that must never reach ESP_LOG*.
    //
    // Single-buffered because esp_http_server serves every socket from ONE task
    // (cfg.max_open_sockets, ot_http.c), so two config posts cannot be in this function at once.
    static ot_wire_patch_storage_t storage;
    ot_config_patch_t              patch = {0};

    const ot_wire_result_t decoded = ot_wire_parse_config(body, &patch, &storage);
    if (decoded.status != OT_WIRE_OK)
        return send_wire_error(req,
                               decoded.status == OT_WIRE_BAD_BODY ? "400 Bad Request"
                                                                       : "422 Unprocessable Entity",
                               decoded);

    // What a value may BE is ot_config_apply()'s answer and not this layer's. Asking the
    // same question in two places is how the two come to disagree -- and the one that would then
    // be wrong is the one without the tests.
    const ot_config_err_t err = ot_net_config_apply(&patch);
    if (err != OT_CONFIG_OK) {
        const ot_wire_result_t rejected = {OT_WIRE_REFUSED, err,
                                                 ot_config_err_name(err)};
        return send_wire_error(req,
                               err == OT_CONFIG_ERR_READ_ONLY ? "409 Conflict"
                                                                    : "422 Unprocessable Entity",
                               rejected);
    }

    // ANSWERED, and nothing follows the answer. A saved broker setting reaches the client within a
    // second WITHOUT a call from here: ot_mqtt_link's task follows ot_net_broker() and restarts its
    // own client. In the firmware this one was carried over from, the line after this one
    // restarted the broker client on THIS task -- the single task esp_http_server serves every
    // socket from -- and esp_mqtt_client_stop() blocked it for as long as a dying socket took to
    // time out. DO NOT add a call into ot_mqtt_link here: the httpd task must never enter esp-mqtt.
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

esp_err_t ot_http_status_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    ot_net_status_t st;
    ot_net_status(&st);

    // Rendered by ot_wire, where the two things this document gets wrong by hand are pinned
    // by tests: the failure is a NAME decided once in ot_provision -- four different answers
    // to the owner is an invariant, and it dies the moment REST and MQTT each invent a spelling --
    // and OT_PROV_NEVER is rendered as null rather than as 4294967295, because "no time
    // left" and "no end" are opposite answers about a device sealed behind a front panel.

    // The broker block, filled in whole by ot_mqtt_link: configured, connected, the
    // counters and the last refusal. Here it is always passed -- this device has an MQTT link, and
    // before it starts every counter is zero, which is the truth. POST /api/provision below still
    // passes NULL, for the reason given there.
    ot_wire_mqtt_t mqtt;
    ot_mqtt_link_status(&mqtt);
    char                         id_buf[OT_CONFIG_DEVICE_ID_LEN + 1];
    char                         mac_buf[18];
    const ot_wire_device_t device = this_device(id_buf, mac_buf);

    const size_t used =
        ot_wire_render_provision(&st, &mqtt, &device, ot_http_scratch, sizeof ot_http_scratch);
    if (used == 0)
        return ot_http_send_error(req, "500 Internal Server Error", "status does not fit");
    return ot_http_send_json(req, used);
}
