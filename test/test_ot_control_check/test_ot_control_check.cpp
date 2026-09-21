// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Ownership and bounds of every command, the final answer of apply(), and the wire
// spellings. ot_control_check() is stateless and is also ot_command_check()'s early answer
// so its truth table is pinned here, once, against the executor's own vocabulary.
//
// The literals 400, 700, 399 and 701 are the flow bounds of the fixture written out on purpose:
// a boundary computed from the value it guards survives any change to that value.
#include <initializer_list>
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_control.h"
}

void setUp(void) {}
void tearDown(void) {}

// === group: names ===

void test_state_names_are_the_wire_spellings(void) {
  // The option strings of the generated control_state entity, in ladder order.
  TEST_ASSERT_EQUAL_STRING("season_off", ot_control_state_name(OT_CONTROL_SEASON_OFF));
  TEST_ASSERT_EQUAL_STRING("boost", ot_control_state_name(OT_CONTROL_BOOST));
  TEST_ASSERT_EQUAL_STRING("local", ot_control_state_name(OT_CONTROL_LOCAL));
  TEST_ASSERT_EQUAL_STRING("ha_waiting", ot_control_state_name(OT_CONTROL_HA_WAITING));
  TEST_ASSERT_EQUAL_STRING("failsafe", ot_control_state_name(OT_CONTROL_FAILSAFE));
  TEST_ASSERT_EQUAL_STRING("ha", ot_control_state_name(OT_CONTROL_HA));
  // Never NULL: the renderers print it straight into JSON.
  TEST_ASSERT_EQUAL_STRING("unknown", ot_control_state_name(OT_CONTROL_STATE_COUNT));
  TEST_ASSERT_EQUAL_STRING("unknown", ot_control_state_name((ot_control_state_t)-1));
}

void test_reason_names_are_the_wire_spellings(void) {
  TEST_ASSERT_EQUAL_STRING("none", ot_control_reason_name(OT_CONTROL_REASON_NONE));
  TEST_ASSERT_EQUAL_STRING("watchdog", ot_control_reason_name(OT_CONTROL_REASON_WATCHDOG));
  TEST_ASSERT_EQUAL_STRING("ha_blind", ot_control_reason_name(OT_CONTROL_REASON_HA_BLIND));
  TEST_ASSERT_EQUAL_STRING("fs_disarmed", ot_control_reason_name(OT_CONTROL_REASON_FS_DISARMED));
  TEST_ASSERT_EQUAL_STRING("fs_blind", ot_control_reason_name(OT_CONTROL_REASON_FS_BLIND));
  TEST_ASSERT_EQUAL_STRING("fs_room_cold", ot_control_reason_name(OT_CONTROL_REASON_FS_ROOM_COLD));
  TEST_ASSERT_EQUAL_STRING("fs_room_warm", ot_control_reason_name(OT_CONTROL_REASON_FS_ROOM_WARM));
  TEST_ASSERT_EQUAL_STRING("min_cycle", ot_control_reason_name(OT_CONTROL_REASON_MIN_CYCLE));
  TEST_ASSERT_EQUAL_STRING("await_setpoint",
                           ot_control_reason_name(OT_CONTROL_REASON_AWAIT_SETPOINT));
  TEST_ASSERT_EQUAL_STRING("unknown", ot_control_reason_name(OT_CONTROL_REASON_COUNT));
  TEST_ASSERT_EQUAL_STRING("unknown", ot_control_reason_name((ot_control_reason_t)-1));
}

// === group: check ===

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

static const ot_control_mode_t LOCAL = OT_CONTROL_MODE_LOCAL;
static const ot_control_mode_t HAM   = OT_CONTROL_MODE_HA;
static const ot_origin_t       WEB   = OT_ORIGIN_WEB;
static const ot_origin_t       HA    = OT_ORIGIN_HA;

static ot_control_err_t check(ot_control_mode_t mode, ot_origin_t origin, ot_control_cmd_t cmd,
                              int16_t value) {
  const ot_control_cfg_t k = cfg_in(mode);
  return ot_control_check(&k, origin, cmd, value);
}

