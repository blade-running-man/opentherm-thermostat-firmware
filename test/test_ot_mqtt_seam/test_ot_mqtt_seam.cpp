// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Four probes kept as a permanent suite because each pins a
// behaviour no other suite pins -- test_ot_mqtt_control drives the same real ot_control through
// ot_mqtt_handle, but none of its own tests combine a restored reboot with a retained command,
// a stale early-answer snapshot racing the executor's own view, the three worlds ID 48
// and ID 56 put dhw_setpoint in, or all six writable frame rows plus the claim that fx->write
// is never reached for any of them. Everything else probed (embedded
// NUL, wildcard/space keys, zero-spellings, prefix confusion, a command burst) was already, or is
// now, pinned by test_ot_mqtt / test_ot_mqtt_out / test_ot_mqtt_control themselves and is not
// repeated here -- a fifth copy of a fact already on record costs the next reader's time for
// nothing.
#include <stdio.h>
#include <string.h>
#include <unity.h>

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
    if (p.set_heating_season) cfg.heating_season = p.heating_season;
    if (p.set_dhw_enable)     cfg.dhw_enable     = p.dhw_enable;
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
static void run(uint32_t seconds) { for (uint32_t s = 0; s < seconds; s++) step(); }

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

static void default_cfg(ot_control_mode_t mode)
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
}

void setUp(void) { ot_state_reset(); writes = 0; now = 0; memset(&out, 0, sizeof out); }
void tearDown(void) {}

// The overdue counter lives in RTC_NOINIT memory so it survives a soft reset, and a
// retained command is ignored. A reboot restores the overdue counter, not
// zero; the failsafe must arrive exactly when THAT restored schedule says, and a retained command
// replayed by the broker on reconnect (every reboot resubscribes) must not move it either way --
// neither feeding the watchdog nor, by being refused, resetting anything. watchdog_s=120s here;
// overdue is restored to 5000 ms short of expiry, so the boundary is exactly the 5th step.
static void test_a_retained_command_after_a_restored_reboot_does_not_move_the_schedule(void)
{
    default_cfg(OT_CONTROL_MODE_HA);
    ot_control_restore_t r = {0};
    r.overdue_valid = true;
    r.overdue_ms    = 115000;   // 5000 ms left on a 120000 ms watchdog
    ot_control_init(&ctl, &cfg, &r, now);
    for (int i = 0; i < 4; i++) {
        const ot_mqtt_verdict_t v = send("ch_setpoint", "50", true);   // retained: must feed nothing
        TEST_ASSERT_FALSE(v.accepted);
        step();
        TEST_ASSERT_EQUAL_MESSAGE(OT_CONTROL_HA_WAITING, out.state, "before the 5th step");
    }
    send("ch_setpoint", "50", true);
    step();   // 115000 + 5000 = 120000 >= watchdog_s*1000 -- failsafe, on schedule, not late
    TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
    TEST_ASSERT_EQUAL(OT_CONTROL_REASON_WATCHDOG, out.cause);
}

// ot_control_apply() re-checks ownership under the caller's spinlock...
// against the mode the executor last observed... ot_command's answer is the early one;
// ot_control's is final." This is the race stated in prose, driven end to end: the executor has
// already stepped to LOCAL, but the snapshot ot_mqtt_handle's fx->cfg hands to the EARLY answer
// still says HA (built outside the executor's lock, per ot_control.h's SNAPSHOTS MAY BE STALE).
// The early answer alone would accept; only the final one, judged against what the executor
// itself observed, may refuse -- and must.
static void test_a_stale_ha_snapshot_is_overruled_by_the_executors_own_view(void)
{
    default_cfg(OT_CONTROL_MODE_HA);
    ot_control_init(&ctl, &cfg, NULL, now);
    step();
    TEST_ASSERT_TRUE(send("ch_enable", "1", false).accepted);
    run(1);
    TEST_ASSERT_EQUAL(OT_CONTROL_HA, out.state);

    cfg.mode = OT_CONTROL_MODE_LOCAL;   // stored
    step();                             // executor SEES the flip
    TEST_ASSERT_EQUAL(OT_CONTROL_LOCAL, out.state);

    cfg.mode = OT_CONTROL_MODE_HA;      // fx_cfg's snapshot says HA again -- stale vs. ctl.seen_ha
    const ot_mqtt_verdict_t v = send("ch_enable", "1", false);
    TEST_ASSERT_FALSE(v.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OWNED_BY_LOCAL), v.reason);
}

