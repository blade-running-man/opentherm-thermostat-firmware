// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_command_check(): every refusal of every writer, with an origin.
//
// A suite of its own rather than more of test_ot_command: that one pins the frame encoder, this
// one pins the translation of ownership, and the two answer to different rules.
// It links the real ot_control, so every ownership verdict below is ot_control_check()'s; what is
// tested HERE is that ot_command_check() hands it the right command, value, origin and
// configuration, maps its answer, and runs the boiler's own checks first.
//
// The order of refusals is ONE rule, stated once in ot_command.h (ot_command_check()'s contract):
// what is wrong with the request whoever sends it, then ownership, then the executor's own bounds.
// test_the_order_of_refusals_is_one_rule pins one cell of each step through this entry point.
// DO NOT grow that into ot_control_check()'s truth table: the ownership-versus-own-bounds half is
// test_ot_control_check's (test_ownership_is_answered_before_the_value), and two cell-by-cell
// copies of one order are two places for it to diverge.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "ot_command.h"
#include "ot_control.h"
#include "ot_registry.h"
#include "ot_state.h"

void setUp(void) { ot_state_reset(); }
void tearDown(void) {}

// The configuration the task layer builds, with the store's unset defaults. Only the mode and the flow
// bounds matter to ot_control_check(); the rest is filled so that no field is garbage.
static ot_control_cfg_t cfg_for(ot_control_mode_t mode)
{
    ot_control_cfg_t c;
    memset(&c, 0, sizeof c);
    c.mode                    = mode;
    c.heating_season          = true;
    c.watchdog_s              = 900;
    c.failsafe_setpoint_dc    = 450;
    c.failsafe_room_target_dc = 180;
    c.failsafe_heat_days      = 3;
    c.failsafe_min_cycle_s    = 600;
    c.flow_min_dc             = 400;
    c.flow_max_dc             = 700;
    c.local_ch_setpoint_dc    = 450;
    c.dhw_enable              = true;
    return c;
}

// Flow bounds that span the whole int16_t, so that ot_control accepts every setpoint and the
// dc value ot_command_check() produced can be read back from out.value -- negative ones, zero
// and the int16_t edges included. ot_config would never hold this; ot_control_check() takes
// plain values and does not care.
static ot_control_cfg_t wide_cfg(void)
{
    ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    c.flow_min_dc      = INT16_MIN;
    c.flow_max_dc      = INT16_MAX;
    return c;
}

static void expect_refused(ot_command_err_t want, const char *key, float value,
                           ot_origin_t origin, const ot_control_cfg_t *cfg)
{
    ot_command_out_t out, before;
    memset(&out, 0xA5, sizeof out);
    memcpy(&before, &out, sizeof out);
    TEST_ASSERT_EQUAL_MESSAGE(want, ot_command_check(key, value, origin, cfg, &out), key);
    // On a refusal *out is untouched: the ot_command.h contract, byte for byte.
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &out, sizeof out, key);
}

static void expect_control(const char *key, float value, ot_origin_t origin,
                           const ot_control_cfg_t *cfg, ot_control_cmd_t cmd, int16_t want)
{
    ot_command_out_t out;
    memset(&out, 0xA5, sizeof out);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CMD_OK, ot_command_check(key, value, origin, cfg, &out), key);
    TEST_ASSERT_EQUAL_MESSAGE(OT_CMD_OUT_CONTROL, out.kind, key);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)cmd, (int)out.control, key);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(want, out.value, key);
    // The frame half of a CONTROL answer is zeroed, not left as the caller's garbage.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out.frame.data_id, key);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, out.frame.raw, key);
}

