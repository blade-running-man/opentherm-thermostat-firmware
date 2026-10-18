// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The executor's own writes (ot_control_io_send(), ot_control_io.h): the order in which the held
// ID 1 and the ID 56 are offered to the bus, and the ID 56 a busy bus refused, OWED
// until a slot is idle. A suite of its own: test_ot_control_io has no room left under the 600-line
// ceiling.
//
// It links the REAL ot_control, on purpose: an owed write is right only if one ASK of ot_control is
// followed by exactly one write on the bus, and the ask is ot_control's to make. The bus is a queue
// of one, as ot_bus_track's: a write lands only in an idle slot, the slot goes out before the next
// step, and an ID 1 that went out is counted the way ot_bus_write_state() reports it.
#include <string.h>
#include <unity.h>

#include "ot_config.h"
#include "ot_control.h"
#include "ot_control_io.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t HAND = 200;   // a hand write's frame: neither ID 1 nor ID 56

struct Bus {
  bool     pending;
  uint8_t  id;
  uint16_t value;
  uint32_t id1_seq;      // ID 1 writes gone out, as ot_bus_write_state() counts them
  uint16_t id1_raw;
  int      n56;          // ID 56 writes gone out
  uint16_t first56, last56;
  char     sent[16];     // the executor's first writes, in the order they went out: '1', 'D' (56)
  size_t   n;
};

// The bus the write function reaches: ot_bus_write_if_idle() takes no context, so neither does
// ot_control_io_write_fn, and each test points this at its own bus.
static Bus *s_bus;

static bool bus_write(uint8_t data_id, uint16_t value) {
  Bus *b = s_bus;
  if (b->pending)
    return false;
  b->pending = true;
  b->id      = data_id;
  b->value   = value;
  return true;
}

// The slot goes out, and the boiler answers whatever it was.
static void slot(Bus &b) {
  if (!b.pending)
    return;
  b.pending = false;
  char c = 0;
  if (b.id == 1) {
    b.id1_seq++;
    b.id1_raw = b.value;
    c = '1';
  } else if (b.id == 56) {
    if (b.n56++ == 0)
      b.first56 = b.value;
    b.last56 = b.value;
    c = 'D';
  }
  if (c != 0 && b.n < sizeof b.sent - 1) {
    b.sent[b.n++] = c;
    b.sent[b.n]   = '\0';
  }
}

static void hand_write(Bus &b) {
  b.pending = true;
  b.id      = HAND;
}

static ot_control_cfg_t cfg_of(int16_t dhw_dc) {
  ot_config_t store;
  ot_config_defaults(&store, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(&store, &pub);
  pub.dhw_setpoint_dc = (uint16_t)dhw_dc;
  ot_control_cfg_t cfg;
  ot_control_io_cfg(&pub, &cfg);
  return cfg;
}

struct Rig {
  ot_control_cfg_t         cfg;
  ot_control_t             c;
  ot_control_io_owed_t     owed;
  ot_control_io_readback_t rb;
  ot_control_out_t         out;
  uint32_t                 seen, now;
  int                      asked56;   // steps on which ot_control asked for ID 56
  Bus                      bus;
};

static void boot(Rig &r, const ot_control_cfg_t &cfg) {
  memset(&r, 0, sizeof r);
  r.cfg = cfg;
  ot_control_init(&r.c, &r.cfg, nullptr, 0);
}

// One tick as ot_thermostat.c has it: the bus's report and the readback in, the step, the writes;
// then the slot goes out. busy: a hand write already waits in the slot when the tick offers.
static void tick(Rig &r, bool busy = false) {
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  ot_control_io_confirm(r.bus.id1_seq, r.bus.id1_raw, &r.seen, &in);
  ot_control_io_readback_in(&r.rb, &in);
  ot_control_step(&r.c, &r.cfg, &in, r.now += 1000, &r.out);
  if (r.out.send_dhw_setpoint)
    r.asked56++;
  if (busy)
    hand_write(r.bus);
  s_bus = &r.bus;
  ot_control_io_send(&r.owed, &r.cfg, &r.out, bus_write);
  slot(r.bus);
}

static void accept_dhw(Rig &r, int16_t dc) {
  ot_control_persist_t p;
  TEST_ASSERT_EQUAL_INT(OT_CONTROL_OK, ot_control_apply(&r.c, &r.cfg, OT_ORIGIN_WEB,
                                                        OT_CONTROL_CMD_DHW_SETPOINT, dc, r.now, &p));
  r.cfg = cfg_of(dc);   // the store has it from the next snapshot on
}

// THE PROBE, as the tick now runs. No readback, so an accepted
// command gets ONE write; the owner writes 50.0 and a hand write holds the slot on the one
// step ot_control asks. Dropped there, it was never written -- after its 202.
void test_a_dhw_write_the_busy_bus_refused_goes_out_at_the_next_idle_slot(void) {
  Rig r;
  boot(r, cfg_of(450));
  for (int s = 0; s < 5; s++)
    tick(r);   // flush the boot re-arm of the persisted 45.0, never read back
  r.asked56  = 0;   // then measure only the accepted command below
  r.bus.n56  = 0;
  accept_dhw(r, 500);
  tick(r, true);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.asked56, "ot_control asks on the step the store has it");
  for (int s = 0; s < 1800; s++)
    tick(r);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.asked56, "ot_control asked exactly once");
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.bus.n56, "the owner's 202 dhw_setpoint was never written");
  TEST_ASSERT_EQUAL_HEX16(0x3200, r.bus.last56);
}

