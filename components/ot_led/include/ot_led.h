// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ot_control.h"

// The status LED's decision: colour = health of the control chain (WiFi -> MQTT -> Home
// Assistant), motion = boiler activity. Two independent axes, decided by two independent
// functions over the same sampled world.
//
// PURE. No ESP-IDF header, no timer, no task, no pin -- time and every input enter as
// arguments (ot_led_world_t.now_ms is the monotonic clock, everything else is a sampled
// fact). That is what lets the health ladder and the animation curves be host-tested: the
// glue (a future ot_led_task) owns the clock, the sampling and the WS2812 write; this
// component owns none of that and cannot stall or touch the OpenTherm bus, because it
// never runs on a task at all.
//
// DO NOT #include "ot_net.h" here, even though `net` is conceptually an ot_net_state_t.
// Verified by running the host build, not by reasoning about it (the same way the ot_bus /
// ot_bus_sched split was found): ot_net.h itself pulls "esp_err.h" unconditionally, which
// does not exist for [env:native], and even if it did, ot_net is a genuinely impure
// component (REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash) with no lean,
// dependency-free header of its own -- PlatformIO's host build compiles EVERY .c file of a
// library the moment a suite reaches one of its headers (the ot_bus_sched CMakeLists.txt
// comment records the same discovery), so ot_net.c/_prov.c/_radio.c/_config.c would all be
// pulled into test_ot_led and fail to compile. ot_led_net_state_t below is this component's
// OWN copy of the three states ot_net_state_t carries; the glue that fills ot_led_world_t
// converts one to the other with a one-line switch (values are deliberately kept in the same
// order to make that conversion obviously correct on inspection, but the conversion must
// still be an explicit switch, not a cast -- see the glue's own source guard).
//
// The WiFi grace window (OT_LED_WIFI_GRACE_MS) exists so that an ordinary reassociation --
// the AP hiccups for a second, roaming between channels -- does not flash red. Only a DOWN
// that outlives the grace window, while credentials are actually stored, is worth alarming
// the owner over; a DOWN with no credentials at all is the fresh-out-of-the-box state and
// reads as "connecting", not "down".

#ifdef __cplusplus
extern "C" {
#endif

// Already scaled to the brightness ceiling -- the caller (the glue) writes this straight to
// the strip, no further math.
typedef struct {
    uint8_t r, g, b;
} ot_led_rgb_t;

// ot_led's own copy of ot_net_state_t's three values -- see the DO NOT note above for why this
// is not ot_net_state_t itself. Order matches ot_net_state_t on purpose (DOWN, ACCESS_POINT,
// CONNECTED) so the glue's conversion switch reads as an identity map, but it must remain a
// switch: DO NOT replace it with a cast, because nothing here enforces that the two enums stay
// numbered the same way if either grows a member.
typedef enum {
    OT_LED_NET_DOWN = 0,
    OT_LED_NET_ACCESS_POINT,
    OT_LED_NET_CONNECTED,
} ot_led_net_state_t;

// The world as the glue sampled it. Pure input: ot_led reads only this, never a clock or a pin.
typedef struct {
    uint32_t           now_ms;             // monotonic; drives the animation curves
    ot_led_net_state_t net;                // DOWN / ACCESS_POINT / CONNECTED
    uint32_t           net_down_since_ms;  // when net last left CONNECTED; 0 while CONNECTED
    bool               has_credentials;
    bool               mqtt_configured;    // a broker host is stored
    bool               mqtt_connected;
    ot_control_state_t control;
    bool               flame;
    bool               ota_active;         // dormant in v1 (always false)
} ot_led_world_t;

// The health tier. ENUM ORDER IS THE PRIORITY (first match wins), exactly like ot_control's
// ladder. A switch over this enum is guarded by -Werror=switch under [env:native].
typedef enum {
    OT_LED_OTA = 0,      // fast blue blink    -- updating, do not power off (dormant in v1)
    OT_LED_SETUP,        // slow blue blink    -- AP config portal open, waiting for the owner
    OT_LED_CONNECTING,   // blue breathing     -- associating / within the WiFi grace window
    OT_LED_WIFI_DOWN,    // red, slow blink    -- credentials set, net gone past the grace window
    OT_LED_MQTT_DOWN,    // amber              -- WiFi up, broker configured but not connected
    OT_LED_FAILSAFE,     // orange, slow blink -- links up but HA silent -> executor in failsafe
    OT_LED_NORMAL,       // green              -- everything healthy
    OT_LED_HEALTH_COUNT
} ot_led_health_t;

// Pure. First-match ladder over the world (see the README for the table).
ot_led_health_t ot_led_health(const ot_led_world_t *w);

// Pure. Full render: tier + its motion + the curve at w->now_ms, scaled to `ceiling` (0..255,
// the board's rgb.brightness). This is the single frame the glue writes to the strip.
ot_led_rgb_t ot_led_render(const ot_led_world_t *w, uint8_t ceiling);

// Pure, for logs/tests. "ota","setup","connecting","wifi_down","mqtt_down","failsafe","normal".
const char *ot_led_health_name(ot_led_health_t h);

// The WiFi grace window: DOWN shorter than this is "connecting" (blue), longer is "wifi_down"
// (red). Exposed so the test and the doc cannot drift from the code.
#define OT_LED_WIFI_GRACE_MS 10000u

#ifdef __cplusplus
}
#endif
