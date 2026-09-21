// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The executor's translations (ot_control_io.h), the inbound half: the configuration snapshot,
// the patch a command persists through, the bus's report, the ID 56 readback and the numbers a
// request carries. The outbound half -- what survives a reset and what the executor shows -- is
// test_ot_control_io_state, cut along the header's own sections at the 600-line suite ceiling.
//
// It links the REAL ot_control, ot_config and ot_frame, on purpose: what is pinned here is the
// seam between them -- the configuration snapshot against ot_config's defaults and projection, the
// executor's patch against ot_config_apply(), the f8.8 codec against ot_codec_float_to_f88(), the
// bus's report and the ID 56 readback against the real executor's step. A fake of either side pins
// the fake.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "ot_codec.h"
#include "ot_config.h"
#include "ot_control.h"
#include "ot_control_io.h"

void setUp(void) {}
void tearDown(void) {}

static ot_config_public_t pub_of(ot_config_t *store) {
  ot_config_defaults(store, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(store, &pub);
  return pub;
}

// --- the configuration ---------------------------------------------------------------------------

// Every field by value, each distinct, so a crossed wire names itself.
void test_the_config_maps_field_by_field(void) {
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  pub.control_mode            = OT_CONFIG_MODE_HA;
  pub.heating_season          = true;
  pub.watchdog_s              = 1234;
  pub.failsafe_setpoint_dc    = 455;
  pub.failsafe_room_target_dc = 215;
  pub.failsafe_heat_days      = 7;
  pub.failsafe_min_cycle_s    = 777;
  pub.flow_min_dc             = 405;
  pub.flow_max_dc             = 695;
  pub.local_ch_enable         = true;
  pub.local_ch_setpoint_dc    = 505;
  pub.dhw_enable              = false;
  pub.dhw_setpoint_dc         = 523;

  ot_control_cfg_t c;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_EQUAL(OT_CONTROL_MODE_HA, c.mode);
  TEST_ASSERT_TRUE(c.heating_season);
  TEST_ASSERT_EQUAL_UINT16(1234, c.watchdog_s);
  TEST_ASSERT_EQUAL_INT16(455, c.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_INT16(215, c.failsafe_room_target_dc);
  TEST_ASSERT_EQUAL_UINT8(7, c.failsafe_heat_days);
  TEST_ASSERT_EQUAL_UINT16(777, c.failsafe_min_cycle_s);
  TEST_ASSERT_EQUAL_INT16(405, c.flow_min_dc);
  TEST_ASSERT_EQUAL_INT16(695, c.flow_max_dc);
  TEST_ASSERT_TRUE(c.local_ch_enable);
  TEST_ASSERT_EQUAL_INT16(505, c.local_ch_setpoint_dc);
  TEST_ASSERT_FALSE(c.dhw_enable);
  TEST_ASSERT_TRUE(c.dhw_setpoint_set);
  TEST_ASSERT_EQUAL_INT16(523, c.dhw_setpoint_dc);
}

// A freshly flashed device asks for nothing, and the mapping must not be
// the place that changes it.
void test_the_defaults_map_to_a_device_that_asks_for_nothing(void) {
  ot_config_t store;
  const ot_config_public_t pub = pub_of(&store);
  ot_control_cfg_t c;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_EQUAL(OT_CONTROL_MODE_LOCAL, c.mode);
  TEST_ASSERT_FALSE(c.heating_season);
  TEST_ASSERT_FALSE(c.local_ch_enable);
  TEST_ASSERT_TRUE(c.dhw_enable);
  TEST_ASSERT_FALSE_MESSAGE(c.dhw_setpoint_set, "0 is the store's unset");
  TEST_ASSERT_EQUAL_INT16(450, c.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_INT16(400, c.flow_min_dc);
  TEST_ASSERT_EQUAL_INT16(700, c.flow_max_dc);
}

// Only the store's HA is HA. A damaged number that sanitize somehow missed is LOCAL -- the mode in
// which HA owns nothing -- never a guess at HA.
void test_a_mode_other_than_ha_is_local(void) {
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  const uint16_t local[] = {0, 2, 7, 0xFFFF};
  for (uint16_t m : local) {
    pub.control_mode = m;
    ot_control_cfg_t c;
    ot_control_io_cfg(&pub, &c);
    TEST_ASSERT_EQUAL(OT_CONTROL_MODE_LOCAL, c.mode);
  }
  pub.control_mode = 1;
  ot_control_cfg_t c;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_EQUAL(OT_CONTROL_MODE_HA, c.mode);
}

void test_dhw_setpoint_zero_is_unset_and_anything_else_is_set(void) {
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  ot_control_cfg_t c;
  pub.dhw_setpoint_dc = 0;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_FALSE(c.dhw_setpoint_set);
  pub.dhw_setpoint_dc = 1;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_TRUE(c.dhw_setpoint_set);
  TEST_ASSERT_EQUAL_INT16(1, c.dhw_setpoint_dc);
}

// uint16_t in the store, int16_t and uint8_t in the executor. A wrap would turn 40000 into a
// NEGATIVE setpoint and 300 days into 44; a saturation keeps the order of magnitude.
void test_a_number_too_wide_saturates_rather_than_wraps(void) {
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  pub.failsafe_setpoint_dc = 40000;
  pub.local_ch_setpoint_dc = 0xFFFF;
  pub.dhw_setpoint_dc      = 0xFFFF;
  pub.failsafe_heat_days   = 300;
  pub.flow_max_dc          = 32768;
  ot_control_cfg_t c;
  ot_control_io_cfg(&pub, &c);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, c.failsafe_setpoint_dc);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, c.local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, c.dhw_setpoint_dc);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, c.flow_max_dc);
  TEST_ASSERT_EQUAL_UINT8(255, c.failsafe_heat_days);
}

