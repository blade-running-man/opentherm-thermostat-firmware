// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The request body and the refusal: two helpers used by EVERY writing route.
//
// The seventh file of the component rather than a place inside ot_http.c, and that was decided by
// a line count. The helpers lived in ot_http_config.c until a third caller needed them
// -- POST /api/entities/<key>; they were due to be raised into the common ones. But by that point
// ot_http.c holds 297 lines of access policy, route table and server start-up against a ceiling of
// 350, and forty lines of body reading would have left it four lines of headroom -- less than one
// new route costs together with an explanation of why it stands above write_gate. A separate file
// here is cheaper than cutting up ot_http.c later.
//
// DO NOT make a local copy of either of them in a route file: two limits on the body size are two
// answers to one question, and they will diverge silently.

#include "ot_http_internal.h"

#include <stdio.h>

#include "esp_http_server.h"

bool ot_http_read_body(httpd_req_t *req, char *out, size_t cap)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= cap) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"body too large\"}");
        return false;
    }
    size_t got = 0;
    // A LOOP, not a single call. httpd_req_recv can return less than was asked for, and a single
    // call is the usual way to get a body truncated at a packet boundary and then parse it as
    // impeccable JSON that simply has no tail.
    while (got < (size_t)req->content_len) {
        const int n = httpd_req_recv(req, out + got, (size_t)req->content_len - got);
        if (n <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"truncated body\"}");
            return false;
        }
        got += (size_t)n;
    }
    out[got] = '\0';
    return true;
}

esp_err_t ot_http_send_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[256];
    // `message` is ALWAYS a LITERAL: ot_wire_strerror(), ot_command_strerror() or a constant at
    // the call site. What the client submitted is never formatted into it: this same body goes
    // into the log ring, and GET /api/log hands the ring to whoever can reach the device -- on the
    // access point that is everybody.
    snprintf(body, sizeof body, "{\"error\":\"%s\"}", message);
    return httpd_resp_sendstr(req, body);
}
