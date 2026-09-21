// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The executor's translations (ot_control_io.h), the outbound half: what survives a reset
// (RTC_NOINIT, the reset reason, heat hours) and what the executor shows (the synthetic entities,
// the GET /api/control document). The inbound half is test_ot_control_io; the two were one suite
// until it crossed the 600-line ceiling, and they are cut along the header's own sections.
//
// It links the REAL ot_control, ot_config, ot_state and ot_registry, on purpose: what is pinned
// here is the seam between them -- the registry's option order against ot_control's enums, the
// virtual keys against ot_state_set_virtual(), a missing heat-hours key against the failsafe.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "ot_config.h"
#include "ot_control.h"
#include "ot_control_io.h"
#include "ot_registry.h"
#include "ot_state.h"

void setUp(void) { ot_state_reset(); }
void tearDown(void) {}

static ot_config_public_t pub_of(ot_config_t *store) {
  ot_config_defaults(store, "aabbccddeeff");
  ot_config_public_t pub;
  ot_config_project(store, &pub);
  return pub;
}

// --- what survives a reset -----------------------------------------------------------------------

void test_a_soft_reset_restores_the_overdue_count(void) {
  const int soft[] = {OT_CONTROL_IO_RST_SW, OT_CONTROL_IO_RST_PANIC, OT_CONTROL_IO_RST_INT_WDT,
                      OT_CONTROL_IO_RST_TASK_WDT, OT_CONTROL_IO_RST_WDT};
  for (int r : soft) {
    ot_control_io_rtc_t rtc;
    ot_control_io_rtc_store(&rtc, 612345, 1234567);
    ot_control_restore_t out;
    ot_control_io_restore(r, &rtc, true, 5, &out);
    TEST_ASSERT_TRUE_MESSAGE(out.overdue_valid, ot_control_io_reset_name(r));
    TEST_ASSERT_EQUAL_UINT32(612345, out.overdue_ms);
    TEST_ASSERT_TRUE_MESSAGE(out.hh_valid, ot_control_io_reset_name(r));
    TEST_ASSERT_EQUAL_UINT32(1234567, out.hh_ms);
    TEST_ASSERT_EQUAL_UINT16(5, out.heat_hours);
  }
}

// Power-on is not a loop: it starts from zero even when the blob happens to be intact -- as it is
// after a power cut short enough for the RTC RAM to keep its charge. So does every reason not
// listed, a later ESP-IDF's included. 10..15 are ESP-IDF 5.5's own esp_reset_reason_t values past
// BROWNOUT, raw here because ot_control_io names none of them: ESP_RST_SDIO, ESP_RST_USB,
// ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP (esp_system.h).
void test_every_other_reset_starts_from_zero_even_with_an_intact_blob(void) {
  const int hard[] = {OT_CONTROL_IO_RST_UNKNOWN, OT_CONTROL_IO_RST_POWERON, OT_CONTROL_IO_RST_EXT,
                      OT_CONTROL_IO_RST_DEEPSLEEP, 10, 11, 12, 13, 14, 15, -1, 99};
  char msg[48];
  for (int r : hard) {
    ot_control_io_rtc_t rtc;
    ot_control_io_rtc_store(&rtc, 612345, 1234567);
    ot_control_restore_t out;
    ot_control_io_restore(r, &rtc, true, 5, &out);
    snprintf(msg, sizeof msg, "reset reason %d (%s)", r, ot_control_io_reset_name(r));
    TEST_ASSERT_FALSE_MESSAGE(out.overdue_valid, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, out.overdue_ms, msg);
    TEST_ASSERT_FALSE_MESSAGE(out.hh_valid, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, out.hh_ms, msg);
  }
}

// A brown-out counts as a soft reset: a brown-out loop -- WiFi transmitting on a weak
// supply -- is the commonest reboot loop of an ESP32-C3, and zeroing the watchdog at each of its
// boots would hold HA mode in ha_waiting with CH off for as long as it lasts. The CHECK WORD, not
// the reason, is what rejects RTC RAM the dip corrupted.
void test_a_brownout_restores_under_the_check_word(void) {
  ot_control_io_rtc_t rtc;
  ot_control_restore_t out;
  ot_control_io_rtc_store(&rtc, 612345, 1234567);
  ot_control_io_restore(OT_CONTROL_IO_RST_BROWNOUT, &rtc, true, 5, &out);
  TEST_ASSERT_TRUE_MESSAGE(out.overdue_valid, "a brown-out loop is a reboot loop");
  TEST_ASSERT_EQUAL_UINT32(612345, out.overdue_ms);
  TEST_ASSERT_TRUE(out.hh_valid);
  TEST_ASSERT_EQUAL_UINT32(1234567, out.hh_ms);

  rtc.hh_ms ^= 0x10u;                                 // one bit the dip flipped
  ot_control_io_restore(OT_CONTROL_IO_RST_BROWNOUT, &rtc, true, 5, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);
  TEST_ASSERT_EQUAL_UINT32(0, out.overdue_ms);
  TEST_ASSERT_FALSE(out.hh_valid);
  TEST_ASSERT_EQUAL_UINT32(0, out.hh_ms);
}

