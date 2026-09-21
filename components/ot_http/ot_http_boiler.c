// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The routes addressed to the boiler. Exactly one: the raw observation of the bus.
//
// Separated from ot_http_config.c by addressee, not by volume: those six configure the device and
// work on an UNCONFIGURED one, behind an open access point. This one shows what has been heard
// from the boiler and is meaningful only once the bus is already running.
//
// HERE THERE WERE the Data-ID sweep and the line test. They moved to ot_http_ops.c and became
// operations under /api/ops/, because operations are what they were: "do this once, the result
// will come later". GET /api/ot/raw did not move and will not -- it is a read of the bus state
// with no body, no parameters and no deferred result.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "ot_bus.h"
#include "ot_master.h"
#include "ot_http_internal.h"
#include "ot_observe.h"

esp_err_t ot_http_ot_raw_get(httpd_req_t *req)
{
    // First of all and without exception: there is one access policy for all the routes, and it
    // has no second door.
    if (!ot_http_allowed(req))
        return ESP_OK;

    ot_bus_stats_t st;
    ot_bus_stats(&st);
    ot_master_stats_t ms;
    ot_master_stats(&ms);

    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const ot_observe_header_t header = {
        .uptime_ms = now,
        .cycles    = st.cycles,
        .ok        = st.ok,
        .failed    = st.failed,
        .overdue   = st.overdue,
        .answering = st.boiler_answering,
        .in_duty      = ot_master_input_duty(),
        .timeout      = ms.timeout,
        .frame_error  = ms.frame_error,
        .parity_error = ms.parity_error,
        .rx_edges     = ms.rx_edges,
        .pins_shorted = ms.pins_shorted,
        .scan_done    = st.scan_done,
        .scan_total   = st.scan_total,
    };

    const size_t need = ot_observe_render_json(ot_bus_observed(), &header, now,
                                               ot_http_scratch, OT_HTTP_SCRATCH_SIZE);
    if (need >= OT_HTTP_SCRATCH_SIZE) {
        // Unreachable with 128 records of about seventy bytes each, but it is checked rather
        // than assumed: the renderer returns the REQUIRED size for exactly this purpose, and
        // sending truncated JSON is worse than honestly answering with a refusal.
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response too large");
        return ESP_OK;
    }
    return ot_http_send_json(req, need);
}
