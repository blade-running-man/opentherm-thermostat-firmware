// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// GET /api/control: the executor's live document, and the one
// translation of the thermostat's verdicts into HTTP answers, shared by the entity route and the
// boost operation.
//
// A file of its own by addressee, as this component's CMakeLists asks: it answers for the
// executor, not for the boiler (ot_http_boiler.c) nor the registry (ot_http_registry.c).
#include "esp_http_server.h"

#include "ot_api_control.h"
#include "ot_command.h"
#include "ot_http_internal.h"
#include "ot_thermostat.h"

// The BAD_MINUTES sentence below is the only writing of the boost's limit on the HTTP surface
// (op_boost answers a minutes value that does not parse with it too), and it must be ot_control's.
_Static_assert(OT_CONTROL_BOOST_MAX_MINUTES == 480u, "the BAD_MINUTES sentence says 1..480");

esp_err_t ot_http_control_get(httpd_req_t *req)
{
    // First of all and without exception: one access policy for all the routes. An ordinary read.
    if (!ot_http_allowed(req))
        return ESP_OK;

    ot_thermostat_control_info_t doc;
    ot_thermostat_control_get(&doc);
    const size_t need = ot_api_render_control(&doc, ot_http_scratch, OT_HTTP_SCRATCH_SIZE);
    if (need >= OT_HTTP_SCRATCH_SIZE) {
        // Unreachable at a few hundred bytes; checked rather than assumed, as everywhere here --
        // sending truncated JSON is worse than honestly refusing.
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response too large");
        return ESP_OK;
    }
    return ot_http_send_json(req, need);
}

// Every code explicitly, and a trailing 500 for a code this build does not know. The device
// build compiles components without -Wall, so no compiler would point at a missing case -- the
// trailing return is what keeps a code appended to ot_thermostat_err_t from being answered 202.
// The ownership sentences are ot_command_strerror()'s, so the early refusal (ot_command_check) and
// the final one (ot_control_apply, after a mode flip in between) read the same to the client.
ot_http_refusal_t ot_http_control_refusal(ot_thermostat_err_t e, ot_config_err_t store_err)
{
    switch (e) {
    case OT_THERMOSTAT_OK:
        return (ot_http_refusal_t){NULL, NULL};
    case OT_THERMOSTAT_OWNED_BY_HA:
        return (ot_http_refusal_t){"409 Conflict", ot_command_strerror(OT_CMD_OWNED_BY_HA)};
    case OT_THERMOSTAT_OWNED_BY_LOCAL:
        return (ot_http_refusal_t){"409 Conflict", ot_command_strerror(OT_CMD_OWNED_BY_LOCAL)};
    case OT_THERMOSTAT_SEASON_ON_IS_LOCAL:
        return (ot_http_refusal_t){"409 Conflict", ot_command_strerror(OT_CMD_SEASON_ON_IS_LOCAL)};
    case OT_THERMOSTAT_SEASON_IS_OFF:
        // A state of the device, not of the request: the same boost is accepted once the season
        // is on. Refused with a reason rather than accepted as a no-op.
        return (ot_http_refusal_t){"409 Conflict",
                                   "heating_season is off: a boost would not heat"};
    case OT_THERMOSTAT_OUT_OF_RANGE:
        return (ot_http_refusal_t){"422 Unprocessable Content",
                                   "value out of range: a setpoint lies within flow_min_dc.."
                                   "flow_max_dc (GET /api/config), a switch is 0 or 1"};
    case OT_THERMOSTAT_BAD_MINUTES:
        return (ot_http_refusal_t){"422 Unprocessable Content",
                                   "minutes must be a whole number 1..480"};
    case OT_THERMOSTAT_NO_TASK:
        // 503, not 409: nothing the client can wait out on this boot -- and not 500, because the
        // request broke nothing. The boot log says why the task is missing.
        return (ot_http_refusal_t){"503 Service Unavailable",
                                   "the thermostat task is not running; "
                                   "nothing would carry it out"};
    case OT_THERMOSTAT_NOT_SAVED:
        // The executor accepted it and the store REFUSED it: an error, NEVER a silent 202 -- the
        // next step reads the store, so the command has no effect. Refused only: a store that took
        // the patch and then failed to write flash answers OK, so the command gets its 202 and
        // holds until the next reboot (ot_net logs the save error). 409 for a store written by a
        // newer firmware (as POST /api/config answers it), 500 for everything else, which is the
        // firmware contradicting itself. The sentence is the store's own.
        return (ot_http_refusal_t){store_err == OT_CONFIG_ERR_READ_ONLY
                                       ? "409 Conflict"
                                       : "500 Internal Server Error",
                                   ot_config_strerror(store_err)};
    }
    return (ot_http_refusal_t){"500 Internal Server Error", "unhandled thermostat verdict"};
}
