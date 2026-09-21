// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// /ws and the ticket it is opened with. A separate file rather than a continuation of ot_http.c:
// the file ceiling is 350 lines, and the route table and the live push are different
// responsibilities. The push itself lives in ot_http_ws_push.c: here are the routes, the ticket
// and the lifecycle.
//
// There is not one privileged endpoint here. The ticket is issued by an
// ORDINARY route under the common access policy (ot_http_policy.c), and the socket hands out
// exactly the same values GET /api/state hands out: it saves polling, it does not open up data
// that is otherwise invisible.
//
// **Why a ticket at all.** The browser does not send an Authorization header on a WebSocket
// handshake -- neither `new WebSocket()` nor the `ws://user:pass@host` form does so reliably. See
// the header of ot_ticket.h.
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ot_http_internal.h"
#include "ot_ticket.h"

static const char *TAG = "ws";

// Once a second. There is no point going faster: the bus polling ring produces a new
// value for one entity no more often than once every few hundred milliseconds. A tick spent for
// nothing no longer costs anything: with an empty socket list ot_ws_push_queue() queues no work
// and the server task does not wake at all.
#define WS_TICK_US 1000000

// A frame from the client that we agree to read before tearing the connection down. The channel is
// ONE-WAY: commands go through REST, which has both an access policy and response codes. But we
// are obliged to read what was sent -- an unread frame leaves the TCP stream misaligned, and the
// next parse would see garbage.
#define WS_MAX_INBOUND 128

// VOLATILE, and that is not decoration: it is written by the server task
// (ot_http_start/ot_http_stop) and read by the timer task. Without it the compiler is entitled to
// keep the read value in a register, and a tick after the server has stopped would call
// httpd_queue_work() on a dangling pointer.
static httpd_handle_t volatile s_server;
static esp_timer_handle_t      s_tick;

// A tick is executing right now. The only way to wait for it: esp_timer_delete() does NOT wait for
// the callback -- it queues a delete event on that same timer task and returns
// (esp_timer.c:266-291).
static volatile bool s_in_tick;

// Belongs to THE SERVER TASK and is touched only by it: issuing a ticket is a route handler,
// checking a ticket is the pre-handshake callback, and both are executed by one thread. That is
// why there is no mutex here, and it is not forgetfulness.
static ot_ticket_table_t s_tickets;

// --- routes -------------------------------------------------------------------------------

esp_err_t ot_http_ws_ticket_post(httpd_req_t *req)
{
    // First of all and without exception: there is one access policy for all the routes. It is
    // exactly here that the ticket becomes an ordinary endpoint rather than a privileged one.
    if (!ot_http_allowed(req))
        return ESP_OK;

    uint8_t bytes[OT_TICKET_LEN / 2];
    // esp_random() is a true source only while the radio is running (esp_random.h). For THIS call
    // the condition holds by construction: the request arrived over the network, that is, Wi-Fi is
    // up. The caveat in ot_config_nvs_kdf.c is about the SALT, for which being unique is enough;
    // here it is about a token, and a weak source would not be a mitigation but a hole.
    // **DO NOT** call this before the network has started.
    esp_fill_random(bytes, sizeof bytes);

    static const char HEX[] = "0123456789abcdef";
    char              value[OT_TICKET_LEN + 1];
    for (size_t i = 0; i < sizeof bytes; i++) {
        value[2 * i]     = HEX[bytes[i] >> 4];
        value[2 * i + 1] = HEX[bytes[i] & 0x0F];
    }
    value[OT_TICKET_LEN] = '\0';

    // The table can refuse here only on an incorrect length, that is, on a defect in the code
    // above. It is checked rather than assumed.
    if (!ot_ticket_issue(&s_tickets, value, ot_http_now_ms()))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ticket not issued");

    char      out[96];
    const int len = snprintf(out, sizeof out, "{\"ticket\":\"%s\",\"ttl_ms\":%u}", value,
                             (unsigned)OT_TICKET_TTL_MS);
    // Truncation here is impossible -- 32 characters of ticket and a number in a 96-byte buffer --
    // but a length taken from snprintf's return value without a check is a way to one day send
    // truncated JSON that looks valid until somebody tries to parse it.
    if (len < 0 || (size_t)len >= sizeof out)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ticket not issued");

    httpd_resp_set_type(req, "application/json");
    // A one-time secret has no place in a cache -- not the browser's and not anybody's along the
    // way.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, out, len);
}

