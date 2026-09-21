// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The boost as ladder row 2.
//
// What changed in the port: validation moved in from the HTTP glue (mode, season, flow bounds);
// the boost's own 30 s re-send is gone, subsumed by the held-ID-1 cadence; and expiry is
// observed through step(), which is where it now happens.
//
// 480, 481, 60000 and 59999 are literals on purpose: a boundary computed from the constant it
// guards survives any change to that constant, and the eight-hour cap is the contract.
#include <initializer_list>
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_control.h"
}

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T0  = 1000000u;
static const uint32_t MIN = 60000u;

struct H {
  ot_control_t     c;
  ot_control_cfg_t cfg;
  ot_control_in_t  in;
  ot_control_out_t out;
  uint32_t         now;
};

static ot_control_cfg_t cfg_in(ot_control_mode_t mode) {
  ot_control_cfg_t k = {};
  k.mode = mode;
  k.heating_season = true;
  k.watchdog_s = 900;
  k.failsafe_setpoint_dc = 450;
  k.failsafe_room_target_dc = 180;
  k.failsafe_heat_days = 3;
  k.failsafe_min_cycle_s = 600;
  k.flow_min_dc = 400;
  k.flow_max_dc = 700;
  k.local_ch_setpoint_dc = 550;
  k.dhw_enable = true;
  return k;
}

static H local_at(uint32_t start = T0) {
  H h;
  memset(&h, 0, sizeof h);
  h.cfg = cfg_in(OT_CONTROL_MODE_LOCAL);
  h.now = start;
  ot_control_init(&h.c, &h.cfg, nullptr, start);
  return h;
}

// A step at an absolute moment, on an ideal bus.
static void step_at(H &h, uint32_t now) {
  h.in.setpoint_confirmed = h.out.send_setpoint;
  h.in.confirmed_dc = h.out.held_setpoint_dc;
  h.now = now;
  ot_control_step(&h.c, &h.cfg, &h.in, h.now, &h.out);
}

static ot_control_err_t start(H &h, int16_t sp, uint32_t minutes, uint32_t now) {
  return ot_control_boost_start(&h.c, &h.cfg, sp, minutes, now);
}

static bool ch(const H &h) { return (h.out.status_high & OT_STATUS_CH_ENABLE) != 0; }

#define ASSERT_STATE(s, h) TEST_ASSERT_EQUAL_STRING(ot_control_state_name(s), ot_control_state_name((h).out.state))

// === group: start ===

void test_a_fresh_executor_has_no_boost(void) {
  H h = local_at();
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&h.c, T0));
  TEST_ASSERT_EQUAL_INT16(0, ot_control_boost_setpoint_dc(&h.c));
  step_at(h, T0 + 1000);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
}

void test_one_minute_and_eight_hours_are_both_accepted(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 500, 1, T0));
  TEST_ASSERT_EQUAL_UINT32(60, ot_control_boost_remaining_s(&h.c, T0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 500, 480, T0));
  TEST_ASSERT_EQUAL_UINT32(480u * 60u, ot_control_boost_remaining_s(&h.c, T0));
  TEST_ASSERT_EQUAL_INT16(500, ot_control_boost_setpoint_dc(&h.c));
}

void test_zero_minutes_is_refused_there_is_no_forever(void) {
  // "Forever" is local_ch_enable. A boost of 0 minutes is not a way to spell it.
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_BAD_MINUTES, start(h, 500, 0, T0));
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_one_minute_over_eight_hours_is_refused(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_BAD_MINUTES, start(h, 500, 481, T0));
  // Refused BEFORE it is multiplied into milliseconds: UINT32_MAX * 60000 wraps to a small,
  // plausible-looking duration.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_BAD_MINUTES, start(h, 500, UINT32_MAX, T0));
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_a_boost_is_refused_in_ha_mode(void) {
  // LOCAL only [OWNER]: in HA mode a boost is a web write like any other, and HA owns CH.
  H h = local_at();
  h.cfg.mode = OT_CONTROL_MODE_HA;
  step_at(h, T0 + 1000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA, start(h, 500, 60, T0 + 1000));
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_a_boost_asked_for_with_a_stale_local_snapshot_is_refused(void) {
  // The request's snapshot predates LOCAL -> HA, which the executor has already stepped on.
  // Accepted, it would survive HA mode and come back with LOCAL, heating for up to eight hours.
  H h = local_at();
  h.cfg.mode = OT_CONTROL_MODE_HA;
  step_at(h, T0 + 1000);
  ot_control_cfg_t stale = h.cfg;
  stale.mode = OT_CONTROL_MODE_LOCAL;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA,
                        ot_control_boost_start(&h.c, &stale, 650, 480, T0 + 1500));
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  step_at(h, T0 + 2000);
  step_at(h, T0 + 3000);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "a boost accepted in HA mode came back with LOCAL");
}

