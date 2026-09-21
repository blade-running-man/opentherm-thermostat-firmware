// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The HTTP surface: the same API for everyone, and the SPA served from flash.
//
// The web UI has no privileged endpoint. Every path below is reachable by
// curl exactly as it is by the browser, and the browser gets no header, cookie or route the
// API does not give anyone else. If a screen ever needs something the API does not offer,
// the API is wrong.
#pragma once

#include "esp_err.h"
#include "ot_http_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;
} ot_http_config_t;

#define OT_HTTP_DEFAULT_CONFIG() ((ot_http_config_t){.port = 80})

esp_err_t ot_http_start(const ot_http_config_t *config);
esp_err_t ot_http_stop(void);

// HERE THERE WAS ot_http_notify_state_changed(): "the state has changed, wake /ws up".
// Both the function and /ws itself work together with ot_state -- there is nobody
// to wake while there is nothing to change. Its only callers were there too. It has to be brought
// back whole, together with the pusher task: it must stay cheap and callable from ANY task (the
// OpenTherm bus reading thread included) -- it only sets a flag and wakes, because a stream of
// telemetry must not turn into a stream of frames on every open socket.

// There is deliberately NO ot_http_set_provisioned(). Whether the device counts as claimed
// is asked of ot_net on every request, not pushed here and cached.
//
// A cached copy is a tick out of date, and the tick it is out of date by is exactly the window
// that must be closed: the access point leaves the air the instant the station is given an
// address. Worse, the thing that used to be pushed was ot_net_has_credentials() -- pushing
// "credentials are stored" is rejected, because storing credentials would then
// make the device writable while its open access point is still up and the passer-by who supplied
// them is still on it.

#ifdef __cplusplus
}
#endif
