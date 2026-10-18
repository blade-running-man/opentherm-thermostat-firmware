// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The in-memory log the web UI reads.
//
// A ring buffer, because the alternative is a device that runs out of RAM after a week of
// uptime. What matters is that it never grows, never blocks the task that logged, and that
// what it hands back is valid JSON even when a log line contains a quote or a newline --
// log lines are the one thing on this device that carries arbitrary text.
#include <unity.h>

#include <cstring>
#include <string>

#include "ot_log.h"

void setUp(void) { ot_log_reset(); }
void tearDown(void) {}

static std::string dump(void) {
  static char buf[8192];
  const size_t n = ot_log_render(buf, sizeof buf);
  return std::string(buf, n);
}

static bool contains(const std::string &h, const char *n) {
  return h.find(n) != std::string::npos;
}

void test_an_empty_log_is_an_empty_array(void) {
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());
}

void test_lines_come_back_in_the_order_they_were_written(void) {
  ot_log_write("first");
  ot_log_write("second");
  const std::string json = dump();
  TEST_ASSERT_TRUE(contains(json, "\"first\""));
  TEST_ASSERT_TRUE(contains(json, "\"second\""));
  TEST_ASSERT_TRUE_MESSAGE(json.find("first") < json.find("second"), "order was lost");
}

void test_the_oldest_line_is_dropped_rather_than_the_newest(void) {
  // A log that stops recording once full is worse than useless during an incident: the
  // interesting lines are always the last ones.
  for (int i = 0; i < OT_LOG_LINES + 5; i++) {
    char line[32];
    snprintf(line, sizeof line, "line%d", i);
    ot_log_write(line);
  }
  const std::string json = dump();
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"line0\""), "the newest lines were dropped");
  char newest[32];
  snprintf(newest, sizeof newest, "\"line%d\"", OT_LOG_LINES + 4);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, newest), "the newest line is missing");
}

void test_a_line_longer_than_the_slot_is_truncated_not_overflowed(void) {
  std::string huge(OT_LOG_LINE_LEN * 3, 'x');
  ot_log_write(huge.c_str());
  const std::string json = dump();
  TEST_ASSERT_TRUE(json.size() < OT_LOG_LINE_LEN * 3);
}

void test_quotes_and_newlines_in_a_line_stay_valid_json(void) {
  // Log lines are the only place on this device that carries arbitrary text, so this is
  // where invalid JSON would come from.
  ot_log_write("said \"hello\"\nand left\\");
  const std::string json = dump();
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\\""), "a quote was not escaped");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\n"), "a newline was not escaped");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\\\\"), "a backslash was not escaped");
}

void test_a_null_line_is_ignored(void) {
  ot_log_write(nullptr);
  TEST_ASSERT_EQUAL_STRING("[]", dump().c_str());
}

void test_a_buffer_too_small_reports_failure(void) {
  ot_log_write("something");
  char small[4];
  TEST_ASSERT_EQUAL_size_t(0, ot_log_render(small, sizeof small));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_log_is_an_empty_array);
  RUN_TEST(test_lines_come_back_in_the_order_they_were_written);
  RUN_TEST(test_the_oldest_line_is_dropped_rather_than_the_newest);
  RUN_TEST(test_a_line_longer_than_the_slot_is_truncated_not_overflowed);
  RUN_TEST(test_quotes_and_newlines_in_a_line_stay_valid_json);
  RUN_TEST(test_a_null_line_is_ignored);
  RUN_TEST(test_a_buffer_too_small_reports_failure);
  return UNITY_END();
}
