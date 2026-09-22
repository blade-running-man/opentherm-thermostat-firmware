// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include "ot_led.h"

void setUp(void) {}
void tearDown(void) {}

// A fully healthy, CONNECTED-to-everything world. Every ladder test below mutates exactly one
// field away from this baseline, so a failing test names its own cause.
static ot_led_world_t healthy(void)
{
    ot_led_world_t w;
    w.now_ms            = 1000;
    w.net                = OT_LED_NET_CONNECTED;
    w.net_down_since_ms  = 0;
    w.has_credentials    = true;
    w.mqtt_configured    = true;
    w.mqtt_connected     = true;
    w.control            = OT_CONTROL_HA;
    w.flame              = false;
    w.ota_active         = false;
    return w;
}

// --- the ladder --------------------------------------------------------------------------

static void test_healthy_world_is_normal(void)
{
    ot_led_world_t w = healthy();
    TEST_ASSERT_EQUAL(OT_LED_NORMAL, ot_led_health(&w));
}

static void test_failsafe_control_is_failsafe(void)
{
    ot_led_world_t w = healthy();
    w.control = OT_CONTROL_FAILSAFE;
    TEST_ASSERT_EQUAL(OT_LED_FAILSAFE, ot_led_health(&w));
}

static void test_mqtt_configured_but_not_connected_is_mqtt_down(void)
{
    ot_led_world_t w = healthy();
    w.mqtt_connected = false;
    TEST_ASSERT_EQUAL(OT_LED_MQTT_DOWN, ot_led_health(&w));
}

// The first broken link wins: a dead broker AND a failsafe executor both point at the same
// dead link (HA cannot be heard through a dead broker), and it would be misleading to paint
// the executor's symptom (failsafe, orange) over its cause (no broker, amber).
static void test_mqtt_down_wins_over_failsafe(void)
{
    ot_led_world_t w = healthy();
    w.mqtt_connected = false;
    w.control        = OT_CONTROL_FAILSAFE;
    TEST_ASSERT_EQUAL(OT_LED_MQTT_DOWN, ot_led_health(&w));
}

// No broker configured at all (a local-only install) is not a broken link -- it is a choice --
// so it must read NORMAL, not amber.
static void test_no_broker_configured_is_normal_not_amber(void)
{
    ot_led_world_t w = healthy();
    w.mqtt_configured = false;
    w.mqtt_connected  = false;
    TEST_ASSERT_EQUAL(OT_LED_NORMAL, ot_led_health(&w));
}

static void test_net_down_past_grace_with_credentials_is_wifi_down(void)
{
    ot_led_world_t w    = healthy();
    w.net               = OT_LED_NET_DOWN;
    w.has_credentials   = true;
    w.net_down_since_ms = 0;
    w.now_ms            = OT_LED_WIFI_GRACE_MS;
    TEST_ASSERT_EQUAL(OT_LED_WIFI_DOWN, ot_led_health(&w));
}

// One millisecond inside the grace window: still "connecting", not yet an alarm.
static void test_net_down_inside_grace_is_connecting(void)
{
    ot_led_world_t w    = healthy();
    w.net               = OT_LED_NET_DOWN;
    w.has_credentials   = true;
    w.net_down_since_ms = 0;
    w.now_ms            = OT_LED_WIFI_GRACE_MS - 1;
    TEST_ASSERT_EQUAL(OT_LED_CONNECTING, ot_led_health(&w));
}

// No credentials at all -- the fresh-out-of-the-box state -- never escalates to the red alarm,
// however long it has been down: there is nothing yet to reconnect to.
static void test_net_down_without_credentials_is_connecting(void)
{
    ot_led_world_t w  = healthy();
    w.net             = OT_LED_NET_DOWN;
    w.has_credentials = false;
    w.now_ms          = OT_LED_WIFI_GRACE_MS * 10;
    TEST_ASSERT_EQUAL(OT_LED_CONNECTING, ot_led_health(&w));
}

