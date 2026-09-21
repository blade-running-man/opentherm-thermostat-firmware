// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What goes on the bus: the held ID 1 and its invariant, the re-send cadence,
// the DHW bit and the ID 56 reconciliation, and the status byte's two bits.
//
// Here the bus is NOT ideal: step() takes what the bus confirmed as an argument, because the
// invariant is about exactly the steps on which it has not confirmed anything yet.
#include <initializer_list>
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_control.h"
}

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T0 = 1000000u;
static const ot_control_mode_t LOCAL = OT_CONTROL_MODE_LOCAL;
static const ot_control_mode_t HAM   = OT_CONTROL_MODE_HA;

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

static H boot(const ot_control_cfg_t &cfg, const ot_control_restore_t *r = nullptr) {
  H h;
  memset(&h, 0, sizeof h);
  h.cfg = cfg;
  h.now = T0;
  ot_control_init(&h.c, &h.cfg, r, h.now);
  return h;
}

// One step on an ideal bus: what the executor asked to send on the previous step went out.
static void tick(H &h, uint32_t dt = 1000) {
  h.in.setpoint_confirmed = h.out.send_setpoint;
  h.in.confirmed_dc = h.out.held_setpoint_dc;
  h.now += dt;
  ot_control_step(&h.c, &h.cfg, &h.in, h.now, &h.out);
}

static void ticks(H &h, int n) {
  for (int i = 0; i < n; i++) tick(h);
}

// One step with the bus reporting exactly what the test says.
static void step(H &h, bool confirmed, int16_t dc, uint32_t dt = 1000) {
  h.in.setpoint_confirmed = confirmed;
  h.in.confirmed_dc = dc;
  h.now += dt;
  ot_control_step(&h.c, &h.cfg, &h.in, h.now, &h.out);
}

static void ha(H &h, ot_control_cmd_t cmd, int16_t v) {
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&h.c, &h.cfg, OT_ORIGIN_HA, cmd, v, h.now, &p));
}

static void web(H &h, ot_control_cmd_t cmd, int16_t v) {
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&h.c, &h.cfg, OT_ORIGIN_WEB, cmd, v, h.now, &p));
}

static bool ch(const H &h) { return (h.out.status_high & OT_STATUS_CH_ENABLE) != 0; }
static bool dhw(const H &h) { return (h.out.status_high & OT_STATUS_DHW_ENABLE) != 0; }

#define ASSERT_STATE(s, h) TEST_ASSERT_EQUAL_STRING(ot_control_state_name(s), ot_control_state_name((h).out.state))

// Settled in state s, with the DHW switch as given.
static H reach(ot_control_state_t s, bool dhw_on) {
  ot_control_cfg_t k = cfg_in(s >= OT_CONTROL_HA_WAITING ? HAM : LOCAL);
  k.dhw_enable = dhw_on;
  k.heating_season = s != OT_CONTROL_SEASON_OFF;
  k.local_ch_enable = s == OT_CONTROL_LOCAL;
  const ot_control_restore_t expired = {true, 900000u, 0};
  H h = boot(k, s == OT_CONTROL_FAILSAFE ? &expired : nullptr);
  if (s == OT_CONTROL_BOOST) TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_boost_start(&h.c, &h.cfg, 650, 60, h.now));
  if (s == OT_CONTROL_HA) ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  ticks(h, 3);
  ASSERT_STATE(s, h);
  return h;
}

// === group: invariant ===

// Each returns an executor one step before it wants CH with a held ID 1 the bus has not carried.
static H want_local(void) {
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.local_ch_enable = true;   // held is 450 from boot; LOCAL wants 550
  return boot(k);
}
static H want_boost(void) {
  H h = boot(cfg_in(LOCAL));
  ticks(h, 3);                // 550 confirmed, CH down
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_boost_start(&h.c, &h.cfg, 650, 60, h.now));
  return h;
}
static H want_ha(void) {
  H h = boot(cfg_in(HAM));
  ticks(h, 3);                // 450 confirmed, waiting
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600);
  return h;
}
static H want_failsafe(void) {
  H h = boot(cfg_in(HAM));
  ticks(h, 3);
  ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600);   // HA holds 600 with CH down...
  ticks(h, 899);                            // ...and dies; the next step is failsafe at 450
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_FALSE(ch(h));
  return h;
}