void test_a_blob_that_fails_its_check_is_noise(void) {
  ot_control_io_rtc_t rtc;
  ot_control_restore_t out;

  ot_control_io_rtc_store(&rtc, 1000, 2000);
  rtc.overdue_ms = 999999;                      // torn: magic intact, check stale
  ot_control_io_restore(OT_CONTROL_IO_RST_SW, &rtc, true, 0, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);
  TEST_ASSERT_FALSE(out.hh_valid);

  ot_control_io_rtc_store(&rtc, 1000, 2000);
  rtc.hh_ms = 3000;                             // torn in the part-hour: BOTH are refused
  ot_control_io_restore(OT_CONTROL_IO_RST_SW, &rtc, true, 0, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);
  TEST_ASSERT_FALSE(out.hh_valid);

  ot_control_io_rtc_store(&rtc, 1000, 2000);
  rtc.magic ^= 1u;
  rtc.check = ~(rtc.magic ^ rtc.overdue_ms ^ rtc.hh_ms);   // consistent, of another magic
  ot_control_io_restore(OT_CONTROL_IO_RST_SW, &rtc, true, 0, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);

  memset(&rtc, 0, sizeof rtc);
  ot_control_io_restore(OT_CONTROL_IO_RST_SW, &rtc, true, 0, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);

  ot_control_io_restore(OT_CONTROL_IO_RST_SW, nullptr, true, 0, &out);
  TEST_ASSERT_FALSE(out.overdue_valid);
  TEST_ASSERT_EQUAL_UINT32(0, out.overdue_ms);
  TEST_ASSERT_FALSE(out.hh_valid);
  TEST_ASSERT_EQUAL_UINT32(0, out.hh_ms);
}

void test_the_store_writes_the_magic_and_the_check(void) {
  ot_control_io_rtc_t rtc;
  memset(&rtc, 0, sizeof rtc);
  ot_control_io_rtc_store(&rtc, 42, 43);
  TEST_ASSERT_EQUAL_HEX32(OT_CONTROL_IO_RTC_MAGIC, rtc.magic);
  TEST_ASSERT_EQUAL_UINT32(42, rtc.overdue_ms);
  TEST_ASSERT_EQUAL_UINT32(43, rtc.hh_ms);
  TEST_ASSERT_EQUAL_HEX32(~(OT_CONTROL_IO_RTC_MAGIC ^ 42u ^ 43u), rtc.check);
}

// No heat hours stored means HA has never asked for heat: DISARMED. End to end through the real
// executor, this exercises on the host -- HA mode, a watchdog that runs out,
// and a failsafe that holds CH down with fs_disarmed instead of heating blind.
void test_no_heat_hours_stored_is_disarmed(void) {
  ot_control_restore_t r;
  ot_control_io_restore(OT_CONTROL_IO_RST_POWERON, nullptr, false, 0, &r);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, r.heat_hours);
  ot_control_io_restore(OT_CONTROL_IO_RST_POWERON, nullptr, true, 0, &r);
  TEST_ASSERT_EQUAL_UINT16(0, r.heat_hours);

  ot_config_t store;
  ot_config_public_t pub = pub_of(&store);
  pub.control_mode = OT_CONFIG_MODE_HA;
  pub.heating_season = true;
  pub.watchdog_s = 60;
  ot_control_cfg_t cfg;
  ot_control_io_cfg(&pub, &cfg);
  ot_control_io_restore(OT_CONTROL_IO_RST_POWERON, nullptr, false, 0, &r);
  ot_control_t c;
  ot_control_init(&c, &cfg, &r, 0);
  ot_control_in_t in;
  memset(&in, 0, sizeof in);
  ot_control_out_t out;
  for (uint32_t t = 1000; t <= 61000; t += 1000)
    ot_control_step(&c, &cfg, &in, t, &out);
  TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, out.state);
  TEST_ASSERT_EQUAL(OT_CONTROL_REASON_FS_DISARMED, out.reason);
  TEST_ASSERT_EQUAL_HEX8(OT_STATUS_DHW_ENABLE, out.status_high);
}