// The registry's control numbers are emitted by the generator from a Python mapping; the enum
// is ot_control.h's. Nothing but this test ties the two together: a drift would send a
// ch_enable write to the executor as a DHW_ENABLE, and every other test here would still pass.
static void test_every_control_row_names_the_command_of_its_key(void)
{
    static const struct {
        const char      *key;
        ot_control_cmd_t cmd;
    } want[] = {
        {"ch_enable", OT_CONTROL_CMD_CH_ENABLE},
        {"ch_setpoint", OT_CONTROL_CMD_CH_SETPOINT},
        {"dhw_enable", OT_CONTROL_CMD_DHW_ENABLE},
        {"dhw_setpoint", OT_CONTROL_CMD_DHW_SETPOINT},
        {"heating_season", OT_CONTROL_CMD_SEASON},
    };
    unsigned seen = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        if (e->control == 0)
            continue;
        seen++;
        bool known = false;
        for (unsigned w = 0; w < sizeof want / sizeof want[0]; w++) {
            if (strcmp(e->key, want[w].key) != 0)
                continue;
            known = true;
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)want[w].cmd, (int)e->control, e->key);
        }
        TEST_ASSERT_TRUE_MESSAGE(known, e->key);        // a control row nobody listed here
        TEST_ASSERT_TRUE_MESSAGE(e->writable, e->key);  // a control nobody can write
    }
    TEST_ASSERT_EQUAL_UINT(sizeof want / sizeof want[0], seen);
}

// For every row without a control the answer is ot_command_encode()'s -- in either mode, for the
// WEB. The values reach every refusal encode has -- read-only rows, synthetic rows, the table
// range, NaN, infinity, an unsupported ID -- and the frame on success.
//
// Home Assistant gets OT_CMD_NOT_FOR_HA instead, but only where the request was otherwise good:
// what is wrong with a value is answered whoever sends it, and only then who may send it (the one
// order, ot_command.h).
static void test_a_row_without_a_control_answers_as_ot_command_encode_for_the_web(void)
{
    ot_state_apply_dataid(57, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(57, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);

    const float values[] = {-40.0f, -1.0f, 0.0f, 1.0f, 21.5f, 55.0f, 95.0f, 258.0f,
                            1e6f,   NAN,   INFINITY, -INFINITY};
    const ot_origin_t       origins[] = {OT_ORIGIN_WEB, OT_ORIGIN_HA};
    const ot_control_mode_t modes[]   = {OT_CONTROL_MODE_LOCAL, OT_CONTROL_MODE_HA};

    unsigned frames = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        if (e->control != 0)
            continue;
        for (unsigned v = 0; v < sizeof values / sizeof values[0]; v++)
            for (unsigned o = 0; o < 2; o++)
                for (unsigned m = 0; m < 2; m++) {
                    const ot_control_cfg_t cfg = cfg_for(modes[m]);
                    ot_command_frame_t     f   = {.data_id = 0xEE, .raw = 0xBEEF};
                    const ot_command_err_t want = ot_command_encode(e->key, values[v], &f);
                    if (want != OT_CMD_OK) {
                        expect_refused(want, e->key, values[v], origins[o], &cfg);
                        continue;
                    }
                    if (origins[o] == OT_ORIGIN_HA) {
                        expect_refused(OT_CMD_NOT_FOR_HA, e->key, values[v], origins[o], &cfg);
                        continue;
                    }
                    ot_command_out_t out;
                    memset(&out, 0xA5, sizeof out);
                    TEST_ASSERT_EQUAL_MESSAGE(
                        OT_CMD_OK, ot_command_check(e->key, values[v], origins[o], &cfg, &out),
                        e->key);
                    TEST_ASSERT_EQUAL_MESSAGE(OT_CMD_OUT_FRAME, out.kind, e->key);
                    TEST_ASSERT_EQUAL_UINT8_MESSAGE(f.data_id, out.frame.data_id, e->key);
                    TEST_ASSERT_EQUAL_HEX16_MESSAGE(f.raw, out.frame.raw, e->key);
                    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)out.control, e->key);
                    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out.value, e->key);
                    frames++;
                }
    }
    // Not vacuous: some rows really did encode.
    TEST_ASSERT_TRUE(frames > 0);
}

