// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "ot_api.h"
#include "ot_api_control.h"
#include "ot_registry.h"
#include "ot_state.h"

static char buf[16384];

void setUp(void) { ot_state_reset(); memset(buf, 0, sizeof buf); }
void tearDown(void) {}

static void test_the_entity_list_names_every_entity(void)
{
    const size_t need = ot_api_render_entities(buf, sizeof buf);
    TEST_ASSERT_TRUE(need < sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"key\":\"flow_temperature\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"key\":\"dhw_setpoint\""));
}

// snprintf semantics: a caller that ran out of room learns how much is needed, rather
// than getting a truncated document that looks valid.
static void test_a_short_buffer_reports_the_required_size(void)
{
    char small[64];
    const size_t need = ot_api_render_entities(small, sizeof small);
    TEST_ASSERT_TRUE(need > sizeof small);
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof small - 1]);
}

static void test_state_renders_a_value_that_arrived(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"flow_temperature\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "43.00"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ok\""));
}

static void test_state_distinguishes_invalid_from_unsupported(void)
{
    ot_state_apply_dataid(27, OT_MSG_DATA_INVALID, 0x0000, 1000);
    ot_state_apply_dataid(33, OT_MSG_UNKNOWN_DATAID, 0x0000, 1000);
    ot_state_apply_dataid(33, OT_MSG_UNKNOWN_DATAID, 0x0000, 2000);
    ot_api_render_state(buf, sizeof buf, 2500);

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"invalid\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"unsupported\""));
}

static void test_a_value_that_never_arrived_renders_null_not_zero(void)
{
    ot_api_render_state(buf, sizeof buf, 1000);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"value\":null"));
}

static void test_a_single_entity_renders_alone(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    const size_t need = ot_api_render_entity("flow_temperature", buf, sizeof buf, 1500);
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "flow_temperature"));
    TEST_ASSERT_NULL(strstr(buf, "dhw_setpoint"));
}

static void test_an_unknown_key_renders_nothing(void)
{
    TEST_ASSERT_TRUE(ot_api_render_entity("no_such", buf, sizeof buf, 0) == 0);
}

// No API projection returns a stored password. The registry is a new
// projection, and it is obliged not to violate it.
static void test_no_projection_leaks_a_secret(void)
{
    ot_api_render_entities(buf, sizeof buf);
    TEST_ASSERT_NULL(strstr(buf, "password"));
    TEST_ASSERT_NULL(strstr(buf, "psk"));
}

// --- Synthetic rows ----------------------------------------------------------------

// `%u` of an int16_t -1 is 4294967295 -- a number a client would take for a Data-ID.
static void test_a_synthetic_row_prints_data_id_null(void)
{
    ot_api_render_entities(buf, sizeof buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"key\":\"control_mode\",\"name\":\"Control mode\","
                                     "\"data_id\":null,"));
    TEST_ASSERT_NULL(strstr(buf, "4294967295"));
    TEST_ASSERT_NULL(strstr(buf, "\"data_id\":-1"));
}

// The other side of "null for a synthetic row": ID 0 is a real Data-ID and prints as 0. A `<= 0`
// in place of `< 0` would print every ID 0 row as null, and a client would take all six status
// flags for synthetic rows.
static void test_an_id_0_row_prints_data_id_0(void)
{
    ot_api_render_entity("fault", buf, sizeof buf, 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"meta\":{\"key\":\"fault\",\"name\":\"Fault\",\"data_id\":0,"
        "\"writable\":false,\"device_class\":\"problem\"},"
        "\"value\":{\"availability\":\"unknown\",\"value\":null,\"age_ms\":null}}",
        buf);
}

// Real rows, byte for byte as they rendered before the synthetic rows existed.
static void test_a_real_row_renders_unchanged(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    ot_api_render_entity("flow_temperature", buf, sizeof buf, 1500);
    TEST_ASSERT_EQUAL_STRING(
        "{\"meta\":{\"key\":\"flow_temperature\",\"name\":\"Flow temperature\",\"data_id\":25,"
        "\"writable\":false,\"unit\":\"°C\",\"device_class\":\"temperature\"},"
        "\"value\":{\"availability\":\"ok\",\"value\":43.00,\"age_ms\":500}}",
        buf);

    ot_api_render_entity("dhw_setpoint", buf, sizeof buf, 1500);
    TEST_ASSERT_EQUAL_STRING(
        "{\"meta\":{\"key\":\"dhw_setpoint\",\"name\":\"Hot water setpoint\",\"data_id\":56,"
        "\"writable\":true,\"unit\":\"°C\",\"device_class\":\"temperature\","
        "\"min\":30.00,\"max\":80.00},"
        "\"value\":{\"availability\":\"unknown\",\"value\":null,\"age_ms\":null}}",
        buf);
}

