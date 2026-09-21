// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Operations: POST /api/ops/<name> with a flat object of numeric parameters in the body.
//
// The eighth file of the component. It exists BECAUSE it has users, not the other way round: a
// framework of operations without a single operation is speculation, which YAGNI forbids. The
// Data-ID sweep and the line test ARE operations: "do this once, the answer will come later". The
// separate endpoints /api/ot/scan and /api/ot/linetest were a special case with no justification
// and were deleted together with the move.
//
// GET /api/ot/raw stayed where it was and stays in ot_http_boiler.c: it is a READ of the bus
// state, not an operation, and it has no body, no parameters and no deferred result.
//
// What the move changed in the rights, and how that was fixed. /api/ops/ is a command in the terms
// of ot_http_policy.c, so on a device on a network WITHOUT a password set the operations turned
// out to be permitted, whereas /api/ot/* answered 403. For the Data-ID sweep that is exactly the
// relaxation the owner already accepted for writing a setpoint, and no more.
//
// For the LINE TEST it is not, and the previous comment here asserted the opposite. It said that
// stopping the frames is "exactly what a written setpoint does". That is wrong. A setpoint is
// limited by the registry bounds and will be overridden by the control loop's next decision;
// master silence is overridden by NOTHING, because the control loop's frames are precisely what is
// not going out. Silence longer than five seconds is treated by the slave as a
// short-circuited thermostat and it goes into a heat demand. A loop of `curl -XPOST
// /api/ops/linetest -d '{"duration_ms":30000}'` every 29 seconds keeps the boiler hot
// indefinitely, and the executor is powerless.
//
// Hence the `halts_bus` flag in the table below: an operation that stops the conversation with the
// boiler requires a password ALWAYS -- even where an ordinary write does not. The decision lives in
// ot_http_check_op() (ot_policy), where it is verified without a network; here there is only the
// flag, next to the operation it describes.
//
// DO NOT add an operation here that has no caller.

#include <string.h>

#include "esp_http_server.h"

#include "ot_bus.h"
#include "ot_control_io.h"
#include "ot_http_internal.h"
#include "ot_http_policy.h"
#include "ot_net.h"
#include "ot_thermostat.h"
#include "ot_wire.h"

// The refusal of an operation: an HTTP status line and a LITERAL reason.
//
// One string for two things will not do, because the codes tell the client different things: "a
// range outside 0..127" is a correctly formed request with an invalid number (422), while "a test
// is already running" is a state of the device, not of the request (409). Reducing them to a single
// 400 would mean telling the client it was wrong where it was not.
//
// `message` must be a literal: the body of a refusal goes into the log ring, which GET /api/log
// hands to whoever can reach the device (ot_http_internal.h).
typedef struct {
    const char *status;   // NULL -- the operation was accepted
    const char *message;
} op_refusal_t;

#define OP_ACCEPTED ((op_refusal_t){NULL, NULL})

typedef struct {
    const char *name;
    // Whether the operation stops the conversation with the boiler. See the file header: for such
    // an operation ot_http_check_op() requires a password to be set ALWAYS, because the relaxation
    // of the /api/ops/ route was accepted for a command that the next command overrides, whereas a
    // master gone silent is overridden by nothing.
    bool halts_bus;
    op_refusal_t (*run)(const ot_wire_params_t *p);
} op_t;

// The defaults repeat what was hard-wired into the deleted endpoint: the whole range defined by
// the protocol. The body `{}` must work exactly like the former request with no parameters --
// otherwise the move would have broken the button the endpoint exists for.
static op_refusal_t op_scan(const ot_wire_params_t *p)
{
    float from = 0, to = 127;
    ot_wire_param(p, "from", &from);
    ot_wire_param(p, "to", &to);
    // A 422, not a silent clamp to 0..127: a sweep over a range the client did not name would
    // return a picture that does not answer the question asked, and do it silently.
    if (from < 0 || to > 127 || from > to)
        return (op_refusal_t){"422 Unprocessable Content", "scan range outside 0..127"};
    ot_bus_scan((uint8_t)from, (uint8_t)to);
    return OP_ACCEPTED;
}