void test_ch_never_rises_before_the_held_setpoint_is_confirmed(void) {
  // THE invariant: a CH bit that rises before its setpoint has gone out
  // lights the burner at whatever TSet the boiler last held -- after a boost, the boost's.
  const struct {
    const char        *name;
    H                (*setup)(void);
    int16_t            held;
    ot_control_state_t state;
  } cases[] = {
      {"local", want_local, 550, OT_CONTROL_LOCAL},
      {"boost", want_boost, 650, OT_CONTROL_BOOST},
      {"ha", want_ha, 600, OT_CONTROL_HA},
      {"failsafe", want_failsafe, 450, OT_CONTROL_FAILSAFE},
  };
  for (const auto &k : cases) {
    H h = k.setup();
    for (int i = 0; i < 5; i++) {
      step(h, false, 0);
      ASSERT_STATE(k.state, h);
      TEST_ASSERT_EQUAL_INT16_MESSAGE(k.held, h.out.held_setpoint_dc, k.name);
      TEST_ASSERT_FALSE_MESSAGE(ch(h), k.name);
      TEST_ASSERT_EQUAL_INT_MESSAGE(OT_CONTROL_REASON_AWAIT_SETPOINT, h.out.reason, k.name);
      TEST_ASSERT_TRUE_MESSAGE(h.out.send_setpoint, k.name);
    }
    step(h, true, (int16_t)(k.held - 5));   // the bus carried some other value
    TEST_ASSERT_FALSE_MESSAGE(ch(h), k.name);
    TEST_ASSERT_TRUE(h.out.send_setpoint);
    step(h, true, k.held);
    TEST_ASSERT_TRUE_MESSAGE(ch(h), k.name);
    TEST_ASSERT_NOT_EQUAL(OT_CONTROL_REASON_AWAIT_SETPOINT, h.out.reason);
  }
}

void test_a_confirmed_setpoint_lets_ch_rise_on_the_same_step(void) {
  // The invariant costs nothing when the value on the wire is already the held one.
  H h = boot(cfg_in(LOCAL));
  ticks(h, 3);
  h.cfg.local_ch_enable = true;
  step(h, false, 0);
  TEST_ASSERT_TRUE(ch(h));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.reason);
}

void test_a_setpoint_change_never_drops_a_ch_already_up(void) {
  // The invariant is about RISING. Dropping CH to wait for a new setpoint would cost the boiler
  // a burner cycle for every slider move.
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.local_ch_enable = true;
  H h = boot(k);
  ticks(h, 3);
  TEST_ASSERT_TRUE(ch(h));
  h.cfg.local_ch_setpoint_dc = 600;
  step(h, false, 0);
  TEST_ASSERT_TRUE(ch(h));
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
}

void test_a_foreign_value_on_the_wire_is_not_ours(void) {
  // Something else put ID 1 = 500 on the bus. The wire no longer holds the executor's value, so
  // it is asked for again, and CH may not rise on the strength of an older confirmation.
  H h = boot(cfg_in(LOCAL));
  ticks(h, 3);
  step(h, true, 500);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  h.cfg.local_ch_enable = true;
  step(h, false, 0);
  TEST_ASSERT_FALSE(ch(h));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_AWAIT_SETPOINT, h.out.reason);
  step(h, true, 550);
  TEST_ASSERT_TRUE(ch(h));
}

// === group: held ===

