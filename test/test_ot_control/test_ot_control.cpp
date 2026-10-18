// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The ladder and the entries into HA ownership.
//
// Every boundary between adjacent rows has a test here, and every entry
// into HA ownership has one showing that a value HA sent before it never comes back after it.
// The bus is simulated as ideal by tick(): whatever the executor asked to send on one step has
// gone out by the next -- the invariant and its failures are test_ot_control_bus's.
//
// Setpoints in the fixture are distinct on purpose -- failsafe 450, local 550, HA 600, boost 650
// -- so every held value names the row that chose it.
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_control.h"
}

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T0 = 1000000u;

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

static ot_control_err_t ha(H &h, ot_control_cmd_t cmd, int16_t v) {
  ot_control_persist_t p;
  return ot_control_apply(&h.c, &h.cfg, OT_ORIGIN_HA, cmd, v, h.now, &p);
}

static bool ch(const H &h) { return (h.out.status_high & OT_STATUS_CH_ENABLE) != 0; }

#define ASSERT_STATE(s, h) TEST_ASSERT_EQUAL_STRING(ot_control_state_name(s), ot_control_state_name((h).out.state))

// === group: ladder ===

void test_season_off_beats_a_running_boost(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_boost_start(&h.c, &h.cfg, 650, 60, h.now));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  TEST_ASSERT_TRUE(ch(h));
  h.cfg.heating_season = false;
  tick(h);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "a boost heated with the season off");
  // The ladder decided, not a cancel: the boost's deadline keeps running (the ladder's overlap).
  TEST_ASSERT_TRUE(ot_control_boost_active(&h.c));
  h.cfg.heating_season = true;
  tick(h);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  TEST_ASSERT_TRUE(ch(h));
}

void test_a_boost_beats_local(void) {
  ot_control_cfg_t k = cfg_in(OT_CONTROL_MODE_LOCAL);
  k.local_ch_enable = true;
  H h = boot(k);
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_EQUAL_INT16(550, h.out.held_setpoint_dc);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_boost_start(&h.c, &h.cfg, 650, 60, h.now));
  ticks(h, 2);
  ASSERT_STATE(OT_CONTROL_BOOST, h);
  TEST_ASSERT_EQUAL_INT16(650, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE(ch(h));
  ot_control_boost_cancel(&h.c);
  tick(h);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_EQUAL_INT16(550, h.out.held_setpoint_dc);
}

void test_local_follows_its_own_switch_and_setpoint(void) {
  ot_control_cfg_t k = cfg_in(OT_CONTROL_MODE_LOCAL);
  H h = boot(k);
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  TEST_ASSERT_FALSE(ch(h));
  TEST_ASSERT_EQUAL_INT16(550, h.out.held_setpoint_dc);
  h.cfg.local_ch_enable = true;
  tick(h);
  TEST_ASSERT_TRUE(ch(h));
  h.cfg.local_ch_enable = false;
  tick(h);
  TEST_ASSERT_FALSE(ch(h));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.reason);
}

void test_local_mode_never_reaches_the_ha_rows(void) {
  // An expired watchdog restored from RTC and a blind HA are both HA's business; in LOCAL the
  // person is the controller, and neither may take the boiler away from them.
  const ot_control_restore_t r = {true, 5000000u, 0};
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
  h.in.ha_forwarded_stale = true;
  for (int i = 0; i < 2000; i++) {
    tick(h);
    ASSERT_STATE(OT_CONTROL_LOCAL, h);
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
  }
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_LOCAL, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
}

void test_ha_waiting_holds_ch_down_until_the_first_accepted_ha_ch_command(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_FALSE(ch(h));
  // A DHW toggle is a command but not a decision about heat; a refused command is nothing.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_DHW_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, ha(h, OT_CONTROL_CMD_CH_ENABLE, 2));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_TRUE(ch(h));
}

void test_a_ch_setpoint_alone_also_ends_ha_waiting(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "HA asked for a setpoint, not for heat");
}

