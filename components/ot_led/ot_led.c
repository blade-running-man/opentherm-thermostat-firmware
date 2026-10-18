// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_led.h"

#include <math.h>

// Not part of the frozen contract (only OT_LED_WIFI_GRACE_MS is, in ot_led.h): these are the
// render's own period and shape constants, private to this file. A caller never needs to name
// them -- it samples ot_led_render() at whatever now_ms it has, and gets back a colour.
#define OT_LED_BLINK_FAST_MS       150u   // OTA -- "do not power off", must read as urgent
#define OT_LED_BLINK_SLOW_MS      2000u   // SETUP / WIFI_DOWN / FAILSAFE -- 1 s on, 1 s off
#define OT_LED_BREATHE_MS         3000u   // CONNECTING, and MQTT_DOWN/NORMAL while the flame is on
#define OT_LED_HEARTBEAT_MS       4000u   // MQTT_DOWN/NORMAL, idle: the whole "alive" cycle
#define OT_LED_HEARTBEAT_BLIP_MS   400u   // the brief gentle blip inside that cycle

// Breathing floor: 10% of the ceiling. Never fully dark, so "breathing" reads as alive-and-
// waiting rather than a fault blink.
#define OT_LED_BREATHE_FLOOR_FRAC   0.10
// Heartbeat floor and the blip's peak. The peak is deliberately far below 1.0 -- "well below
// full C" -- because a full-brightness pulse every four seconds reads as a night-light, not a
// quiet "still here" signal; it is the same reasoning that keeps OT_SENSOR_MAX_JUMP_C
// deliberately small in ot_sensor: the number encodes what "not alarming" means.
#define OT_LED_HEARTBEAT_FLOOR_FRAC 0.12
#define OT_LED_HEARTBEAT_PEAK_FRAC  0.35

// Amber vs orange: same hue family (red-heavy, a little green, no blue), separated on the green
// axis (0.47 vs 0.20 of the ceiling, matching the design table) AND on motion, so a colour-
// vision deficiency that flattens one axis still leaves the other.
#define OT_LED_AMBER_G_FRAC   (120.0 / 255.0)
#define OT_LED_ORANGE_G_FRAC   (50.0 / 255.0)

#ifndef OT_LED_PI
#define OT_LED_PI 3.14159265358979323846
#endif

// A hard on/off square wave: `ceiling` for the first half of `period_ms`, 0 for the second.
// Deterministic in now_ms alone -- no state, so two calls at the same instant always agree,
// which is what lets the glue re-render at any cadence without drifting from what was last
// pushed to the strip.
static double blink_level(uint32_t now_ms, uint32_t period_ms, double ceiling)
{
    return ((now_ms % period_ms) < period_ms / 2) ? ceiling : 0.0;
}

// A smooth rise and fall between `floor_frac*ceiling` and `ceiling` over one `period_ms`,
// trough at t=0 (and every multiple of period_ms), peak at the half period. (1-cos)/2 rather
// than a plain triangle: the boiler's flame is not a machine with sharp edges, and a triangle's
// corners are exactly where the eye notices a synthetic animation.
static double breathe_level(uint32_t now_ms, uint32_t period_ms, double floor_frac,
                             double ceiling)
{
    double floor  = floor_frac * ceiling;
    double phase  = (2.0 * OT_LED_PI * (double)(now_ms % period_ms)) / (double)period_ms;
    double s      = (1.0 - cos(phase)) / 2.0;
    return floor + s * (ceiling - floor);
}

// Mostly the floor, with one gentle sine-shaped blip of width `blip_ms` at the start of every
// `period_ms` -- "alive and idle", never a nightlight. DO NOT let `peak_frac` approach 1.0: the
// whole point of a heartbeat instead of a breath is that it stays visibly dimmer than the
// flame-on animation, so the owner can tell "boiler idle" from "boiler firing" out of the
// corner of an eye, without reading the colour.
static double heartbeat_level(uint32_t now_ms, uint32_t period_ms, uint32_t blip_ms,
                               double floor_frac, double peak_frac, double ceiling)
{
    double floor = floor_frac * ceiling;
    double peak  = peak_frac * ceiling;
    uint32_t t   = now_ms % period_ms;
    double bump  = (t < blip_ms) ? sin(OT_LED_PI * (double)t / (double)blip_ms) : 0.0;
    return floor + bump * (peak - floor);
}

// frac is the hue's share of the ceiling at full brightness (0, ~0.20, ~0.47 or 1.0); level is
// the animation's current brightness, already expressed in the same 0..ceiling domain the hue
// is. Rounding happens exactly once, here, so a channel that is nominally zero (frac == 0)
// stays exactly zero regardless of level -- the colour-identity tests below depend on it.
static uint8_t chan(double frac, double level)
{
    double v = frac * level;
    if (v <= 0.0)
        return 0;
    if (v >= 255.0)
        return 255;
    return (uint8_t)(v + 0.5);
}

