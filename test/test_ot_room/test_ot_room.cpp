// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <math.h>

extern "C" {
#include "ot_room.h"
}

void setUp(void) {}
void tearDown(void) {}

#define STALE_MS 60000u   // matches the DS18B20 slot's stale_after today

// --- helpers -----------------------------------------------------------------

static void init_one(ot_room_t *r, ot_room_role_t role, bool ha_forwarded, uint32_t stale_after_ms) {
    ot_room_cfg_t cfg{};
    cfg.count = 1;
    cfg.cfg[0].role = role;
    cfg.cfg[0].ha_forwarded = ha_forwarded;
    cfg.cfg[0].stale_after_ms = stale_after_ms;
    ot_room_init(r, &cfg);
}

static void init_two(ot_room_t *r,
                      ot_room_role_t role0, ot_room_role_t role1) {
    ot_room_cfg_t cfg{};
    cfg.count = 2;
    cfg.cfg[0].role = role0;
    cfg.cfg[0].ha_forwarded = false;
    cfg.cfg[0].stale_after_ms = STALE_MS;
    cfg.cfg[1].role = role1;
    cfg.cfg[1].ha_forwarded = false;
    cfg.cfg[1].stale_after_ms = STALE_MS;
    ot_room_init(r, &cfg);
}

// --- the core safety assertion --------------------------------------------

// An ambient-role slot (the DS18B20's default) must never be able to hold
// heat off through the failsafe: it is filtered out of the steer selection entirely, but
// still shows up in the display selection so the shield reading stays visible in the panel.
static void test_ambient_only_never_steers(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_AMBIENT, false, STALE_MS);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 22.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
    TEST_ASSERT_EQUAL_INT(-1, st.active_slot);

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_TRUE(disp.have);
    TEST_ASSERT_EQUAL_INT16(220, disp.value_dc);
    TEST_ASSERT_EQUAL_INT(0, disp.active_slot);
    TEST_ASSERT_EQUAL_INT(OT_ROOM_AMBIENT, disp.active_role);
}

static void test_room_slot_steers(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, false, STALE_MS);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 20.5f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_TRUE(st.fresh);
    TEST_ASSERT_EQUAL_INT16(205, st.value_dc);
    TEST_ASSERT_EQUAL_INT(0, st.active_slot);
}

// Priority is slot index: among two fresh room-role slots, the lower index wins.
static void test_priority_lowest_index_wins(void) {
    ot_room_t r;
    init_two(&r, OT_ROOM_ROOM, OT_ROOM_ROOM);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 21.0f, 1000));
    TEST_ASSERT_TRUE(ot_room_submit(&r, 1, 19.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_TRUE(st.fresh);
    TEST_ASSERT_EQUAL_INT16(210, st.value_dc);
    TEST_ASSERT_EQUAL_INT(0, st.active_slot);
}

// A room-role slot always outranks an ambient one for steering, whatever their indices --
// the safety guard is structural (role), not merely "priority happens to work out".
// Display now shares that same room-over-ambient preference: slot 1 (room) wins
// display too, even though slot 0 (ambient)
// has the lower index -- see test_display_prefers_room_over_ambient below for the dedicated
// case, and test_display_falls_back_to_ambient / test_ambient_only_never_steers for why the
// ambient reading is still not simply discarded.
static void test_room_preferred_over_ambient_for_steer_regardless_of_index(void) {
    ot_room_t r;
    init_two(&r, OT_ROOM_AMBIENT, OT_ROOM_ROOM);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 25.0f, 1000));
    TEST_ASSERT_TRUE(ot_room_submit(&r, 1, 18.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_TRUE(st.fresh);
    TEST_ASSERT_EQUAL_INT16(180, st.value_dc);
    TEST_ASSERT_EQUAL_INT(1, st.active_slot);

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_TRUE(disp.have);
    TEST_ASSERT_EQUAL_INT16(180, disp.value_dc);
    TEST_ASSERT_EQUAL_INT(1, disp.active_slot);
    TEST_ASSERT_EQUAL_INT(OT_ROOM_ROOM, disp.active_role);
}

// --- display prefers a fresh room-role slot over a fresh ambient one --------------------

// Two-pass display: a fresh room-role slot wins
// display even when a fresh ambient slot has the lower index -- the panel should show the
// genuine room reading over the boiler-side one whenever both are available.
static void test_display_prefers_room_over_ambient(void) {
    ot_room_t r;
    init_two(&r, OT_ROOM_AMBIENT, OT_ROOM_ROOM);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 25.0f, 1000));
    TEST_ASSERT_TRUE(ot_room_submit(&r, 1, 20.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_TRUE(disp.have);
    TEST_ASSERT_EQUAL_INT16(200, disp.value_dc);
    TEST_ASSERT_EQUAL_INT(1, disp.active_slot);
    TEST_ASSERT_EQUAL_INT(OT_ROOM_ROOM, disp.active_role);
}

// No room-role slot is fresh (the room slot is STALE, not merely never-fed) -- the second
// pass falls back to the fresh ambient slot, exactly the display behavior this refinement
// must not break: ambient is still shown when no room is fresh.
static void test_display_falls_back_to_ambient(void) {
    ot_room_t r;
    init_two(&r, OT_ROOM_AMBIENT, OT_ROOM_ROOM);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 1, 20.0f, 1000));
    ot_room_tick(&r, 1000);                    // slot 1 briefly fresh, then let it go stale
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 25.0f, 1000 + STALE_MS));
    ot_room_tick(&r, 1000 + STALE_MS);          // slot 1 now STALE, slot 0 FRESH

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_TRUE(disp.have);
    TEST_ASSERT_EQUAL_INT16(250, disp.value_dc);
    TEST_ASSERT_EQUAL_INT(0, disp.active_slot);
    TEST_ASSERT_EQUAL_INT(OT_ROOM_AMBIENT, disp.active_role);
}

