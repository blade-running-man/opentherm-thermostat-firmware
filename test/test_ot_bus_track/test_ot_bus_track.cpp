// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The bus's bookkeeping of its writes (ot_bus_track.h), driven through the REAL scheduler: what is
// pinned is the tracker against ot_bus_sched_done()'s actual clearing rule, not against a fake of
// it. talk() is one conversation as bus_task() in ot_bus.c has it -- the step and the generation
// under the lock, the exchange outside it (where another task's call may land), done under the lock.
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_bus_sched.h"
#include "ot_bus_track.h"
}

void setUp(void) {}
void tearDown(void) {}

struct Bus {
  ot_bus_sched_t s;
  ot_bus_track_t t;
  uint32_t now;
};

static Bus boot(void) {
  static const uint8_t poll[] = {25, 26};
  Bus b;
  ot_bus_sched_init(&b.s, poll, 2);
  memset(&b.t, 0, sizeof b.t);
  b.now = 0;
  return b;
}

typedef void (*during_fn)(Bus &);

static ot_bus_step_t talk(Bus &b, bool answered, during_fn during) {
  const ot_bus_step_t st = ot_bus_sched_step(&b.s, b.now);
  TEST_ASSERT_EQUAL_INT(OT_BUS_TALK, st.verb);
  const uint32_t gen = b.t.gen;
  if (during != nullptr)
    during(b);   // on the wire: outside the lock, in bus_task()
  ot_bus_track_done(&b.t, &b.s, &st, gen, answered, b.now, b.now + 100);
  b.now += OT_BUS_PERIOD_MS;
  return st;
}

// The meaningful slot: ID 0 goes out first on every other step (ot_bus_sched.h), then this one.
static ot_bus_step_t slot(Bus &b, bool answered = true, during_fn during = nullptr) {
  const ot_bus_step_t id0 = talk(b, true, nullptr);
  TEST_ASSERT_EQUAL_UINT8(0, id0.data_id);
  return talk(b, answered, during);
}

static void hand_write_56(Bus &b) { ot_bus_track_write(&b.t, &b.s, 56, 0x3200); }
static void hand_write_id1(Bus &b) { ot_bus_track_write(&b.t, &b.s, 1, 0x3C00); }
static bool s_if_idle_result;
static void executor_write(Bus &b) { s_if_idle_result = ot_bus_track_write_if_idle(&b.t, &b.s, 1, 0x2D00); }

void test_a_write_takes_the_next_meaningful_slot(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D00);
  const ot_bus_step_t st = slot(b);
  TEST_ASSERT_TRUE(st.is_write);
  TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
  TEST_ASSERT_EQUAL_HEX16(0x2D00, st.value);
  TEST_ASSERT_FALSE_MESSAGE(slot(b).is_write, "a write goes out once");
}

// The queue of one stays a queue of one: a stale setpoint is worse than a lost one (ot_bus.h).
void test_the_next_write_evicts_a_queued_one(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 56, 0x3200);
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D00);
  const ot_bus_step_t st = slot(b);
  TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
  TEST_ASSERT_FALSE(slot(b).is_write);
}

// The executor's write never evicts a pending one.
void test_write_if_idle_queues_only_into_an_idle_slot(void) {
  Bus b = boot();
  TEST_ASSERT_TRUE(ot_bus_track_write_if_idle(&b.t, &b.s, 56, 0x3200));
  TEST_ASSERT_FALSE(ot_bus_track_write_if_idle(&b.t, &b.s, 1, 0x2D00));
  const ot_bus_step_t st = slot(b);
  TEST_ASSERT_EQUAL_UINT8(56, st.data_id);
  TEST_ASSERT_EQUAL_HEX16(0x3200, st.value);
  TEST_ASSERT_TRUE_MESSAGE(ot_bus_track_write_if_idle(&b.t, &b.s, 1, 0x2D00), "idle again");
}

// Sent means answered: a frame nobody answered may not have been heard. The pending write is
// cleared either way -- retrying is the executor's business (ot_bus_sched_done()).
void test_an_id1_counts_only_when_the_boiler_answered_it(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D00);
  slot(b, false);
  TEST_ASSERT_EQUAL_UINT32(0, b.t.id1_seq);
  TEST_ASSERT_FALSE_MESSAGE(slot(b).is_write, "the bus does not retry");
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D80);
  slot(b, true);
  TEST_ASSERT_EQUAL_UINT32(1, b.t.id1_seq);
  TEST_ASSERT_EQUAL_HEX16(0x2D80, b.t.id1_raw);
}

// Only ID 1 moves the ID 1 count, and a later write of another ID does not hide one that went
// out before it.
void test_an_id1_behind_a_later_write_is_still_counted(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D00);
  slot(b);
  ot_bus_track_write(&b.t, &b.s, 56, 0x3200);
  slot(b);
  TEST_ASSERT_EQUAL_UINT32(1, b.t.id1_seq);
  TEST_ASSERT_EQUAL_HEX16(0x2D00, b.t.id1_raw);
}

