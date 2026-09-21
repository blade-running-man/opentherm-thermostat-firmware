// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

extern "C" {
#include "ot_observe.h"
}

static ot_observe_t o;
void setUp(void) { ot_observe_reset(&o); }
void tearDown(void) {}

static const ot_observe_header_t H = { 1000, 10, 9, 1, 0, true, 0, 1, 0, 0, 0, false, 0, 0 };

static void test_starts_empty(void) {
    TEST_ASSERT_EQUAL_UINT32(0, ot_observe_seen_count(&o));
    TEST_ASSERT_FALSE(ot_observe_get(&o, 0)->seen);
}

static void test_records_one(void) {
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 0x2A80, 5000);
    const ot_observe_entry_t *e = ot_observe_get(&o, 25);
    TEST_ASSERT_TRUE(e->seen);
    TEST_ASSERT_EQUAL_HEX16(0x2A80, e->raw);
    TEST_ASSERT_EQUAL_INT(OT_MSG_READ_ACK, e->type);
    TEST_ASSERT_EQUAL_UINT32(5000, e->last_ms);
    TEST_ASSERT_EQUAL_UINT32(1, e->count);
    TEST_ASSERT_EQUAL_UINT32(1, ot_observe_seen_count(&o));
}

// A repeated response updates the value and increments the counter, it does not start a second row.
static void test_second_response_updates(void) {
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 0x2A80, 5000);
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 0x2B00, 6000);
    const ot_observe_entry_t *e = ot_observe_get(&o, 25);
    TEST_ASSERT_EQUAL_HEX16(0x2B00, e->raw);
    TEST_ASSERT_EQUAL_UINT32(6000, e->last_ms);
    TEST_ASSERT_EQUAL_UINT32(2, e->count);
    TEST_ASSERT_EQUAL_UINT32(1, ot_observe_seen_count(&o));
}

// UNKNOWN-DATAID is an observation too, and a valuable one: it says what the boiler does
// NOT have. Throwing it away would mean hiding half the answer to "what can the boiler do".
static void test_unknown_dataid_is_recorded(void) {
    ot_observe_record(&o, 33, OT_MSG_UNKNOWN_DATAID, 0, 7000);
    TEST_ASSERT_TRUE(ot_observe_get(&o, 33)->seen);
    TEST_ASSERT_EQUAL_INT(OT_MSG_UNKNOWN_DATAID, ot_observe_get(&o, 33)->type);
}

// A Data-ID is eight bits, that is 0..255, while the table holds 128. Everything above
// must be dropped, not written past the end of the array.
static void test_ids_above_the_table_are_dropped(void) {
    ot_observe_record(&o, 200, OT_MSG_READ_ACK, 0x1111, 8000);
    TEST_ASSERT_EQUAL_UINT32(0, ot_observe_seen_count(&o));
    TEST_ASSERT_NULL(ot_observe_get(&o, 200));
}

static void test_json_empty(void) {
    char buf[256];
    const size_t n = ot_observe_render_json(&o, &H, 1000, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0 && n < sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ids\":[]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"answering\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cycles\":10"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"in_duty\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"timeout\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"frame_error\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rx_edges\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pins_shorted\":false"));
}

static void test_json_one_entry(void) {
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 0x2A80, 4000);
    char buf[512];
    const size_t n = ot_observe_render_json(&o, &H, 5000, buf, sizeof buf);
    TEST_ASSERT_TRUE(n > 0 && n < sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":25"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"read-ack\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw\":10880"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"age_ms\":1000"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"count\":1"));
}

// Ordering by ascending ID: the table is read by eye, and jumping rows ruin it.
static void test_json_is_ordered_by_id(void) {
    ot_observe_record(&o, 28, OT_MSG_READ_ACK, 1, 1000);
    ot_observe_record(&o, 3,  OT_MSG_READ_ACK, 2, 1000);
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 3, 1000);
    char buf[1024];
    ot_observe_render_json(&o, &H, 1000, buf, sizeof buf);
    const char *a = strstr(buf, "\"id\":3");
    const char *b = strstr(buf, "\"id\":25");
    const char *c = strstr(buf, "\"id\":28");
    TEST_ASSERT_NOT_NULL(a); TEST_ASSERT_NOT_NULL(b); TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(a < b);
    TEST_ASSERT_TRUE(b < c);
}

// The age survives the millisecond wraparound: the difference is in unsigned arithmetic.
static void test_json_age_survives_wraparound(void) {
    ot_observe_record(&o, 25, OT_MSG_READ_ACK, 1, 0xFFFFFF00u);
    char buf[512];
    ot_observe_render_json(&o, &H, 0xFFFFFF00u + 250u, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"age_ms\":250"));
}

// snprintf semantics: when there is not enough room, the REQUIRED size is returned, the
// buffer does not overflow and stays a string. Without this the caller cannot allocate
// memory by size, and the only way out would be a buffer "with a margin" -- that is, a guess.
static void test_json_truncation_reports_needed_size(void) {
    for (uint8_t id = 0; id < 64; ++id)
        ot_observe_record(&o, id, OT_MSG_READ_ACK, 0xBEEF, 1000);
    char small[64];
    memset(small, 0x7F, sizeof small);
    const size_t need = ot_observe_render_json(&o, &H, 1000, small, sizeof small);
    TEST_ASSERT_TRUE(need >= sizeof small);
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof small - 1]);
    char *big = (char *)malloc(need + 1);
    TEST_ASSERT_NOT_NULL(big);
    const size_t n = ot_observe_render_json(&o, &H, 1000, big, need + 1);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)need, (uint32_t)n);
    free(big);
}

// Every message type has a word of its own: "unknown" and "data invalid" are different
// answers from the boiler, and merging them would mean losing the diagnostics.
static void test_json_every_message_type_has_a_word(void) {
    const char *want[] = { "read-data", "write-data", "invalid-data", "reserved",
                           "read-ack", "write-ack", "data-invalid", "unknown-dataid" };
    for (int t = 0; t < 8; ++t) {
        ot_observe_reset(&o);
        ot_observe_record(&o, 1, (ot_msg_type_t)t, 0, 1000);
        char buf[512];
        ot_observe_render_json(&o, &H, 1000, buf, sizeof buf);
        char needle[48];
        snprintf(needle, sizeof needle, "\"type\":\"%s\"", want[t]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, needle), want[t]);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_records_one);
    RUN_TEST(test_second_response_updates);
    RUN_TEST(test_unknown_dataid_is_recorded);
    RUN_TEST(test_ids_above_the_table_are_dropped);
    RUN_TEST(test_json_empty);
    RUN_TEST(test_json_one_entry);
    RUN_TEST(test_json_is_ordered_by_id);
    RUN_TEST(test_json_age_survives_wraparound);
    RUN_TEST(test_json_truncation_reports_needed_size);
    RUN_TEST(test_json_every_message_type_has_a_word);
    return UNITY_END();
}