// The same, spelled out for the one row a reader will look for: 21.5 °C = 0x1580 on ID 16, from
// the web, in either mode. Never from Home Assistant -- ID 16 is kept away from it.
static void test_room_setpoint_is_a_frame_for_the_web_and_never_for_home_assistant(void)
{
    const ot_control_cfg_t local = cfg_for(OT_CONTROL_MODE_LOCAL);
    const ot_control_cfg_t ha    = cfg_for(OT_CONTROL_MODE_HA);
    const ot_control_cfg_t *cfgs[] = {&local, &ha};
    for (unsigned c = 0; c < 2; c++) {
        ot_command_out_t out;
        TEST_ASSERT_EQUAL(OT_CMD_OK,
                          ot_command_check("room_setpoint", 21.5f, OT_ORIGIN_WEB, cfgs[c], &out));
        TEST_ASSERT_EQUAL(OT_CMD_OUT_FRAME, out.kind);
        TEST_ASSERT_EQUAL_UINT8(16, out.frame.data_id);
        TEST_ASSERT_EQUAL_HEX16(0x1580, out.frame.raw);
        expect_refused(OT_CMD_NOT_FOR_HA, "room_setpoint", 21.5f, OT_ORIGIN_HA, cfgs[c]);
    }
}

// Home Assistant drives the EXECUTOR, and no OpenTherm frame is its to send
// -- in EITHER mode, because the entity is nobody's and no change of owner cures
// the refusal. This is the row-by-row statement: every writable entity without a control command,
// with a value its codec and the table accept, so that what is refused is the origin and nothing
// else. Over MQTT this is the whole of what a broker client may not reach (test_ot_mqtt_control).
static void test_every_frame_row_is_refused_for_home_assistant_in_either_mode(void)
{
    static const struct {
        const char *key;
        float       value;
    } rows[] = {
        {"max_relative_modulation", 50.0f}, {"room_setpoint", 21.5f},
        {"room_temperature", 20.0f},        {"max_ch_setpoint", 60.0f},
        {"master_ot_version", 2.2f},        {"master_product_version", 1.0f},
    };
    const ot_control_cfg_t  local   = cfg_for(OT_CONTROL_MODE_LOCAL);
    const ot_control_cfg_t  ha      = cfg_for(OT_CONTROL_MODE_HA);
    const ot_control_cfg_t *cfgs[]  = {&local, &ha};

    // Every writable frame row is listed above, or a new one would reach Home Assistant untested.
    unsigned writable_frames = 0;
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const ot_entity_t *e = ot_registry_at(i);
        if (e->control != 0 || !e->writable)
            continue;
        writable_frames++;
        bool listed = false;
        for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++)
            listed = listed || strcmp(e->key, rows[r].key) == 0;
        TEST_ASSERT_TRUE_MESSAGE(listed, e->key);
    }
    TEST_ASSERT_EQUAL_UINT(sizeof rows / sizeof rows[0], writable_frames);

    for (unsigned r = 0; r < sizeof rows / sizeof rows[0]; r++)
        for (unsigned c = 0; c < 2; c++) {
            ot_command_out_t out;
            TEST_ASSERT_EQUAL_MESSAGE(
                OT_CMD_OK,
                ot_command_check(rows[r].key, rows[r].value, OT_ORIGIN_WEB, cfgs[c], &out),
                rows[r].key);
            TEST_ASSERT_EQUAL_MESSAGE(OT_CMD_OUT_FRAME, out.kind, rows[r].key);
            expect_refused(OT_CMD_NOT_FOR_HA, rows[r].key, rows[r].value, OT_ORIGIN_HA, cfgs[c]);
        }

    // The order holds here as everywhere: the request's own faults first, the origin after.
    expect_refused(OT_CMD_OUT_OF_RANGE, "max_ch_setpoint", 1e6f, OT_ORIGIN_HA, &ha);
    expect_refused(OT_CMD_NOT_WRITABLE, "flow_temperature", 50.0f, OT_ORIGIN_HA, &ha);
    // A sentence of its own: the line a refused broker client puts in GET /api/log must say why.
    TEST_ASSERT_EQUAL_STRING("this entity is not Home Assistant's to write",
                             ot_command_strerror(OT_CMD_NOT_FOR_HA));
}