void test_the_held_setpoint_starts_at_the_failsafe_setpoint(void) {
  // There is no "no setpoint" state: the first step of every boot has a value to send.
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.heating_season = false;
  H h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(450, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  H g = boot(cfg_in(HAM));
  step(g, false, 0);
  TEST_ASSERT_EQUAL_INT16(450, g.out.held_setpoint_dc);
}

void test_every_assignment_is_quantised_to_half_a_degree(void) {
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.heating_season = false;
  k.failsafe_setpoint_dc = 452;
  H h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(450, h.out.held_setpoint_dc);   // down
  k.failsafe_setpoint_dc = 453;
  h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(455, h.out.held_setpoint_dc);   // up
  k.heating_season = true;
  k.local_ch_setpoint_dc = 557;
  h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(555, h.out.held_setpoint_dc);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_boost_start(&h.c, &h.cfg, 648, 60, h.now));
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(650, h.out.held_setpoint_dc);
  H g = boot(cfg_in(HAM));
  ha(g, OT_CONTROL_CMD_CH_SETPOINT, 601);
  step(g, false, 0);
  TEST_ASSERT_EQUAL_INT16(600, g.out.held_setpoint_dc);
}

void test_the_held_setpoint_never_leaves_the_flow_bounds(void) {
  // A stored value is validated when it is written, but the bounds can move after that.
  ot_control_cfg_t k = cfg_in(LOCAL);
  H h = boot(k);
  ticks(h, 2);
  TEST_ASSERT_EQUAL_INT16(550, h.out.held_setpoint_dc);
  h.cfg.flow_max_dc = 500;
  tick(h);
  TEST_ASSERT_EQUAL_INT16(500, h.out.held_setpoint_dc);
  h.cfg.flow_max_dc = 700;
  h.cfg.flow_min_dc = 600;
  tick(h);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  // In the states that hold ID 1 where it is, too: season_off keeps 600 until the bounds move.
  h.cfg.heating_season = false;
  tick(h);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  h.cfg.flow_min_dc = 400;
  h.cfg.flow_max_dc = 580;
  tick(h);
  TEST_ASSERT_EQUAL_INT16(580, h.out.held_setpoint_dc);
  // And at boot: a failsafe setpoint below flow_min is not sent as it is.
  k.failsafe_setpoint_dc = 300;
  k.heating_season = false;
  h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(400, h.out.held_setpoint_dc);
}

void test_the_bounds_win_over_the_quantisation(void) {
  // A bound that is not a multiple of 5 cannot satisfy both; being inside is the one that keeps
  // the boiler safe, so a flow_max of 50.3 degrees is held as 50.3, not 50.5.
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.flow_max_dc = 503;
  H h = boot(k);
  step(h, false, 0);
  TEST_ASSERT_EQUAL_INT16(503, h.out.held_setpoint_dc);
}

// === group: cadence ===

void test_a_confirmed_setpoint_is_resent_every_ten_seconds(void) {
  H h = boot(cfg_in(LOCAL));
  step(h, false, 0);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_setpoint, "the first step");
  step(h, true, 550);
  TEST_ASSERT_FALSE(h.out.send_setpoint);
  for (int i = 1; i <= 9; i++) {
    step(h, false, 0);
    TEST_ASSERT_FALSE_MESSAGE(h.out.send_setpoint, "sooner than ten seconds");
  }
  step(h, false, 0);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_setpoint, "ten seconds after the last confirmation");
  // Skipped (a hand write was pending): asked again on the next step, with no memory needed.
  step(h, false, 0);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step(h, true, 550);
  TEST_ASSERT_FALSE(h.out.send_setpoint);
}

void test_a_change_is_sent_at_once_and_until_the_bus_confirms_it(void) {
  H h = boot(cfg_in(LOCAL));
  ticks(h, 3);
  TEST_ASSERT_FALSE(h.out.send_setpoint);
  h.cfg.local_ch_setpoint_dc = 600;
  step(h, false, 0);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step(h, false, 0);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step(h, true, 550);   // the old value, still in flight
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step(h, true, 600);
  TEST_ASSERT_FALSE(h.out.send_setpoint);
}