// Within the first pass (room-role), priority is still slot index -- the same tie-break the
// steer selection already uses, so the two selections stay consistent with each other.
static void test_display_room_lowest_index_wins(void) {
    ot_room_t r;
    init_two(&r, OT_ROOM_ROOM, OT_ROOM_ROOM);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 21.0f, 1000));
    TEST_ASSERT_TRUE(ot_room_submit(&r, 1, 19.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_TRUE(disp.have);
    TEST_ASSERT_EQUAL_INT16(210, disp.value_dc);
    TEST_ASSERT_EQUAL_INT(0, disp.active_slot);
    TEST_ASSERT_EQUAL_INT(OT_ROOM_ROOM, disp.active_role);
}

static void test_stale_room_not_fresh(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, false, STALE_MS);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 20.0f, 1000));
    ot_room_tick(&r, 1000 + STALE_MS);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
    TEST_ASSERT_EQUAL_INT(-1, st.active_slot);
}

// A room-role, ha_forwarded slot going stale is a distinct signal from "no room source at
// all" -- it feeds the ha_blind failsafe input (HA still commanding while its own room
// sensor is dead) so a future caller can tell the two apart even though steer.fresh is false
// in both cases.
static void test_ha_forwarded_stale_flag(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, true, STALE_MS);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 20.0f, 1000));
    ot_room_tick(&r, 1000 + STALE_MS);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
    TEST_ASSERT_TRUE(st.ha_forwarded_stale);
}

// ha_forwarded_stale is STALE specifically, not "anything but FRESH". A never-fed slot
// (state NEVER) must NOT set it: were the check ever loosened to state != FRESH, a room
// slot that never received a reading would silently feed ha_blind and force the failsafe.
static void test_ha_forwarded_never_not_stale(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, true, STALE_MS);
    ot_room_tick(&r, 1000);   // never submitted -> state NEVER

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
    TEST_ASSERT_FALSE(st.ha_forwarded_stale);
}

// A FRESH ha_forwarded room slot is steering, not blind: ha_forwarded_stale stays false.
static void test_ha_forwarded_fresh_not_stale(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, true, STALE_MS);
    TEST_ASSERT_TRUE(ot_room_submit(&r, 0, 20.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_TRUE(st.fresh);
    TEST_ASSERT_FALSE(st.ha_forwarded_stale);
}

// The outlier filter is ot_sensor's, reused as-is: ot_room composes
// ot_sensor per slot. ot_room_submit only forwards ot_sensor_update's verdict.
static void test_submit_out_of_range_rejected(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, false, STALE_MS);
    TEST_ASSERT_FALSE(ot_room_submit(&r, 0, 200.0f, 1000));
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
}

static void test_submit_bad_slot_rejected(void) {
    ot_room_t r;
    init_one(&r, OT_ROOM_ROOM, false, STALE_MS);
    TEST_ASSERT_FALSE(ot_room_submit(&r, 9, 20.0f, 1000));
}

static void test_no_sources_fresh(void) {
    ot_room_t r;
    ot_room_cfg_t cfg{};
    cfg.count = 0;
    ot_room_init(&r, &cfg);
    ot_room_tick(&r, 1000);

    ot_room_steer_t st;
    ot_room_select_steer(&r, &st);
    TEST_ASSERT_FALSE(st.fresh);
    TEST_ASSERT_EQUAL_INT(-1, st.active_slot);

    ot_room_display_t disp;
    ot_room_select_display(&r, &disp);
    TEST_ASSERT_FALSE(disp.have);
    TEST_ASSERT_EQUAL_INT(-1, disp.active_slot);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ambient_only_never_steers);
    RUN_TEST(test_room_slot_steers);
    RUN_TEST(test_priority_lowest_index_wins);
    RUN_TEST(test_room_preferred_over_ambient_for_steer_regardless_of_index);
    RUN_TEST(test_display_prefers_room_over_ambient);
    RUN_TEST(test_display_falls_back_to_ambient);
    RUN_TEST(test_display_room_lowest_index_wins);
    RUN_TEST(test_stale_room_not_fresh);
    RUN_TEST(test_ha_forwarded_stale_flag);
    RUN_TEST(test_ha_forwarded_never_not_stale);
    RUN_TEST(test_ha_forwarded_fresh_not_stale);
    RUN_TEST(test_submit_out_of_range_rejected);
    RUN_TEST(test_submit_bad_slot_rejected);
    RUN_TEST(test_no_sources_fresh);
    return UNITY_END();
}