void test_a_boost_is_refused_with_the_season_off(void) {
  // Refused with a reason rather than accepted as a no-op: a running boost on the panel
  // over a cold boiler is the lie the refusal prevents. The season is the one the executor saw,
  // so a snapshot from before the season went off does not get a boost in either.
  H h = local_at();
  h.cfg.heating_season = false;
  step_at(h, T0 + 1000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_SEASON_IS_OFF, start(h, 500, 60, T0 + 1000));
  ot_control_cfg_t stale = h.cfg;
  stale.heating_season = true;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_SEASON_IS_OFF, ot_control_boost_start(&h.c, &stale, 500, 60, T0 + 1000));
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_a_setpoint_outside_the_flow_bounds_is_refused_and_the_bounds_are_inclusive(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, start(h, 399, 60, T0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, start(h, 701, 60, T0));
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 400, 60, T0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 700, 60, T0));
}

void test_the_refusals_are_answered_in_order(void) {
  // Ownership, then the season, then the duration, then the value -- the most fundamental "no"
  // first, so the answer never invites fixing something that would still be refused.
  H h = local_at();
  h.cfg.mode = OT_CONTROL_MODE_HA;
  h.cfg.heating_season = false;
  step_at(h, T0 + 1000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA, start(h, 9999, 0, T0 + 1000));
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  step_at(h, T0 + 2000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_SEASON_IS_OFF, start(h, 9999, 0, T0 + 2000));
  h.cfg.heating_season = true;
  step_at(h, T0 + 3000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_BAD_MINUTES, start(h, 9999, 0, T0 + 3000));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, start(h, 9999, 60, T0 + 3000));
}

void test_a_refused_start_leaves_the_running_boost_alone(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 500, 60, T0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_BAD_MINUTES, start(h, 450, 0, T0 + MIN));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, start(h, 900, 30, T0 + MIN));
  TEST_ASSERT_TRUE(ot_control_boost_active(&h.c));
  TEST_ASSERT_EQUAL_INT16(500, ot_control_boost_setpoint_dc(&h.c));
  TEST_ASSERT_EQUAL_UINT32(3540, ot_control_boost_remaining_s(&h.c, T0 + MIN));
}

// === group: row 2 ===

void test_a_boost_holds_its_setpoint_on_id1_and_raises_ch(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  step_at(h, T0 + 1000);
  step_at(h, T0 + 2000);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  TEST_ASSERT_EQUAL_INT16(650, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE(ch(h));
}

void test_the_boost_ends_at_its_deadline_exactly(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 1, T0));
  step_at(h, T0 + 59999);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  step_at(h, T0 + 60000);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_a_boost_never_stepped_before_its_deadline_is_not_resurrected(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 1, T0));
  step_at(h, T0 + 5 * MIN);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_FALSE(ch(h));
}

void test_expiry_hands_the_bit_back_to_local(void) {
  // With local_ch_enable on, heat continues -- now at LOCAL's setpoint, which the held ID 1 of
  // The held ID 1 re-asserts; the boost's 65.0 no longer lingers on the boiler.
  for (bool local_on : {true, false}) {
    H h = local_at();
    h.cfg.local_ch_enable = local_on;
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 1, T0));
    step_at(h, T0 + 1000);
    step_at(h, T0 + 2000);
    TEST_ASSERT_TRUE(ch(h));
    step_at(h, T0 + 60000);
    step_at(h, T0 + 61000);
    ASSERT_STATE(OT_CONTROL_LOCAL, h);
    TEST_ASSERT_EQUAL(local_on, ch(h));
    TEST_ASSERT_EQUAL_INT16(550, h.out.held_setpoint_dc);
  }
}

void test_a_switch_to_ha_mode_ends_the_boost(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  step_at(h, T0 + 1000);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  step_at(h, T0 + 2000);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;   // and it does not come back with LOCAL
  step_at(h, T0 + 3000);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
}

void test_the_boost_has_no_resend_of_its_own(void) {
  // Its 30 s re-send is subsumed: ID 1 is asked for every 10 s after the last
  // confirmation, in every state, the boost included.
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  step_at(h, T0 + 1000);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step_at(h, T0 + 2000);   // confirmed here
  TEST_ASSERT_FALSE(h.out.send_setpoint);
  step_at(h, T0 + 11999);
  TEST_ASSERT_FALSE(h.out.send_setpoint);
  step_at(h, T0 + 12000);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
}

// === group: time ===

void test_the_millisecond_wrap_does_not_end_a_boost_early(void) {
  // uint32 milliseconds wrap after 49.7 days. Ten seconds before the wrap, the deadline lands
  // after it, numerically SMALLER than now: `now >= deadline` would end it on the first step.
  const uint32_t t = UINT32_MAX - 10000u;
  H h = local_at(t);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 1, t));
  step_at(h, t + 1);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  TEST_ASSERT_EQUAL_UINT32(50, ot_control_boost_remaining_s(&h.c, t + 10000u));
  step_at(h, t + 59999u);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  step_at(h, t + 60000u);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
}

void test_a_new_start_replaces_the_boost(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  step_at(h, T0 + 1000);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 600, 30, T0 + 10000));
  TEST_ASSERT_EQUAL_INT16(600, ot_control_boost_setpoint_dc(&h.c));
  TEST_ASSERT_EQUAL_UINT32(30u * 60u, ot_control_boost_remaining_s(&h.c, T0 + 10000));
  step_at(h, T0 + 10000);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_setpoint, "the new setpoint must not wait");
  step_at(h, T0 + 10000 + 30 * MIN - 1);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  step_at(h, T0 + 10000 + 30 * MIN);   // the fresh deadline, not the old one
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
}