// --- the patch -----------------------------------------------------------------------------------

void test_nothing_to_persist_is_an_empty_patch(void) {
  ot_control_persist_t p;
  memset(&p, 0, sizeof p);
  ot_config_patch_t patch, zero;
  memset(&patch, 0xA5, sizeof patch);
  memset(&zero, 0, sizeof zero);
  TEST_ASSERT_FALSE(ot_control_io_patch(&p, &patch));
  TEST_ASSERT_EQUAL_MEMORY(&zero, &patch, sizeof patch);
}

// One persisted field lands in exactly one patch member, and nothing else is present: a patch
// member set by accident is a stored value overwritten by accident.
void test_each_persisted_field_lands_in_its_own_member_and_nowhere_else(void) {
  for (int which = 0; which < 5; which++) {
    ot_control_persist_t p;
    memset(&p, 0, sizeof p);
    p.any = true;
    ot_config_patch_t want;
    memset(&want, 0, sizeof want);
    switch (which) {
    case 0: p.set_local_ch_enable = true; p.local_ch_enable = true;
            want.has_local_ch_enable = true; want.local_ch_enable = true; break;
    case 1: p.set_local_ch_setpoint = true; p.local_ch_setpoint_dc = 505;
            want.has_local_ch_setpoint_dc = true; want.local_ch_setpoint_dc = 505; break;
    case 2: p.set_dhw_enable = true; p.dhw_enable = true;
            want.has_dhw_enable = true; want.dhw_enable = true; break;
    case 3: p.set_dhw_setpoint = true; p.dhw_setpoint_dc = 550;
            want.has_dhw_setpoint_dc = true; want.dhw_setpoint_dc = 550; break;
    case 4: p.set_heating_season = true; p.heating_season = true;
            want.has_heating_season = true; want.heating_season = true; break;
    }
    ot_config_patch_t got;
    memset(&got, 0xA5, sizeof got);
    TEST_ASSERT_TRUE(ot_control_io_patch(&p, &got));
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&want, &got, sizeof got, "the patch carries one field");
  }
}