void test_the_ownership_truth_table(void) {
  // Every origin x mode x command, each with a value that is valid on its own, so the only
  // question asked is "whose is it". The season is the person's switch, not the controller's:
  // the web writes it in either mode, and HA is refused in LOCAL like everything else.
  const struct {
    ot_control_cmd_t cmd;
    int16_t          value;
  } cmds[] = {
      {OT_CONTROL_CMD_CH_ENABLE, 1},   {OT_CONTROL_CMD_CH_SETPOINT, 550},
      {OT_CONTROL_CMD_DHW_ENABLE, 1},  {OT_CONTROL_CMD_DHW_SETPOINT, 500},
      {OT_CONTROL_CMD_SEASON, 0},
  };
  for (const auto &k : cmds) {
    const bool season = k.cmd == OT_CONTROL_CMD_SEASON;
    TEST_ASSERT_EQUAL_INT_MESSAGE(OT_CONTROL_OK, check(LOCAL, WEB, k.cmd, k.value), "web, local");
    TEST_ASSERT_EQUAL_INT_MESSAGE(season ? OT_CONTROL_OK : OT_CONTROL_OWNED_BY_HA,
                                  check(HAM, WEB, k.cmd, k.value), "web, ha");
    TEST_ASSERT_EQUAL_INT_MESSAGE(OT_CONTROL_OWNED_BY_LOCAL, check(LOCAL, HA, k.cmd, k.value),
                                  "ha, local");
    TEST_ASSERT_EQUAL_INT_MESSAGE(OT_CONTROL_OK, check(HAM, HA, k.cmd, k.value), "ha, ha");
  }
}

void test_the_season_truth_table(void) {
  // The season is the person's master kill. Every cell of origin x mode x {0, 1}:
  //   the web, either mode, either value: accepted -- a person can always say "off", and "on";
  //   HA in HA mode: 0 accepted, 1 refused -- a bound the bounded party can lift is no bound;
  //   HA in LOCAL: refused both ways, and ownership outranks SEASON_ON_IS_LOCAL.
  const struct {
    ot_origin_t       origin;
    ot_control_mode_t mode;
    int16_t           value;
    ot_control_err_t  want;
  } cells[] = {
      {WEB, LOCAL, 0, OT_CONTROL_OK},
      {WEB, LOCAL, 1, OT_CONTROL_OK},
      {WEB, HAM, 0, OT_CONTROL_OK},
      {WEB, HAM, 1, OT_CONTROL_OK},
      {HA, HAM, 0, OT_CONTROL_OK},
      {HA, HAM, 1, OT_CONTROL_SEASON_ON_IS_LOCAL},
      {HA, LOCAL, 0, OT_CONTROL_OWNED_BY_LOCAL},
      {HA, LOCAL, 1, OT_CONTROL_OWNED_BY_LOCAL},
  };
  for (const auto &k : cells) {
    TEST_ASSERT_EQUAL_INT(k.want, check(k.mode, k.origin, OT_CONTROL_CMD_SEASON, k.value));
  }
}

void test_bool_values_other_than_0_and_1_are_refused_for_both_origins(void) {
  const ot_control_cmd_t bools[] = {OT_CONTROL_CMD_CH_ENABLE, OT_CONTROL_CMD_DHW_ENABLE,
                                    OT_CONTROL_CMD_SEASON};
  const struct {
    ot_origin_t       origin;
    ot_control_mode_t owner;
  } writers[] = {{WEB, LOCAL}, {HA, HAM}};
  for (ot_control_cmd_t cmd : bools) {
    for (const auto &w : writers) {
      for (int16_t bad : {(int16_t)-1, (int16_t)2, (int16_t)100}) {
        TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, check(w.owner, w.origin, cmd, bad));
      }
      TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(w.owner, w.origin, cmd, 0));
    }
  }
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(LOCAL, WEB, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(HAM, HA, OT_CONTROL_CMD_DHW_ENABLE, 1));
}