// Twenty seconds, two in each state -- the same numbers that stood as constants in the deleted
// endpoint: the multimeter's needle keeps up, and the total is knowingly below
// OT_BUS_TEST_MAX_MS.
static op_refusal_t op_linetest(const ot_wire_params_t *p)
{
    float duration = 20000, half = 2000;
    ot_wire_param(p, "duration_ms", &duration);
    ot_wire_param(p, "half_period_ms", &half);
    // BEFORE the cast to uint32_t: a negative value would become enormous, ot_bus_line_test()
    // would reject it by the duration ceiling, and the client would get a 409 "already running"
    // for a request that is simply invalid.
    //
    // ALL the limits that ot_bus_line_test() checks, not just the sign. The neighbouring parameter
    // was already covered by this comment, but half_period_ms was not: {"half_period_ms": 10}
    // passed `half > 0`, was rejected by the bus on the 100 ms lower limit and returned a 409
    // "already running". The client was being told the state of the device where the mistake was
    // its own.
    //
    // The numbers are taken from ot_bus.h rather than written out here again: two writings of one
    // limit will one day diverge, and diverge silently -- a 422 for what the bus would accept, or a
    // 409 for what it would reject.
    if (duration <= 0 || duration > (float)OT_BUS_TEST_MAX_MS)
        return (op_refusal_t){"422 Unprocessable Content", "duration_ms outside 1..30000"};
    if (half < (float)OT_BUS_TEST_HALF_MIN_MS || half > (float)OT_BUS_TEST_HALF_MAX_MS)
        return (op_refusal_t){"422 Unprocessable Content", "half_period_ms outside 100..5000"};
    // The refusal must reach the client. Swallowing it means showing in the interface that the
    // test has started when it has not -- and the owner will spend twenty seconds looking at a
    // multimeter that will show nothing.
    // A 409 now means EXACTLY one thing: a test is already running. The limits were filtered out
    // above, so no other way of getting false from ot_bus_line_test() remains, and the message no
    // longer invites the client to guess between two causes.
    if (!ot_bus_line_test((uint32_t)duration, (uint32_t)half))
        return (op_refusal_t){"409 Conflict", "line test already running"};
    return OP_ACCEPTED;
}

static op_refusal_t control_refusal(ot_thermostat_err_t e)
{
    const ot_http_refusal_t r = ot_http_control_refusal(e, OT_CONFIG_OK);
    return (op_refusal_t){r.status, r.message};
}

// The timed heat boost, ladder row 2 of the executor. Its caller is the owner --
// "heat for an hour at a flow of 50" -- and that caller is what satisfies the DO NOT at the top of
// this file.
//
// BOTH PARAMETERS ARE REQUIRED; there are no defaults. A boost at a guessed temperature for a
// guessed time is a boiler heating on the firmware's assumption.
//
// This operation only PARSES and MAPS. Every rule -- HA mode, the season, 1..480 minutes, the flow
// bounds -- is ot_control_boost_start()'s, in pure code a host suite reaches. DO NOT add a
// range check here "for a better message"; improve ot_http_control_refusal()'s sentence instead.
static op_refusal_t op_boost(const ot_wire_params_t *p)
{
    float setpoint = 0, minutes = 0;
    if (!ot_wire_param(p, "setpoint", &setpoint) || !ot_wire_param(p, "minutes", &minutes))
        return (op_refusal_t){"422 Unprocessable Content", "boost needs both setpoint and minutes"};
    // The parse, not the rule: 1.5 would silently become one, a boost shorter than was asked for,
    // and -5 an enormous one. 0 and 481 pass here and are ot_control's BAD_MINUTES. A minutes value
    // that does not parse is answered AS BAD_MINUTES, by the mapper: DO NOT write the "1..480"
    // sentence here again -- two writings of one limit diverge, and silently.
    uint32_t whole = 0;
    if (!ot_control_io_minutes(minutes, &whole))
        return control_refusal(OT_THERMOSTAT_BAD_MINUTES);
    int16_t dc = 0;
    if (!ot_control_io_dc(setpoint, &dc))
        return (op_refusal_t){"422 Unprocessable Content", "setpoint must be a finite temperature"};
    return control_refusal(ot_thermostat_boost_start(dc, whole));
}