// A value the executor was not asked to persist stays out even when the struct carries one: the
// flag decides, not whatever is left in the value beside it.
void test_an_unflagged_value_does_not_travel(void) {
  ot_control_persist_t p;
  memset(&p, 0, sizeof p);
  p.any = true;
  p.set_dhw_enable       = true;
  p.local_ch_enable      = true;
  p.local_ch_setpoint_dc = 505;
  ot_config_patch_t got;
  ot_control_io_patch(&p, &got);
  TEST_ASSERT_FALSE(got.has_local_ch_enable);
  TEST_ASSERT_FALSE(got.local_ch_enable);
  TEST_ASSERT_FALSE(got.has_local_ch_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(0, got.local_ch_setpoint_dc);
}

// The seam that matters: ot_wire refuses the four executor fields from a settings body, and the
// store must still take them from the executor, within their bounds.
void test_a_patch_the_executor_builds_is_one_the_store_accepts(void) {
  ot_config_t store;
  ot_config_defaults(&store, "aabbccddeeff");
  ot_control_persist_t p;
  memset(&p, 0, sizeof p);
  p.any = p.set_local_ch_enable = p.set_local_ch_setpoint = true;
  p.set_dhw_enable = p.set_dhw_setpoint = p.set_heating_season = true;
  p.local_ch_enable = true;  p.local_ch_setpoint_dc = 505;
  p.dhw_enable = false;      p.dhw_setpoint_dc = 550;
  p.heating_season = true;
  ot_config_patch_t patch;
  TEST_ASSERT_TRUE(ot_control_io_patch(&p, &patch));
  TEST_ASSERT_EQUAL(OT_CONFIG_OK, ot_config_apply(&store, &patch, nullptr));
  TEST_ASSERT_TRUE(store.local_ch_enable);
  TEST_ASSERT_EQUAL_UINT16(505, store.local_ch_setpoint_dc);
  TEST_ASSERT_FALSE(store.dhw_enable);
  TEST_ASSERT_EQUAL_UINT16(550, store.dhw_setpoint_dc);
  TEST_ASSERT_TRUE(store.heating_season);
}

// The store is the last line, and the patch must not get round it: a value off the half-degree
// grid or below zero is refused as it is, never rounded or wrapped into a legal one on the
// way. ot_thermostat answers such a refusal with an error, never with a silent success.
void test_the_store_refuses_what_the_grid_refuses_and_a_negative_never_wraps(void) {
  ot_config_t store;
  ot_config_defaults(&store, "aabbccddeeff");
  const int16_t bad[] = {453, -5, -32768};
  for (int16_t v : bad) {
    ot_control_persist_t p;
    memset(&p, 0, sizeof p);
    p.any = p.set_local_ch_setpoint = true;
    p.local_ch_setpoint_dc = v;
    ot_config_patch_t patch;
    ot_control_io_patch(&p, &patch);
    TEST_ASSERT_EQUAL(OT_CONFIG_ERR_RANGE, ot_config_apply(&store, &patch, nullptr));
  }
  TEST_ASSERT_EQUAL_UINT16(450, store.local_ch_setpoint_dc);
}

// --- the bus -------------------------------------------------------------------------------------

void test_f88_decodes_to_tenths_half_away_from_zero(void) {
  TEST_ASSERT_EQUAL_INT16(450, ot_control_io_f88_dc(0x2D00));    // 45.0
  TEST_ASSERT_EQUAL_INT16(455, ot_control_io_f88_dc(0x2D80));    // 45.5
  TEST_ASSERT_EQUAL_INT16(503, ot_control_io_f88_dc(0x324D));    // 50.30078 -- someone's 50.3
  TEST_ASSERT_EQUAL_INT16(0, ot_control_io_f88_dc(0x0000));
  TEST_ASSERT_EQUAL_INT16(-10, ot_control_io_f88_dc(0xFF00));    // -1.0
  TEST_ASSERT_EQUAL_INT16(-1280, ot_control_io_f88_dc(0x8000));  // -128.0
  TEST_ASSERT_EQUAL_INT16(1280, ot_control_io_f88_dc(0x7FFF));   // 127.996
  TEST_ASSERT_EQUAL_INT16(1, ot_control_io_f88_dc(0x0014));      // 0.078 -> 0.1: the half rounds up
  TEST_ASSERT_EQUAL_INT16(-1, ot_control_io_f88_dc(0xFFEC));     // and away from zero below it
  TEST_ASSERT_EQUAL_INT16(3, ot_control_io_f88_dc(0x0040));      // 0.25 -> 2.5 tenths: an exact tie
  TEST_ASSERT_EQUAL_INT16(-3, ot_control_io_f88_dc(0xFFC0));     // -0.25: the tie goes away from zero
}

// The decoder is the inverse of the codec (ot_codec_float_to_f88()), which ot_control_io_dc_f88()
// matches -- see the next test -- for every value on the half-degree grid the executor holds.
// Without this, a held 45.5 could come back as 455 or 456 and CH would wait for a confirmation
// forever.
void test_every_value_the_executor_sends_decodes_back_exactly(void) {
  for (int dc = 0; dc <= 1000; dc += 5)
    TEST_ASSERT_EQUAL_INT16(dc, ot_control_io_f88_dc(ot_codec_float_to_f88((float)dc / 10.0f)));
}

// The executor encodes its own writes (ot_control_io.h says why not through ot_command_encode()),
// and it must put on the wire exactly what the codec would -- or the boiler sees 45.4 for 45.5 --
// and exactly what the decoder reads back, or the held value is never confirmed.
void test_the_executor_encodes_f88_exactly_as_the_codec_does(void) {
  for (int dc = -1000; dc <= 1000; dc++)
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(ot_codec_float_to_f88((float)dc / 10.0f),
                                    ot_control_io_dc_f88((int16_t)dc), "against ot_codec");
  for (int dc = -1000; dc <= 1000; dc += 5)
    TEST_ASSERT_EQUAL_INT16(dc, ot_control_io_f88_dc(ot_control_io_dc_f88((int16_t)dc)));
  TEST_ASSERT_EQUAL_HEX16(0x7FFF, ot_control_io_dc_f88(2000));
  TEST_ASSERT_EQUAL_HEX16(0x8000, ot_control_io_dc_f88(-2000));
  TEST_ASSERT_EQUAL_HEX16(0x7FFF, ot_control_io_dc_f88(INT16_MAX));
  TEST_ASSERT_EQUAL_HEX16(0x8000, ot_control_io_dc_f88(INT16_MIN));
}

void test_an_unchanged_count_confirms_nothing_and_touches_nothing_else(void) {
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  in.room_fresh = true;
  in.dhw_readback_valid = true;
  in.dhw_readback_dc = 500;
  uint32_t seen = 5;
  ot_control_io_confirm(5, 0x2D00, &seen, &in);
  TEST_ASSERT_FALSE(in.setpoint_confirmed);
  TEST_ASSERT_EQUAL_UINT32(5, seen);
  TEST_ASSERT_TRUE(in.room_fresh);
  TEST_ASSERT_TRUE(in.dhw_readback_valid);
  TEST_ASSERT_EQUAL_INT16(500, in.dhw_readback_dc);
}

void test_a_moved_count_confirms_the_value_on_the_wire(void) {
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  uint32_t seen = 0;
  ot_control_io_confirm(0, 0x2D00, &seen, &in);
  TEST_ASSERT_FALSE_MESSAGE(in.setpoint_confirmed, "nothing has gone out at boot");
  ot_control_io_confirm(1, 0x2D80, &seen, &in);
  TEST_ASSERT_TRUE(in.setpoint_confirmed);
  TEST_ASSERT_EQUAL_INT16(455, in.confirmed_dc);
  TEST_ASSERT_EQUAL_UINT32(1, seen);
  ot_control_io_confirm(1, 0x2D80, &seen, &in);
  TEST_ASSERT_FALSE_MESSAGE(in.setpoint_confirmed, "one write confirms one step");
}

// End to end through the real executor: ours goes out and CH rises; a FOREIGN ID 1 before the
// rise holds CH down -- the glue has to hand every ID 1 over, not only ours.
void test_a_foreign_id1_holds_ch_down_until_ours_goes_out(void) {
  ot_control_cfg_t cfg;
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  pub.heating_season = true;
  pub.local_ch_enable = true;
  pub.local_ch_setpoint_dc = 550;
  ot_control_io_cfg(&pub, &cfg);
  ot_control_t c;
  ot_control_init(&c, &cfg, nullptr, 1000);
  ot_control_in_t in;
  ot_control_out_t out;
  uint32_t seen = 0, now = 1000;

  memset(&in, 0, sizeof in);
  ot_control_step(&c, &cfg, &in, now += 1000, &out);
  TEST_ASSERT_TRUE(out.send_setpoint);
  TEST_ASSERT_EQUAL_HEX8(OT_STATUS_DHW_ENABLE, out.status_high);

  ot_control_io_confirm(1, ot_codec_float_to_f88(60.0f), &seen, &in);   // someone else's ID 1
  ot_control_step(&c, &cfg, &in, now += 1000, &out);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(OT_STATUS_DHW_ENABLE, out.status_high, "a foreign 60.0 is not ours");
  TEST_ASSERT_EQUAL(OT_CONTROL_REASON_AWAIT_SETPOINT, out.reason);

  ot_control_io_confirm(2, ot_codec_float_to_f88(55.0f), &seen, &in);   // ours
  ot_control_step(&c, &cfg, &in, now += 1000, &out);
  TEST_ASSERT_EQUAL_HEX8(OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE, out.status_high);
}

// Only what the boiler says it KEEPS is a readback: a WRITE-ACK is the echo of our own
// write, and no other type or Data-ID says anything about ID 56.
void test_only_a_read_ack_of_id_56_is_a_readback(void) {
  ot_control_io_readback_t rb;
  memset(&rb, 0, sizeof rb);
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  in.dhw_readback_valid = true;
  in.dhw_readback_dc    = 999;
  ot_control_io_readback_in(&rb, &in);
  TEST_ASSERT_FALSE_MESSAGE(in.dhw_readback_valid, "nothing read back yet");

  const ot_msg_type_t other[] = {OT_MSG_WRITE_ACK, OT_MSG_DATA_INVALID, OT_MSG_UNKNOWN_DATAID,
                                 OT_MSG_READ_DATA, OT_MSG_WRITE_DATA};
  for (ot_msg_type_t t : other) {
    ot_control_io_readback(&rb, 56, t, 0x3280);
    ot_control_io_readback_in(&rb, &in);
    TEST_ASSERT_FALSE_MESSAGE(in.dhw_readback_valid, "only a READ-ACK is what the boiler keeps");
  }
  ot_control_io_readback(&rb, 57, OT_MSG_READ_ACK, 0x3280);
  ot_control_io_readback(&rb, 1, OT_MSG_READ_ACK, 0x3280);
  ot_control_io_readback_in(&rb, &in);
  TEST_ASSERT_FALSE_MESSAGE(in.dhw_readback_valid, "another Data-ID is not ID 56");

  ot_control_io_readback(&rb, 56, OT_MSG_READ_ACK, 0x3200);
  ot_control_io_readback(&rb, 56, OT_MSG_WRITE_ACK, 0x3280);   // our echo, after it
  in.room_fresh = true;
  in.setpoint_confirmed = true;
  in.confirmed_dc = 455;
  ot_control_io_readback_in(&rb, &in);
  TEST_ASSERT_TRUE(in.dhw_readback_valid);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(500, in.dhw_readback_dc, "the echo does not overwrite it");
  TEST_ASSERT_TRUE(in.room_fresh);
  TEST_ASSERT_TRUE(in.setpoint_confirmed);
  TEST_ASSERT_EQUAL_INT16(455, in.confirmed_dc);
}

// The heard record carries the ID 56 readback by the rule above, unchanged: ot_control_io_heard()
// is what the bus task calls, so the rule has to hold through it too, not only through
// ot_control_io_readback().
void test_the_heard_record_keeps_the_readback_rule(void) {
  ot_control_io_heard_t h;
  memset(&h, 0, sizeof h);
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  const ot_msg_type_t other[] = {OT_MSG_WRITE_ACK, OT_MSG_DATA_INVALID, OT_MSG_UNKNOWN_DATAID,
                                 OT_MSG_READ_DATA, OT_MSG_WRITE_DATA};
  for (ot_msg_type_t t : other) {
    ot_control_io_heard(&h, OT_CONTROL_IO_ID_TDHW_SET, t, 0x3280);
    ot_control_io_readback_in(&h.rb, &in);
    TEST_ASSERT_FALSE_MESSAGE(in.dhw_readback_valid, "only a READ-ACK is what the boiler keeps");
  }
  ot_control_io_heard(&h, OT_CONTROL_IO_ID_TDHW_SET, OT_MSG_READ_ACK, 0x3200);
  ot_control_io_heard(&h, OT_CONTROL_IO_ID_TDHW_SET, OT_MSG_WRITE_ACK, 0x3280);   // our echo
  ot_control_io_readback_in(&h.rb, &in);
  TEST_ASSERT_TRUE(in.dhw_readback_valid);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(500, in.dhw_readback_dc, "the echo does not overwrite it");
  TEST_ASSERT_FALSE_MESSAGE(h.id1_invalid, "an ID 56 answer says nothing about ID 1");
}

// The first DATA-INVALID answer to ID 1 is kept with its value, for the task to say ONCE outside
// its spinlock (ot_thermostat_heard() may not log). The first, not the last: the line names the
// value the boiler first refused, and a later one changes nothing about what the owner must check.
void test_a_data_invalid_answer_to_id1_is_kept_with_its_value(void) {
  ot_control_io_heard_t h;
  memset(&h, 0, sizeof h);
  TEST_ASSERT_FALSE_MESSAGE(h.id1_invalid, "a zeroed record has heard nothing");
  ot_control_io_heard(&h, OT_CONTROL_IO_ID_TSET, OT_MSG_DATA_INVALID, 0x5000);   // 80.0
  TEST_ASSERT_TRUE(h.id1_invalid);
  TEST_ASSERT_EQUAL_HEX16(0x5000, h.id1_invalid_raw);

  ot_control_io_heard(&h, OT_CONTROL_IO_ID_TSET, OT_MSG_DATA_INVALID, 0x2800);
  TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x5000, h.id1_invalid_raw, "the first refusal is the one kept");
  ot_control_io_heard(&h, OT_CONTROL_IO_ID_TSET, OT_MSG_WRITE_ACK, 0x2D00);
  TEST_ASSERT_TRUE_MESSAGE(h.id1_invalid, "a later WRITE-ACK does not unsay it");
  TEST_ASSERT_EQUAL_HEX16(0x5000, h.id1_invalid_raw);

  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  ot_control_io_readback_in(&h.rb, &in);
  TEST_ASSERT_FALSE_MESSAGE(in.dhw_readback_valid, "an ID 1 answer is no ID 56 readback");
}

