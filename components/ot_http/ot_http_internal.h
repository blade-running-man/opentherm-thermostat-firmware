// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The private seam between the ot_http files. NOT installed into include/: no component
// from outside has the right to register a route -- the web interface has no privileged
// endpoint (acceptance criterion 7), and neither should a neighbour in the tree.
//
// Eleven files behind this seam: ot_http.c -- the route table and the start-up; ot_http_gate.c --
// the one access gate every request passes (the Host-header allowlist, the Content-Type gate, the
// credential check and the policy verdict), split off ot_http.c at the ceiling for the security batch;
// ot_http_body.c -- reading the request body and the shape of a refusal, common to all writing
// routes; ot_http_config.c -- the six configuration routes; ot_http_static.c -- the SPA and the
// captive-portal probes; ot_http_boiler.c -- the raw bus observation; ot_http_registry.c -- the
// entity registry, the state model for reading and the write into an entity; ot_http_ops.c --
// the operation table behind POST /api/ops/<name>; ot_http_ws.c -- the /ws and ticket routes;
// ot_http_ws_push.c -- the socket list and what goes out into it; ot_http_control.c -- the
// executor's live document and the HTTP answer to each of its verdicts.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "ot_config.h"
#include "ot_thermostat.h"

// --- how many clients the server holds --------------------------------------------------------

// SEVEN CLIENT SESSIONS, not "seven minus three service ones".
//
// esp_http_server.h:196 calls max_open_sockets the number of clients and says that the three
// service sockets go ON TOP of it; httpd_main.c:438 allocates exactly max_open_sockets sock_db
// entries, and httpd_main.c:500-503 checks that max_open_sockets + 3 fits into the LWIP budget
// (10). That is, seven means seven browser connections, and the LWIP ceiling stands exactly on
// them.
//
// The number lives here rather than as two literals: the /ws socket list must be NO SHORTER than
// the server's queue. A shorter list means the server accepted the handshake while the push
// refused -- a disconnect after the one-time ticket has already been spent.
#define OT_HTTP_MAX_CLIENTS 7

// Milliseconds since start-up. One clock for all the files of the component: the ticket, the
// "changed" marks and the keepalive must measure time by the same clock, otherwise the TTL and
// the age of a value diverge.
uint32_t ot_http_now_ms(void);

// --- the shared response buffer ------------------------------------------------------------

// One buffer for every route file. See the note at its definition in ot_http.c for the size;
// what matters here is that it is NOT re-entrant. esp_http_server serves every socket from ONE
// task (max_open_sockets in ot_http_start), so two handlers can never be rendering into it at
// once -- and that fact is what makes a single 32 KB buffer legitimate instead of reckless.
// DO NOT hand a pointer into it to anything that outlives the response.
#define OT_HTTP_SCRATCH_SIZE 32768
extern char ot_http_scratch[OT_HTTP_SCRATCH_SIZE];

// Sends `len` bytes of ot_http_scratch as application/json. No CORS header, deliberately: the UI
// is served from this device, so there is no cross-origin case to support.
esp_err_t ot_http_send_json(httpd_req_t *req, size_t len);

// --- ot_http_body.c ---------------------------------------------------------------------------

// Bounded because the body is read into a fixed buffer from an open network. A configuration
// document is a few hundred bytes; anything above this is not one. Shared rather than private
// the moment a second file reads a body -- a second spelling of this limit is a second answer
// to "how large may a request be", and the two would drift.
#define OT_HTTP_BODY_MAX 2048

// Reads the whole body into `out`, or answers (413 / 400) and returns false -- the caller then
// returns ESP_OK, exactly as with ot_http_allowed(). Loops, because httpd_req_recv can return
// short and a single call is the usual way a body arrives truncated and is then parsed as valid
// JSON that happens to be missing its tail.
bool ot_http_read_body(httpd_req_t *req, char *out, size_t cap);

// One refusal shape for every route: {"error":"<message>"}.
//
// `message` MUST be a literal -- ot_wire_strerror(), ot_command_strerror() or a constant at the
// call site. Nothing the client submitted is ever formatted into it: this body also goes into the
// log ring, and GET /api/log hands that ring to whoever can reach the device -- which on the
// setup access point is everybody.
esp_err_t ot_http_send_error(httpd_req_t *req, const char *status, const char *message);

