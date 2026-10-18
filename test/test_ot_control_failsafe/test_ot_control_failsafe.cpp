// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The watchdog and the bounded failsafe: what feeds the watchdog, the accumulator
// and its restore, the two summer bounds, the hysteresis with its minimum cycle, the heat-hours
// arm, and the counters that make a failsafe findable after the fact.
//
// The accumulator is ot_sensor's saturating add, copied rather than linked, so ot_sensor's
// two wrap tests are ported here in the executor's own terms.
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_control.h"
}

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T0   = 1000000u;
static const uint32_t HOUR = 3600000u;

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

static H boot(const ot_control_cfg_t &cfg, const ot_control_restore_t *r = nullptr,
              uint32_t start = T0) {
  H h;
  memset(&h, 0, sizeof h);
  h.cfg = cfg;
  h.now = start;
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

static ot_control_err_t send(H &h, ot_origin_t o, ot_control_cmd_t cmd, int16_t v) {
  ot_control_persist_t p;
  return ot_control_apply(&h.c, &h.cfg, o, cmd, v, h.now, &p);
}
static ot_control_err_t ha(H &h, ot_control_cmd_t cmd, int16_t v) { return send(h, OT_ORIGIN_HA, cmd, v); }

static bool ch(const H &h) { return (h.out.status_high & OT_STATUS_CH_ENABLE) != 0; }

#define ASSERT_STATE(s, h) TEST_ASSERT_EQUAL_STRING(ot_control_state_name(s), ot_control_state_name((h).out.state))
#define ASSERT_REASON(r, h) TEST_ASSERT_EQUAL_STRING(ot_control_reason_name(r), ot_control_reason_name((h).out.reason))

// === group: watchdog ===

void test_accepted_ha_ch_enable_and_ch_setpoint_feed_the_watchdog(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 100);
  TEST_ASSERT_EQUAL_UINT32(100000, h.out.overdue_ms);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_ENABLE, 0));
  tick(h);
  TEST_ASSERT_EQUAL_UINT32(1000, h.out.overdue_ms);
  ticks(h, 50);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600));
  tick(h);
  TEST_ASSERT_EQUAL_UINT32(1000, h.out.overdue_ms);
}

void test_nothing_else_feeds_the_watchdog(void) {
  // Not DHW, not the season, not the web, not a refused command -- a heartbeat proves HA
  // is alive, not that its controller is producing outputs.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 100);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_DHW_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_DHW_SETPOINT, 500));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ha(h, OT_CONTROL_CMD_SEASON, 0));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OWNED_BY_HA, send(h, OT_ORIGIN_WEB, OT_CONTROL_CMD_CH_ENABLE, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, send(h, OT_ORIGIN_WEB, OT_CONTROL_CMD_SEASON, 1));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, ha(h, OT_CONTROL_CMD_CH_SETPOINT, 900));
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OUT_OF_RANGE, ha(h, OT_CONTROL_CMD_CH_ENABLE, 2));
  tick(h);
  TEST_ASSERT_EQUAL_UINT32(101000, h.out.overdue_ms);
}

void test_the_overdue_accumulates_across_the_millisecond_wrap(void) {
  // Five minutes before uint32 milliseconds wrap. `now - fed_at` would read the wrap as a fresh
  // HA; the accumulator counts straight through it.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), nullptr, UINT32_MAX - 300000u);
  ticks(h, 899);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_UINT32(899000, h.out.overdue_ms);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
}

void test_the_overdue_saturates_instead_of_wrapping(void) {
  // An HA dead for sixty days must not come back round to "fresh".
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  for (int i = 0; i < 100; i++) tick(h, 0x4C4B4000u);   // ~14.8 days a step
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, h.out.overdue_ms);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
}

void test_a_restored_overdue_continues_counting(void) {
  // A reboot loop faster than watchdog_s would otherwise hold ha_waiting, CH off, for ever.
  const ot_control_restore_t r = {true, 850000u, 0};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &r);
  ticks(h, 49);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_UINT32(899000, h.out.overdue_ms);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
}

void test_an_invalid_restore_starts_from_zero(void) {
  // A power-on reset is not a loop: RTC_NOINIT holds garbage, and the magic word says so.
  const ot_control_restore_t r = {false, 850000u, 0};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &r);
  ticks(h, 50);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_UINT32(50000, h.out.overdue_ms);
}