void test_the_held_setpoint_is_asserted_in_every_state(void) {
  // Also with CH down (season_off, ha_waiting): harmless, since the CH bit overrides ID 1
  // (OpenTherm v2.2 §5.2), and it keeps the invariant cheap -- the value is already out when CH
  // comes to rise.
  const ot_control_state_t states[] = {OT_CONTROL_SEASON_OFF, OT_CONTROL_BOOST, OT_CONTROL_LOCAL,
                                       OT_CONTROL_HA_WAITING, OT_CONTROL_FAILSAFE, OT_CONTROL_HA};
  for (ot_control_state_t s : states) {
    H h = reach(s, true);
    step(h, true, h.out.held_setpoint_dc);   // the bus confirms the held value now
    TEST_ASSERT_FALSE_MESSAGE(h.out.send_setpoint, ot_control_state_name(s));
    for (int i = 1; i <= 9; i++) {
      step(h, false, 0);
      TEST_ASSERT_FALSE_MESSAGE(h.out.send_setpoint, ot_control_state_name(s));
    }
    step(h, false, 0);
    TEST_ASSERT_TRUE_MESSAGE(h.out.send_setpoint, ot_control_state_name(s));
  }
}

void test_a_confirmation_older_than_the_clock_wrap_is_still_old(void) {
  // The age of the last confirmation saturates: 49.7 days without one must not wrap round to
  // "a moment ago" and silence the re-send for ten seconds.
  H h = boot(cfg_in(LOCAL));
  ticks(h, 3);
  step(h, false, 0, UINT32_MAX - 5000u);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
  step(h, false, 0, 6000u);
  TEST_ASSERT_TRUE(h.out.send_setpoint);
}

// === group: dhw ===

void test_the_dhw_bit_is_the_configured_one_in_every_state(void) {
  // Not touched by the season, ha_waiting or the failsafe: on a combi with no tank, the
  // DHW bit at 0 most likely means no hot water at the tap.
  const ot_control_state_t states[] = {OT_CONTROL_SEASON_OFF, OT_CONTROL_BOOST, OT_CONTROL_LOCAL,
                                       OT_CONTROL_HA_WAITING, OT_CONTROL_FAILSAFE, OT_CONTROL_HA};
  for (ot_control_state_t s : states) {
    for (bool on : {true, false}) {
      const H h = reach(s, on);
      TEST_ASSERT_EQUAL_MESSAGE(on, dhw(h), ot_control_state_name(s));
    }
  }
}

void test_no_bit_above_dhw_is_ever_set(void) {
  // Cooling, OTC and a second circuit are not driven by this firmware and this boiler does not
  // have them. A stray bit is the kind of thing a boiler answers by doing something unasked.
  const ot_control_state_t states[] = {OT_CONTROL_SEASON_OFF, OT_CONTROL_BOOST, OT_CONTROL_LOCAL,
                                       OT_CONTROL_HA_WAITING, OT_CONTROL_FAILSAFE, OT_CONTROL_HA};
  for (ot_control_state_t s : states) {
    for (bool on : {true, false}) {
      const H h = reach(s, on);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, h.out.status_high & (uint8_t)~(OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE),
                                     ot_control_state_name(s));
    }
  }
}

static H dhw_rig(bool set, int16_t target, bool readback_valid, int16_t readback) {
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.dhw_setpoint_set = set;
  k.dhw_setpoint_dc = target;
  H h = boot(k);
  h.in.dhw_readback_valid = readback_valid;
  h.in.dhw_readback_dc = readback;
  return h;
}

static int dhw_sends(H &h, int steps) {
  int n = 0;
  for (int i = 0; i < steps; i++) {
    tick(h);
    n += h.out.send_dhw_setpoint;
  }
  return n;
}