static void test_an_enum_lists_its_options(void)
{
    ot_api_render_entity("control_state", buf, sizeof buf, 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"meta\":{\"key\":\"control_state\",\"name\":\"Control state\",\"data_id\":null,"
        "\"writable\":false,\"device_class\":\"enum\","
        "\"options\":[\"season_off\",\"boost\",\"local\",\"ha_waiting\",\"failsafe\",\"ha\"]},"
        "\"value\":{\"availability\":\"unknown\",\"value\":null,\"age_ms\":null}}",
        buf);
}

static void test_only_an_enum_carries_options(void)
{
    ot_api_render_entities(buf, sizeof buf);
    unsigned n = 0;
    for (const char *p = buf; (p = strstr(p, "\"options\":")) != NULL; p++)
        n++;
    // control_mode, control_state and room_source -- the registry's whole enum count.
    // A fourth kind that renders "options" is either a new enum (bump this) or a bug leaking
    // the field onto a non-enum row (opentherm_ids.py's VIRTUALS is the single source, per
    // "one list of entities" -- CLAUDE.md).
    TEST_ASSERT_EQUAL_UINT(3, n);
}

// The option, not its index: Home Assistant stores the string.
static void test_an_enum_value_renders_as_its_option(void)
{
    ot_state_set_virtual("control_state", 4.0f, 1000);
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(buf, "\"control_state\":{\"availability\":\"ok\",\"value\":\"failsafe\","
                    "\"age_ms\":500}"),
        buf);
}

// An option is printed for a whole index only: emit_option_value() refuses a fraction rather than
// truncate it to the option below. That guard cannot be reached today -- ot_state_set_virtual()
// refuses a non-whole enum value and every enum is synthetic -- so this pins the chain instead:
// 1.5 is refused and the stored value keeps its age, and the LAST option still renders, which a
// guard written one short (`< count - 1`) would lose.
static void test_an_enum_value_between_two_options_is_refused_and_the_last_one_renders(void)
{
    TEST_ASSERT_TRUE(ot_state_set_virtual("control_state", 5.0f, 1000));   // "ha", the last
    TEST_ASSERT_FALSE(ot_state_set_virtual("control_state", 1.5f, 1200));
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(buf, "\"control_state\":{\"availability\":\"ok\",\"value\":\"ha\",\"age_ms\":500}"),
        buf);
}

static void test_a_synthetic_switch_renders_a_boolean(void)
{
    ot_state_set_virtual("ch_enable", 1.0f, 1000);
    ot_state_set_virtual("ch_enable_effective", 0.0f, 1000);
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ch_enable\":{\"availability\":\"ok\",\"value\":true,"));
    TEST_ASSERT_NOT_NULL(
        strstr(buf, "\"ch_enable_effective\":{\"availability\":\"ok\",\"value\":false,"));
}

static void test_a_flag_still_renders_a_boolean(void)
{
    ot_state_apply_dataid(0, OT_MSG_READ_ACK, 0x0002, 1000);
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ch_active\":{\"availability\":\"ok\",\"value\":true,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fault\":{\"availability\":\"ok\",\"value\":false,"));
}

// No min/max: a synthetic row is bounded by ot_control, not by the table.
static void test_a_synthetic_switch_declares_no_bounds(void)
{
    ot_api_render_entity("ch_enable", buf, sizeof buf, 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"meta\":{\"key\":\"ch_enable\",\"name\":\"Central heating enabled\",\"data_id\":null,"
        "\"writable\":true},"
        "\"value\":{\"availability\":\"unknown\",\"value\":null,\"age_ms\":null}}",
        buf);
}

// --- GET /api/control ------------------------------------------------------------------------