void test_every_reset_has_a_name_and_an_unknown_one_is_other(void) {
  TEST_ASSERT_EQUAL_STRING("unknown", ot_control_io_reset_name(OT_CONTROL_IO_RST_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("poweron", ot_control_io_reset_name(OT_CONTROL_IO_RST_POWERON));
  TEST_ASSERT_EQUAL_STRING("ext", ot_control_io_reset_name(OT_CONTROL_IO_RST_EXT));
  TEST_ASSERT_EQUAL_STRING("sw", ot_control_io_reset_name(OT_CONTROL_IO_RST_SW));
  TEST_ASSERT_EQUAL_STRING("panic", ot_control_io_reset_name(OT_CONTROL_IO_RST_PANIC));
  TEST_ASSERT_EQUAL_STRING("int_wdt", ot_control_io_reset_name(OT_CONTROL_IO_RST_INT_WDT));
  TEST_ASSERT_EQUAL_STRING("task_wdt", ot_control_io_reset_name(OT_CONTROL_IO_RST_TASK_WDT));
  TEST_ASSERT_EQUAL_STRING("wdt", ot_control_io_reset_name(OT_CONTROL_IO_RST_WDT));
  TEST_ASSERT_EQUAL_STRING("deepsleep", ot_control_io_reset_name(OT_CONTROL_IO_RST_DEEPSLEEP));
  TEST_ASSERT_EQUAL_STRING("brownout", ot_control_io_reset_name(OT_CONTROL_IO_RST_BROWNOUT));
  TEST_ASSERT_EQUAL_STRING("other", ot_control_io_reset_name(10));
  TEST_ASSERT_EQUAL_STRING("other", ot_control_io_reset_name(-1));
}

// --- what the executor shows ---------------------------------------------------------------------

// An enum travels as its option INDEX (ot_state_set_virtual()), and the executor hands in its
// ORDINAL. The two orders are one fact written in two places -- tools/opentherm_ids.py and
// ot_control.h -- and this is the test that ties them: a reordering on either side would publish
// "failsafe" as "ha" to the owner's Home Assistant.
void test_the_enum_options_are_the_executor_ordinals(void) {
  const ot_entity_t *state = ot_registry_by_key("control_state");
  TEST_ASSERT_NOT_NULL(state);
  TEST_ASSERT_EQUAL_UINT8(OT_CONTROL_STATE_COUNT, ot_registry_option_count(state));
  for (int i = 0; i < OT_CONTROL_STATE_COUNT; i++) {
    size_t len = 0;
    const char *o = ot_registry_option(state, (unsigned)i, &len);
    const char *name = ot_control_state_name((ot_control_state_t)i);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(name), len, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(o, name, len), name);
  }
  const ot_entity_t *mode = ot_registry_by_key("control_mode");
  TEST_ASSERT_NOT_NULL(mode);
  TEST_ASSERT_EQUAL_UINT8(2, ot_registry_option_count(mode));
  size_t len = 0;
  const char *o = ot_registry_option(mode, OT_CONTROL_MODE_LOCAL, &len);
  TEST_ASSERT_EQUAL_INT(0, strncmp(o, "local", len));
  TEST_ASSERT_EQUAL_size_t(5, len);
  o = ot_registry_option(mode, OT_CONTROL_MODE_HA, &len);
  TEST_ASSERT_EQUAL_INT(0, strncmp(o, "ha", len));
  TEST_ASSERT_EQUAL_size_t(2, len);
}

// Every field distinct from its neighbours, so a crossed wire names itself: DHW on with the season
// OFF, and the DHW bit ALONE in the status byte -- a CH/DHW mix-up passes on a sample where both
// are on. Not a step the executor can take (row 1 would make it season_off): a mapping test wants
// distinct fields, not a consistent step.
static void sample(ot_control_cfg_t *cfg, ot_control_out_t *out) {
  memset(cfg, 0, sizeof *cfg);
  memset(out, 0, sizeof *out);
  cfg->mode = OT_CONTROL_MODE_HA;
  cfg->heating_season = false;
  cfg->dhw_enable = true;
  cfg->dhw_setpoint_set = true;
  cfg->dhw_setpoint_dc = 505;
  out->state = OT_CONTROL_FAILSAFE;
  out->reason = OT_CONTROL_REASON_MIN_CYCLE;  // 7: no state has it, so a crossed wire shows
  out->cause = OT_CONTROL_REASON_WATCHDOG;
  out->status_high = OT_STATUS_DHW_ENABLE;
  out->held_setpoint_dc = 450;
  out->failsafe_count = 3;
  out->last_failsafe_duration_s = 120;
  out->overdue_ms = 61999;
}

static float value_of(const ot_control_io_virtual_t *v, size_t n, const char *key, bool *found) {
  for (size_t i = 0; i < n; i++)
    if (strcmp(v[i].key, key) == 0) {
      *found = true;
      return v[i].value;
    }
  *found = false;
  return NAN;
}

// Every key the task publishes is a synthetic row, every synthetic row is published, and the
// state model takes every value as it comes -- set_virtual() refuses silently otherwise.
void test_every_synthetic_row_is_published_and_accepted(void) {
  ot_control_cfg_t cfg;
  ot_control_out_t out;
  sample(&cfg, &out);
  ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX];
  const size_t n = ot_control_io_virtuals(&cfg, &out, true, true, 455, v);
  size_t synthetic = 0;
  for (uint16_t i = 0; i < ot_registry_count(); i++) {
    const ot_entity_t *e = ot_registry_at(i);
    // room_temperature_effective and room_source are synthetic rows too, but the ot_room
    // registry (via ot_thermostat) feeds them, not the executor;
    // they are published, just not from ot_control_io_virtuals().
    if (e->data_id < 0 && strcmp(e->key, "room_temperature_effective") != 0 &&
        strcmp(e->key, "room_source") != 0)
      synthetic++;
  }
  TEST_ASSERT_EQUAL_size_t(synthetic, n);
  for (size_t i = 0; i < n; i++) {
    const ot_entity_t *e = ot_registry_by_key(v[i].key);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, v[i].key);
    TEST_ASSERT_TRUE_MESSAGE(e->data_id < 0, v[i].key);
    TEST_ASSERT_TRUE_MESSAGE(ot_state_set_virtual(v[i].key, v[i].value, 1000), v[i].key);
  }
}