// ID 1 FIRST when both are due: CH waits on it. The heating is switched on while an ID 56
// is due; ID 1 takes the idle slot, CH rises on the next step, and ID 56 follows -- owed, not lost.
void test_the_id1_before_ch_rises_goes_before_the_dhw_write(void) {
  ot_control_cfg_t cfg = cfg_of(450);
  cfg.heating_season       = true;
  cfg.local_ch_enable      = true;
  cfg.local_ch_setpoint_dc = 550;
  Rig r;
  boot(r, cfg);
  ot_control_io_readback(&r.rb, 56, OT_MSG_READ_ACK, 0x3C00);   // the boiler keeps 60.0
  tick(r);
  TEST_ASSERT_EQUAL_INT(1, r.asked56);
  TEST_ASSERT_EQUAL_STRING("1", r.bus.sent);
  TEST_ASSERT_EQUAL_HEX8(OT_STATUS_DHW_ENABLE, r.out.status_high);
  tick(r);
  TEST_ASSERT_EQUAL_HEX8_MESSAGE(OT_STATUS_CH_ENABLE | OT_STATUS_DHW_ENABLE, r.out.status_high,
                                 "CH rises on the step after ID 1 went out");
  TEST_ASSERT_EQUAL_STRING("1D", r.bus.sent);
  TEST_ASSERT_EQUAL_HEX16(0x2D00, r.bus.last56);
}

// The ten-second re-send takes the slot from an owed ID 56 too, and the ID 56 waits one slot.
void test_the_id1_resend_goes_before_an_owed_dhw_write(void) {
  const ot_control_cfg_t cfg = cfg_of(450);
  ot_control_out_t out;
  memset(&out, 0, sizeof out);
  out.held_setpoint_dc = 450;
  ot_control_io_owed_t owed;
  memset(&owed, 0, sizeof owed);
  Bus b;
  memset(&b, 0, sizeof b);
  s_bus = &b;

  out.send_dhw_setpoint = true;   // asked while a hand write holds the slot
  hand_write(b);
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  out.send_dhw_setpoint = false;
  out.send_setpoint     = true;   // the re-send falls due
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  out.send_setpoint = false;
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  TEST_ASSERT_EQUAL_STRING("1D", b.sent);
  TEST_ASSERT_EQUAL_HEX16(0x2D00, b.last56);
}