// A separate operation rather than {"minutes": 0} to op_boost: explicit beats clever, and zero is
// refused there precisely so it can mean neither "forever" nor "stop" by accident. Idempotent --
// "make sure no boost is running" is a legitimate request, and an error for it would only teach
// clients to ignore this endpoint's errors.
static op_refusal_t op_boost_off(const ot_wire_params_t *p)
{
    (void)p;
    ot_thermostat_boost_cancel();
    return OP_ACCEPTED;
}

static const op_t OPS[] = {
    // scan does NOT stop the master: the sweep occupies the meaningful slot, the mandatory ID 0
    // keeps going out on every second step (ot_bus_sched.h), the conversation is not interrupted.
    // Marking it with the flag would mean closing behind a password the very endpoint people
    // reach for the device for in the first place.
    {"scan", false, op_scan},
    // linetest does stop it: ot_bus.c does a `continue` before any conversation while the test is
    // running. This is precisely the operation the flag was created for.
    {"linetest", true, op_linetest},
    // Neither boost operation halts the bus: a boost asks for heat through the ID 0 byte and ID 1,
    // both riding the ordinary conversation, and the next command -- boost_off, or the deadline --
    // overrides it. That is the relaxation the flag exists to withhold from linetest, not from
    // these.
    {"boost", false, op_boost},
    {"boost_off", false, op_boost_off},
};

static const op_t *find_op(const char *name)
{
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++)
        if (strcmp(OPS[i].name, name) == 0)
            return &OPS[i];
    return NULL;
}

esp_err_t ot_http_op_post(httpd_req_t *req)
{
    // First of all and without exception: there is one access policy for all the routes.
    if (!ot_http_allowed(req))
        return ESP_OK;

    // Through ot_http_request_path, not from req->uri: the server hands the handler the raw
    // request target together with the query string, and "/api/ops/scan?t=1" would give the name
    // "scan?t=1".
    char        buf[128];
    const char *path = ot_http_request_path(req, buf, sizeof buf);
    const char *name = path + strlen("/api/ops");
    if (*name == '/')
        name++;

    const op_t *op = *name ? find_op(name) : NULL;
    // A 404 and a LITERAL. The known names are not listed in the body and the submitted name is
    // not substituted into it: the body of a refusal goes into the log, and the log is open to
    // anyone who can reach the device. The list of operations is no secret, but the rule "nothing
    // from the client is formatted into a message" gets relaxed exactly once, after which it stops
    // being a rule.
    if (op == NULL)
        return ot_http_send_error(req, "404 Not Found", "no such operation");

    // AFTER looking the operation up, because the flag belongs to the operation, not to the route:
    // ot_http_allowed() above judged by the path and let /api/ops/ through on a device with no
    // password by the same line that lets a setpoint write through. For an operation that stops the
    // conversation with the boiler that is not enough -- see the file header.
    //
    // The password is asked about in THE SAME way ot_http_allowed() asks about it: through
    // ot_net_password_set(). There is no second answer to the question "is a password set" in the
    // firmware, and creating one here would mean having two answers that will one day diverge.
    if (ot_http_check_op(op->halts_bus, ot_net_password_set()) != OT_HTTP_ALLOW)
        return ot_http_send_error(req, "403 Forbidden",
                                  "operation halts the boiler conversation; set a password first");

    // There may be no body at all, and that is a legitimate call of an operation without
    // parameters. An empty body is NOT handed to ot_http_read_body(): that one answers 413 on
    // content_len == 0, because for a configuration document an absent body is a refusal, while
    // here it means the defaults.
    ot_wire_params_t p = {0};
    if (req->content_len > 0) {
        char body[OT_HTTP_BODY_MAX];
        if (!ot_http_read_body(req, body, sizeof body))
            return ESP_OK;
        const ot_wire_status_t we = ot_wire_parse_operation(body, &p);
        if (we != OT_WIRE_OK)
            return ot_http_send_error(req, "400 Bad Request",
                                      ot_wire_strerror((ot_wire_result_t){.status = we}));
    }

    const op_refusal_t r = op->run(&p);
    if (r.status != NULL)
        return ot_http_send_error(req, r.status, r.message);

    // A 202, not a 200, for the same reason as with a write into an entity: the operation was
    // QUEUED. The sweep takes minutes, the line test twenty seconds, and at the moment of the reply
    // nobody has the result. The progress of the operation is visible in GET /api/ot/raw
    // (scan_done / scan_total).
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"running\":true}");
}
