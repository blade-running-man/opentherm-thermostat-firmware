// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The rules that span fields, decided on the MERGED document: the patch's value where it has one,
// the stored one where it does not. Each field alone can be legal
// while the document is a device with no legal setpoint, and that is what these cases are about.
//
// Cut out of test/test_config_apply/: that suite had reached 576 of its
// 600 lines and these rules were about to grow, and a suite can only be cut along directories,
// because PlatformIO links every .cpp of a directory into one binary (CLAUDE.md, File ceiling).
// The helpers below are this directory's OWN copy of the ones its tests call. DO NOT hoist them
// into a shared directory: once two suites share a helper, "it grew a capability for suite B"
// becomes a way to quietly weaken suite A (CLAUDE.md, Tests).
//
// Only the PURE half of ot_config is reached here, as in every test_config_* directory.
#include <unity.h>

#include <cstring>

#include "ot_config.h"

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static ot_config_t fresh(void) {
  ot_config_t cfg{};
  ot_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

void test_the_flow_bounds_are_judged_against_the_stored_half_of_the_pair(void) {
  // Each value alone is legal: 700 is a fine lowest flow and 400 a fine highest. On the MERGED
  // document -- the patch's value where it has one, the stored one where it does not -- each is
  // a band with nothing inside it.
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  ot_config_patch_t lo{};
  lo.has_flow_min_dc = true;
  lo.flow_min_dc     = 700;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FLOW, ot_config_apply(&cfg, &lo, nullptr));
  ot_config_patch_t hi{};
  hi.has_flow_max_dc = true;
  hi.flow_max_dc     = 400;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FLOW, ot_config_apply(&cfg, &hi, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);

  // Both halves in one document are judged together, which is how a page moves the whole band;
  // the stored failsafe 450 sits on its new lower edge, and the edge is inside.
  ot_config_patch_t both{};
  both.has_flow_min_dc = true;
  both.flow_min_dc     = 450;
  both.has_flow_max_dc = true;
  both.flow_max_dc     = 800;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &both, nullptr));
  TEST_ASSERT_EQUAL_UINT16(450, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(800, cfg.flow_max_dc);
}

void test_moving_the_band_away_from_the_stored_failsafe_setpoint_is_refused(void) {
  // The failsafe setpoint is what a dead Home Assistant heats the house at, blind. A band
  // that no longer contains it is a failsafe the executor cannot carry out.
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  ot_config_patch_t up{};
  up.has_flow_min_dc = true;
  up.flow_min_dc     = 500;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FAILSAFE, ot_config_apply(&cfg, &up, nullptr));
  ot_config_patch_t fs{};
  fs.has_failsafe_setpoint_dc = true;
  fs.failsafe_setpoint_dc     = 750;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FAILSAFE, ot_config_apply(&cfg, &fs, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);

  up.has_failsafe_setpoint_dc = true;
  up.failsafe_setpoint_dc     = 500;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &up, nullptr));
  TEST_ASSERT_EQUAL_UINT16(500, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(500, cfg.failsafe_setpoint_dc);
}

void test_home_assistant_mode_needs_a_broker_in_the_same_document_or_the_stored_one(void) {
  ot_config_t cfg = fresh();
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  ot_config_patch_t ha{};
  ha.has_control_mode = true;
  ha.control_mode     = 1;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_MODE_NEEDS_BROKER, ot_config_apply(&cfg, &ha, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);

  ha.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &ha, nullptr));
  TEST_ASSERT_EQUAL_UINT16(1, cfg.control_mode);

  // And the broker cannot be taken away from under it: the merged document is HA with no host.
  ot_config_patch_t clear{};
  clear.mqtt_host = "";
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_MODE_NEEDS_BROKER, ot_config_apply(&cfg, &clear, nullptr));
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
  clear.has_control_mode = true;
  clear.control_mode     = 0;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &clear, nullptr));
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
}

// --- a local setpoint the band strands ---------------------------------------

void test_a_stored_local_setpoint_a_new_band_strands_moves_to_its_nearest_edge(void) {
  // The probe: local 650 stored, then flow_max 600. Refusing the band change is
  // not possible -- local_ch_setpoint_dc is read-only on the wire, so the owner could not resolve
  // the refusal -- and leaving 650 behind makes ot_control clamp it silently: ID 1 = 60.0 on the
  // bus while GET /api/config shows 65.0. So the merge step moves it, in the same commit.
  ot_config_t       cfg = fresh();
  ot_config_patch_t local{};
  local.has_local_ch_setpoint_dc = true;
  local.local_ch_setpoint_dc     = 650;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &local, nullptr));
  ot_config_patch_t band{};
  band.has_flow_max_dc = true;
  band.flow_max_dc     = 600;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &band, nullptr));
  TEST_ASSERT_EQUAL_UINT16(600, cfg.flow_max_dc);
  TEST_ASSERT_EQUAL_UINT16(600, cfg.local_ch_setpoint_dc);

  // From below too, and this time the caller asks what moved.
  local.local_ch_setpoint_dc = 450;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&cfg, &local, nullptr));
  ot_config_patch_t up{};
  up.has_flow_min_dc          = true;
  up.flow_min_dc              = 500;
  up.has_failsafe_setpoint_dc = true;
  up.failsafe_setpoint_dc     = 500;
  ot_config_repairs_t moved   = 0xffffffffu;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply_ex(&cfg, &up, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT16(500, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT), moved);
}