// Nothing else is that answer: a WRITE-ACK (the boiler took it), UNKNOWN-DATAID (ot_state's mark,
// warned about separately) or any other type to ID 1, and a DATA-INVALID to any other Data-ID.
void test_only_data_invalid_to_id1_sets_the_flag(void) {
  const ot_msg_type_t types[] = {OT_MSG_READ_DATA, OT_MSG_WRITE_DATA, OT_MSG_INVALID_DATA,
                                 OT_MSG_RESERVED, OT_MSG_READ_ACK, OT_MSG_WRITE_ACK,
                                 OT_MSG_UNKNOWN_DATAID};
  char msg[48];
  for (ot_msg_type_t t : types) {
    ot_control_io_heard_t h;
    memset(&h, 0, sizeof h);
    ot_control_io_heard(&h, OT_CONTROL_IO_ID_TSET, t, 0x5000);
    snprintf(msg, sizeof msg, "ID 1, message type %d", (int)t);
    TEST_ASSERT_FALSE_MESSAGE(h.id1_invalid, msg);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, h.id1_invalid_raw, msg);
  }
  const uint8_t ids[] = {0, 2, 56, 57, 129, 255};
  for (uint8_t id : ids) {
    ot_control_io_heard_t h;
    memset(&h, 0, sizeof h);
    ot_control_io_heard(&h, id, OT_MSG_DATA_INVALID, 0x5000);
    snprintf(msg, sizeof msg, "DATA-INVALID to ID %u", (unsigned)id);
    TEST_ASSERT_FALSE_MESSAGE(h.id1_invalid, msg);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, h.id1_invalid_raw, msg);
  }
}

