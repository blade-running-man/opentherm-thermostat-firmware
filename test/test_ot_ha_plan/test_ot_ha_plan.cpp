// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// When a discovery document is owed (ot_ha_want, ot_ha_wants, ot_ha_bounds, ot_ha_plan_*):
// IDs the boiler does not support are skipped; runtime bounds are re-published
// when they change; everything re-published on HA's `online`.
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "ot_ha.h"
#include "ot_registry.h"
#include "ot_state.h"

static ot_ha_plan_t plan;
static ot_ha_want_t want[OT_HA_DOC_COUNT];
static int16_t      lo[OT_HA_DOC_COUNT], hi[OT_HA_DOC_COUNT];

void setUp(void)
{
    ot_state_reset();
    ot_ha_plan_reset(&plan);
    memset(lo, 0, sizeof lo);
    memset(hi, 0, sizeof hi);
}
void tearDown(void) {}

static int index_of(const char *object_id)
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++)
        if (strcmp(OT_HA_DOCS[i].object_id, object_id) == 0)
            return i;
    TEST_FAIL_MESSAGE(object_id);
    return -1;
}

static void all(ot_ha_want_t w)
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++)
        want[i] = w;
}

// Publishes everything the plan names, as the glue does; returns how many it published.
static int drain(void)
{
    int  n = 0;
    bool empty;
    for (int i; (i = ot_ha_plan_next(&plan, want, lo, hi, &empty)) >= 0; n++) {
        TEST_ASSERT_TRUE(n <= 2 * OT_HA_DOC_COUNT);
        ot_ha_plan_done(&plan, i, empty, lo[i], hi[i]);
    }
    return n;
}

static void test_a_row_read_from_the_boiler_waits_for_its_answer_and_goes_if_unsupported(void)
{
    const ot_ha_doc_t *wire = &OT_HA_DOCS[index_of("flow_temperature")];
    TEST_ASSERT_TRUE(wire->wire);
    TEST_ASSERT_EQUAL(OT_HA_WAIT, ot_ha_want(wire, true, OT_AVAIL_UNKNOWN));
    TEST_ASSERT_EQUAL(OT_HA_WANT, ot_ha_want(wire, true, OT_AVAIL_OK));
    TEST_ASSERT_EQUAL(OT_HA_WANT, ot_ha_want(wire, true, OT_AVAIL_INVALID));
    TEST_ASSERT_EQUAL(OT_HA_DROP, ot_ha_want(wire, true, OT_AVAIL_UNSUPPORTED));
}

// ID 1's unsupported flag never clears: ch_setpoint is not `wire`, so it stays wanted.
static void test_an_entity_not_read_from_the_boiler_is_wanted_whatever_the_state_says(void)
{
    for (const char *id : {"ch_setpoint", "ch_enable", "heating_season_off", "control_state"}) {
        const ot_ha_doc_t *d = &OT_HA_DOCS[index_of(id)];
        TEST_ASSERT_FALSE_MESSAGE(d->wire, id);
        TEST_ASSERT_EQUAL_MESSAGE(OT_HA_WANT, ot_ha_want(d, true, OT_AVAIL_UNSUPPORTED), id);
        TEST_ASSERT_EQUAL_MESSAGE(OT_HA_WANT, ot_ha_want(d, true, OT_AVAIL_UNKNOWN), id);
    }
}

static void test_discovery_off_drops_every_document(void)
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++)
        TEST_ASSERT_EQUAL(OT_HA_DROP, ot_ha_want(&OT_HA_DOCS[i], false, OT_AVAIL_OK));
    TEST_ASSERT_EQUAL(OT_HA_DROP, ot_ha_want(NULL, true, OT_AVAIL_OK));
}

static void test_wants_reads_the_availability_from_the_state_model(void)
{
    ot_ha_wants(true, want);
    TEST_ASSERT_EQUAL(OT_HA_WAIT, want[index_of("flow_temperature")]);
    TEST_ASSERT_EQUAL(OT_HA_WAIT, want[index_of("exhaust_temperature")]);
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    ot_state_apply_dataid(33, OT_MSG_UNKNOWN_DATAID, 0, 1000);
    ot_ha_wants(true, want);
    TEST_ASSERT_EQUAL(OT_HA_WANT, want[index_of("flow_temperature")]);
    TEST_ASSERT_EQUAL(OT_HA_WAIT, want[index_of("exhaust_temperature")]);   // one answer is not two
    ot_state_apply_dataid(33, OT_MSG_UNKNOWN_DATAID, 0, 2000);
    ot_ha_wants(true, want);
    TEST_ASSERT_EQUAL(OT_HA_DROP, want[index_of("exhaust_temperature")]);
}

static void test_bounds_come_from_the_flow_band_and_from_the_state(void)
{
    ot_ha_bounds(405, 655, lo, hi);
    const int ch = index_of("ch_setpoint"), dhw = index_of("dhw_setpoint");
    TEST_ASSERT_EQUAL(405, lo[ch]);
    TEST_ASSERT_EQUAL(655, hi[ch]);
    TEST_ASSERT_EQUAL(300, lo[dhw]);   // the table's 30..80 until ID 48 is read
    TEST_ASSERT_EQUAL(800, hi[dhw]);
    ot_state_apply_dataid(48, OT_MSG_READ_ACK, (60 << 8) | 35, 1000);   // max 60, min 35
    ot_ha_bounds(405, 655, lo, hi);
    TEST_ASSERT_EQUAL(350, lo[dhw]);
    TEST_ASSERT_EQUAL(600, hi[dhw]);
    TEST_ASSERT_EQUAL(0, lo[index_of("flow_temperature")]);
    TEST_ASSERT_EQUAL(0, hi[index_of("flow_temperature")]);
}