void test_a_ch_setpoint_outside_the_flow_bounds_is_refused_not_clamped(void) {
  // Refused, not clamped: an HA automation that keeps being refused feeds no watchdog and
  // lands in failsafe, which is the right outcome for a broken automation.
  const struct {
    ot_origin_t       origin;
    ot_control_mode_t owner;
  } writers[] = {{WEB, LOCAL}, {HA, HAM}};
  for (const auto &w : writers) {
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE,
                          check(w.owner, w.origin, OT_CONTROL_CMD_CH_SETPOINT, 399));
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(w.owner, w.origin, OT_CONTROL_CMD_CH_SETPOINT, 400));
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(w.owner, w.origin, OT_CONTROL_CMD_CH_SETPOINT, 700));
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE,
                          check(w.owner, w.origin, OT_CONTROL_CMD_CH_SETPOINT, 701));
  }
  // The bounds are the snapshot's, not a copy: move them and the answer moves.
  ot_control_cfg_t k = cfg_in(LOCAL);
  k.flow_min_dc = 450;
  k.flow_max_dc = 600;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, ot_control_check(&k, WEB, OT_CONTROL_CMD_CH_SETPOINT, 449));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, ot_control_check(&k, WEB, OT_CONTROL_CMD_CH_SETPOINT, 601));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_check(&k, WEB, OT_CONTROL_CMD_CH_SETPOINT, 450));
}

void test_ownership_is_answered_before_the_value(void) {
  // A 409 says "not yours" whatever the value; a caller must not learn the bounds of a command
  // it may not send, and must not be told to fix a value when the value is not the problem.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA, check(HAM, WEB, OT_CONTROL_CMD_CH_SETPOINT, 9999));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA, check(HAM, WEB, OT_CONTROL_CMD_CH_ENABLE, 7));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_LOCAL, check(LOCAL, HA, OT_CONTROL_CMD_CH_ENABLE, 7));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_LOCAL, check(LOCAL, HA, OT_CONTROL_CMD_SEASON, 7));
}

void test_a_dhw_setpoint_of_zero_or_below_is_refused(void) {
  // 0 is the store's "unset": accepted, it would silently switch reconciliation off. The
  // upper bound is ID 48's and ot_command_encode() asks it first, so none is copied here.
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, check(LOCAL, WEB, OT_CONTROL_CMD_DHW_SETPOINT, 0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, check(HAM, HA, OT_CONTROL_CMD_DHW_SETPOINT, -5));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(LOCAL, WEB, OT_CONTROL_CMD_DHW_SETPOINT, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, check(HAM, HA, OT_CONTROL_CMD_DHW_SETPOINT, 800));
}

void test_an_unknown_command_is_refused(void) {
  for (ot_origin_t o : {WEB, HA}) {
    const ot_control_mode_t owner = o == WEB ? LOCAL : HAM;
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, check(owner, o, (ot_control_cmd_t)0, 0));
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, check(owner, o, (ot_control_cmd_t)6, 0));
  }
}

// === group: apply ===

static const uint32_t T0 = 1000000u;

static void garbage(ot_control_persist_t *p) { memset(p, 0xA5, sizeof *p); }

static bool persists_nothing(const ot_control_persist_t &p) {
  return !p.any && !p.set_local_ch_enable && !p.set_local_ch_setpoint && !p.set_dhw_enable &&
         !p.set_dhw_setpoint && !p.set_heating_season;
}

void test_a_refused_apply_leaves_the_executor_untouched_and_persists_nothing(void) {
  const ot_control_cfg_t k = cfg_in(HAM);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_t before;
  memcpy(&before, &c, sizeof c);
  ot_control_persist_t p;
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA,
                        ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
  TEST_ASSERT_TRUE_MESSAGE(persists_nothing(p), "a refusal asked to persist");
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE,
                        ot_control_apply(&c, &k, HA, OT_CONTROL_CMD_CH_SETPOINT, 900, T0, &p));
  TEST_ASSERT_TRUE(persists_nothing(p));
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_SEASON_ON_IS_LOCAL,
                        ot_control_apply(&c, &k, HA, OT_CONTROL_CMD_SEASON, 1, T0, &p));
  TEST_ASSERT_TRUE(persists_nothing(p));
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &c, sizeof c, "a refusal changed the executor");
}