// Called by the server BEFORE the 101 reply and only on a handshake (httpd_uri.c:337-346).
//
// The route handler will not do for this: on seeing an Upgrade the server answers the handshake
// itself and `return ESP_OK` bypasses uri->handler entirely. That is why the ticket check lives in
// the callback -- the only place where the protocol is still HTTP and a 401 makes sense. A refusal
// (anything but ESP_OK) closes the socket.
esp_err_t ot_http_ws_pre_handshake(httpd_req_t *req)
{
    // ROOM FIRST, TICKET AFTERWARDS. The reverse order spends a one-time ticket on a connection
    // that is immediately torn down: the client honestly obtained a ticket, honestly presented it,
    // received not one byte -- and the next attempt requires a new ticket.
    if (!ot_ws_push_room(req->handle)) {
        ESP_LOGW(TAG, "no free socket slot -- handshake refused, ticket kept");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"no free socket\"}");
        return ESP_FAIL;
    }

    // From the RAW query string, not from ot_http_request_path(): that one cuts it off together
    // with the fragment and the authority -- precisely for the sake of routing by path. The ticket
    // lives on the far side of the question mark, and the browser has no other way of passing it
    // on a handshake.
    char query[160];
    char ticket[OT_TICKET_LEN + 2];
    bool ok = false;
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK &&
        // A value longer than the ticket returns ESP_ERR_HTTPD_RESULT_TRUNC rather than ESP_OK,
        // and is therefore rejected together with the truncated remainder.
        httpd_query_key_value(query, "ticket", ticket, sizeof ticket) == ESP_OK)
        ok = ot_ticket_redeem(&s_tickets, ticket, ot_http_now_ms());

    if (!ok) {
        ESP_LOGW(TAG, "handshake without a valid ticket -- socket refused");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"ticket required\"}");
        return ESP_FAIL;
    }

    // The room was checked a line above and there is nobody to take it between the check and the
    // write: both lines are executed by one task. A refusal here is a defect in the code, not an
    // occupied slot.
    if (!ot_ws_push_add(req->handle, httpd_req_to_sockfd(req))) {
        ESP_LOGE(TAG, "socket slot vanished between the check and the write");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// DATA frames from the client, and only those: the handshake does not come here (see above), and
// PING and CLOSE are handled by the server itself (handle_ws_control_frames is not set).
//
// The channel is one-way, but we are obliged to read what was sent: httpd_parse.c:807-816 calls
// the handler for a PONG as well precisely so that the frame's bytes are taken off the socket. Not
// taking them means leaving the TCP stream misaligned.
esp_err_t ot_http_ws_handler(httpd_req_t *req)
{
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof frame);
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK)
        return ESP_FAIL;
    if (frame.len == 0)
        return ESP_OK;
    // Commands go through REST. A frame of that size on this channel is either a client error or
    // not our client; both are cheaper to tear down than to read out.
    if (frame.len > WS_MAX_INBOUND)
        return ESP_FAIL;

    uint8_t buf[WS_MAX_INBOUND + 1];
    frame.payload = buf;
    if (httpd_ws_recv_frame(req, &frame, sizeof buf - 1) != ESP_OK)
        return ESP_FAIL;
    return ESP_OK;
}

// --- lifecycle -------------------------------------------------------------------------------

// The timer task. It does not read the state, does not render, does not touch the socket list --
// it only queues the work, and only when there is somebody to address it to.
static void ws_tick(void *arg)
{
    (void)arg;
    // The order is load-bearing: mark ourselves BEFORE reading the pointer. Then a stop that
    // nulled the pointer either catches the mark and waits, or the mark is not there yet -- and
    // then the tick will read the already nulled pointer.
    s_in_tick = true;
    ot_ws_push_queue(s_server);
    s_in_tick = false;
}

void ot_http_ws_start(httpd_handle_t server)
{
    ot_ws_push_forget_all();
    ot_ticket_reset(&s_tickets);
    s_server = server;

    if (s_tick == NULL) {
        const esp_timer_create_args_t args = {
            .callback = ws_tick,
            .name     = "ws_tick",
            // A TASK, not an ISR: httpd_queue_work() writes to a socket, which must not be done
            // from an interrupt.
            .dispatch_method = ESP_TIMER_TASK,
        };
        // A LOG, not an ESP_ERROR_CHECK. A device without live page updates works and controls the
        // boiler; a device that called abort() at start-up does neither (see the same caveat in
        // ot_http_start).
        const esp_err_t err = esp_timer_create(&args, &s_tick);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create: %s -- the socket will stay silent",
                     esp_err_to_name(err));
            s_tick = NULL;
            return;
        }
    }
    esp_timer_start_periodic(s_tick, WS_TICK_US);
}

void ot_http_ws_stop(void)
{
    // FIRST: a tick that begins after this line will read NULL and queue nothing.
    s_server = NULL;

    // Stop and DELETE: a stopped but still living timer would survive a repeated start of the
    // server and a second esp_timer_create() -- a leak on every stop/start cycle.
    // esp_timer_delete() requires an already stopped timer (ESP_ERR_INVALID_STATE).
    if (s_tick != NULL) {
        esp_timer_stop(s_tick);
        esp_timer_delete(s_tick);
        s_tick = NULL;
    }

    // Neither stop nor delete waits for an executing callback, and it could have read the pointer
    // BEFORE it was nulled and be standing inside httpd_queue_work() right now. The caller then
    // calls httpd_stop() (ot_http.c), and the handle will stop being valid. There is nothing to
    // wait on but the flag, so we wait on the flag.
    while (s_in_tick)
        vTaskDelay(1);

    ot_ws_push_forget_all();
}