void test_ha_waiting_becomes_failsafe_when_the_watchdog_expires(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 899);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
}

void test_the_boundary_between_ha_and_failsafe_is_the_watchdog(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  ticks(h, 899);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
}

void test_ha_blind_is_failsafe_once_ha_has_spoken(void) {
  // "HA's commands are still fresh" is part of the fresh-commands condition: before HA has said anything
  // there is nothing to be blind about, and ha_waiting already holds CH down.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  h.in.ha_forwarded_stale = true;
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_HA_BLIND, h.out.cause);
}

void test_ha_is_the_last_row_and_carries_has_values(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
  TEST_ASSERT_TRUE(ch(h));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.reason);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 0));
  tick(h);
  TEST_ASSERT_FALSE(ch(h));
}

// === group: transitions ===

// HA in charge and heating at 600, settled.
static H ha_heating(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600);
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_TRUE(ch(h));
  return h;
}

// After an entry: ha_waiting, then a CH_SETPOINT alone. If HA's CH_ENABLE = 1 from before the
// entry had survived, CH would come up here.
static void assert_nothing_came_back(H &h) {
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_FALSE(ch(h));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 620));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "HA's CH_ENABLE from before the entry came back");
  TEST_ASSERT_EQUAL_INT16(620, h.out.held_setpoint_dc);
}

void test_boot_in_ha_mode_waits_with_the_failsafe_setpoint_held(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_FALSE(ch(h));
  TEST_ASSERT_EQUAL_INT16(450, h.out.held_setpoint_dc);
}

void test_local_to_ha_forgets_what_ha_said_before(void) {
  H h = ha_heating();
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  tick(h);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  tick(h);
  assert_nothing_came_back(h);
}

void test_local_to_ha_forgets_has_setpoint_too(void) {
  H h = ha_heating();
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  tick(h);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  tick(h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(450, h.out.held_setpoint_dc, "HA's 600 from before came back");
}

void test_season_off_then_on_in_ha_mode_forgets_and_waits(void) {
  H h = ha_heating();
  h.cfg.heating_season = false;
  tick(h);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  TEST_ASSERT_FALSE(ch(h));
  h.cfg.heating_season = true;
  tick(h);
  assert_nothing_came_back(h);
}

void test_a_command_sent_while_the_season_is_off_does_not_survive_its_return(void) {
  // Season 1 -> 0 in HA mode is not an entry: HA's commands are still accepted while it lasts,
  // and they feed the watchdog. Season 0 -> 1 IS an entry: what HA said during the
  // season-off -- a summer keepalive of CH_ENABLE = 1, say -- is forgotten at its return.
  H h = ha_heating();
  h.cfg.heating_season = false;
  tick(h);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  ticks(h, 100);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 610));
  tick(h);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, h.out.overdue_ms, "fed while the season was off");
  h.cfg.heating_season = true;
  tick(h);
  assert_nothing_came_back(h);
}

void test_has_clock_runs_through_a_season_off(void) {
  // Neither season edge touches the watchdog. An HA that died in July is still dead when the
  // season comes back in October: straight to failsafe, not another watchdog_s of CH off.
  H h = ha_heating();
  h.cfg.heating_season = false;
  ticks(h, 1000);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  h.cfg.heating_season = true;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
}

void test_leaving_failsafe_needs_an_accepted_ch_enable(void) {
  H h = ha_heating();
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_DHW_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  // A setpoint feeds the watchdog but is not a decision about heat.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));
  ticks(h, 5);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_TRUE(ch(h));
  TEST_ASSERT_EQUAL_INT16_MESSAGE(450, h.out.held_setpoint_dc,
                                  "a setpoint sent during failsafe came back after it");
}

void test_a_setpoint_sent_after_the_exiting_ch_enable_is_kept(void) {
  H h = ha_heating();
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT16(600, h.out.held_setpoint_dc);
}

