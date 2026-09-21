// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The registry routes. The fourth file of the component rather than a continuation of ot_http.c:
// the file ceiling is 350 lines, and the route table and the handler bodies are different
// responsibilities.
//
// DO NOT merge them back: ot_http.c holds the access policy, the server start-up and the table,
// while only the bodies of the registry handlers live here.
//
// There is not one privileged endpoint here and there cannot be: this is the
// same API that MQTT and any other client use.
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

#include "ot_api.h"
#include "ot_bus.h"
#include "ot_command.h"
#include "ot_http_internal.h"
#include "ot_thermostat.h"
#include "ot_wire.h"

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// The tail after "/api/entities" in the request path, "" when there is no tail.
//
// ONE parse for two handlers: reading the list, reading one entity and writing into an entity
// differ by exactly the presence and content of this tail, and a second parse of it would one day
// diverge from the first over whether to count "/api/entities/" as a key.
//
// The path is taken through ot_http_request_path and NOT from req->uri directly: the server hands
// the handler the raw request target together with the query and, in absolute-form, with the
// authority (ot_http.c, the comment on that function). On a raw req->uri, "/api/entities/x?t=1"
// would give the key "x?t=1" and an honest request would turn into a 404.
static const char *entity_key(httpd_req_t *req, char *buf, size_t cap)
{
    const char *path = ot_http_request_path(req, buf, cap);
    const char *tail = path + strlen("/api/entities");
    if (*tail == '/')
        tail++;
    return tail;
}

esp_err_t ot_http_state_get(httpd_req_t *req)
{
    // First of all and without exception: there is one access policy for all the routes.
    if (!ot_http_allowed(req))
        return ESP_OK;

    const size_t need = ot_api_render_state(ot_http_scratch, OT_HTTP_SCRATCH_SIZE, now_ms());
    // A returned size larger than the buffer is a DEFECT, not an edge case: the buffer is chosen
    // with headroom for the whole registry. We answer 500, because the client would parse
    // truncated JSON as a valid document with entities missing from it.
    if (need >= OT_HTTP_SCRATCH_SIZE)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "state does not fit");

    return ot_http_send_json(req, need);
}

esp_err_t ot_http_entities_get(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    // One handler for two routes -- "/api/entities" and "/api/entities/*" -- because
    // esp_http_server does not consider the first a match for the second, and the list and a
    // single entity differ by exactly the presence of the tail.
    char        buf[128];
    const char *tail = entity_key(req, buf, sizeof buf);

    const size_t need = *tail
        ? ot_api_render_entity(tail, ot_http_scratch, OT_HTTP_SCRATCH_SIZE, now_ms())
        : ot_api_render_entities(ot_http_scratch, OT_HTTP_SCRATCH_SIZE);

    // A zero from render_entity means "there is no such key in the registry", and that is a 404,
    // not an empty document: an empty document would be taken by the client for an entity with no
    // value.
    if (*tail && need == 0)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such entity");
    if (need >= OT_HTTP_SCRATCH_SIZE)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "registry does not fit");

    return ot_http_send_json(req, need);
}