static void test_access_point_is_setup(void)
{
    ot_led_world_t w = healthy();
    w.net            = OT_LED_NET_ACCESS_POINT;
    TEST_ASSERT_EQUAL(OT_LED_SETUP, ot_led_health(&w));
}

// OTA preempts everything, even a net that is otherwise down.
static void test_ota_active_preempts_everything(void)
{
    ot_led_world_t w  = healthy();
    w.net             = OT_LED_NET_DOWN;
    w.has_credentials = false;
    w.ota_active      = true;
    TEST_ASSERT_EQUAL(OT_LED_OTA, ot_led_health(&w));
}

// Every non-failsafe control state with the links up reads NORMAL: the LED is not a control-
// state indicator, only "is the chain healthy".
static void test_local_control_with_links_up_is_normal(void)
{
    ot_led_world_t w = healthy();
    w.control        = OT_CONTROL_LOCAL;
    TEST_ASSERT_EQUAL(OT_LED_NORMAL, ot_led_health(&w));
}

static void test_boost_control_with_links_up_is_normal(void)
{
    ot_led_world_t w = healthy();
    w.control        = OT_CONTROL_BOOST;
    TEST_ASSERT_EQUAL(OT_LED_NORMAL, ot_led_health(&w));
}

static void test_season_off_control_with_links_up_is_normal(void)
{
    ot_led_world_t w = healthy();
    w.control        = OT_CONTROL_SEASON_OFF;
    TEST_ASSERT_EQUAL(OT_LED_NORMAL, ot_led_health(&w));
}

// --- render: colour identity + motion shape --------------------------------------------------
//
// Sample times below are chosen against ot_led.c's own period constants (documented there, not
// re-exported -- a caller never needs to name them, only ot_led_render() does):
//   fast blink (OTA)      period  150 ms -- on [0,75), off [75,150)
//   slow blink (others)   period 2000 ms -- on [0,1000), off [1000,2000)
//   breathing             period 3000 ms -- trough at t=0, peak at t=1500
//   heartbeat             period 4000 ms, a 400 ms blip at the start -- floor at t=2000 (well
//                         past the blip), blip peak near t=200
// If those constants ever change, these samples must move with them -- that is the point of
// picking them from the real periods rather than from round numbers that happen to work today.

static void test_normal_no_flame_is_green_and_stays_dim(void)
{
    ot_led_world_t w = healthy();
    w.flame          = false;

    w.now_ms          = 2000; // past the blip: heartbeat floor
    ot_led_rgb_t floor = ot_led_render(&w, 255);
    w.now_ms          = 200; // inside the blip: its peak
    ot_led_rgb_t blip  = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, floor.r);
    TEST_ASSERT_EQUAL(0, floor.b);
    TEST_ASSERT_EQUAL(0, blip.r);
    TEST_ASSERT_EQUAL(0, blip.b);
    // Pin the actual constants, not just the shape: heartbeat floor is 0.12*255 = 30.6 -> 31,
    // the blip peak is 0.35*255 = 89.25 -> 89. A loose "< 0.6*C" upper bound would let the
    // peak_frac/floor_frac constants drift ~0.2 and still pass; these +-2/3 windows will not.
    TEST_ASSERT_INT_WITHIN(2, 31, floor.g);
    TEST_ASSERT_INT_WITHIN(3, 89, blip.g);
    TEST_ASSERT_GREATER_THAN(floor.g, blip.g);
}