static void test_local_mode_accepts_every_control_from_the_web(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_control("ch_enable", 1.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_ENABLE, 1);
    expect_control("ch_enable", 0.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_ENABLE, 0);
    expect_control("ch_setpoint", 45.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 450);
    expect_control("dhw_enable", 0.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_DHW_ENABLE, 0);
    expect_control("dhw_setpoint", 55.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_DHW_SETPOINT, 550);
    expect_control("heating_season", 1.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_SEASON, 1);
    expect_control("heating_season", 0.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_SEASON, 0);
}

static void test_local_mode_refuses_home_assistant_on_every_owned_command(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_refused(OT_CMD_OWNED_BY_LOCAL, "ch_enable", 1.0f, OT_ORIGIN_HA, &c);
    expect_refused(OT_CMD_OWNED_BY_LOCAL, "ch_setpoint", 45.0f, OT_ORIGIN_HA, &c);
    expect_refused(OT_CMD_OWNED_BY_LOCAL, "dhw_enable", 1.0f, OT_ORIGIN_HA, &c);
    expect_refused(OT_CMD_OWNED_BY_LOCAL, "dhw_setpoint", 55.0f, OT_ORIGIN_HA, &c);
    // In LOCAL mode even HA's season "off" is refused: every HA command is OWNED_BY_LOCAL.
    expect_refused(OT_CMD_OWNED_BY_LOCAL, "heating_season", 0.0f, OT_ORIGIN_HA, &c);
}

static void test_ha_mode_refuses_the_web_on_every_owned_command(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_HA);
    expect_refused(OT_CMD_OWNED_BY_HA, "ch_enable", 1.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OWNED_BY_HA, "ch_setpoint", 45.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OWNED_BY_HA, "dhw_enable", 1.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OWNED_BY_HA, "dhw_setpoint", 55.0f, OT_ORIGIN_WEB, &c);
    // The season is not one of them: the web sets it either way in either mode.
    expect_control("heating_season", 1.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_SEASON, 1);
    expect_control("heating_season", 0.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_SEASON, 0);
}

static void test_ha_mode_accepts_every_owned_command_from_home_assistant(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_HA);
    expect_control("ch_enable", 1.0f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_CH_ENABLE, 1);
    expect_control("ch_setpoint", 52.5f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_CH_SETPOINT, 525);
    expect_control("dhw_enable", 1.0f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_DHW_ENABLE, 1);
    expect_control("dhw_setpoint", 48.0f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_DHW_SETPOINT, 480);
}

// HA over MQTT may turn the season off, never on -- the season is the person's "off".
static void test_home_assistant_may_turn_the_season_off_but_never_on(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_HA);
    expect_control("heating_season", 0.0f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_SEASON, 0);
    expect_refused(OT_CMD_SEASON_ON_IS_LOCAL, "heating_season", 1.0f, OT_ORIGIN_HA, &c);
}

// 0.5 is neither on nor off. Rounding it would turn a malformed request into a guess.
static void test_a_switch_value_must_be_a_whole_number(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", 0.5f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", 0.999f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_enable", 0.25f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "heating_season", 1.5f, OT_ORIGIN_WEB, &c);
}

// A whole number other than 0 or 1 is ot_control's refusal; it arrives there unscaled.
static void test_a_whole_switch_value_other_than_zero_or_one_is_refused(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", 2.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", -1.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_enable", 10.0f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "heating_season", 3.0f, OT_ORIGIN_WEB, &c);
}