// --- the access policy ------------------------------------------------------------------------

// EVERY handler calls this FIRST and returns ESP_OK when it answers false -- it has already
// written the 401 or the 403. A handler that forgets is the bug this arrangement exists to make
// obvious: there is no second door, and no route file may decide access for itself.
bool ot_http_allowed(httpd_req_t *req);

// The path component of the request target, with the query, the fragment, an absolute-form
// authority and a trailing slash removed. `out` is the caller's; the return value is `out`.
// Needed by anything that matches on the path, because esp_http_server hands the handler the RAW
// target (httpd_uri.c:307-310) -- "/api/config?x=1" does not compare equal to "/api/config".
const char *ot_http_request_path(httpd_req_t *req, char *out, size_t cap);

// --- ot_http_config.c ---------------------------------------------------------------------

esp_err_t ot_http_config_get(httpd_req_t *req);
esp_err_t ot_http_config_post(httpd_req_t *req);
esp_err_t ot_http_wifi_scan_get(httpd_req_t *req);
esp_err_t ot_http_provision_post(httpd_req_t *req);
esp_err_t ot_http_status_get(httpd_req_t *req);
esp_err_t ot_http_log_get(httpd_req_t *req);

// --- ot_http_boiler.c -------------------------------------------------------------------

// The raw observation of the OpenTherm bus: what the boiler answered for each Data-ID.
// Diagnostics, not the state model: there are no names and no units here and there cannot be --
// the single list of entities is produced by the generator.
esp_err_t ot_http_ot_raw_get(httpd_req_t *req);

// --- ot_http_ops.c ---------------------------------------------------------------------------

// One operation at "/api/ops/<name>", the body being a flat object of numeric parameters or
// nothing: an absent body means "run with the defaults", and that is a legitimate call.
//
// The Data-ID sweep and the line test live HERE, not in ot_http_boiler.c: they are operations,
// not reads, and the separate endpoints /api/ot/scan and /api/ot/linetest were a special case
// with no justification.
//
// Refusal codes: 404 -- there is no such operation (a literal, the submitted name does not get
// into the body); 400 -- the body did not parse; 422 -- the parameters parsed but are invalid
// (a sweep range outside 0..127, a non-positive duration, boost minutes that are not a whole
// number 1..480, a boost setpoint outside flow_min_dc..flow_max_dc); 409 -- the request is correct
// but the device cannot do it right now: a line test already running, a boost in HA mode or with
// the heating season off; 503 -- a boost on a device whose thermostat task failed to start at
// boot, where nothing would ever end it. The boost's answers are ot_http_control_refusal()'s, the
// same the entity route gives. Success is 202: the operation was queued, not performed.
esp_err_t ot_http_op_post(httpd_req_t *req);

// --- ot_http_registry.c -------------------------------------------------------------------

// The values of every registry entity: value, availability, age_ms. The ot_api projection, the
// same one for REST, MQTT and the web interface -- there is no privileged endpoint.
esp_err_t ot_http_state_get(httpd_req_t *req);

// The whole registry metadata at "/api/entities" and one entity together with its value at
// "/api/entities/<key>". ONE handler for TWO routes: esp_http_server does not consider
// "/api/entities" a match for "/api/entities/*", so both are registered separately and the tail
// of the path is parsed here. An unknown key is a 404.
esp_err_t ot_http_entities_get(httpd_req_t *req);

// A write into one entity at "/api/entities/<key>", the body `{"value": …}`.
//
// Every refusal is ot_command_check()'s, in the one order ot_command.h states, and the final
// answer for an executor command is ot_control_apply()'s. The codes do NOT collapse
// into a single 400, because they tell the client different things: 404 -- there is no such key;
// 405 -- the entity is read-only or its codec describes half a word, and in both cases it can
// NEVER be written to; 409 -- the boiler does not support the Data-ID (never for ch_setpoint), or
// the entity is owned by the other mode, early or finally; 422 -- the value is out of bounds;
// 503 -- no executor task; 500 -- the store refused a command the executor accepted (409 for a
// store a newer firmware wrote). The executor's answers are ot_http_control_refusal()'s, the same
// the boost gets.
//
// Success is 202, not 200: {"queued":{…}} for a frame -- the bus queue is sized for ONE write and
// the next evicts the unexecuted one (ot_bus_sched.h), so at the reply nobody knows the boiler
// took it -- and {"applied":{"command":N}} for an executor command, stored and carried out on the
// executor's next step, with no value echoed: the executor quantises before it persists.
esp_err_t ot_http_entity_post(httpd_req_t *req);

