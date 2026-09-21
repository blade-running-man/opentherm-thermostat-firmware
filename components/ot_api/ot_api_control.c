// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_api_control.h"

#include "ot_api_sink.h"

// The registry's own spellings of control_mode ("local|ha", tools/opentherm_ids.py), so the page
// and Home Assistant read one word in both documents. Anything but HA is LOCAL, as ot_control
// reads it. ot_control has no name function for the mode because nothing else prints it.
static const char *mode_name(ot_control_mode_t m)
{
    return m == OT_CONTROL_MODE_HA ? "ha" : "local";
}

size_t ot_api_render_control(const ot_api_control_t *c, char *out, size_t cap)
{
    ot_api_sink_t s = {out, cap, 0};
    ot_api_emit(&s,
                "{\"schema\":%u,\"mode\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\","
                "\"cause\":\"%s\",\"heating_season\":%s,\"status_high\":%u,"
                "\"held_setpoint_dc\":%d,\"dhw\":{\"enable\":%s,\"setpoint_dc\":",
                (unsigned)OT_API_CONTROL_SCHEMA, mode_name(c->mode),
                ot_control_state_name(c->state), ot_control_reason_name(c->reason),
                ot_control_reason_name(c->cause), c->heating_season ? "true" : "false",
                (unsigned)c->status_high, (int)c->held_setpoint_dc,
                c->dhw_enable ? "true" : "false");
    if (c->dhw_setpoint_set)
        ot_api_emit(&s, "%d}", (int)c->dhw_setpoint_dc);
    else
        ot_api_emit(&s, "null}");

    // The boost's numbers ONLY while one runs: whatever the struct carries otherwise is left over
    // from the last one, and printing it would describe a boost nobody started.
    if (c->boost_active)
        ot_api_emit(&s, ",\"boost\":{\"active\":true,\"setpoint_dc\":%d,\"remaining_s\":%u}",
                    (int)c->boost_setpoint_dc, (unsigned)c->boost_remaining_s);
    else
        ot_api_emit(&s, ",\"boost\":{\"active\":false,\"setpoint_dc\":null,\"remaining_s\":null}");

    ot_api_emit(&s, ",\"failsafe\":{\"count\":%u,\"last_duration_s\":%u},\"watchdog_overdue_s\":%u,"
                    "\"stack_hwm\":",
                (unsigned)c->failsafe_count, (unsigned)c->last_failsafe_duration_s,
                (unsigned)c->watchdog_overdue_s);
    if (c->stack_known)
        ot_api_emit(&s, "%u}", (unsigned)c->stack_hwm);
    else
        ot_api_emit(&s, "null}");

    ot_api_terminate(&s);
    return s.need;
}