static void test_normal_flame_breathes(void)
{
    ot_led_world_t w = healthy();
    w.flame          = true;

    w.now_ms            = 0; // trough
    ot_led_rgb_t trough = ot_led_render(&w, 255);
    w.now_ms            = 1500; // half period: peak
    ot_led_rgb_t peak    = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, trough.r);
    TEST_ASSERT_EQUAL(0, trough.b);
    TEST_ASSERT_EQUAL(0, peak.r);
    TEST_ASSERT_EQUAL(0, peak.b);
    // Breathing swings the full band: peak is the ceiling exactly (level == C at the half
    // period), trough is the 0.10*255 = 25.5 -> 26 floor. Pin both, so a shrunk swing or a
    // raised floor is caught, not just "peak > trough".
    TEST_ASSERT_INT_WITHIN(1, 255, peak.g);
    TEST_ASSERT_INT_WITHIN(2, 26, trough.g);
    TEST_ASSERT_GREATER_THAN(trough.g, peak.g);
}

static void test_mqtt_down_flame_breathes_amber(void)
{
    ot_led_world_t w  = healthy();
    w.mqtt_connected  = false;
    w.flame           = true;

    w.now_ms            = 0;
    ot_led_rgb_t trough = ot_led_render(&w, 255);
    w.now_ms            = 1500;
    ot_led_rgb_t peak    = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, peak.b);
    // Amber at the breathing peak (level == C): red is the full ceiling, green is 120/255*255
    // = 120 -- the yellower half that separates amber from orange's 50. Pin the green channel
    // so the amber constant cannot drift toward orange unnoticed.
    TEST_ASSERT_INT_WITHIN(1, 255, peak.r);
    TEST_ASSERT_INT_WITHIN(2, 120, peak.g);
    TEST_ASSERT_GREATER_THAN(trough.r, peak.r);
    TEST_ASSERT_GREATER_THAN(trough.g, peak.g);
}

static void test_mqtt_down_idle_is_amber_heartbeat(void)
{
    ot_led_world_t w  = healthy();
    w.mqtt_connected  = false;
    w.flame           = false;

    w.now_ms           = 2000; // floor
    ot_led_rgb_t floor = ot_led_render(&w, 255);
    w.now_ms           = 200; // blip peak
    ot_led_rgb_t blip  = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, floor.b);
    TEST_ASSERT_EQUAL(0, blip.b);
    // Amber heartbeat: red rides the heartbeat curve (floor 0.12*255 -> 31, blip peak
    // 0.35*255 -> 89), green is the amber fraction of the same level (0.47*31 -> 14, 0.47*89
    // -> 42). Pin all four so neither the heartbeat constants nor the amber hue can drift.
    TEST_ASSERT_INT_WITHIN(2, 31, floor.r);
    TEST_ASSERT_INT_WITHIN(2, 14, floor.g);
    TEST_ASSERT_INT_WITHIN(3, 89, blip.r);
    TEST_ASSERT_INT_WITHIN(3, 42, blip.g);
    TEST_ASSERT_GREATER_THAN(floor.r, blip.r);
}

static void test_failsafe_is_orange_and_blinks(void)
{
    ot_led_world_t w = healthy();
    w.control        = OT_CONTROL_FAILSAFE;

    w.now_ms       = 500; // on-half of the 2 s slow blink
    ot_led_rgb_t on = ot_led_render(&w, 255);
    w.now_ms       = 1500; // off-half
    ot_led_rgb_t off = ot_led_render(&w, 255);

    TEST_ASSERT_GREATER_THAN(0, on.r);
    TEST_ASSERT_GREATER_THAN(0, on.g);
    TEST_ASSERT_LESS_THAN(on.r, on.g); // orange: small green next to red, not amber's near-half
    TEST_ASSERT_EQUAL(0, on.b);
    TEST_ASSERT_EQUAL(0, off.r);
    TEST_ASSERT_EQUAL(0, off.g);
    TEST_ASSERT_EQUAL(0, off.b);
}