// Half away from zero, at the .x5 boundary where a float sits just BELOW the decimal the client
// wrote (45.05 is stored as 45.0499992). The answer is the decimal's, not the float's.
//
// This is the EARLY answer, and 451 is not what gets stored: ot_control_apply() quantises a CH
// setpoint to 5 dc (0.5 degrees) before it persists it, so 45.05 is persisted as 450.
// This suite pins the conversion; the quantisation is ot_control's, and test_ot_control's to pin.
static void test_a_ch_setpoint_is_rounded_to_tenths_half_away_from_zero(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_control("ch_setpoint", 45.04f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 450);
    expect_control("ch_setpoint", 45.05f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 451);
    expect_control("ch_setpoint", 45.06f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 451);
    expect_control("ch_setpoint", 45.15f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 452);
    expect_control("ch_setpoint", 44.95f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 450);
    expect_control("ch_setpoint", 69.95f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 700);
}

static void test_rounding_is_half_away_from_zero_below_zero_too(void)
{
    const ot_control_cfg_t c = wide_cfg();
    expect_control("ch_setpoint", -0.04f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 0);
    expect_control("ch_setpoint", -0.05f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -1);
    expect_control("ch_setpoint", -0.5f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -5);
    expect_control("ch_setpoint", -12.34f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -123);
    expect_control("ch_setpoint", -12.35f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -124);
    expect_control("ch_setpoint", 20.05f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 201);
}

// The bounds are ot_control's and are applied to the ROUNDED value: 39.95 is 400 dc, inside.
static void test_the_flow_bounds_apply_to_the_rounded_value(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_control("ch_setpoint", 40.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 400);
    expect_control("ch_setpoint", 39.95f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 400);
    expect_control("ch_setpoint", 70.04f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 700);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 39.94f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 70.05f, OT_ORIGIN_WEB, &c);
}

// The cast to int16_t is undefined outside it: a million degrees is a refusal, not a wrap into
// something ot_control would accept.
static void test_a_value_with_no_int16_representation_is_refused(void)
{
    const ot_control_cfg_t c = wide_cfg();
    expect_control("ch_setpoint", 3276.7f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 32767);
    expect_control("ch_setpoint", -3276.8f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -32768);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 3276.8f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", -3276.9f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 1e6f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", -1e6f, OT_ORIGIN_WEB, &c);
    // 6553.6 would wrap to 0 through a careless cast, and 0 is inside these bounds.
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 6553.6f, OT_ORIGIN_WEB, &c);
}

// The int16_t guard at its exact edges -- a contract, even though real flow bounds never come
// near it. The product value * 10.0f is rounded to float, so no decimal literal names the edge:
// these are the two adjacent floats on either side of it, and the preconditions prove they are.
// One float past the edge lroundf gives 32768 / -32769, which the cast wraps to -32768 / 32767 --
// both inside these bounds, so a guard off by one float would ACCEPT, not refuse.
static void test_the_int16_guard_refuses_exactly_past_its_edges(void)
{
    const float hi_last = 3276.74976f, hi_first = 3276.75f;
    const float lo_last = -3276.84961f, lo_first = -3276.84985f;
    TEST_ASSERT_TRUE(nextafterf(hi_last, INFINITY) == hi_first);
    TEST_ASSERT_TRUE(nextafterf(lo_last, -INFINITY) == lo_first);
    TEST_ASSERT_TRUE(hi_last * 10.0f < 32767.5f && hi_first * 10.0f == 32767.5f);
    TEST_ASSERT_TRUE(lo_last * 10.0f > -32768.5f && lo_first * 10.0f == -32768.5f);

    const ot_control_cfg_t c = wide_cfg();
    expect_control("ch_setpoint", hi_last, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, 32767);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", hi_first, OT_ORIGIN_WEB, &c);
    expect_control("ch_setpoint", lo_last, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_CH_SETPOINT, -32768);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", lo_first, OT_ORIGIN_WEB, &c);
}

// NaN and infinity on every control row, with bounds that contain 0: lroundf(NaN) is 0 on the
// hosts measured, and 0 dc is exactly what a missing check would let through.
static void test_nan_and_infinity_are_refused_on_every_control_row(void)
{
    const ot_control_cfg_t c      = wide_cfg();
    const char *const      keys[] = {"ch_enable", "ch_setpoint", "dhw_enable", "dhw_setpoint",
                                     "heating_season"};
    for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        expect_refused(OT_CMD_OUT_OF_RANGE, keys[k], NAN, OT_ORIGIN_WEB, &c);
        expect_refused(OT_CMD_OUT_OF_RANGE, keys[k], INFINITY, OT_ORIGIN_WEB, &c);
        expect_refused(OT_CMD_OUT_OF_RANGE, keys[k], -INFINITY, OT_ORIGIN_WEB, &c);
    }
}