void test_apply_judges_ownership_by_the_mode_the_executor_has_seen(void) {
  // The snapshot is built outside the lock, so it can predate a flip the step has already
  // seen. A web request snapshotted in LOCAL: the early answer says yes, the mode
  // went HA and the executor stepped, and the final answer -- the executor's -- says no.
  const ot_control_cfg_t local = cfg_in(LOCAL);
  const ot_control_cfg_t ham   = cfg_in(HAM);
  ot_control_t c;
  ot_control_init(&c, &local, nullptr, T0);
  ot_control_in_t  in = {};
  ot_control_out_t out;
  ot_control_step(&c, &ham, &in, T0 + 1000, &out);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_check(&local, WEB, OT_CONTROL_CMD_CH_ENABLE, 1));
  ot_control_persist_t p;
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA,
                        ot_control_apply(&c, &local, WEB, OT_CONTROL_CMD_CH_ENABLE, 1, T0 + 1500, &p));
  TEST_ASSERT_TRUE(persists_nothing(p));
  // And the other way: a fresh HA snapshot the executor has not stepped on does not let HA in.
  ot_control_t d;
  ot_control_init(&d, &local, nullptr, T0);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_LOCAL,
                        ot_control_apply(&d, &ham, HA, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
}

void test_web_ch_commands_persist_the_local_values(void) {
  const ot_control_cfg_t k = cfg_in(LOCAL);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_persist_t p;
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
  TEST_ASSERT_TRUE(p.any);
  TEST_ASSERT_TRUE(p.set_local_ch_enable);
  TEST_ASSERT_TRUE(p.local_ch_enable);
  TEST_ASSERT_FALSE(p.set_local_ch_setpoint || p.set_dhw_enable || p.set_dhw_setpoint ||
                    p.set_heating_season);
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_CH_SETPOINT, 600, T0, &p));
  TEST_ASSERT_TRUE(p.any && p.set_local_ch_setpoint);
  TEST_ASSERT_EQUAL_INT16(600, p.local_ch_setpoint_dc);
  TEST_ASSERT_FALSE(p.set_local_ch_enable || p.set_dhw_enable || p.set_dhw_setpoint ||
                    p.set_heating_season);
}

void test_dhw_and_season_persist_from_both_origins(void) {
  const struct {
    ot_origin_t       origin;
    ot_control_mode_t owner;
  } writers[] = {{WEB, LOCAL}, {HA, HAM}};
  for (const auto &w : writers) {
    const ot_control_cfg_t k = cfg_in(w.owner);
    ot_control_t c;
    ot_control_init(&c, &k, nullptr, T0);
    ot_control_persist_t p;
    garbage(&p);
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, w.origin, OT_CONTROL_CMD_DHW_ENABLE, 0, T0, &p));
    TEST_ASSERT_TRUE(p.any && p.set_dhw_enable);
    TEST_ASSERT_FALSE(p.dhw_enable);
    TEST_ASSERT_FALSE(p.set_local_ch_enable || p.set_local_ch_setpoint || p.set_dhw_setpoint ||
                      p.set_heating_season);
    garbage(&p);
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, w.origin, OT_CONTROL_CMD_DHW_SETPOINT, 480, T0, &p));
    TEST_ASSERT_TRUE(p.any && p.set_dhw_setpoint);
    TEST_ASSERT_EQUAL_INT16(480, p.dhw_setpoint_dc);
    TEST_ASSERT_FALSE(p.set_dhw_enable || p.set_heating_season);
    garbage(&p);
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, w.origin, OT_CONTROL_CMD_SEASON, 0, T0, &p));
    TEST_ASSERT_TRUE(p.any && p.set_heating_season);
    TEST_ASSERT_FALSE(p.heating_season);
    TEST_ASSERT_FALSE(p.set_dhw_enable || p.set_dhw_setpoint);
  }
  const ot_control_cfg_t k = cfg_in(HAM);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_SEASON, 1, T0, &p));
  TEST_ASSERT_TRUE(p.set_heating_season && p.heating_season);
}