void test_each_virtual_carries_its_own_value(void) {
  ot_control_cfg_t cfg;
  ot_control_out_t out;
  sample(&cfg, &out);
  ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX];
  const size_t n = ot_control_io_virtuals(&cfg, &out, true, true, 455, v);
  bool f = false;
  TEST_ASSERT_EQUAL_FLOAT(1.0f, value_of(v, n, "ch_enable", &f));   // the command, not the bit
  TEST_ASSERT_TRUE(f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, value_of(v, n, "ch_enable_effective", &f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, value_of(v, n, "dhw_enable", &f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, value_of(v, n, "heating_season", &f));
  TEST_ASSERT_EQUAL_FLOAT((float)OT_CONTROL_MODE_HA, value_of(v, n, "control_mode", &f));
  TEST_ASSERT_EQUAL_FLOAT((float)OT_CONTROL_FAILSAFE, value_of(v, n, "control_state", &f));
  TEST_ASSERT_EQUAL_FLOAT(3.0f, value_of(v, n, "failsafe_count", &f));
  TEST_ASSERT_EQUAL_FLOAT(120.0f, value_of(v, n, "last_failsafe_duration_s", &f));
  TEST_ASSERT_EQUAL_FLOAT(45.5f, value_of(v, n, "ch_setpoint_effective", &f));

  // Every one of the four flipped, and the two pairs still apart.
  cfg.mode = OT_CONTROL_MODE_LOCAL;
  cfg.dhw_enable = false;
  cfg.heating_season = true;
  out.status_high = OT_STATUS_CH_ENABLE;
  const size_t m = ot_control_io_virtuals(&cfg, &out, false, true, 455, v);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, value_of(v, m, "ch_enable", &f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, value_of(v, m, "ch_enable_effective", &f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, value_of(v, m, "dhw_enable", &f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, value_of(v, m, "heating_season", &f));
  TEST_ASSERT_EQUAL_FLOAT((float)OT_CONTROL_MODE_LOCAL, value_of(v, m, "control_mode", &f));
}

// "The ID 1 value actually on the wire": nothing is on the wire before the first write, and a
// held value is not a sent one.
void test_no_wire_value_leaves_the_effective_setpoint_out(void) {
  ot_control_cfg_t cfg;
  ot_control_out_t out;
  sample(&cfg, &out);
  ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX];
  const size_t n = ot_control_io_virtuals(&cfg, &out, true, false, 455, v);
  TEST_ASSERT_EQUAL_size_t(OT_CONTROL_IO_VIRTUAL_MAX - 1, n);
  bool f = true;
  value_of(v, n, "ch_setpoint_effective", &f);
  TEST_ASSERT_FALSE(f);
}

void test_the_document_carries_the_step(void) {
  ot_control_cfg_t cfg;
  ot_control_out_t out;
  sample(&cfg, &out);
  ot_api_control_t doc;
  memset(&doc, 0xA5, sizeof doc);
  ot_control_io_document(&cfg, &out, &doc);
  TEST_ASSERT_EQUAL(OT_CONTROL_MODE_HA, doc.mode);
  TEST_ASSERT_EQUAL(OT_CONTROL_FAILSAFE, doc.state);
  TEST_ASSERT_EQUAL(OT_CONTROL_REASON_MIN_CYCLE, doc.reason);
  TEST_ASSERT_EQUAL(OT_CONTROL_REASON_WATCHDOG, doc.cause);
  TEST_ASSERT_FALSE(doc.heating_season);
  TEST_ASSERT_EQUAL_HEX8(OT_STATUS_DHW_ENABLE, doc.status_high);
  TEST_ASSERT_EQUAL_INT16(450, doc.held_setpoint_dc);
  TEST_ASSERT_TRUE(doc.dhw_enable);
  TEST_ASSERT_TRUE(doc.dhw_setpoint_set);
  TEST_ASSERT_EQUAL_INT16(505, doc.dhw_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(3, doc.failsafe_count);
  TEST_ASSERT_EQUAL_UINT32(120, doc.last_failsafe_duration_s);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(61, doc.watchdog_overdue_s, "whole seconds, truncated");
  TEST_ASSERT_FALSE(doc.boost_active);
  TEST_ASSERT_EQUAL_INT16(0, doc.boost_setpoint_dc);
  TEST_ASSERT_EQUAL_UINT32(0, doc.boost_remaining_s);
  TEST_ASSERT_FALSE(doc.stack_known);
  TEST_ASSERT_EQUAL_UINT32(0, doc.stack_hwm);

  cfg.mode = (ot_control_mode_t)7;
  ot_control_io_document(&cfg, &out, &doc);
  TEST_ASSERT_EQUAL(OT_CONTROL_MODE_LOCAL, doc.mode);
}

// From the store to the wire: a DHW setpoint nobody has written (0 in the store) reaches
// GET /api/control as null, not as 0 degrees -- through the real mapping, the real document and
// the real renderer, so a 0 leaking at any of the three seams shows.
void test_an_unset_dhw_setpoint_reaches_the_document_as_null(void) {
  ot_config_t store;
  const ot_config_public_t pub = pub_of(&store);   // the defaults: DHW on, setpoint unset
  ot_control_cfg_t cfg;
  ot_control_io_cfg(&pub, &cfg);
  ot_control_out_t out;
  memset(&out, 0, sizeof out);
  ot_api_control_t doc;
  ot_control_io_document(&cfg, &out, &doc);
  static char buf[1024];
  TEST_ASSERT_TRUE(ot_api_render_control(&doc, buf, sizeof buf) < sizeof buf);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"dhw\":{\"enable\":true,\"setpoint_dc\":null}"), buf);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_soft_reset_restores_the_overdue_count);
  RUN_TEST(test_every_other_reset_starts_from_zero_even_with_an_intact_blob);
  RUN_TEST(test_a_brownout_restores_under_the_check_word);
  RUN_TEST(test_a_blob_that_fails_its_check_is_noise);
  RUN_TEST(test_the_store_writes_the_magic_and_the_check);
  RUN_TEST(test_no_heat_hours_stored_is_disarmed);
  RUN_TEST(test_every_reset_has_a_name_and_an_unknown_one_is_other);
  RUN_TEST(test_the_enum_options_are_the_executor_ordinals);
  RUN_TEST(test_every_synthetic_row_is_published_and_accepted);
  RUN_TEST(test_each_virtual_carries_its_own_value);
  RUN_TEST(test_no_wire_value_leaves_the_effective_setpoint_out);
  RUN_TEST(test_the_document_carries_the_step);
  RUN_TEST(test_an_unset_dhw_setpoint_reaches_the_document_as_null);
  return UNITY_END();
}