void test_boot_in_local_mode_discards_a_restored_overdue(void) {
  // HA could not feed a watchdog it did not own: the first LOCAL step zeroes a restored overdue,
  // and a later switch to HA starts the count from nothing.
  const ot_control_restore_t r = {true, 899000u, 0};
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
  tick(h);
  TEST_ASSERT_EQUAL_UINT32(0, h.out.overdue_ms);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  ticks(h, 5);   // the first of them sees the switch and counts nothing
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_UINT32(4000, h.out.overdue_ms);
}

void test_an_expired_watchdog_is_zeroed_not_frozen_by_a_spell_of_local(void) {
  // HA dead, failsafe; the person takes over in LOCAL, then hands back to HA. HA gets a whole
  // watchdog_s to speak, not the failsafe it left behind.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 901);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  h.cfg.mode = OT_CONTROL_MODE_LOCAL;
  tick(h);
  ASSERT_STATE(OT_CONTROL_LOCAL, h);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  TEST_ASSERT_EQUAL_UINT32(0, h.out.overdue_ms);
}

void test_local_mode_holds_the_watchdog_at_zero(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL));
  ticks(h, 2000);
  TEST_ASSERT_EQUAL_UINT32(0, h.out.overdue_ms);
  h.cfg.mode = OT_CONTROL_MODE_HA;
  tick(h);   // the step that sees the switch: its interval was LOCAL's, and counts for nothing
  TEST_ASSERT_EQUAL_UINT32(0, h.out.overdue_ms);
  ticks(h, 899);
  ASSERT_STATE(OT_CONTROL_HA_WAITING, h);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
}

// === group: failsafe CH ===

// HA was heating, then died: failsafe, blind (no room source), armed.
static H dead_ha(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 3);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  ha(h, OT_CONTROL_CMD_CH_SETPOINT, 600);
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  return h;
}

void test_a_blind_failsafe_heats_at_the_failsafe_setpoint(void) {
  H h = dead_ha();
  ticks(h, 3);
  TEST_ASSERT_TRUE(ch(h));
  TEST_ASSERT_EQUAL_INT16(450, h.out.held_setpoint_dc);
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
}

void test_a_failsafe_never_heats_with_the_season_off(void) {
  // Bound 1: HA's own summer logic turns the season off, and ladder row 1 outranks row 5.
  H h = dead_ha();
  ticks(h, 3);
  TEST_ASSERT_TRUE(ch(h));
  h.cfg.heating_season = false;
  for (int i = 0; i < 100; i++) {
    tick(h);
    ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
    TEST_ASSERT_FALSE(ch(h));
  }
}

void test_the_failsafe_is_disarmed_after_failsafe_heat_days_without_a_heat_request(void) {
  // Bound 2, the DIYLESS July: a controller that has not asked for heat in three days is
  // not in heating season, whatever the switch says. 3 days = 72 powered hours.
  const ot_control_restore_t disarmed = {true, 900000u, 72};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &disarmed);
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_FALSE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_DISARMED, h);
  const ot_control_restore_t armed = {true, 900000u, 71};
  h = boot(cfg_in(OT_CONTROL_MODE_HA), &armed);
  ticks(h, 3);
  TEST_ASSERT_TRUE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
  ot_control_cfg_t one_day = cfg_in(OT_CONTROL_MODE_HA);
  one_day.failsafe_heat_days = 1;
  const ot_control_restore_t at24 = {true, 900000u, 24};
  h = boot(one_day, &at24);
  ticks(h, 3);
  ASSERT_REASON(OT_CONTROL_REASON_FS_DISARMED, h);
}

void test_an_ha_heat_request_rearms_even_a_blind_failsafe(void) {
  const ot_control_restore_t disarmed = {false, 0, 72};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &disarmed);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 0);   // HA speaks, but asks for no heat
  h.in.ha_forwarded_stale = true;
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  ASSERT_REASON(OT_CONTROL_REASON_FS_DISARMED, h);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);   // blind still: stays failsafe, but armed now
  ticks(h, 2);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
  TEST_ASSERT_TRUE(ch(h));
}

void test_a_dead_ha_is_reported_as_dead_not_as_blind(void) {
  H h = dead_ha();
  h.in.ha_forwarded_stale = true;
  tick(h);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_WATCHDOG, h.out.cause);
}

// Failsafe with a fresh room source at `room`, CH down, the minimum cycle as given.
static H fresh_room(int16_t room, uint16_t min_cycle_s) {
  ot_control_cfg_t k = cfg_in(OT_CONTROL_MODE_HA);
  k.failsafe_min_cycle_s = min_cycle_s;
  const ot_control_restore_t expired = {true, 900000u, 0};
  H h = boot(k, &expired);
  h.in.room_fresh = true;
  h.in.room_dc = room;
  ticks(h, 3);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  return h;
}