// The first step of the rule in ot_command.h: dhw_setpoint meets the boiler's own ID 48
// bounds BEFORE ownership. The boiler's refusal holds whoever sends the value, in either mode, so
// no change of owner cures it and it is answered first -- a 422, not a 409 inviting a mode switch
// that would only be refused again -- and the executor never persists a DHW value the boiler
// would refuse. ID 48 = 0x4128: ceiling 65, floor 40.
static void test_dhw_setpoint_meets_the_boilers_id_48_bounds_before_ownership(void)
{
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4128, 1000);

    const ot_control_cfg_t ha = cfg_for(OT_CONTROL_MODE_HA);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 70.0f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 38.0f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OWNED_BY_HA, "dhw_setpoint", 60.0f, OT_ORIGIN_WEB, &ha);
    expect_control("dhw_setpoint", 60.0f, OT_ORIGIN_HA, &ha, OT_CONTROL_CMD_DHW_SETPOINT, 600);
}

// The boiler's bounds are checked on the value that will be PERSISTED, the rounded dc, not on the
// float: 65.04 becomes 650 dc = 65.0 °C and is accepted; 65.05 becomes 651 dc and is not.
static void test_dhw_bounds_are_checked_on_the_rounded_value(void)
{
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4128, 1000);

    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_control("dhw_setpoint", 65.0f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_DHW_SETPOINT, 650);
    expect_control("dhw_setpoint", 65.04f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_DHW_SETPOINT, 650);
    expect_control("dhw_setpoint", 39.95f, OT_ORIGIN_WEB, &c, OT_CONTROL_CMD_DHW_SETPOINT, 400);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 65.05f, OT_ORIGIN_WEB, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 39.94f, OT_ORIGIN_WEB, &c);
}

// Before the boiler has spoken, the table's 30..80 -- also with the owner matching, so the
// refusal cannot be mistaken for an ownership one.
static void test_dhw_setpoint_meets_the_table_range_before_the_boiler_has_spoken(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_HA);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 85.0f, OT_ORIGIN_HA, &c);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 29.9f, OT_ORIGIN_HA, &c);
    expect_control("dhw_setpoint", 80.0f, OT_ORIGIN_HA, &c, OT_CONTROL_CMD_DHW_SETPOINT, 800);
}