void test_a_local_setpoint_inside_the_band_stays_and_a_refused_band_moves_nothing(void) {
  ot_config_t       cfg = fresh();  // local 450 inside 400..700
  ot_config_patch_t band{};
  band.has_flow_max_dc      = true;
  band.flow_max_dc          = 600;
  ot_config_repairs_t moved = 0xffffffffu;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply_ex(&cfg, &band, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT16(450, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(0, moved);

  // All or nothing: a band refused for another reason -- the stored failsafe 450 falls out of it --
  // moves nothing either, although it would have stranded the local setpoint too.
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  ot_config_patch_t bad{};
  bad.has_flow_min_dc = true;
  bad.flow_min_dc     = 500;
  moved               = 0xffffffffu;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_FAILSAFE, ot_config_apply_ex(&cfg, &bad, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT32(0, moved);
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

void test_a_local_setpoint_sent_outside_the_band_is_moved_like_a_stranded_one(void) {
  // The executor checks a command against the snapshot it holds, and a band saved over REST can
  // land between that check and this persist. The same rule answers both cases: the band wins,
  // and the caller is told.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.has_local_ch_setpoint_dc = true;
  p.local_ch_setpoint_dc     = 800;
  ot_config_repairs_t moved  = 0;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply_ex(&cfg, &p, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT16(700, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT), moved);
  // NULL is "do not tell me", never a crash.
  p.local_ch_setpoint_dc = 100;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply_ex(&cfg, &p, nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT16(400, cfg.local_ch_setpoint_dc);
}

void test_one_patch_that_moves_the_band_and_sends_the_local_setpoint_past_it(void) {
  // The band and the setpoint in ONE document: 850 is judged against the MERGED band 450..800 and
  // lands on its new top, 800 -- not on the stored top 700 it would have been clamped to alone.
  ot_config_t       cfg = fresh();
  ot_config_patch_t p{};
  p.has_flow_min_dc          = true;
  p.flow_min_dc              = 450;
  p.has_flow_max_dc          = true;
  p.flow_max_dc              = 800;
  p.has_local_ch_setpoint_dc = true;
  p.local_ch_setpoint_dc     = 850;
  ot_config_repairs_t moved  = 0;
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply_ex(&cfg, &p, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT16(450, cfg.flow_min_dc);
  TEST_ASSERT_EQUAL_UINT16(800, cfg.flow_max_dc);
  TEST_ASSERT_EQUAL_UINT16(800, cfg.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT), moved);
}

void test_a_read_only_store_refuses_apply_ex_and_reports_that_nothing_moved(void) {
  // A rollback store, through the reporting form: the refusal comes before the merge step, and
  // *moved is zeroed before the refusal, so a caller never prints a move that did not happen.
  ot_config_t cfg          = fresh();
  cfg.read_only            = true;
  cfg.local_ch_setpoint_dc = 650;
  ot_config_t before;
  memcpy(&before, &cfg, sizeof before);
  ot_config_patch_t band{};
  band.has_flow_max_dc      = true;
  band.flow_max_dc          = 600;
  ot_config_repairs_t moved = 0xffffffffu;
  TEST_ASSERT_EQUAL(OT_CONFIG_ERR_READ_ONLY, ot_config_apply_ex(&cfg, &band, nullptr, &moved));
  TEST_ASSERT_EQUAL_UINT32(0, moved);
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_flow_bounds_are_judged_against_the_stored_half_of_the_pair);
  RUN_TEST(test_moving_the_band_away_from_the_stored_failsafe_setpoint_is_refused);
  RUN_TEST(test_home_assistant_mode_needs_a_broker_in_the_same_document_or_the_stored_one);
  RUN_TEST(test_a_stored_local_setpoint_a_new_band_strands_moves_to_its_nearest_edge);
  RUN_TEST(test_a_local_setpoint_inside_the_band_stays_and_a_refused_band_moves_nothing);
  RUN_TEST(test_a_local_setpoint_sent_outside_the_band_is_moved_like_a_stranded_one);
  RUN_TEST(test_one_patch_that_moves_the_band_and_sends_the_local_setpoint_past_it);
  RUN_TEST(test_a_read_only_store_refuses_apply_ex_and_reports_that_nothing_moved);
  return UNITY_END();
}