// DHW on with the season OFF, and the DHW bit alone in the status byte, so a CH/DHW mix-up in the
// renderer cannot pass. A boost outlives the season going off (ot_control.h), so this is a state
// the executor can really be in: season_off, with the boost's deadline still running.
static ot_api_control_t control_doc(void)
{
    ot_api_control_t c;
    memset(&c, 0, sizeof c);
    c.mode                     = OT_CONTROL_MODE_LOCAL;
    c.state                    = OT_CONTROL_SEASON_OFF;
    c.reason                   = OT_CONTROL_REASON_NONE;
    c.cause                    = OT_CONTROL_REASON_NONE;
    c.heating_season           = false;
    c.status_high              = OT_STATUS_DHW_ENABLE;
    c.held_setpoint_dc         = 500;
    c.dhw_enable               = true;
    c.dhw_setpoint_set         = true;
    c.dhw_setpoint_dc          = 505;
    c.boost_active             = true;
    c.boost_setpoint_dc        = 500;
    c.boost_remaining_s        = 3540;
    c.failsafe_count           = 2;
    c.last_failsafe_duration_s = 61;
    c.watchdog_overdue_s       = 7;
    c.stack_known              = true;
    c.stack_hwm                = 1184;
    return c;
}

// The whole document, byte for byte: the shape IS the contract a client is written against.
static void test_control_renders_byte_for_byte(void)
{
    const ot_api_control_t c = control_doc();
    const size_t need = ot_api_render_control(&c, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING(
        "{\"schema\":1,\"mode\":\"local\",\"state\":\"season_off\",\"reason\":\"none\","
        "\"cause\":\"none\",\"heating_season\":false,\"status_high\":2,\"held_setpoint_dc\":500,"
        "\"dhw\":{\"enable\":true,\"setpoint_dc\":505},"
        "\"boost\":{\"active\":true,\"setpoint_dc\":500,\"remaining_s\":3540},"
        "\"failsafe\":{\"count\":2,\"last_duration_s\":61},\"watchdog_overdue_s\":7,"
        "\"stack_hwm\":1184}",
        buf);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), need);
}

// Leftover numbers must not leak out, and "none" is null, not 0: no boost is not a boost whose
// time is up, an unset DHW setpoint is not 0 degrees, an unmeasured stack is not a full one.
static void test_control_absent_values_are_null_not_zero(void)
{
    ot_api_control_t c = control_doc();
    c.boost_active     = false;
    c.dhw_setpoint_set = false;
    c.stack_known      = false;
    ot_api_render_control(&c, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"dhw\":{\"enable\":true,\"setpoint_dc\":null}"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr(buf, "\"boost\":{\"active\":false,\"setpoint_dc\":null,\"remaining_s\":null}"), buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"stack_hwm\":null}"), buf);
}

// The failsafe's two answers side by side: what the CH bit is doing, and why the
// state is failsafe at all -- the names are ot_control's, the words the owner greps the log for.
static void test_control_names_the_failsafe_and_its_cause(void)
{
    ot_api_control_t c = control_doc();
    c.mode   = OT_CONTROL_MODE_HA;
    c.state  = OT_CONTROL_FAILSAFE;
    c.reason = OT_CONTROL_REASON_FS_DISARMED;
    c.cause  = OT_CONTROL_REASON_WATCHDOG;
    ot_api_render_control(&c, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"mode\":\"ha\",\"state\":\"failsafe\","
                                             "\"reason\":\"fs_disarmed\",\"cause\":\"watchdog\""),
                                 buf);
}

// ot_control reads anything but HA as LOCAL, and so must the document that describes it.
static void test_control_a_mode_that_is_not_ha_reads_local(void)
{
    ot_api_control_t c = control_doc();
    c.mode = (ot_control_mode_t)7;
    ot_api_render_control(&c, buf, sizeof buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"mode\":\"local\""), buf);
}

static void test_control_short_buffer_reports_the_required_size(void)
{
    const ot_api_control_t c = control_doc();
    const size_t full = ot_api_render_control(&c, buf, sizeof buf);
    char small[16];
    TEST_ASSERT_TRUE(full > sizeof small);
    TEST_ASSERT_EQUAL_UINT32(full, ot_api_render_control(&c, small, sizeof small));
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof small - 1]);
}

// --- the /ws frame ----------------------------------------------------------------------------

static void set_some_values(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);   // flow_temperature 43.00
    ot_state_apply_dataid(0, OT_MSG_READ_ACK, 0x0002, 1000);    // ch_active true, fault false
    ot_state_set_virtual("ch_enable", 1.0f, 1000);
    ot_state_set_virtual("control_state", 4.0f, 1000);         // failsafe
    ot_state_set_virtual("ch_setpoint_effective", 45.5f, 1000);
}