// --- ot_http_ws.c ---------------------------------------------------------------------------

// Issues a one-time ticket: {"ticket":"…","ttl_ms":30000}. An ORDINARY route under the common
// access policy -- the web interface has no privileged endpoint.
esp_err_t ot_http_ws_ticket_post(httpd_req_t *req);

// The ticket check BEFORE the 101 reply. Registered as .ws_pre_handshake_cb rather than called
// from the handler: on seeing an Upgrade the server answers the handshake itself and bypasses
// uri->handler entirely (httpd_uri.c:337-363). A refusal is a 401 and a closed socket.
esp_err_t ot_http_ws_pre_handshake(httpd_req_t *req);

// Data frames from the client. The channel is one-way: what is read is discarded, but it IS read
// -- an unread frame leaves the TCP stream misaligned.
esp_err_t ot_http_ws_handler(httpd_req_t *req);

// Remembers the server and starts the one-second push tick. Called AFTER httpd_start(): the
// handle is needed by both httpd_queue_work() and httpd_ws_send_frame_async().
void ot_http_ws_start(httpd_handle_t server);

// Called BEFORE httpd_stop() and waits out a tick that may have begun: after it returns, no task
// will call httpd_queue_work() on this handle.
void ot_http_ws_stop(void);

// --- ot_http_ws_push.c -----------------------------------------------------------------------

// EVERYTHING except ot_ws_push_queue() is executed BY THE SERVER TASK and only by it: room() and
// add() are called by the pre-handshake callback, the rest arrives through httpd_queue_work().
// That is why there is no mutex either on the socket list or on the ticket table, and it is not
// forgetfulness.

// Whether there is a free slot. Asked BEFORE the ticket is spent: a handshake rejected after
// ot_ticket_redeem() burns a one-time ticket for nothing.
bool ot_ws_push_room(httpd_handle_t server);

// Takes a socket into the push list. On the very first tick it will receive the FULL state -- a
// page that has just been opened must see everything at once rather than wait until each entity
// changes by itself.
bool ot_ws_push_add(httpd_handle_t server, int fd);

// Forgets all the sockets. Closes nothing: called at start-up and after stopping the server,
// which has already closed the sockets itself.
void ot_ws_push_forget_all(void);

// Queues the push work. The ONLY thing called from the timer task, and the only thing entitled to
// read someone else's pointer to the server: with an empty socket list it does nothing, so a tick
// on a device nobody is connected to is free.
void ot_ws_push_queue(httpd_handle_t server);

// --- ot_http_control.c ----------------------------------------------------------------------

// The executor's live document at "/api/control" (ot_api_control.h). An ordinary read under the
// ordinary policy.
esp_err_t ot_http_control_get(httpd_req_t *req);

// An HTTP status line and a LITERAL reason; status NULL means accepted. The body of a refusal goes
// into the log ring, which GET /api/log hands to whoever can reach the device -- so nothing from
// the client is ever formatted into `message`.
typedef struct {
    const char *status;
    const char *message;
} ot_http_refusal_t;

// The one translation of an ot_thermostat_err_t into an answer, shared by POST /api/entities/<key>
// and POST /api/ops/boost so the two cannot answer one verdict differently: ownership and a season
// that is off -> 409, a value or minutes out of range -> 422, no task -> 503, a command the store
// refused -> 500 (409 for a read-only store), with `store_err`'s sentence. Every code explicitly,
// and a trailing 500 for one this build does not know -- never an accidental 202.
ot_http_refusal_t ot_http_control_refusal(ot_thermostat_err_t e, ot_config_err_t store_err);

// --- ot_http_static.c ---------------------------------------------------------------------

// The captive-portal probes, and the SPA behind them: a path ot_captive does not claim falls
// through to the asset table and then to the shell. Registered LAST, as the /* GET catch-all.
esp_err_t ot_http_captive_probe_get(httpd_req_t *req);