// A new target accepted while one is owed: ONE write, of the newest value, and the three tries of
// counted once each -- the owed write asks ot_control nothing. A boiler that keeps whole
// degrees never agrees with 50.5, so the cap decides how many writes it gets in thirty minutes.
void test_a_target_accepted_while_one_is_owed_is_written_once_at_its_newest(void) {
  Rig r;
  boot(r, cfg_of(450));
  uint16_t kept = 0x3C00;   // 60.0, before we ever write
  ot_control_io_readback(&r.rb, 56, OT_MSG_READ_ACK, kept);
  tick(r, true);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, r.asked56, "45.0 asked for; a hand write holds the slot");
  accept_dhw(r, 505);
  tick(r, true);
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, r.asked56, "50.5 asked for; the slot is still held");
  TEST_ASSERT_EQUAL_INT(0, r.bus.n56);
  for (int s = 0; s < 1800; s++) {
    if (r.now % 60000u == 0)   // the poll ring reads ID 56 once a minute
      ot_control_io_readback(&r.rb, 56, OT_MSG_READ_ACK, kept);
    const int before = r.bus.n56;
    tick(r);
    if (r.bus.n56 != before)
      kept = (uint16_t)(r.bus.last56 & 0xFF00u);   // whole degrees
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(3, r.bus.n56, "ID 56 writes in 30 minutes");
  TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x3280, r.bus.first56, "the first write is the newest target");
  TEST_ASSERT_EQUAL_HEX16(0x3280, r.bus.last56);
  TEST_ASSERT_EQUAL_INT_MESSAGE(r.asked56 - 1, r.bus.n56, "one write per ask, 45.0 superseded");
}

// DHW switched off, or the mode flipped, while an ID 56 is owed: it is still written, with the
// target as it is now. ID 56 is what the boiler keeps for when DHW is on, whoever owns the value --
// and ot_control writes it in either case too (dhw_due() reads neither).
void test_dhw_off_or_a_mode_flip_does_not_cancel_what_is_owed(void) {
  for (int which = 0; which < 2; which++) {
    ot_control_cfg_t cfg = cfg_of(505);
    ot_control_out_t out;
    memset(&out, 0, sizeof out);
    ot_control_io_owed_t owed;
    memset(&owed, 0, sizeof owed);
    Bus b;
    memset(&b, 0, sizeof b);
    s_bus = &b;
    out.send_dhw_setpoint = true;
    hand_write(b);
    ot_control_io_send(&owed, &cfg, &out, bus_write);
    slot(b);
    out.send_dhw_setpoint = false;
    if (which == 0)
      cfg.dhw_enable = false;
    else
      cfg.mode = OT_CONTROL_MODE_HA;
    ot_control_io_send(&owed, &cfg, &out, bus_write);
    slot(b);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, b.n56, which == 0 ? "DHW off" : "a mode flip");
    TEST_ASSERT_EQUAL_HEX16(0x3280, b.last56);
  }
}

// A zeroed record owes nothing -- the task's is static, zeroed at start; what is owed goes out
// ONCE; and an unset target -- 0, the store's "nobody wrote it" -- is never written as 0 degrees:
// it drops what is owed, and a target set again later is not written on the strength of the old ask.
void test_owed_goes_out_once_and_never_as_an_unset_target(void) {
  ot_control_cfg_t cfg = cfg_of(505);
  ot_control_out_t out;
  memset(&out, 0, sizeof out);
  ot_control_io_owed_t owed;
  memset(&owed, 0, sizeof owed);
  Bus b;
  memset(&b, 0, sizeof b);
  s_bus = &b;
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  TEST_ASSERT_FALSE_MESSAGE(b.pending, "a zeroed record owes nothing");

  out.send_dhw_setpoint = true;
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  out.send_dhw_setpoint = false;
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, b.n56, "an idle bus takes it at once, and once");

  out.send_dhw_setpoint = true;
  hand_write(b);
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  out.send_dhw_setpoint = false;
  cfg.dhw_setpoint_set  = false;
  cfg.dhw_setpoint_dc   = 0;
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  cfg = cfg_of(505);
  ot_control_io_send(&owed, &cfg, &out, bus_write);
  slot(b);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, b.n56, "an unset target drops what is owed");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_dhw_write_the_busy_bus_refused_goes_out_at_the_next_idle_slot);
  RUN_TEST(test_the_id1_before_ch_rises_goes_before_the_dhw_write);
  RUN_TEST(test_the_id1_resend_goes_before_an_owed_dhw_write);
  RUN_TEST(test_a_target_accepted_while_one_is_owed_is_written_once_at_its_newest);
  RUN_TEST(test_dhw_off_or_a_mode_flip_does_not_cancel_what_is_owed);
  RUN_TEST(test_owed_goes_out_once_and_never_as_an_unset_target);
  return UNITY_END();
}