// A boiler's refusal comes before ownership (the rule in ot_command.h): dhw_setpoint on an ID 56
// the boiler answered unknown-dataid twice is refused whoever asks, in either mode.
static void test_an_unsupported_data_id_is_refused_before_ownership(void)
{
    ot_state_apply_dataid(56, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(56, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);

    const ot_control_cfg_t ha    = cfg_for(OT_CONTROL_MODE_HA);
    const ot_control_cfg_t local = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_refused(OT_CMD_UNSUPPORTED_BY_BOILER, "dhw_setpoint", 55.0f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_UNSUPPORTED_BY_BOILER, "dhw_setpoint", 55.0f, OT_ORIGIN_HA, &ha);
    expect_refused(OT_CMD_UNSUPPORTED_BY_BOILER, "dhw_setpoint", 55.0f, OT_ORIGIN_HA, &local);
}

// ch_setpoint ignores ID 1's "unsupported" flag. The flag never clears, and the replies
// to the executor's own 10 s re-sends of ID 1 reach the state model like any other
// (src/main.cpp): two stray UNKNOWN-DATAID answers would freeze the setpoint until a reboot,
// while the executor went on sending ID 1 with the old value anyway.
static void test_ch_setpoint_ignores_the_sticky_unsupported_flag_of_id_1(void)
{
    ot_state_apply_dataid(1, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(1, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);
    TEST_ASSERT_TRUE(ot_state_is_unsupported(1)); // precondition: the flag really is up

    const ot_control_cfg_t ha    = cfg_for(OT_CONTROL_MODE_HA);
    const ot_control_cfg_t local = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_control("ch_setpoint", 45.0f, OT_ORIGIN_HA, &ha, OT_CONTROL_CMD_CH_SETPOINT, 450);
    expect_control("ch_setpoint", 45.0f, OT_ORIGIN_WEB, &local, OT_CONTROL_CMD_CH_SETPOINT, 450);
    // Not a bypass: the flag is ignored, ownership and the flow bounds are not.
    expect_refused(OT_CMD_OWNED_BY_HA, "ch_setpoint", 45.0f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 99.0f, OT_ORIGIN_WEB, &local);
    // A synthetic row has no Data-ID to be unsupported: the CH switch is untouched too.
    expect_control("ch_enable", 1.0f, OT_ORIGIN_WEB, &local, OT_CONTROL_CMD_CH_ENABLE, 1);
}

// ONE RULE (ot_command.h): what is wrong with the request whoever sends it -- its representation
// and the boiler's own refusals -- first; then ownership; then the executor's own bounds. A web
// write in HA mode is refused by ownership whatever the value, so every cell here that is not a
// 409 is a refusal that must come before it. The review found these cells stated two ways.
static void test_the_order_of_refusals_is_one_rule(void)
{
    const ot_control_cfg_t ha = cfg_for(OT_CONTROL_MODE_HA);
    // 1. Representation: no owner makes 0.5 a switch value or a million degrees an int16_t.
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", 0.5f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", NAN, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 1e6f, OT_ORIGIN_WEB, &ha);
    // 2. The boiler's bounds: the table's 30..80 until ID 48 has spoken.
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 99.0f, OT_ORIGIN_WEB, &ha);
    // 3. Ownership, before the executor's bounds: 2 is no switch value and 99 is above flow_max,
    //    but those are the owner's to learn.
    expect_refused(OT_CMD_OWNED_BY_HA, "ch_enable", 2.0f, OT_ORIGIN_WEB, &ha);
    expect_refused(OT_CMD_OWNED_BY_HA, "ch_setpoint", 99.0f, OT_ORIGIN_WEB, &ha);
    // 4. The owner meets them.
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_enable", 2.0f, OT_ORIGIN_HA, &ha);
    expect_refused(OT_CMD_OUT_OF_RANGE, "ch_setpoint", 99.0f, OT_ORIGIN_HA, &ha);
}

// ot_control's "0 is unset" guard is reachable through this entry point only when the boiler
// names a DHW floor of 0: ID 48 then lets 0 through, and the guard is the last word. ID 48 = 0x4100:
// ceiling 65, floor 0. 0.04 rounds to 0 dc and is refused with it; 0.05 rounds to 1 dc and is
// accepted, which proves the floor in force is the boiler's 0, not the table's 30.
static void test_a_dhw_floor_of_zero_still_refuses_the_unset_value(void)
{
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, 0x4100, 1000);

    const ot_control_cfg_t local = cfg_for(OT_CONTROL_MODE_LOCAL);
    const ot_control_cfg_t ha    = cfg_for(OT_CONTROL_MODE_HA);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 0.0f, OT_ORIGIN_WEB, &local);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 0.04f, OT_ORIGIN_WEB, &local);
    expect_refused(OT_CMD_OUT_OF_RANGE, "dhw_setpoint", 0.0f, OT_ORIGIN_HA, &ha);
    expect_control("dhw_setpoint", 0.05f, OT_ORIGIN_WEB, &local, OT_CONTROL_CMD_DHW_SETPOINT, 1);
}

