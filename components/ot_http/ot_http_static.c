// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The SPA out of flash, and the captive-portal probes in front of it. Split from ot_http.c
// because these two answer with BYTES THAT ARE NOT THE API: a gzipped asset, or the empty 204 a
// phone wants to hear. Nothing here reads the configuration and nothing here writes anything, so
// the policy is the only thing it shares with the rest of the component.
//
// DO NOT merge back into ot_http.c: with these two in it the file was over the 350-line ceiling
// on the day it was split, and the ceiling exists because nobody read the 1039-line version.

#include "ot_http_internal.h"

#include <string.h>

#include "ot_captive.h"
#include "ot_net.h"
#include "esp_http_server.h"
#include "web_assets.h"

static esp_err_t asset_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    char path[128];
    ot_http_request_path(req, path, sizeof path);

    for (const web_asset_t *a = web_assets(); a->path; a++) {
        if (strcmp(path, a->path) != 0)
            continue;
        httpd_resp_set_type(req, a->content_type);
        // Vite gzipped these at build time; nothing is compressed at runtime.
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)a->start, a->len);
    }

    // An unknown /api/ path is a missing endpoint, not a client-side route. Returning the
    // SPA shell with 200 turns "no such endpoint" into "the endpoint returned HTML", which
    // is a great deal harder to diagnose than a 404.
    if (strncmp(path, "/api/", 5) == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"no such endpoint\"}");
        return ESP_OK;
    }

    // Anything else is a client-side route: hand back the shell and let the SPA resolve it.
    // Without this, a reload on /settings is a 404.
    const web_asset_t *index = web_assets();
    httpd_resp_set_type(req, index->content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index->start, index->len);
}

// The paths a phone probes to decide whether a network has internet. Answering the SPA shell
// with 200 to these is what makes an Android decide the network is useless and switch back to
// mobile data -- taking the form submission with it.
esp_err_t ot_http_captive_probe_get(httpd_req_t *req)
{
    char path[128];
    ot_http_request_path(req, path, sizeof path);

    const ot_captive_answer_t *answer = ot_captive_answer(path);
    if (answer == NULL)
        return asset_get(req);

    ot_net_note_client();
    httpd_resp_set_status(req, answer->status);
    httpd_resp_set_type(req, answer->content_type);
    if (answer->cache_control != NULL)
        httpd_resp_set_hdr(req, "Cache-Control", answer->cache_control);
    // body is NULL for the 204s, and httpd_resp_send takes that as "no body" only when the
    // length is zero too -- which ot_captive guarantees and its tests assert.
    return httpd_resp_send(req, answer->body, answer->body_len);
}