void test_the_hysteresis_switches_three_tenths_either_side_of_the_target(void) {
  H h = fresh_room(181, 60);   // target 180: 181 is inside the band, and CH was down
  TEST_ASSERT_FALSE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_WARM, h);
  h.in.room_dc = 178;
  tick(h);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "178 is inside the band");
  h.in.room_dc = 177;
  tick(h);
  TEST_ASSERT_TRUE_MESSAGE(ch(h), "177 is target - 0.3 K");
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_COLD, h);
  h.in.room_dc = 182;
  tick(h, 60000);
  TEST_ASSERT_TRUE_MESSAGE(ch(h), "182 is inside the band");
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_COLD, h);
  h.in.room_dc = 183;
  tick(h);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "183 is target + 0.3 K");
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_WARM, h);
  TEST_ASSERT_EQUAL_INT16(450, h.out.held_setpoint_dc);
}

void test_the_minimum_cycle_holds_the_bit_both_ways(void) {
  H h = fresh_room(177, 600);   // CH rose on the second step, one second ago
  TEST_ASSERT_TRUE(ch(h));
  h.in.room_dc = 183;
  tick(h, 598000);              // 599 s after the rising edge
  TEST_ASSERT_TRUE_MESSAGE(ch(h), "minimum on time");
  ASSERT_REASON(OT_CONTROL_REASON_MIN_CYCLE, h);
  tick(h);
  TEST_ASSERT_FALSE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_WARM, h);
  h.in.room_dc = 177;
  tick(h, 599000);
  TEST_ASSERT_FALSE_MESSAGE(ch(h), "minimum off time");
  ASSERT_REASON(OT_CONTROL_REASON_MIN_CYCLE, h);
  tick(h);
  TEST_ASSERT_TRUE(ch(h));
}

void test_a_room_source_lost_mid_failsafe_heats_blind_at_once(void) {
  // Pinned deliberately: the minimum cycle belongs to the hysteresis, and "no fresh
  // source" is "CH=1, blind" (point 4) -- a freeze costs more than a short cycle.
  H h = fresh_room(183, 600);
  TEST_ASSERT_FALSE(ch(h));
  h.in.room_fresh = false;
  tick(h);
  TEST_ASSERT_TRUE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
}

void test_a_failsafe_entered_with_ch_up_keeps_heating_inside_the_band(void) {
  // The hysteresis starts from what the boiler was last told: HA was heating, the room is at the
  // target, inside the band -- the failsafe carries on rather than cutting a running burner.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  h.in.room_fresh = true;
  h.in.room_dc = 180;
  ticks(h, 3);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_TRUE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_COLD, h);
}

void test_a_room_source_returning_inside_the_band_keeps_the_blind_verdict(void) {
  // Blind, the failsafe heats; a source that comes back inside the band finds CH up and holds
  // it, instead of the verdict from before the blindness.
  ot_control_cfg_t k = cfg_in(OT_CONTROL_MODE_HA);
  k.failsafe_min_cycle_s = 60;
  const ot_control_restore_t expired = {true, 900000u, 0};
  H h = boot(k, &expired);
  ticks(h, 3);
  TEST_ASSERT_TRUE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
  h.in.room_fresh = true;
  h.in.room_dc = 180;
  tick(h, 61000);
  TEST_ASSERT_TRUE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_COLD, h);
}

void test_a_rearmed_failsafe_does_not_inherit_the_blind_verdict(void) {
  // Blind (CH up), then disarmed (CH down), then re-armed while HA is still blind, then a room
  // source inside the band: the band holds what the boiler was last told -- down -- not the
  // verdict from before the disarm.
  ot_control_cfg_t k = cfg_in(OT_CONTROL_MODE_HA);
  k.failsafe_min_cycle_s = 60;
  const ot_control_restore_t r = {false, 0, 71};
  H h = boot(k, &r);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 0);
  h.in.ha_forwarded_stale = true;
  ticks(h, 3);
  ASSERT_REASON(OT_CONTROL_REASON_FS_BLIND, h);
  TEST_ASSERT_TRUE(ch(h));
  tick(h, HOUR);
  ASSERT_REASON(OT_CONTROL_REASON_FS_DISARMED, h);
  TEST_ASSERT_FALSE(ch(h));
  tick(h, 61000);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  h.in.room_fresh = true;
  h.in.room_dc = 180;
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_FALSE(ch(h));
  ASSERT_REASON(OT_CONTROL_REASON_FS_ROOM_WARM, h);
}

// === group: heat hours ===