static void test_unknown_and_read_only_keys_answer_as_before_for_either_origin(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_HA);
    const ot_origin_t origins[] = {OT_ORIGIN_WEB, OT_ORIGIN_HA};
    for (unsigned o = 0; o < 2; o++) {
        expect_refused(OT_CMD_UNKNOWN_KEY, "no_such", 1.0f, origins[o], &c);
        expect_refused(OT_CMD_UNKNOWN_KEY, NULL, 1.0f, origins[o], &c);
        expect_refused(OT_CMD_NOT_WRITABLE, "flow_temperature", 50.0f, origins[o], &c);
        expect_refused(OT_CMD_NOT_WRITABLE, "control_mode", 1.0f, origins[o], &c);
        expect_refused(OT_CMD_NOT_WRITABLE, "control_state", 0.0f, origins[o], &c);
    }
}

// A NULL configuration or output is a caller's defect, answered like ot_command_encode()'s NULL
// output: a refusal, never a dereference -- for a frame row as well, so that the contract has one
// shape.
static void test_a_null_configuration_or_output_is_refused(void)
{
    const ot_control_cfg_t c = cfg_for(OT_CONTROL_MODE_LOCAL);
    expect_refused(OT_CMD_UNKNOWN_KEY, "ch_enable", 1.0f, OT_ORIGIN_WEB, NULL);
    expect_refused(OT_CMD_UNKNOWN_KEY, "room_setpoint", 20.0f, OT_ORIGIN_WEB, NULL);
    TEST_ASSERT_EQUAL(OT_CMD_UNKNOWN_KEY,
                      ot_command_check("ch_enable", 1.0f, OT_ORIGIN_WEB, &c, NULL));
    TEST_ASSERT_EQUAL(OT_CMD_UNKNOWN_KEY,
                      ot_command_check("room_setpoint", 20.0f, OT_ORIGIN_WEB, &c, NULL));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_control_row_names_the_command_of_its_key);
    RUN_TEST(test_a_row_without_a_control_answers_as_ot_command_encode_for_the_web);
    RUN_TEST(test_room_setpoint_is_a_frame_for_the_web_and_never_for_home_assistant);
    RUN_TEST(test_every_frame_row_is_refused_for_home_assistant_in_either_mode);
    RUN_TEST(test_local_mode_accepts_every_control_from_the_web);
    RUN_TEST(test_local_mode_refuses_home_assistant_on_every_owned_command);
    RUN_TEST(test_ha_mode_refuses_the_web_on_every_owned_command);
    RUN_TEST(test_ha_mode_accepts_every_owned_command_from_home_assistant);
    RUN_TEST(test_home_assistant_may_turn_the_season_off_but_never_on);
    RUN_TEST(test_a_switch_value_must_be_a_whole_number);
    RUN_TEST(test_a_whole_switch_value_other_than_zero_or_one_is_refused);
    RUN_TEST(test_a_ch_setpoint_is_rounded_to_tenths_half_away_from_zero);
    RUN_TEST(test_rounding_is_half_away_from_zero_below_zero_too);
    RUN_TEST(test_the_flow_bounds_apply_to_the_rounded_value);
    RUN_TEST(test_a_value_with_no_int16_representation_is_refused);
    RUN_TEST(test_the_int16_guard_refuses_exactly_past_its_edges);
    RUN_TEST(test_nan_and_infinity_are_refused_on_every_control_row);
    RUN_TEST(test_dhw_setpoint_meets_the_boilers_id_48_bounds_before_ownership);
    RUN_TEST(test_dhw_bounds_are_checked_on_the_rounded_value);
    RUN_TEST(test_dhw_setpoint_meets_the_table_range_before_the_boiler_has_spoken);
    RUN_TEST(test_an_unsupported_data_id_is_refused_before_ownership);
    RUN_TEST(test_ch_setpoint_ignores_the_sticky_unsupported_flag_of_id_1);
    RUN_TEST(test_the_order_of_refusals_is_one_rule);
    RUN_TEST(test_a_dhw_floor_of_zero_still_refuses_the_unset_value);
    RUN_TEST(test_unknown_and_read_only_keys_answer_as_before_for_either_origin);
    RUN_TEST(test_a_null_configuration_or_output_is_refused);
    return UNITY_END();
}