// The value text of `key` in a frame: from after `"key":` to the next `,"` or the closing `}`.
static std::string frame_value(const char *frame, const char *key)
{
    const std::string f(frame), k = std::string("\"") + key + "\":";
    const size_t a = f.find(k);
    if (a == std::string::npos)
        return "<absent>";
    const size_t b = a + k.size();
    size_t e = f.find(",\"", b);
    if (e == std::string::npos)
        e = f.find('}', b);
    return f.substr(b, e - b);
}

// The same, from GET /api/state: from after `"key":{"availability":"…","value":` to `,"age_ms"`.
static std::string state_value(const char *state, const char *key)
{
    const std::string s(state), k = std::string("\"") + key + "\":{\"availability\":";
    const size_t a = s.find(k);
    if (a == std::string::npos)
        return "<absent>";
    const size_t v = s.find("\"value\":", a) + strlen("\"value\":");
    return s.substr(v, s.find(",\"age_ms\"", v) - v);
}

// THE property: for every entity the socket says what GET /api/state says, byte for byte. A
// synthetic switch is `true` on both, an enum its option string on both, an absent value null.
static void test_frame_prints_every_value_as_the_state_document_does(void)
{
    set_some_values();
    static char state[16384];
    ot_api_render_state(state, sizeof state, 1500);
    const size_t need = ot_api_render_frame("state", NULL, buf, sizeof buf);
    TEST_ASSERT_TRUE(need < sizeof buf);
    for (uint16_t i = 0; i < ot_registry_count(); i++) {
        const char *key = ot_registry_at(i)->key;
        TEST_ASSERT_EQUAL_STRING_MESSAGE(state_value(state, key).c_str(),
                                         frame_value(buf, key).c_str(), key);
    }
    TEST_ASSERT_EQUAL_STRING("true", frame_value(buf, "ch_enable").c_str());
    TEST_ASSERT_EQUAL_STRING("\"failsafe\"", frame_value(buf, "control_state").c_str());
    TEST_ASSERT_EQUAL_STRING("45.50", frame_value(buf, "ch_setpoint_effective").c_str());
    TEST_ASSERT_EQUAL_STRING("43.00", frame_value(buf, "flow_temperature").c_str());
    TEST_ASSERT_EQUAL_STRING("true", frame_value(buf, "ch_active").c_str());
    TEST_ASSERT_EQUAL_STRING("null", frame_value(buf, "dhw_setpoint").c_str());
}

// The shape web/src/api/ws.ts accepts, and nothing but it: a frame of any other shape is
// discarded silently by the client. And no stored secret: the frame is the
// registry's keys and values only, and the registry holds none.
static void test_frame_a_snapshot_is_the_whole_registry_in_the_clients_shape(void)
{
    const size_t need = ot_api_render_frame("state", NULL, buf, sizeof buf);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), need);
    TEST_ASSERT_EQUAL_INT(0, strncmp(buf, "{\"type\":\"state\",\"values\":{", 26));
    TEST_ASSERT_EQUAL_STRING("}}", buf + need - 2);
    unsigned keys = 0;
    for (const char *p = buf; (p = strstr(p, "\":")) != NULL; p++)
        keys++;
    TEST_ASSERT_EQUAL_UINT(ot_registry_count() + 2u, keys);   // + "type": and "values":
    TEST_ASSERT_NULL(strstr(buf, "password"));
    TEST_ASSERT_NULL(strstr(buf, "psk"));
}

static void test_frame_a_delta_carries_only_the_marked_entities(void)
{
    set_some_values();
    uint32_t mask[(OT_ENTITY_COUNT + 31) / 32] = {0};
    const int i = ot_registry_index_of("flow_temperature");
    const int j = ot_registry_index_of("control_state");
    mask[i / 32] |= 1u << (i % 32);
    mask[j / 32] |= 1u << (j % 32);
    ot_api_render_frame("delta", mask, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING(
        i < j ? "{\"type\":\"delta\",\"values\":{\"flow_temperature\":43.00,\"control_state\":\"failsafe\"}}"
              : "{\"type\":\"delta\",\"values\":{\"control_state\":\"failsafe\",\"flow_temperature\":43.00}}",
        buf);

    uint32_t none[(OT_ENTITY_COUNT + 31) / 32] = {0};
    ot_api_render_frame("delta", none, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"delta\",\"values\":{}}", buf);
}

static void test_frame_short_buffer_reports_the_required_size(void)
{
    const size_t full = ot_api_render_frame("state", NULL, buf, sizeof buf);
    char small[32];
    TEST_ASSERT_TRUE(full > sizeof small);
    TEST_ASSERT_EQUAL_UINT32(full, ot_api_render_frame("state", NULL, small, sizeof small));
    TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof small - 1]);
}