void test_ha_ch_commands_live_in_ram_and_persist_nothing(void) {
  // HA's command is forgotten at every entry into its ownership. Persisted, a reboot
  // would bring it back -- exactly what ha_waiting exists to prevent.
  const ot_control_cfg_t k = cfg_in(HAM);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_persist_t p;
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, HA, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
  TEST_ASSERT_TRUE(persists_nothing(p));
  garbage(&p);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, HA, OT_CONTROL_CMD_CH_SETPOINT, 600, T0, &p));
  TEST_ASSERT_TRUE(persists_nothing(p));
}

void test_a_ch_setpoint_is_persisted_on_the_half_degree_grid(void) {
  // The store refuses a CH setpoint off its 5 dc grid; a web 45.05 C arrives as 451
  // and must be persisted as 450 -- rounded the same way the held ID 1 is.
  const ot_control_cfg_t k = cfg_in(LOCAL);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  const struct {
    int16_t in, persisted;
  } cases[] = {{451, 450}, {452, 450}, {453, 455}, {400, 400}, {700, 700}};
  for (const auto &x : cases) {
    ot_control_persist_t p;
    TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_CH_SETPOINT, x.in, T0, &p));
    TEST_ASSERT_EQUAL_INT16(x.persisted, p.local_ch_setpoint_dc);
  }
}

void test_an_unknown_origin_is_judged_as_the_web(void) {
  // In apply() as in check(): an origin that is not HA gets the web's rights and the web's path
  // -- a persisted LOCAL value, never HA's RAM command or the watchdog.
  const ot_control_cfg_t k = cfg_in(LOCAL);
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK,
                        ot_control_apply(&c, &k, (ot_origin_t)7, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
  TEST_ASSERT_TRUE(p.any && p.set_local_ch_enable && p.local_ch_enable);
  const ot_control_cfg_t h = cfg_in(HAM);
  ot_control_init(&c, &h, nullptr, T0);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA,
                        ot_control_apply(&c, &h, (ot_origin_t)7, OT_CONTROL_CMD_CH_ENABLE, 1, T0, &p));
}

void test_an_accepted_value_equal_to_the_stored_one_still_asks_to_persist(void) {
  // Comparing with the snapshot here would lose an update when two writers race (the second
  // write's snapshot predates the first's persist); the store skips unchanged writes itself.
  const ot_control_cfg_t k = cfg_in(LOCAL);   // local_ch_enable is already false
  ot_control_t c;
  ot_control_init(&c, &k, nullptr, T0);
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&c, &k, WEB, OT_CONTROL_CMD_CH_ENABLE, 0, T0, &p));
  TEST_ASSERT_TRUE(p.any && p.set_local_ch_enable);
  TEST_ASSERT_FALSE(p.local_ch_enable);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_state_names_are_the_wire_spellings);
  RUN_TEST(test_reason_names_are_the_wire_spellings);
  RUN_TEST(test_the_ownership_truth_table);
  RUN_TEST(test_the_season_truth_table);
  RUN_TEST(test_bool_values_other_than_0_and_1_are_refused_for_both_origins);
  RUN_TEST(test_a_ch_setpoint_outside_the_flow_bounds_is_refused_not_clamped);
  RUN_TEST(test_ownership_is_answered_before_the_value);
  RUN_TEST(test_a_dhw_setpoint_of_zero_or_below_is_refused);
  RUN_TEST(test_an_unknown_command_is_refused);
  RUN_TEST(test_a_refused_apply_leaves_the_executor_untouched_and_persists_nothing);
  RUN_TEST(test_apply_judges_ownership_by_the_mode_the_executor_has_seen);
  RUN_TEST(test_web_ch_commands_persist_the_local_values);
  RUN_TEST(test_dhw_and_season_persist_from_both_origins);
  RUN_TEST(test_ha_ch_commands_live_in_ram_and_persist_nothing);
  RUN_TEST(test_a_ch_setpoint_is_persisted_on_the_half_degree_grid);
  RUN_TEST(test_an_unknown_origin_is_judged_as_the_web);
  RUN_TEST(test_an_accepted_value_equal_to_the_stored_one_still_asks_to_persist);
  return UNITY_END();
}