// The ladder is FIRST MATCH WINS, in the enum's own declared order -- exactly the shape of
// ot_control's ladder (ot_control.h), and for the same reason: a reader who sees "row 3" knows
// every row above it was checked and did not match, without re-deriving the priority from
// scattered conditionals. Do not reorder the `if`s to "simplify" without re-reading the ladder
// in ot_led.h; the order IS the specification.
ot_led_health_t ot_led_health(const ot_led_world_t *w)
{
    if (w->ota_active)
        return OT_LED_OTA;

    if (w->net == OT_LED_NET_ACCESS_POINT)
        return OT_LED_SETUP;

    if (w->net != OT_LED_NET_CONNECTED) {
        // has_credentials gates the alarm colour, not the state itself: a device with nothing
        // to reconnect to (fresh out of the box, or the owner cleared WiFi) is never "down" in
        // the sense that needs the owner's attention -- it just has not been set up yet.
        //
        // PRECONDITION: net_down_since_ms <= now_ms. Both come from the same monotonic clock,
        // and the glue stamps net_down_since_ms at the moment net leaves CONNECTED -- always a
        // past `now_ms` it has already seen -- so the unsigned subtraction below cannot
        // underflow. (Across the uint32 millisecond wrap at 49.7 days the subtraction still
        // yields the true elapsed span, because both operands wrap together; a >= 10 s window
        // is unaffected.) DO NOT reorder to `now_ms - GRACE >= since`, which underflows for the
        // first 10 s after boot when now_ms is small.
        if (w->has_credentials && (w->now_ms - w->net_down_since_ms) >= OT_LED_WIFI_GRACE_MS)
            return OT_LED_WIFI_DOWN;
        return OT_LED_CONNECTING;
    }

    // net == CONNECTED from here on.
    if (w->mqtt_configured && !w->mqtt_connected)
        return OT_LED_MQTT_DOWN;

    if (w->control == OT_CONTROL_FAILSAFE)
        return OT_LED_FAILSAFE;

    return OT_LED_NORMAL;
}

// -Werror=switch (native, platformio.ini) forces every case below to be named explicitly the
// moment a health tier is appended; OT_LED_HEALTH_COUNT is deliberately left out of the switch
// (it is a sentinel, not a tier) and covered instead by the trailing return, so the compiler
// still catches a missing REAL tier without demanding a case for the count itself.
const char *ot_led_health_name(ot_led_health_t h)
{
    switch (h) {
    case OT_LED_OTA:        return "ota";
    case OT_LED_SETUP:      return "setup";
    case OT_LED_CONNECTING: return "connecting";
    case OT_LED_WIFI_DOWN:  return "wifi_down";
    case OT_LED_MQTT_DOWN:  return "mqtt_down";
    case OT_LED_FAILSAFE:   return "failsafe";
    case OT_LED_NORMAL:     return "normal";
    case OT_LED_HEALTH_COUNT: break;
    }
    return "unknown";
}

// The "healthy link, motion = boiler activity" curve, shared verbatim by NORMAL (green) and
// MQTT_DOWN (amber): flame on breathes at full swing, idle ticks over as the dim heartbeat. It
// lives in one place ON PURPOSE -- the two tiers must stay identical in motion (only their hue
// differs), and two copies of this ternary could silently drift so that, say, an idle boiler
// pulsed differently depending on whether the broker was up. DO NOT inline it back per tier.
static double activity_level(const ot_led_world_t *w, double c)
{
    return w->flame
               ? breathe_level(w->now_ms, OT_LED_BREATHE_MS, OT_LED_BREATHE_FLOOR_FRAC, c)
               : heartbeat_level(w->now_ms, OT_LED_HEARTBEAT_MS, OT_LED_HEARTBEAT_BLIP_MS,
                                 OT_LED_HEARTBEAT_FLOOR_FRAC, OT_LED_HEARTBEAT_PEAK_FRAC, c);
}

// The single frame the glue writes to the strip. Colour and motion are decided together per
// tier: which of blink/breathe/heartbeat applies is part of what the tier MEANS (a fault
// blinks, a healthy chain breathes or ticks over), so splitting "pick a colour" from "pick a
// motion" into two independently-combinable axes would let a future edit pair a blink with a
// heartbeat by accident. -Werror=switch (native) forces this to name every tier the moment one
// is added to ot_led_health_t; OT_LED_HEALTH_COUNT is a sentinel, not a tier, and is disposed of
// with an empty case rather than given a made-up colour.
ot_led_rgb_t ot_led_render(const ot_led_world_t *w, uint8_t ceiling)
{
    const double c = (double)ceiling;
    double r_frac = 0.0, g_frac = 0.0, b_frac = 0.0;
    double level  = 0.0;

    switch (ot_led_health(w)) {
    case OT_LED_OTA:
        b_frac = 1.0;
        level  = blink_level(w->now_ms, OT_LED_BLINK_FAST_MS, c);
        break;
    case OT_LED_SETUP:
        b_frac = 1.0;
        level  = blink_level(w->now_ms, OT_LED_BLINK_SLOW_MS, c);
        break;
    case OT_LED_CONNECTING:
        b_frac = 1.0;
        level  = breathe_level(w->now_ms, OT_LED_BREATHE_MS, OT_LED_BREATHE_FLOOR_FRAC, c);
        break;
    case OT_LED_WIFI_DOWN:
        r_frac = 1.0;
        level  = blink_level(w->now_ms, OT_LED_BLINK_SLOW_MS, c);
        break;
    case OT_LED_MQTT_DOWN:
        r_frac = 1.0;
        g_frac = OT_LED_AMBER_G_FRAC;
        level  = activity_level(w, c);
        break;
    case OT_LED_FAILSAFE:
        r_frac = 1.0;
        g_frac = OT_LED_ORANGE_G_FRAC;
        level  = blink_level(w->now_ms, OT_LED_BLINK_SLOW_MS, c);
        break;
    case OT_LED_NORMAL:
        g_frac = 1.0;
        level  = activity_level(w, c);
        break;
    case OT_LED_HEALTH_COUNT:
        break;
    }

    ot_led_rgb_t out = { chan(r_frac, level), chan(g_frac, level), chan(b_frac, level) };
    return out;
}