// ONE value, for MQTT: the bytes /api/state prints for that entity -- the same emitter, so
// the three transports cannot disagree about a switch, an enum or an absent value.
static void test_value_prints_one_entity_as_the_state_document_does(void)
{
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    TEST_ASSERT_EQUAL(5, ot_api_render_value((uint16_t)ot_registry_index_of("flow_temperature"),
                                             buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("43.00", buf);
    ot_state_set_virtual("ch_enable", 1.0f, 1000);
    ot_api_render_value((uint16_t)ot_registry_index_of("ch_enable"), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("true", buf);
    ot_state_set_virtual("control_state", 4.0f, 1000);
    ot_api_render_value((uint16_t)ot_registry_index_of("control_state"), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("\"failsafe\"", buf);
    ot_api_render_value((uint16_t)ot_registry_index_of("outside_temperature"), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("null", buf);
    // The state document says the same for each.
    ot_api_render_state(buf, sizeof buf, 1500);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"control_state\":{\"availability\":\"ok\",\"value\":\"failsafe\""));
}

static void test_value_outside_the_registry_is_zero_and_writes_nothing(void)
{
    buf[0] = 'x';
    TEST_ASSERT_EQUAL(0, ot_api_render_value(OT_ENTITY_COUNT, buf, sizeof buf));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
    char small[3];
    ot_state_apply_dataid(25, OT_MSG_READ_ACK, 0x2B00, 1000);
    TEST_ASSERT_EQUAL(5, ot_api_render_value((uint16_t)ot_registry_index_of("flow_temperature"),
                                             small, sizeof small));
    TEST_ASSERT_EQUAL_CHAR('\0', small[2]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_entity_list_names_every_entity);
    RUN_TEST(test_a_short_buffer_reports_the_required_size);
    RUN_TEST(test_state_renders_a_value_that_arrived);
    RUN_TEST(test_state_distinguishes_invalid_from_unsupported);
    RUN_TEST(test_a_value_that_never_arrived_renders_null_not_zero);
    RUN_TEST(test_a_single_entity_renders_alone);
    RUN_TEST(test_an_unknown_key_renders_nothing);
    RUN_TEST(test_no_projection_leaks_a_secret);
    RUN_TEST(test_a_synthetic_row_prints_data_id_null);
    RUN_TEST(test_an_id_0_row_prints_data_id_0);
    RUN_TEST(test_a_real_row_renders_unchanged);
    RUN_TEST(test_an_enum_lists_its_options);
    RUN_TEST(test_only_an_enum_carries_options);
    RUN_TEST(test_an_enum_value_renders_as_its_option);
    RUN_TEST(test_an_enum_value_between_two_options_is_refused_and_the_last_one_renders);
    RUN_TEST(test_a_synthetic_switch_renders_a_boolean);
    RUN_TEST(test_a_flag_still_renders_a_boolean);
    RUN_TEST(test_a_synthetic_switch_declares_no_bounds);
    RUN_TEST(test_control_renders_byte_for_byte);
    RUN_TEST(test_control_absent_values_are_null_not_zero);
    RUN_TEST(test_control_names_the_failsafe_and_its_cause);
    RUN_TEST(test_control_a_mode_that_is_not_ha_reads_local);
    RUN_TEST(test_control_short_buffer_reports_the_required_size);
    RUN_TEST(test_frame_prints_every_value_as_the_state_document_does);
    RUN_TEST(test_frame_a_snapshot_is_the_whole_registry_in_the_clients_shape);
    RUN_TEST(test_frame_a_delta_carries_only_the_marked_entities);
    RUN_TEST(test_frame_short_buffer_reports_the_required_size);
    RUN_TEST(test_value_prints_one_entity_as_the_state_document_does);
    RUN_TEST(test_value_outside_the_registry_is_zero_and_writes_nothing);
    return UNITY_END();
}