// dhw_setpoint is stored as 0 (unset) or 0.1...90.0 C, with no range of its own.
// The boiler's ID 48 bounds replace the table's, and ot_command_check.c's own note that
// dhw_setpoint, unlike ch_setpoint, KEEPS the unsupported-boiler check. Three worlds an HA client
// can find the boiler in, each with a different answer for the same command.
static void test_dhw_setpoint_bounds_in_three_worlds(void)
{
    default_cfg(OT_CONTROL_MODE_HA);
    ot_control_init(&ctl, &cfg, NULL, now);
    step();

    // World 1: ID 48 never read -- the registry table's bounds (30..80) apply.
    TEST_ASSERT_TRUE(send("dhw_setpoint", "55", false).accepted);
    const ot_mqtt_verdict_t low = send("dhw_setpoint", "20", false);
    TEST_ASSERT_FALSE(low.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OUT_OF_RANGE), low.reason);

    // World 2: ID 48 read -- the boiler's own bounds (35..60) replace the table's. hi = HB (max),
    // lo = LB (min) -- ot_state.c: "for ID 48 and 49 the upper bound is in the high byte".
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, ((uint16_t)60 << 8) | (uint16_t)35, now);
    TEST_ASSERT_TRUE(send("dhw_setpoint", "40", false).accepted);
    const ot_mqtt_verdict_t out_of = send("dhw_setpoint", "70", false);
    TEST_ASSERT_FALSE(out_of.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_OUT_OF_RANGE), out_of.reason);

    // World 3: ID 56 marked unsupported -- refused UNSUPPORTED_BY_BOILER (unlike ch_setpoint,
    // dhw_setpoint does not skip this check; the exemption is only for the executor's own ID 1 re-send).
    ot_state_apply_dataid(56, OT_MSG_UNKNOWN_DATAID, 0, now);
    ot_state_apply_dataid(56, OT_MSG_UNKNOWN_DATAID, 0, now + 1000);
    const ot_mqtt_verdict_t v = send("dhw_setpoint", "50", false);
    TEST_ASSERT_FALSE(v.accepted);
    TEST_ASSERT_EQUAL_STRING(ot_command_strerror(OT_CMD_UNSUPPORTED_BY_BOILER), v.reason);
}

// the mode, turning the season on, the bounds and the watchdog are
// not reachable over MQTT at all, and Home Assistant drives the executor, never the bus.
// Every writable frame row (the six test_ot_command_check.cpp enumerates), through the REAL MQTT
// decode-and-handle path, in both modes -- and fx->write must never fire for any of them.
static void test_every_writable_frame_row_is_refused_over_mqtt_and_write_is_never_called(void)
{
    static const char *const rows[][2] = {
        {"max_relative_modulation", "50"}, {"room_setpoint", "21.5"},
        {"room_temperature", "20"},        {"max_ch_setpoint", "60"},
        {"master_ot_version", "2.2"},      {"master_product_version", "1"},
    };
    for (unsigned m = 0; m < 2; m++) {
        default_cfg(m == 0 ? OT_CONTROL_MODE_LOCAL : OT_CONTROL_MODE_HA);
        ot_control_init(&ctl, &cfg, NULL, now);
        step();
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
    RUN_TEST(test_a_retained_command_after_a_restored_reboot_does_not_move_the_schedule);
    RUN_TEST(test_a_stale_ha_snapshot_is_overruled_by_the_executors_own_view);
    RUN_TEST(test_dhw_setpoint_bounds_in_three_worlds);
    RUN_TEST(test_every_writable_frame_row_is_refused_over_mqtt_and_write_is_never_called);
    return UNITY_END();
}