void test_a_blind_failsafe_holds_until_the_source_recovers_and_ha_speaks(void) {
  H h = ha_heating();
  h.in.ha_forwarded_stale = true;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  // HA keeps commanding -- that is what "alive but blind" means -- and must not get back in.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_HA_BLIND, h.out.cause);
  h.in.ha_forwarded_stale = false;
  ticks(h, 5);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  // The state latches until an accepted HA CH_ENABLE, but the cause is recomputed
  // every step. The blind condition has cleared and the watchdog has not expired, so the latched
  // failsafe no longer claims HA_BLIND -- see test_a_recovered_blind_failsafe_stops_claiming_ha_blind.
  TEST_ASSERT_EQUAL_INT_MESSAGE(OT_CONTROL_REASON_NONE, h.out.cause,
                                "a stale HA_BLIND cause outlived the blind condition");
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
}

void test_a_recovered_blind_failsafe_stops_claiming_ha_blind(void) {
  // The failsafe latches until an accepted HA CH_ENABLE with the cause gone.
  // Entered here via ha_blind; then the room source recovers and HA keeps the watchdog fed with a
  // CH_SETPOINT (no CH_ENABLE, so the latch holds). The cause must be recomputed every step: with
  // neither the watchdog expired nor the source blind, it must not keep claiming HA_BLIND.
  H h = ha_heating();
  h.in.ha_forwarded_stale = true;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_HA_BLIND, h.out.cause);
  h.in.ha_forwarded_stale = false;
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));  // feed the watchdog
    tick(h);
  }
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, h.out.overdue_ms, "the watchdog was not being fed");
  TEST_ASSERT_TRUE_MESSAGE(h.out.cause != OT_CONTROL_REASON_HA_BLIND,
                           "a stale HA_BLIND cause outlived the blind condition");
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
}

void test_a_recovered_watchdog_failsafe_stops_claiming_watchdog(void) {
  // The WATCHDOG twin of the blind case above. Entered via a watchdog expiry; then HA
  // keeps the watchdog FED with a CH_SETPOINT (no CH_ENABLE, so the latch holds). overdue
  // resets to 0 on each accepted command and `expired` becomes false, yet the state stays latched.
  // The cause is recomputed every step: with neither the watchdog expired nor the source blind, it
  // must degrade from WATCHDOG to NONE, not report a watchdog that is being fed.
  H h = ha_heating();
  ticks(h, 900);                       // 900 s of silence -> overdue >= watchdog_s * 1000 -> expired
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));  // feed, no enable
    tick(h);
  }
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1000, h.out.overdue_ms, "the watchdog was not being fed");
  TEST_ASSERT_TRUE_MESSAGE(h.out.cause != OT_CONTROL_REASON_WATCHDOG,
                           "a stale WATCHDOG cause outlived the fed watchdog");
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
}

void test_ha_is_refused_until_the_step_sees_the_flip(void) {
  // The store says HA; the executor has not stepped since. Ownership is what the executor has
  // seen, so HA's first command is refused for at most one step -- and then accepted.
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL));
  ticks(h, 3);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_LOCAL, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_TRUE(ch(h));
}

void test_a_stale_snapshot_replays_no_edge(void) {
  // A web request built its snapshot before LOCAL -> HA; it lands after the step saw HA and
  // HA's CH_ENABLE = 1 was accepted. Were the snapshot's mode an edge, the next step would see
  // HA -> LOCAL -> HA, take an entry, and forget HA's command: CH off until the next keepalive.
  H h = ha_heating();
  ot_control_cfg_t stale = h.cfg;
  stale.mode = OT_CONTROL_MODE_LOCAL;
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&h.c, &stale, OT_ORIGIN_WEB,
                                                        OT_CONTROL_CMD_SEASON, 1, h.now, &p));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_TRUE_MESSAGE(ch(h), "a stale snapshot made HA's accepted command forgotten");
}