// THE PROBE, as the task wires it: a boiler that stores whole degrees,
// a WRITE-ACK that echoes the value written, ID 56 polled once a minute, thirty minutes of the
// real executor. The writes of one target the readback has not agreed with are capped at three.
void test_a_rounding_boiler_gets_three_dhw_writes_not_one_a_minute(void) {
  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  pub.dhw_setpoint_dc = 505;                         // 50.5: the boiler will keep 50.0
  ot_control_cfg_t cfg;
  ot_control_io_cfg(&pub, &cfg);
  ot_control_t c;
  ot_control_init(&c, &cfg, nullptr, 0);
  ot_control_io_readback_t rb;
  memset(&rb, 0, sizeof rb);
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  ot_control_out_t out;
  uint16_t kept = 0x3C00;                            // 60.0, before we ever write
  int writes = 0;
  uint32_t first_write = 0;
  for (uint32_t s = 1; s <= 1800; s++) {
    if (s % 60 == 0)                                 // the poll ring reads ID 56
      ot_control_io_readback(&rb, 56, OT_MSG_READ_ACK, kept);
    ot_control_io_readback_in(&rb, &in);
    ot_control_step(&c, &cfg, &in, s * 1000u, &out);
    if (out.send_dhw_setpoint) {
      writes++;
      if (first_write == 0)
        first_write = s;
      const uint16_t sent = ot_control_io_dc_f88(cfg.dhw_setpoint_dc);
      kept = (uint16_t)(sent & 0xFF00u);             // whole degrees
      ot_control_io_readback(&rb, 56, OT_MSG_WRITE_ACK, sent);   // the echo
    }
  }
  // The first write is the boot re-arm (step 1), not the step-60 poll -- pinned so this
  // test's total-of-3 is not a coincidence of DHW_MAX_TRIES lining up with the old step-60 timing.
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, first_write, "the boot re-arm writes on the first step");
  TEST_ASSERT_EQUAL_INT_MESSAGE(3, writes, "ID 56 writes in 30 minutes (boot + the cap)");
}