// The two red-with-green tiers must never read as the same colour. Amber (MQTT_DOWN) carries
// far more green than orange (FAILSAFE); compare their green channels at full brightness (amber's
// breathing peak vs. orange's blink on-half, both level == C) and demand a large, real gap.
// Nothing else in the suite would catch the two green fractions collapsing toward each other --
// each tier is tested against its own colour in isolation.
static void test_amber_and_orange_greens_are_far_apart(void)
{
    ot_led_world_t amber = healthy();
    amber.mqtt_connected = false;
    amber.flame          = true;
    amber.now_ms         = 1500; // breathing peak: level == C, green == 120/255*C
    ot_led_rgb_t a = ot_led_render(&amber, 255);

    ot_led_world_t orange = healthy();
    orange.control        = OT_CONTROL_FAILSAFE;
    orange.now_ms         = 500; // blink on-half: level == C, green == 50/255*C
    ot_led_rgb_t o = ot_led_render(&orange, 255);

    TEST_ASSERT_GREATER_THAN(o.g, a.g);
    TEST_ASSERT_GREATER_OR_EQUAL(40, (int)a.g - (int)o.g); // ~120 - ~50 = ~70
}

static void test_wifi_down_is_red_and_blinks(void)
{
    ot_led_world_t w    = healthy();
    w.net               = OT_LED_NET_DOWN;
    w.has_credentials   = true;
    w.net_down_since_ms = 0;

    w.now_ms       = OT_LED_WIFI_GRACE_MS + 500; // on-half
    ot_led_rgb_t on = ot_led_render(&w, 255);
    w.now_ms       = OT_LED_WIFI_GRACE_MS + 1500; // off-half
    ot_led_rgb_t off = ot_led_render(&w, 255);

    TEST_ASSERT_GREATER_THAN(0, on.r);
    TEST_ASSERT_EQUAL(0, on.g);
    TEST_ASSERT_EQUAL(0, on.b);
    TEST_ASSERT_EQUAL(0, off.r);
}

static void test_setup_is_blue_and_slow_blinks(void)
{
    ot_led_world_t w = healthy();
    w.net            = OT_LED_NET_ACCESS_POINT;

    w.now_ms       = 500;
    ot_led_rgb_t on = ot_led_render(&w, 255);
    w.now_ms       = 1500;
    ot_led_rgb_t off = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, on.r);
    TEST_ASSERT_EQUAL(0, on.g);
    TEST_ASSERT_GREATER_THAN(0, on.b);
    TEST_ASSERT_EQUAL(0, off.b);
}

static void test_connecting_is_blue_and_breathes(void)
{
    ot_led_world_t w  = healthy();
    w.net             = OT_LED_NET_DOWN;
    w.has_credentials = false; // stays CONNECTING at any now_ms

    w.now_ms            = 0;
    ot_led_rgb_t trough = ot_led_render(&w, 255);
    w.now_ms            = 1500;
    ot_led_rgb_t peak    = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, peak.r);
    TEST_ASSERT_EQUAL(0, peak.g);
    TEST_ASSERT_GREATER_THAN(trough.b, peak.b);
}

// OTA blinks fast enough to toggle inside one SETUP "on" half -- that is the whole point of a
// faster blink: at a glance, "OTA" and "SETUP" must not be confusable.
static void test_ota_is_blue_and_blinks_faster_than_setup(void)
{
    ot_led_world_t w = healthy();
    w.net            = OT_LED_NET_DOWN; // net is irrelevant once OTA preempts the ladder
    w.ota_active     = true;

    w.now_ms       = 0;
    ot_led_rgb_t on = ot_led_render(&w, 255);
    w.now_ms       = 100;
    ot_led_rgb_t off = ot_led_render(&w, 255);

    TEST_ASSERT_EQUAL(0, on.r);
    TEST_ASSERT_EQUAL(0, on.g);
    TEST_ASSERT_GREATER_THAN(0, on.b);
    TEST_ASSERT_EQUAL(0, off.b); // already toggled off by t=100 -- SETUP would still be "on" here

    ot_led_world_t setup = healthy();
    setup.net            = OT_LED_NET_ACCESS_POINT;
    setup.now_ms         = 0;
    ot_led_rgb_t setup_on0 = ot_led_render(&setup, 255);
    setup.now_ms          = 100;
    ot_led_rgb_t setup_on100 = ot_led_render(&setup, 255);
    TEST_ASSERT_GREATER_THAN(0, setup_on0.b);
    TEST_ASSERT_GREATER_THAN(0, setup_on100.b); // still on: SETUP's period is much longer
}