static void test_each_wanted_document_is_offered_once_and_a_waiting_one_never(void)
{
    all(OT_HA_WANT);
    want[3] = OT_HA_WAIT;
    TEST_ASSERT_EQUAL(OT_HA_DOC_COUNT - 1, drain());
    TEST_ASSERT_EQUAL(0, drain());
    TEST_ASSERT_EQUAL(OT_HA_SENT_NOTHING, plan.sent[3]);
}

static void test_the_lowest_owed_index_comes_first(void)
{
    all(OT_HA_WAIT);
    want[7] = OT_HA_WANT;
    want[2] = OT_HA_DROP;
    bool empty = false;
    TEST_ASSERT_EQUAL(2, ot_ha_plan_next(&plan, want, lo, hi, &empty));
    TEST_ASSERT_TRUE(empty);
    ot_ha_plan_done(&plan, 2, true, 0, 0);
    TEST_ASSERT_EQUAL(7, ot_ha_plan_next(&plan, want, lo, hi, &empty));
    TEST_ASSERT_FALSE(empty);
}

// A dropped document is cleared ONCE per connection, with an empty payload.
static void test_a_dropped_document_is_cleared_once_even_if_it_was_never_published(void)
{
    all(OT_HA_WAIT);
    const int i = index_of("exhaust_temperature");
    want[i]     = OT_HA_DROP;
    TEST_ASSERT_EQUAL(1, drain());
    TEST_ASSERT_EQUAL(OT_HA_SENT_EMPTY, plan.sent[i]);
    TEST_ASSERT_EQUAL(0, drain());
}

static void test_a_published_document_that_becomes_unsupported_is_cleared(void)
{
    all(OT_HA_WAIT);
    const int i = index_of("exhaust_temperature");
    want[i]     = OT_HA_WANT;
    drain();
    want[i] = OT_HA_DROP;
    bool empty = false;
    TEST_ASSERT_EQUAL(i, ot_ha_plan_next(&plan, want, lo, hi, &empty));
    TEST_ASSERT_TRUE(empty);
}

// A changed flow band re-offers ch_setpoint -- and only the bounded documents.
static void test_changed_bounds_re_offer_the_bounded_document_alone(void)
{
    all(OT_HA_WANT);
    ot_ha_bounds(400, 700, lo, hi);
    drain();
    ot_ha_bounds(450, 700, lo, hi);
    bool empty = true;
    TEST_ASSERT_EQUAL(index_of("ch_setpoint"), ot_ha_plan_next(&plan, want, lo, hi, &empty));
    TEST_ASSERT_FALSE(empty);
    TEST_ASSERT_EQUAL(1, drain());
    ot_ha_bounds(450, 650, lo, hi);   // the max alone moves it too
    TEST_ASSERT_EQUAL(1, drain());
}

// An unbounded document ignores the bounds arrays: garbage there re-offers nothing.
static void test_an_unbounded_document_is_not_re_offered_by_bounds(void)
{
    all(OT_HA_WANT);
    drain();
    const int f = index_of("flow_temperature");
    lo[f] = 123;
    hi[f] = -7;
    TEST_ASSERT_EQUAL(0, drain());
}

// HA's `online`, a reconnect, a new context: everything owed again.
static void test_a_reset_offers_everything_again(void)
{
    all(OT_HA_WANT);
    want[0] = OT_HA_DROP;
    TEST_ASSERT_EQUAL(OT_HA_DOC_COUNT, drain());
    ot_ha_plan_reset(&plan);
    TEST_ASSERT_EQUAL(OT_HA_DOC_COUNT, drain());
}

static void test_reset_zeroes_every_byte_of_the_plan(void)
{
    ot_ha_plan_t dirty, zero;
    memset(&dirty, 0xFF, sizeof dirty);
    memset(&zero, 0, sizeof zero);
    ot_ha_plan_reset(&dirty);
    TEST_ASSERT_EQUAL_MEMORY(&zero, &dirty, sizeof dirty);   // every byte, not the ones a test order leaves clean
}

static void test_done_out_of_range_is_ignored(void)
{
    ot_ha_plan_t zero;
    memset(&zero, 0, sizeof zero);
    ot_ha_plan_done(&plan, -1, false, 1, 1);
    ot_ha_plan_done(&plan, OT_HA_DOC_COUNT, false, 1, 1);
    TEST_ASSERT_EQUAL_MEMORY(&zero, &plan, sizeof plan);   // nothing written, next door included
    all(OT_HA_WANT);
    TEST_ASSERT_EQUAL(OT_HA_DOC_COUNT, drain());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_row_read_from_the_boiler_waits_for_its_answer_and_goes_if_unsupported);
    RUN_TEST(test_an_entity_not_read_from_the_boiler_is_wanted_whatever_the_state_says);
    RUN_TEST(test_discovery_off_drops_every_document);
    RUN_TEST(test_wants_reads_the_availability_from_the_state_model);
    RUN_TEST(test_bounds_come_from_the_flow_band_and_from_the_state);
    RUN_TEST(test_each_wanted_document_is_offered_once_and_a_waiting_one_never);
    RUN_TEST(test_the_lowest_owed_index_comes_first);
    RUN_TEST(test_a_dropped_document_is_cleared_once_even_if_it_was_never_published);
    RUN_TEST(test_a_published_document_that_becomes_unsupported_is_cleared);
    RUN_TEST(test_changed_bounds_re_offer_the_bounded_document_alone);
    RUN_TEST(test_an_unbounded_document_is_not_re_offered_by_bounds);
    RUN_TEST(test_a_reset_offers_everything_again);
    RUN_TEST(test_reset_zeroes_every_byte_of_the_plan);
    RUN_TEST(test_done_out_of_range_is_ignored);
    return UNITY_END();
}