void test_an_ha_heat_request_resets_heat_hours_and_asks_to_persist_once(void) {
  const ot_control_restore_t r = {false, 0, 50};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &r);
  tick(h);
  TEST_ASSERT_EQUAL_UINT16(50, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  tick(h);
  TEST_ASSERT_EQUAL_UINT16(0, h.out.heat_hours);
  TEST_ASSERT_TRUE(h.out.persist_heat_hours);
  tick(h);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
}

void test_a_keepalive_of_ch_enable_1_does_not_write_nvs_every_minute(void) {
  // The V3 package re-sends CH_ENABLE = 1 every minute. A reset of a count that is already 0 is
  // not a change, and a write per minute would wear NVS for nothing.
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  for (int i = 0; i < 120; i++) {
    ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
    tick(h, 60000);
    TEST_ASSERT_FALSE(h.out.persist_heat_hours);
    TEST_ASSERT_EQUAL_UINT16(0, h.out.heat_hours);
  }
}

void test_ch_enable_0_is_not_a_heat_request(void) {
  const ot_control_restore_t r = {false, 0, 50};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &r);
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 0);
  tick(h);
  TEST_ASSERT_EQUAL_UINT16(50, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
}

void test_each_full_powered_hour_counts_and_asks_to_persist(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL));
  tick(h, HOUR - 1000);
  TEST_ASSERT_EQUAL_UINT16(0, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
  tick(h, 1000);
  TEST_ASSERT_EQUAL_UINT16(1, h.out.heat_hours);
  TEST_ASSERT_TRUE(h.out.persist_heat_hours);
  tick(h);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
  tick(h, 2 * HOUR);   // a late step does not lose hours
  TEST_ASSERT_EQUAL_UINT16(3, h.out.heat_hours);
  TEST_ASSERT_TRUE(h.out.persist_heat_hours);
  tick(h);             // ...nor counts one twice
  TEST_ASSERT_EQUAL_UINT16(3, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
}

void test_nothing_is_asked_once_the_limit_is_reached(void) {
  // "Written only while below the limit, so a disarmed device stops writing".
  const ot_control_restore_t r = {false, 0, 71};
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
  tick(h, HOUR);
  TEST_ASSERT_EQUAL_UINT16(72, h.out.heat_hours);
  TEST_ASSERT_TRUE_MESSAGE(h.out.persist_heat_hours, "the write that reaches the limit");
  tick(h, HOUR);
  TEST_ASSERT_EQUAL_UINT16(73, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
}

void test_heat_hours_saturate(void) {
  const ot_control_restore_t r = {false, 0, 0xFFFE};
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
  tick(h, 5 * HOUR);
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, h.out.heat_hours);
  TEST_ASSERT_FALSE(h.out.persist_heat_hours);
}

void test_a_part_hour_carries_over(void) {
  // Powered minutes are not lost to a step that completes an hour: 1.5 h then 0.5 h is 2 h.
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL));
  tick(h, HOUR + HOUR / 2);
  TEST_ASSERT_EQUAL_UINT16(1, h.out.heat_hours);
  tick(h, HOUR / 2);
  TEST_ASSERT_EQUAL_UINT16(2, h.out.heat_hours);
  TEST_ASSERT_TRUE(h.out.persist_heat_hours);
}

void test_a_reboot_loop_still_reaches_the_disarm(void) {
  // Bound 2 against a reboot every 30 minutes: heat_hours comes back from NVS (written
  // when an hour completes), the part-hour from RTC_NOINIT. Without the part-hour no boot ever
  // completes an hour, and 200 powered hours leave a 72-hour arm untouched.
  uint16_t nvs = 0;
  bool     rtc_valid = false;
  uint32_t rtc_hh = 0;
  for (int b = 0; b < 400; b++) {
    const ot_control_restore_t r = {false, 0, nvs, rtc_valid, rtc_hh};
    H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
    for (int i = 0; i < 30; i++) {
      tick(h, 60000);
      if (h.out.persist_heat_hours) nvs = h.out.heat_hours;
      rtc_hh = h.out.hh_ms;
      rtc_valid = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT16(72, nvs);
  const ot_control_restore_t dead_ha = {true, 900000u, nvs, rtc_valid, rtc_hh};
  H h = boot(cfg_in(OT_CONTROL_MODE_HA), &dead_ha);
  tick(h);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  ASSERT_REASON(OT_CONTROL_REASON_FS_DISARMED, h);
}

void test_an_invalid_part_hour_restore_starts_from_zero(void) {
  const ot_control_restore_t r = {false, 0, 0, false, HOUR / 2};
  H h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &r);
  tick(h, HOUR / 2);
  TEST_ASSERT_EQUAL_UINT16(0, h.out.heat_hours);
  const ot_control_restore_t v = {false, 0, 0, true, HOUR / 2};
  h = boot(cfg_in(OT_CONTROL_MODE_LOCAL), &v);
  tick(h, HOUR / 2);
  TEST_ASSERT_EQUAL_UINT16(1, h.out.heat_hours);
}

// === group: counters ===

void test_failsafe_count_increments_on_each_entry(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  tick(h);
  TEST_ASSERT_EQUAL_UINT32(0, h.out.failsafe_count);
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_UINT32(1, h.out.failsafe_count);
  ticks(h, 10);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, h.out.failsafe_count, "counted per step, not per entry");
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  TEST_ASSERT_EQUAL_UINT32(2, h.out.failsafe_count);
}