// --- numbers from outside ------------------------------------------------------------------------

void test_a_temperature_becomes_tenths_rounded_half_away_from_zero(void) {
  int16_t dc = 0;
  TEST_ASSERT_TRUE(ot_control_io_dc(45.0f, &dc));
  TEST_ASSERT_EQUAL_INT16(450, dc);
  TEST_ASSERT_TRUE(ot_control_io_dc(45.05f, &dc));
  TEST_ASSERT_EQUAL_INT16(451, dc);
  TEST_ASSERT_TRUE(ot_control_io_dc(-0.05f, &dc));
  TEST_ASSERT_EQUAL_INT16(-1, dc);
  TEST_ASSERT_TRUE(ot_control_io_dc(3276.7f, &dc));
  TEST_ASSERT_EQUAL_INT16(32767, dc);
  TEST_ASSERT_TRUE(ot_control_io_dc(-3276.8f, &dc));
  TEST_ASSERT_EQUAL_INT16(-32768, dc);

  // 3276.75f scales to EXACTLY 32767.5, which lroundf() takes to 32768: the edge of the refusal.
  const float bad[] = {NAN, INFINITY, -INFINITY, 3276.8f, 3276.75f, -3276.9f, 1e9f};
  char msg[48];
  for (float v : bad) {
    dc = 123;
    snprintf(msg, sizeof msg, "%g degrees", (double)v);
    TEST_ASSERT_FALSE_MESSAGE(ot_control_io_dc(v, &dc), msg);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(123, dc, msg);   // a refusal leaves *dc alone
  }
}