// The gluing of four layers and nothing more: ot_wire turns bytes into a value,
// ot_command_check() decides every refusal with an origin, and the accepted write goes to the bus
// (a frame) or to the executor (a control command). There is not one check OF ITS OWN here and
// there cannot be: a check duplicated on the surface will one day diverge from the check in the
// depths, and it will diverge silently (ot_command.h). This file only translates answers.
esp_err_t ot_http_entity_post(httpd_req_t *req)
{
    if (!ot_http_allowed(req))
        return ESP_OK;

    char        buf[128];
    const char *key = entity_key(req, buf, sizeof buf);
    // "/api/entities/" with no key. A 404, not a 400: a resource that does not exist was
    // addressed -- the request body may meanwhile be impeccable.
    if (*key == '\0')
        return ot_http_send_error(req, "404 Not Found", "the path names no entity");

    char body[OT_HTTP_BODY_MAX];
    if (!ot_http_read_body(req, body, sizeof body))
        return ESP_OK;

    ot_wire_value_t        v;
    const ot_wire_status_t we = ot_wire_parse_entity_write(body, &v);
    if (we != OT_WIRE_OK)
        return ot_http_send_error(req, "400 Bad Request",
                                  ot_wire_strerror((ot_wire_result_t){.status = we}));

    // v.number, and not a fresh parse of is_bool: ot_wire has already SEPARATED the two cases and
    // repeats a boolean as 1.0 / 0.0 (ot_wire.h). A command operates on numbers, so there is no
    // second branch here and "true" does not turn into a guess.
    //
    // The configuration through the EXECUTOR'S OWN builder (ot_thermostat_control_cfg()): this
    // early answer and the final one inside ot_control_apply() read one mapping of one store. DO NOT
    // build a snapshot here from ot_net_config_snapshot(): a second config -> tenths mapping is a
    // second answer to "what are the flow bounds".
    ot_control_cfg_t cfg;
    ot_thermostat_control_cfg(&cfg);
    ot_command_out_t       out;
    const ot_command_err_t ce = ot_command_check(key, v.number, OT_ORIGIN_WEB, &cfg, &out);
    // Which refusal wins when several apply is ot_command_check()'s rule, stated once in
    // ot_command.h; this switch only names each answer. DO NOT pre-check anything above it.
    switch (ce) {
    case OT_CMD_OK:
        break;
    case OT_CMD_UNKNOWN_KEY:
        return ot_http_send_error(req, "404 Not Found", ot_command_strerror(ce));
    case OT_CMD_NOT_WRITABLE:
    case OT_CMD_HALF_WORD_CODEC:
        // A 405, not a 403: writing here is NEVER allowed to anyone, it is a property of the
        // entity, not of the caller's rights. The same address reads perfectly well.
        return ot_http_send_error(req, "405 Method Not Allowed", ot_command_strerror(ce));
    case OT_CMD_UNSUPPORTED_BY_BOILER:
        // A 409, not a 404: the entity is in the registry, it is THIS PARTICULAR boiler that does
        // not accept it -- a frame row, or dhw_setpoint. Never ch_setpoint: ID 1's flag is not
        // asked for it (ot_command.h).
        return ot_http_send_error(req, "409 Conflict", ot_command_strerror(ce));
    case OT_CMD_OUT_OF_RANGE:
        // A 422, not a 400: the body was parsed and understood, it is the value that was
        // rejected.
        return ot_http_send_error(req, "422 Unprocessable Content", ot_command_strerror(ce));
    case OT_CMD_OWNED_BY_HA:
    case OT_CMD_OWNED_BY_LOCAL:
    case OT_CMD_SEASON_ON_IS_LOCAL:
        // A 409, not a 422: who may send it, not what is wrong with the value. No
        // promise that the value is accepted after a mode switch: the executor's own bounds are
        // asked after ownership, so the web's ch_setpoint = 99 is 409 in HA mode, 422 in LOCAL.
        return ot_http_send_error(req, "409 Conflict", ot_command_strerror(ce));
    }
    // Every code is handled above -- and the board build has no -Wall, while the host build that
    // has -Werror=switch never compiles ot_http, so no compiler would name one appended later.
    // This line is what keeps such a code from falling through to the bus with a frame nobody
    // filled in, which is what the earlier switch would have done.
    if (ce != OT_CMD_OK)
        return ot_http_send_error(req, "500 Internal Server Error", ot_command_strerror(ce));

    if (out.kind == OT_CMD_OUT_CONTROL) {
        // THE FINAL ANSWER: ot_control_apply() re-checks ownership against the mode the executor
        // last observed, which may have flipped since the early answer above,
        // and the store may refuse what the command asks to persist. Both are errors, never a
        // 202, answered as the boost's are.
        ot_config_err_t           store_err = OT_CONFIG_OK;
        const ot_thermostat_err_t te =
            ot_thermostat_control_apply(OT_ORIGIN_WEB, out.control, out.value, &store_err);
        const ot_http_refusal_t r = ot_http_control_refusal(te, store_err);
        if (r.status != NULL)
            return ot_http_send_error(req, r.status, r.message);
        // A 202: accepted and stored; what the boiler is told follows on the executor's next step
        // and shows in GET /api/control. NO VALUE is echoed: ot_control_apply() quantises a CH
        // setpoint to 0.5 °C before it persists it, so the request's number is not the one kept,
        // and GET /api/config is where the kept one is read.
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_set_type(req, "application/json");
        char      applied[48];
        const int n = snprintf(applied, sizeof applied, "{\"applied\":{\"command\":%u}}",
                               (unsigned)out.control);
        return httpd_resp_send(req, applied, n);
    }
    if (out.kind != OT_CMD_OUT_FRAME)
        return ot_http_send_error(req, "500 Internal Server Error", "unhandled command kind");

    ot_bus_write(out.frame.data_id, out.frame.raw);

    // A 202, not a 200. The bus queue is sized for ONE write, and the next evicts the unexecuted
    // one -- a stale setpoint is worse than a lost one (ot_bus_sched.h). At the moment of the
    // reply we do not know that the boiler accepted it, and "200 OK" would be asserting exactly
    // that.
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    char      queued[96];
    const int n = snprintf(queued, sizeof queued, "{\"queued\":{\"data_id\":%u,\"raw\":%u}}",
                           (unsigned)out.frame.data_id, (unsigned)out.frame.raw);
    return httpd_resp_send(req, queued, n);
}