static void test_ceiling_scales_every_channel(void)
{
    ot_led_world_t w = healthy();
    w.flame          = true;
    w.now_ms         = 1500; // breathing peak: level == ceiling exactly, so scaling is exact

    ot_led_rgb_t full  = ot_led_render(&w, 255);
    ot_led_rgb_t small = ot_led_render(&w, 32);

    int expected = (int)((double)full.g * 32.0 / 255.0 + 0.5);
    TEST_ASSERT_INT_WITHIN(1, expected, small.g);
    TEST_ASSERT_EQUAL(0, small.r);
    TEST_ASSERT_EQUAL(0, small.b);
}

static void test_ceiling_zero_is_all_zero(void)
{
    ot_led_world_t w = healthy();
    w.flame          = true;
    w.now_ms         = 1500;

    ot_led_rgb_t rgb = ot_led_render(&w, 0);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(0, rgb.b);
}

// --- names ---------------------------------------------------------------------------------

static void test_health_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ota", ot_led_health_name(OT_LED_OTA));
    TEST_ASSERT_EQUAL_STRING("setup", ot_led_health_name(OT_LED_SETUP));
    TEST_ASSERT_EQUAL_STRING("connecting", ot_led_health_name(OT_LED_CONNECTING));
    TEST_ASSERT_EQUAL_STRING("wifi_down", ot_led_health_name(OT_LED_WIFI_DOWN));
    TEST_ASSERT_EQUAL_STRING("mqtt_down", ot_led_health_name(OT_LED_MQTT_DOWN));
    TEST_ASSERT_EQUAL_STRING("failsafe", ot_led_health_name(OT_LED_FAILSAFE));
    TEST_ASSERT_EQUAL_STRING("normal", ot_led_health_name(OT_LED_NORMAL));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_healthy_world_is_normal);
    RUN_TEST(test_failsafe_control_is_failsafe);
    RUN_TEST(test_mqtt_configured_but_not_connected_is_mqtt_down);
    RUN_TEST(test_mqtt_down_wins_over_failsafe);
    RUN_TEST(test_no_broker_configured_is_normal_not_amber);
    RUN_TEST(test_net_down_past_grace_with_credentials_is_wifi_down);
    RUN_TEST(test_net_down_inside_grace_is_connecting);
    RUN_TEST(test_net_down_without_credentials_is_connecting);
    RUN_TEST(test_access_point_is_setup);
    RUN_TEST(test_ota_active_preempts_everything);
    RUN_TEST(test_local_control_with_links_up_is_normal);
    RUN_TEST(test_boost_control_with_links_up_is_normal);
    RUN_TEST(test_season_off_control_with_links_up_is_normal);
    RUN_TEST(test_normal_no_flame_is_green_and_stays_dim);
    RUN_TEST(test_normal_flame_breathes);
    RUN_TEST(test_mqtt_down_flame_breathes_amber);
    RUN_TEST(test_mqtt_down_idle_is_amber_heartbeat);
    RUN_TEST(test_failsafe_is_orange_and_blinks);
    RUN_TEST(test_amber_and_orange_greens_are_far_apart);
    RUN_TEST(test_wifi_down_is_red_and_blinks);
    RUN_TEST(test_setup_is_blue_and_slow_blinks);
    RUN_TEST(test_connecting_is_blue_and_breathes);
    RUN_TEST(test_ota_is_blue_and_blinks_faster_than_setup);
    RUN_TEST(test_ceiling_scales_every_channel);
    RUN_TEST(test_ceiling_zero_is_all_zero);
    RUN_TEST(test_health_names);
    return UNITY_END();
}