void test_the_dhw_setpoint_is_never_sent_while_unset(void) {
  H h = dhw_rig(false, 500, true, 450);
  TEST_ASSERT_EQUAL_INT(0, dhw_sends(h, 200));
}

void test_no_dhw_write_without_a_readback_or_when_they_agree(void) {
  // Without a readback there is nothing to reconcile against, so past the one boot re-arm
  // no further writes go out; when the readback agrees, none go out at all.
  H h = dhw_rig(true, 500, false, 450);
  tick(h);   // the boot re-arm is consumed here
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dhw_sends(h, 200), "no readback: nothing to compare with after the boot write");
  h = dhw_rig(true, 500, true, 500);
  TEST_ASSERT_EQUAL_INT(0, dhw_sends(h, 200));
}

void test_a_differing_readback_is_rewritten_at_most_once_a_minute(void) {
  H h = dhw_rig(true, 500, true, 450);
  tick(h);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_dhw_setpoint, "the first difference goes out at once");
  TEST_ASSERT_EQUAL_INT(0, dhw_sends(h, 59));
  tick(h);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_dhw_setpoint, "sixty seconds after the last write");
}

void test_a_new_dhw_target_goes_out_at_once(void) {
  // A new value is a new write, not a retry: it does not wait out the last one's minute.
  H h = dhw_rig(true, 500, true, 450);
  TEST_ASSERT_EQUAL_INT(1, dhw_sends(h, 10));
  h.cfg.dhw_setpoint_dc = 520;
  tick(h);
  TEST_ASSERT_TRUE(h.out.send_dhw_setpoint);
}

void test_a_first_divergence_is_written_at_once_however_long_they_agreed(void) {
  // The time since the last write saturates too: "never written" must stay "long ago".
  H h = dhw_rig(true, 500, true, 500);
  TEST_ASSERT_EQUAL_INT(0, dhw_sends(h, 3));
  h.in.dhw_readback_dc = 450;
  tick(h);
  TEST_ASSERT_TRUE(h.out.send_dhw_setpoint);
}

void test_rewriting_the_same_dhw_target_reopens_it(void) {
  // The cap below gives up on a target; a person writing that target again is asking again.
  H h = dhw_rig(true, 503, true, 500);
  TEST_ASSERT_EQUAL_INT(3, dhw_sends(h, 400));
  web(h, OT_CONTROL_CMD_DHW_SETPOINT, 503);
  TEST_ASSERT_EQUAL_INT(3, dhw_sends(h, 400));
}

void test_without_a_readback_an_accepted_target_is_written_once(void) {
  // No readback (ID 56 not yet polled, or not answered): nothing to reconcile against, so no
  // retries -- but a command a person just gave is carried out once, not never.
  H h = dhw_rig(true, 500, false, 0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, dhw_sends(h, 200), "the persisted target is written once at boot");
  web(h, OT_CONTROL_CMD_DHW_SETPOINT, 520);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dhw_sends(h, 1), "the old target written while the store catches up");
  h.cfg.dhw_setpoint_dc = 520;   // the task layer persisted it; the next snapshot has it
  TEST_ASSERT_EQUAL_INT(1, dhw_sends(h, 200));
  web(h, OT_CONTROL_CMD_DHW_SETPOINT, 520);
  TEST_ASSERT_EQUAL_INT(1, dhw_sends(h, 200));
}

void test_dhw_stops_after_three_unanswered_writes_of_one_target(void) {
  // A boiler that stores ID 56 in whole degrees reads 50.3 back as 50.0 for ever. Without a cap
  // that is 1440 writes a day into what Tasmota warns may be the boiler's flash.
  H h = dhw_rig(true, 503, true, 500);
  TEST_ASSERT_EQUAL_INT(3, dhw_sends(h, 400));
  h.in.dhw_readback_dc = 503;                 // the boiler agrees at last
  TEST_ASSERT_EQUAL_INT(0, dhw_sends(h, 5));
  h.in.dhw_readback_dc = 500;                 // and later loses it: three more tries
  TEST_ASSERT_EQUAL_INT(3, dhw_sends(h, 400));
  h.cfg.dhw_setpoint_dc = 510;                // a new target starts its own three
  TEST_ASSERT_EQUAL_INT(3, dhw_sends(h, 400));
}