void test_last_failsafe_duration_is_set_on_exit(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 900);
  ASSERT_STATE(OT_CONTROL_FAILSAFE, h);
  ticks(h, 89);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, h.out.last_failsafe_duration_s, "not before it ends");
  ha(h, OT_CONTROL_CMD_CH_ENABLE, 1);
  tick(h);
  ASSERT_STATE(OT_CONTROL_HA, h);
  TEST_ASSERT_EQUAL_UINT32(90, h.out.last_failsafe_duration_s);
}

void test_the_season_going_off_ends_a_failsafe_too(void) {
  H h = boot(cfg_in(OT_CONTROL_MODE_HA));
  ticks(h, 900);
  ticks(h, 29);
  h.cfg.heating_season = false;
  tick(h);
  ASSERT_STATE(OT_CONTROL_SEASON_OFF, h);
  TEST_ASSERT_EQUAL_UINT32(30, h.out.last_failsafe_duration_s);
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_REASON_NONE, h.out.cause);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_accepted_ha_ch_enable_and_ch_setpoint_feed_the_watchdog);
  RUN_TEST(test_nothing_else_feeds_the_watchdog);
  RUN_TEST(test_the_overdue_accumulates_across_the_millisecond_wrap);
  RUN_TEST(test_the_overdue_saturates_instead_of_wrapping);
  RUN_TEST(test_a_restored_overdue_continues_counting);
  RUN_TEST(test_an_invalid_restore_starts_from_zero);
  RUN_TEST(test_boot_in_local_mode_discards_a_restored_overdue);
  RUN_TEST(test_local_mode_holds_the_watchdog_at_zero);
  RUN_TEST(test_an_expired_watchdog_is_zeroed_not_frozen_by_a_spell_of_local);
  RUN_TEST(test_a_blind_failsafe_heats_at_the_failsafe_setpoint);
  RUN_TEST(test_a_failsafe_never_heats_with_the_season_off);
  RUN_TEST(test_the_failsafe_is_disarmed_after_failsafe_heat_days_without_a_heat_request);
  RUN_TEST(test_an_ha_heat_request_rearms_even_a_blind_failsafe);
  RUN_TEST(test_a_dead_ha_is_reported_as_dead_not_as_blind);
  RUN_TEST(test_the_hysteresis_switches_three_tenths_either_side_of_the_target);
  RUN_TEST(test_the_minimum_cycle_holds_the_bit_both_ways);
  RUN_TEST(test_a_room_source_lost_mid_failsafe_heats_blind_at_once);
  RUN_TEST(test_a_failsafe_entered_with_ch_up_keeps_heating_inside_the_band);
  RUN_TEST(test_a_room_source_returning_inside_the_band_keeps_the_blind_verdict);
  RUN_TEST(test_a_rearmed_failsafe_does_not_inherit_the_blind_verdict);
  RUN_TEST(test_an_ha_heat_request_resets_heat_hours_and_asks_to_persist_once);
  RUN_TEST(test_a_keepalive_of_ch_enable_1_does_not_write_nvs_every_minute);
  RUN_TEST(test_ch_enable_0_is_not_a_heat_request);
  RUN_TEST(test_each_full_powered_hour_counts_and_asks_to_persist);
  RUN_TEST(test_nothing_is_asked_once_the_limit_is_reached);
  RUN_TEST(test_heat_hours_saturate);
  RUN_TEST(test_a_part_hour_carries_over);
  RUN_TEST(test_a_reboot_loop_still_reaches_the_disarm);
  RUN_TEST(test_an_invalid_part_hour_restore_starts_from_zero);
  RUN_TEST(test_failsafe_count_increments_on_each_entry);
  RUN_TEST(test_last_failsafe_duration_is_set_on_exit);
  RUN_TEST(test_the_season_going_off_ends_a_failsafe_too);
  return UNITY_END();
}
