// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Where MQTT meets the executor: messages decided by ot_mqtt_decide(),
// carried out by ot_mqtt_handle() through the REAL ot_command_check() and the REAL ot_control --
// the effects below forward to ot_control_apply() exactly as ot_mqtt_link's wrapper forwards to
// ot_thermostat_control_apply(), and fold what it asks to persist back into the configuration, as
// the store does. The executor is stepped once a simulated second, as the thermostat task steps it.
//
// The key exit criterion lives here: a retained command does not feed the watchdog.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <initializer_list>

// Every pure component this suite links is named by an include below: PlatformIO's library finder
// reaches a library through a suite's own #include, not through another library's header.
#include "ot_api.h"
#include "ot_command.h"
#include "ot_control.h"
#include "ot_mqtt.h"
#include "ot_registry.h"
#include "ot_state.h"

#define PFX "opentherm/aabbccddeeff"
#define HA_STATUS "homeassistant/status"

static ot_control_t     ctl;
static ot_control_cfg_t cfg;
static ot_control_out_t out;
static uint32_t         now;
static int              writes;

static void fx_cfg(ot_control_cfg_t *o) { *o = cfg; }
static int  fx_apply(ot_origin_t origin, ot_control_cmd_t cmd, int16_t value)
{
    ot_control_persist_t p;
    const ot_control_err_t e = ot_control_apply(&ctl, &cfg, origin, cmd, value, now, &p);
    if (p.set_heating_season)
        cfg.heating_season = p.heating_season;
    if (p.set_dhw_enable)
        cfg.dhw_enable = p.dhw_enable;
    return (int)e;
}
static void fx_write(uint8_t, uint16_t) { writes++; }
static const ot_mqtt_effects_t FX = {fx_cfg, fx_apply, fx_write};

static void step(void)
{
    now += 1000;
    ot_control_in_t in;
    memset(&in, 0, sizeof in);
    in.setpoint_confirmed = true;   // the bus carries every held ID 1 at once
    in.confirmed_dc       = out.held_setpoint_dc;
    ot_control_step(&ctl, &cfg, &in, now, &out);
}

static void run(uint32_t seconds)
{
    for (uint32_t s = 0; s < seconds; s++)
        step();
}

static ot_mqtt_verdict_t send(const char *key, const char *payload, bool retained)
{
    char topic[OT_MQTT_TOPIC_MAX];
    snprintf(topic, sizeof topic, PFX "/%s/set", key);
    const ot_mqtt_in_t in =
        ot_mqtt_decide(PFX, HA_STATUS, topic, strlen(topic), payload, strlen(payload), retained);
    if (in.kind != OT_MQTT_COMMAND) {
        const ot_mqtt_verdict_t v = {false, in.reason};
        return v;
    }
    return ot_mqtt_handle(&in, &FX);
}

static void start(ot_control_mode_t mode)
{
    memset(&cfg, 0, sizeof cfg);
    cfg.mode                    = mode;
    cfg.heating_season          = true;
    cfg.watchdog_s              = 120;
    cfg.failsafe_setpoint_dc    = 450;
    cfg.failsafe_room_target_dc = 180;
    cfg.failsafe_heat_days      = 3;
    cfg.failsafe_min_cycle_s    = 600;
    cfg.flow_min_dc             = 400;
    cfg.flow_max_dc             = 700;
    cfg.local_ch_setpoint_dc    = 450;
    cfg.dhw_enable              = true;
    now                         = 0;
    writes                      = 0;
    memset(&out, 0, sizeof out);
    ot_control_init(&ctl, &cfg, NULL, now);
    step();   // the executor sees the mode; commands are judged against what it saw
}

void setUp(void) { ot_state_reset(); start(OT_CONTROL_MODE_HA); }
void tearDown(void) {}

static void test_a_fresh_ch_enable_every_minute_keeps_ha_in_control(void)
{
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    for (int minute = 0; minute < 6; minute++) {
        run(60);
        TEST_ASSERT_EQUAL(OT_CONTROL_HA, out.state);
        TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    }
}

// THE exit criterion: the same command, replayed retained by the broker, feeds nothing.
static void test_a_retained_ch_enable_never_feeds_the_watchdog(void)
{
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    run(1);
    TEST_ASSERT_EQUAL(OT_CONTROL_HA, out.state);
    for (int i = 0; i < 6; i++) {
        run(30);
        const ot_mqtt_verdict_t v = send("ch_enable", "1", true);
        TEST_ASSERT_FALSE(v.accepted);
        TEST_ASSERT_EQUAL_STRING("a retained command is ignored", v.reason);
    }
    TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
    TEST_ASSERT_EQUAL(OT_CONTROL_REASON_WATCHDOG, out.cause);
}

// After a reboot in HA mode a broker replays what it holds: that must not end ha_waiting.
static void test_a_retained_command_never_ends_ha_waiting(void)
{
    TEST_ASSERT_EQUAL(OT_CONTROL_HA_WAITING, out.state);
    for (int i = 0; i < 8; i++) {
        send("ch_enable", "1", true);
        send("ch_setpoint", "55", true);
        run(20);
        TEST_ASSERT_NOT_EQUAL(OT_CONTROL_HA, out.state);
    }
    TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
}

// Only ch_enable and ch_setpoint feed it -- not DHW, not HA's birth.
static void test_dhw_and_the_birth_message_do_not_feed_the_watchdog(void)
{
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    for (int i = 0; i < 6; i++) {
        run(30);
        TEST_ASSERT_TRUE(send("dhw_enable", "1", false).accepted);
        TEST_ASSERT_EQUAL(OT_MQTT_HA_ONLINE,
                          ot_mqtt_decide(PFX, HA_STATUS, HA_STATUS, 20, "online", 6, false).kind);
    }
    TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
}