void test_a_persisted_dhw_setpoint_is_re_armed_once_at_boot(void) {
  // A persisted DHW setpoint must survive a reboot even on a boiler that never answers
  // the ID 56 readback -- there is nothing to reconcile against there, so dhw_due() would otherwise
  // never re-send it. init() arms one write; it is a one-shot, then the readback (where there is
  // one) governs, and DHW_MAX_TRIES caps everything either way.

  // A non-readback boiler: the persisted target goes out once at boot, then not on every step.
  H h = dhw_rig(true, 500, false, 0);
  tick(h);
  TEST_ASSERT_TRUE_MESSAGE(h.out.send_dhw_setpoint, "the persisted target is re-sent on the first boot step");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dhw_sends(h, 200), "one-shot: not re-sent on every step");

  // No persisted setpoint: nothing is armed at boot.
  H g = dhw_rig(false, 500, false, 0);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dhw_sends(g, 200), "unset: no boot write");

  // A readback boiler whose readback already equals the target: the agree branch consumes the arm
  // with no write (the owner's boiler, unaffected either way).
  H a = dhw_rig(true, 500, true, 500);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, dhw_sends(a, 200), "readback agrees: the boot arm is consumed, no write");

  // A readback boiler whose readback differs: reconciliation writes exactly as before.
  H b = dhw_rig(true, 500, true, 450);
  tick(b);
  TEST_ASSERT_TRUE_MESSAGE(b.out.send_dhw_setpoint, "readback differs: written at once");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ch_never_rises_before_the_held_setpoint_is_confirmed);
  RUN_TEST(test_a_confirmed_setpoint_lets_ch_rise_on_the_same_step);
  RUN_TEST(test_a_setpoint_change_never_drops_a_ch_already_up);
  RUN_TEST(test_a_foreign_value_on_the_wire_is_not_ours);
  RUN_TEST(test_the_held_setpoint_starts_at_the_failsafe_setpoint);
  RUN_TEST(test_every_assignment_is_quantised_to_half_a_degree);
  RUN_TEST(test_the_held_setpoint_never_leaves_the_flow_bounds);
  RUN_TEST(test_the_bounds_win_over_the_quantisation);
  RUN_TEST(test_a_confirmed_setpoint_is_resent_every_ten_seconds);
  RUN_TEST(test_a_change_is_sent_at_once_and_until_the_bus_confirms_it);
  RUN_TEST(test_the_held_setpoint_is_asserted_in_every_state);
  RUN_TEST(test_a_confirmation_older_than_the_clock_wrap_is_still_old);
  RUN_TEST(test_the_dhw_bit_is_the_configured_one_in_every_state);
  RUN_TEST(test_no_bit_above_dhw_is_ever_set);
  RUN_TEST(test_the_dhw_setpoint_is_never_sent_while_unset);
  RUN_TEST(test_no_dhw_write_without_a_readback_or_when_they_agree);
  RUN_TEST(test_a_differing_readback_is_rewritten_at_most_once_a_minute);
  RUN_TEST(test_a_new_dhw_target_goes_out_at_once);
  RUN_TEST(test_a_first_divergence_is_written_at_once_however_long_they_agreed);
  RUN_TEST(test_rewriting_the_same_dhw_target_reopens_it);
  RUN_TEST(test_without_a_readback_an_accepted_target_is_written_once);
  RUN_TEST(test_dhw_stops_after_three_unanswered_writes_of_one_target);
  RUN_TEST(test_a_persisted_dhw_setpoint_is_re_armed_once_at_boot);
  return UNITY_END();
}