// Whole and non-negative, or refused; 0 and 481 are the executor's BAD_MINUTES, not this one's.
void test_minutes_must_be_a_whole_number(void) {
  uint32_t m = 7;
  char msg[48];
  const float good[] = {0.0f, 1.0f, 480.0f, 481.0f, 4294967040.0f};
  for (float v : good) {
    snprintf(msg, sizeof msg, "%.1f minutes", (double)v);
    TEST_ASSERT_TRUE_MESSAGE(ot_control_io_minutes(v, &m), msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)v, m, msg);
  }
  const float bad[] = {1.5f, 0.25f, -1.0f, NAN, INFINITY, -INFINITY, 4294967296.0f};
  for (float v : bad) {
    m = 7;
    snprintf(msg, sizeof msg, "%.2f minutes", (double)v);
    TEST_ASSERT_FALSE_MESSAGE(ot_control_io_minutes(v, &m), msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, m, msg);   // a refusal leaves *out alone
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_config_maps_field_by_field);
  RUN_TEST(test_the_defaults_map_to_a_device_that_asks_for_nothing);
  RUN_TEST(test_a_mode_other_than_ha_is_local);
  RUN_TEST(test_dhw_setpoint_zero_is_unset_and_anything_else_is_set);
  RUN_TEST(test_a_number_too_wide_saturates_rather_than_wraps);
  RUN_TEST(test_nothing_to_persist_is_an_empty_patch);
  RUN_TEST(test_each_persisted_field_lands_in_its_own_member_and_nowhere_else);
  RUN_TEST(test_an_unflagged_value_does_not_travel);
  RUN_TEST(test_a_patch_the_executor_builds_is_one_the_store_accepts);
  RUN_TEST(test_the_store_refuses_what_the_grid_refuses_and_a_negative_never_wraps);
  RUN_TEST(test_f88_decodes_to_tenths_half_away_from_zero);
  RUN_TEST(test_every_value_the_executor_sends_decodes_back_exactly);
  RUN_TEST(test_the_executor_encodes_f88_exactly_as_the_codec_does);
  RUN_TEST(test_an_unchanged_count_confirms_nothing_and_touches_nothing_else);
  RUN_TEST(test_a_moved_count_confirms_the_value_on_the_wire);
  RUN_TEST(test_a_foreign_id1_holds_ch_down_until_ours_goes_out);
  RUN_TEST(test_only_a_read_ack_of_id_56_is_a_readback);
  RUN_TEST(test_the_heard_record_keeps_the_readback_rule);
  RUN_TEST(test_a_data_invalid_answer_to_id1_is_kept_with_its_value);
  RUN_TEST(test_only_data_invalid_to_id1_sets_the_flag);
  RUN_TEST(test_a_rounding_boiler_gets_three_dhw_writes_not_one_a_minute);
  RUN_TEST(test_a_temperature_becomes_tenths_rounded_half_away_from_zero);
  RUN_TEST(test_minutes_must_be_a_whole_number);
  return UNITY_END();
}