void test_cancel_ends_the_boost_on_the_next_step(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  step_at(h, T0 + 1000);
  ot_control_boost_cancel(&h.c);
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
  TEST_ASSERT_EQUAL_INT16(0, ot_control_boost_setpoint_dc(&h.c));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&h.c, T0 + 2000));
  step_at(h, T0 + 2000);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  ot_control_boost_cancel(&h.c);   // harmless on nothing
  TEST_ASSERT_FALSE(ot_control_boost_active(&h.c));
}

void test_remaining_seconds_round_up_and_reach_zero_only_at_the_deadline(void) {
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 60, T0));
  TEST_ASSERT_EQUAL_UINT32(3600, ot_control_boost_remaining_s(&h.c, T0));
  TEST_ASSERT_EQUAL_UINT32(3540, ot_control_boost_remaining_s(&h.c, T0 + MIN));
  TEST_ASSERT_EQUAL_UINT32(1, ot_control_boost_remaining_s(&h.c, T0 + 60 * MIN - 1));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&h.c, T0 + 60 * MIN));
  TEST_ASSERT_TRUE_MESSAGE(ot_control_boost_active(&h.c), "reading the time left ended the boost");
}

void test_remaining_is_zero_past_a_deadline_no_step_has_expired(void) {
  // Between the deadline and the step that ends it the flag still says active, and a reader asks
  // in that window. expires - now is then negative -- as uint32, a seven-week boost on the panel.
  H h = local_at();
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(h, 650, 1, T0));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&h.c, T0 + MIN + 1));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&h.c, T0 + 5 * MIN));
  const uint32_t t = UINT32_MAX - 10000u;
  H w = local_at(t);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(w, 650, 1, t));
  TEST_ASSERT_EQUAL_UINT32(0, ot_control_boost_remaining_s(&w.c, t + MIN + 1));
  TEST_ASSERT_TRUE(ot_control_boost_active(&w.c));
}

void test_a_fresh_or_cancelled_boost_stays_dead_past_2_to_the_31(void) {
  // A cancelled boost has a deadline of 0, and "0 has been reached" holds only while
  // (int32_t)now >= 0 -- the first 24.8 days of uptime. After that only the !active guard in
  // remaining_s() stops a dead boost reporting seven weeks left. The bench never gets there.
  const uint32_t late[] = {0x80000000u, 0xFFFFFFF0u};
  for (uint32_t now : late) {
    H fresh = local_at(now - 2000u);
    H cancelled = local_at(now - 2000u);
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, start(cancelled, 650, 60, now - 1000u));
    ot_control_boost_cancel(&cancelled.c);
    H *dead[] = {&fresh, &cancelled};
    for (H *h : dead) {
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ot_control_boost_remaining_s(&h->c, now), "time left on nothing");
      step_at(*h, now);
      ASSERT_STATE(OT_CONTROL_LOCAL, *h);
      TEST_ASSERT_FALSE(ot_control_boost_active(&h->c));
    }
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_executor_has_no_boost);
  RUN_TEST(test_one_minute_and_eight_hours_are_both_accepted);
  RUN_TEST(test_zero_minutes_is_refused_there_is_no_forever);
  RUN_TEST(test_one_minute_over_eight_hours_is_refused);
  RUN_TEST(test_a_boost_is_refused_in_ha_mode);
  RUN_TEST(test_a_boost_asked_for_with_a_stale_local_snapshot_is_refused);
  RUN_TEST(test_a_boost_is_refused_with_the_season_off);
  RUN_TEST(test_a_setpoint_outside_the_flow_bounds_is_refused_and_the_bounds_are_inclusive);
  RUN_TEST(test_the_refusals_are_answered_in_order);
  RUN_TEST(test_a_refused_start_leaves_the_running_boost_alone);
  RUN_TEST(test_a_boost_holds_its_setpoint_on_id1_and_raises_ch);
  RUN_TEST(test_the_boost_ends_at_its_deadline_exactly);
  RUN_TEST(test_a_boost_never_stepped_before_its_deadline_is_not_resurrected);
  RUN_TEST(test_expiry_hands_the_bit_back_to_local);
  RUN_TEST(test_a_switch_to_ha_mode_ends_the_boost);
  RUN_TEST(test_the_boost_has_no_resend_of_its_own);
  RUN_TEST(test_the_millisecond_wrap_does_not_end_a_boost_early);
  RUN_TEST(test_a_new_start_replaces_the_boost);
  RUN_TEST(test_cancel_ends_the_boost_on_the_next_step);
  RUN_TEST(test_remaining_seconds_round_up_and_reach_zero_only_at_the_deadline);
  RUN_TEST(test_remaining_is_zero_past_a_deadline_no_step_has_expired);
  RUN_TEST(test_a_fresh_or_cancelled_boost_stays_dead_past_2_to_the_31);
  return UNITY_END();
}