static void test_a_setpoint_every_half_minute_feeds_it_and_a_refused_one_does_not(void)
{
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    for (int i = 0; i < 8; i++) {
        run(30);
        TEST_ASSERT_TRUE(send("ch_setpoint", "55", false).accepted);
    }
    TEST_ASSERT_EQUAL(OT_CONTROL_HA, out.state);
    TEST_ASSERT_EQUAL(550, out.held_setpoint_dc);
    for (int i = 0; i < 6; i++) {
        run(30);
        TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OUT_OF_RANGE),
                                 send("ch_setpoint", "80", false).reason);   // flow_max 70
    }
    TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
}

// In LOCAL mode HA's controls are unavailable, and anything that arrives anyway is refused.
// Each value is one the command layer would take from an owner: what is wrong with a value comes
// BEFORE who may send it (the order of refusals, ot_command.h), so "0" for dhw_setpoint -- the
// store's "unset" -- would be refused as out of range, and not for being HA's.
static void test_in_local_mode_every_ha_control_command_is_refused(void)
{
    start(OT_CONTROL_MODE_LOCAL);
    const char *const cmds[][2] = {{"ch_enable", "1"},   {"ch_setpoint", "55"}, {"dhw_enable", "0"},
                                   {"dhw_setpoint", "50"}, {"heating_season", "0"}};
    for (const auto &c : cmds) {
        const ot_mqtt_verdict_t v = send(c[0], c[1], false);
        TEST_ASSERT_FALSE_MESSAGE(v.accepted, c[0]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(ot_command_strerror(OT_CMD_OWNED_BY_LOCAL), v.reason, c[0]);
    }
    run(1);
    TEST_ASSERT_EQUAL(OT_CONTROL_LOCAL, out.state);
}

// HA may turn the season off -- the button's press is "0" -- and never on.
static void test_ha_may_turn_the_season_off_and_never_on(void)
{
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_SEASON_ON_IS_LOCAL),
                             send("heating_season", "1", false).reason);
    TEST_ASSERT_TRUE(send("heating_season", "0", false).accepted);
    run(1);
    TEST_ASSERT_EQUAL(OT_CONTROL_SEASON_OFF, out.state);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_SEASON_ON_IS_LOCAL),
                             send("heating_season", "1", false).reason);
}

// The early answer reads the snapshot, the final one what the executor has SEEN. A
// flip the step has not seen yet is refused by the executor, and accepted one step later.
static void test_a_flip_to_ha_is_honoured_only_after_the_executor_sees_it(void)
{
    start(OT_CONTROL_MODE_LOCAL);
    cfg.mode = OT_CONTROL_MODE_HA;   // stored, not yet stepped
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OWNED_BY_LOCAL),
                             send("ch_enable", "1", false).reason);
    step();
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    run(1);
    TEST_ASSERT_EQUAL(OT_CONTROL_HA, out.state);
    TEST_ASSERT_EQUAL(0, writes);
}

// An OpenTherm frame is not Home Assistant's, in EITHER mode.
// Before the refusal existed, all of these were carried out in LOCAL mode -- and a frame write
// a second holds the bus's one write slot, so the executor's ID 1 re-send is deferred and by the
// invariant the CH bit cannot rise, the failsafe's included. Each value is one the codec and
// the table accept, so what is refused here is the origin and nothing else.
//
// Every writable frame row, the same six test_ot_command_check.cpp's
// test_every_frame_row_is_refused_for_home_assistant_in_either_mode lists -- master_ot_version
// and master_product_version were missing here, so this suite's claim to be "the whole of what a
// broker client may not reach" (test_ot_command_check.cpp) was not actually true of itself.
static void test_a_frame_write_from_ha_is_refused_in_either_mode(void)
{
    static const char *const rows[][2] = {{"room_temperature", "20"},
                                          {"room_setpoint", "21.5"},
                                          {"max_ch_setpoint", "60"},
                                          {"max_relative_modulation", "50"},
                                          {"master_ot_version", "2.2"},
                                          {"master_product_version", "1"}};
    for (unsigned m = 0; m < 2; m++) {
        start(m == 0 ? OT_CONTROL_MODE_LOCAL : OT_CONTROL_MODE_HA);
        for (const auto &row : rows) {
            const ot_mqtt_verdict_t v = send(row[0], row[1], false);
            TEST_ASSERT_FALSE_MESSAGE(v.accepted, row[0]);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(ot_command_strerror(OT_CMD_NOT_FOR_HA), v.reason,
                                             row[0]);
        }
        TEST_ASSERT_EQUAL(0, writes);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_ch_enable_every_minute_keeps_ha_in_control);
    RUN_TEST(test_a_retained_ch_enable_never_feeds_the_watchdog);
    RUN_TEST(test_a_retained_command_never_ends_ha_waiting);
    RUN_TEST(test_dhw_and_the_birth_message_do_not_feed_the_watchdog);
    RUN_TEST(test_a_setpoint_every_half_minute_feeds_it_and_a_refused_one_does_not);
    RUN_TEST(test_in_local_mode_every_ha_control_command_is_refused);
    RUN_TEST(test_ha_may_turn_the_season_off_and_never_on);
    RUN_TEST(test_a_flip_to_ha_is_honoured_only_after_the_executor_sees_it);
    RUN_TEST(test_a_frame_write_from_ha_is_refused_in_either_mode);
    return UNITY_END();
}