void test_a_ch_enable_sent_while_blind_does_not_end_the_failsafe(void) {
  // HA's CH_ENABLE arrives while the last step still saw its sensor stale; the sensor recovers
  // before the next step. That command was a blind HA's, and only the next one -- sent with the
  // sensor back -- may end the failsafe. Otherwise the recovery would let a blind decision in.
  H h = ha_heating();
  h.in.ha_forwarded_stale = true;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  h.in.ha_forwarded_stale = false;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
}

// The ch_enable entity shows the owner's COMMAND, not the bit: in LOCAL mode the stored
// switch, in HA mode HA's own word -- forgotten at every entry into HA ownership, like the rest of
// what HA said. LOCAL's switch is never HA's command.
void test_the_ch_command_is_the_owners(void) {
  ot_control_cfg_t local = cfg_in(OT_CONTROL_MODE_LOCAL);
  local.local_ch_enable = true;
  H h = boot(local);
  TEST_ASSERT_TRUE(ot_control_ch_command(&h.c, &h.cfg));
  h.cfg.local_ch_enable = false;
  TEST_ASSERT_FALSE(ot_control_ch_command(&h.c, &h.cfg));

  h.cfg = cfg_in(OT_CONTROL_MODE_HA);
  h.cfg.local_ch_enable = true;
  tick(h);
  TEST_ASSERT_FALSE_MESSAGE(ot_control_ch_command(&h.c, &h.cfg), "HA has not spoken yet");
  TEST_ASSERT_EQUAL(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_TRUE(ot_control_ch_command(&h.c, &h.cfg));

  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  tick(h);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  tick(h);
  TEST_ASSERT_FALSE_MESSAGE(ot_control_ch_command(&h.c, &h.cfg), "an entry forgets HA's 1");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_season_off_beats_a_running_boost);
  RUN_TEST(test_a_boost_beats_local);
  RUN_TEST(test_local_follows_its_own_switch_and_setpoint);
  RUN_TEST(test_local_mode_never_reaches_the_ha_rows);
  RUN_TEST(test_ha_waiting_holds_ch_down_until_the_first_accepted_ha_ch_command);
  RUN_TEST(test_a_ch_setpoint_alone_also_ends_ha_waiting);
  RUN_TEST(test_ha_waiting_becomes_failsafe_when_the_watchdog_expires);
  RUN_TEST(test_the_boundary_between_ha_and_failsafe_is_the_watchdog);
  RUN_TEST(test_ha_blind_is_failsafe_once_ha_has_spoken);
  RUN_TEST(test_ha_is_the_last_row_and_carries_has_values);
  RUN_TEST(test_boot_in_ha_mode_waits_with_the_failsafe_setpoint_held);
  RUN_TEST(test_local_to_ha_forgets_what_ha_said_before);
  RUN_TEST(test_local_to_ha_forgets_has_setpoint_too);
  RUN_TEST(test_season_off_then_on_in_ha_mode_forgets_and_waits);
  RUN_TEST(test_a_command_sent_while_the_season_is_off_does_not_survive_its_return);
  RUN_TEST(test_has_clock_runs_through_a_season_off);
  RUN_TEST(test_leaving_failsafe_needs_an_accepted_ch_enable);
  RUN_TEST(test_a_setpoint_sent_after_the_exiting_ch_enable_is_kept);
  RUN_TEST(test_a_blind_failsafe_holds_until_the_source_recovers_and_ha_speaks);
  RUN_TEST(test_a_recovered_blind_failsafe_stops_claiming_ha_blind);
  RUN_TEST(test_a_recovered_watchdog_failsafe_stops_claiming_watchdog);
  RUN_TEST(test_ha_is_refused_until_the_step_sees_the_flip);
  RUN_TEST(test_a_stale_snapshot_replays_no_edge);
  RUN_TEST(test_a_ch_enable_sent_while_blind_does_not_end_the_failsafe);
  RUN_TEST(test_the_ch_command_is_the_owners);
  return UNITY_END();
}