// Only a WRITE of ID 1 counts. A poll ring that READS ID 1 -- the boiler's own view of TSet --
// must not confirm the held value with whatever that read carried.
void test_a_read_of_id1_is_not_a_write_sent(void) {
  static const uint8_t poll[] = {1};
  Bus b = boot();
  ot_bus_sched_init(&b.s, poll, 1);
  const ot_bus_step_t st = slot(b);
  TEST_ASSERT_FALSE(st.is_write);
  TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
  TEST_ASSERT_EQUAL_UINT32(0, b.t.id1_seq);
}

// The race of ot_bus_track_done(): ot_bus_sched_done() clears the pending write for whatever is
// queued when it runs, and a hand write that lands while the executor's ID 1 is on the wire --
// already answered 202 -- must still go out. Answered or not, the executor's frame is over.
void test_a_write_queued_while_another_is_on_the_wire_is_not_lost(void) {
  for (int answered = 0; answered <= 1; answered++) {
    Bus b = boot();
    TEST_ASSERT_TRUE(ot_bus_track_write_if_idle(&b.t, &b.s, 1, 0x2D00));
    const ot_bus_step_t ours = slot(b, answered != 0, hand_write_56);
    TEST_ASSERT_EQUAL_UINT8(1, ours.data_id);
    const ot_bus_step_t next = slot(b);
    TEST_ASSERT_TRUE_MESSAGE(next.is_write, "the hand write was dropped");
    TEST_ASSERT_EQUAL_UINT8(56, next.data_id);
    TEST_ASSERT_EQUAL_HEX16(0x3200, next.value);
    TEST_ASSERT_FALSE_MESSAGE(slot(b).is_write, "and it goes out once");
  }
}

// What is counted is what went OUT, not what is queued by the time the exchange ends.
void test_the_value_counted_is_the_one_that_went_out(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 1, 0x2D00);
  slot(b, true, hand_write_id1);
  TEST_ASSERT_EQUAL_UINT32(1, b.t.id1_seq);
  TEST_ASSERT_EQUAL_HEX16(0x2D00, b.t.id1_raw);
  slot(b);
  TEST_ASSERT_EQUAL_UINT32(2, b.t.id1_seq);
  TEST_ASSERT_EQUAL_HEX16(0x3C00, b.t.id1_raw);
}

// While a write is on the wire the slot is not idle, so the executor cannot queue -- and nothing
// is re-armed for it: a re-arm with no write since the step would send the frame just sent twice.
void test_nothing_queued_during_a_write_is_nothing_re_armed(void) {
  Bus b = boot();
  ot_bus_track_write(&b.t, &b.s, 56, 0x3200);
  s_if_idle_result = true;
  slot(b, true, executor_write);
  TEST_ASSERT_FALSE_MESSAGE(s_if_idle_result, "a write on the wire is a pending write");
  TEST_ASSERT_FALSE_MESSAGE(slot(b).is_write, "no second copy of the frame just sent");
}

// A write queued during a READ or during ID 0 needed no rescue -- done() clears nothing then --
// and must still take the next meaningful slot.
void test_a_write_queued_during_a_read_or_id0_goes_out_next(void) {
  Bus b = boot();
  const ot_bus_step_t id0 = talk(b, true, hand_write_56);
  TEST_ASSERT_EQUAL_UINT8(0, id0.data_id);
  ot_bus_step_t st = talk(b, true, nullptr);
  TEST_ASSERT_TRUE(st.is_write);
  TEST_ASSERT_EQUAL_UINT8(56, st.data_id);

  const ot_bus_step_t read = slot(b, true, executor_write);
  TEST_ASSERT_FALSE(read.is_write);
  TEST_ASSERT_TRUE(s_if_idle_result);
  st = slot(b);
  TEST_ASSERT_TRUE(st.is_write);
  TEST_ASSERT_EQUAL_UINT8(1, st.data_id);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_write_takes_the_next_meaningful_slot);
  RUN_TEST(test_the_next_write_evicts_a_queued_one);
  RUN_TEST(test_write_if_idle_queues_only_into_an_idle_slot);
  RUN_TEST(test_an_id1_counts_only_when_the_boiler_answered_it);
  RUN_TEST(test_an_id1_behind_a_later_write_is_still_counted);
  RUN_TEST(test_a_read_of_id1_is_not_a_write_sent);
  RUN_TEST(test_a_write_queued_while_another_is_on_the_wire_is_not_lost);
  RUN_TEST(test_the_value_counted_is_the_one_that_went_out);
  RUN_TEST(test_nothing_queued_during_a_write_is_nothing_re_armed);
  RUN_TEST(test_a_write_queued_during_a_read_or_id0_goes_out_next);
  return UNITY_END();
}
