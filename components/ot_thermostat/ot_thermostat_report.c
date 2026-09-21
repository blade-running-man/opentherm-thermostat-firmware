// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What the task says and shows (ot_thermostat.h): the log lines of a step, the once-only warnings
// about ID 1, the synthetic entities and the time zone it follows. A file of its own, cut from
// ot_thermostat.c at the 350-line ceiling along the one seam where nothing is shared: every static
// here is the task's, none is under the spinlock, and the task calls otth_follow_zone() before the
// step and the rest after it.
#include "ot_thermostat_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
// The OT_STATUS_* bit names, for the log line. ot_bus.h deliberately does not re-export them.
#include "ot_bus_sched.h"
#include "ot_config.h"
#include "ot_control_io.h"
#include "ot_state.h"
#include "ot_time.h"

static const char *TAG = "ot_thermostat";

static int16_t            s_logged_dc    = INT16_MIN;
static ot_control_state_t s_logged_state = OT_CONTROL_STATE_COUNT;
static char               s_tz[OT_CONFIG_TZ_MAX + 1];
static bool               s_tz_applied;

// s_tz is compared with the stored zone every second: shorter than the store's field, it would
// truncate a long zone, differ from it for ever, and re-apply and log it on every tick. Pinned to
// ot_config_t's array, which the projection's tz BORROWS: DO NOT pin it to ot_config_public_t.tz,
// which is a pointer -- its size is the pointer's, and says nothing about the zone.
_Static_assert(sizeof s_tz == sizeof(((const ot_config_t *)0)->tz),
               "s_tz must hold every zone the store can hold, and no more");

// The CONFIGURATION IS POLLED, not pushed: there is no change hook on ot_config, and a
// notification path between the settings page and the boiler is one the next field forgets.
// A saved zone therefore applies within a second -- an earlier defect (a zone read once at start
// in ot_time) is why this reads the zone every tick. DO NOT cache it at boot again.
//
// Applied on the FIRST tick whatever it is, the empty zone included: that is what makes
// ot_time.c's explicit "UTC0" hold at boot. Compared with s_tz alone, an unset zone matched the
// empty s_tz and was never applied -- UTC by newlib's default, not by anything this firmware did.
// DO NOT drop s_tz_applied "because s_tz starts empty".
void otth_follow_zone(const char *tz)
{
    if (s_tz_applied && strcmp(tz, s_tz) == 0)
        return;
    s_tz_applied = true;
    snprintf(s_tz, sizeof s_tz, "%s", tz);
    ot_time_set_zone(s_tz);
    ESP_LOGI(TAG, "time zone %s", s_tz[0] != '\0' ? s_tz : "UTC (none set)");
}

// The boiler said UNKNOWN-DATAID to ID 1 (ot_state marks it for good). Said ONCE: the held value
// keeps going out -- the bus counts an answered write as sent whatever the answer, so the
// CH-before-ID-1 invariant is not held hostage by it -- and CH then follows the command alone.
void otth_warn_unsupported_id1(void)
{
    static bool warned;
    if (warned || !ot_state_is_unsupported(OT_CONTROL_IO_ID_TSET))
        return;
    warned = true;
    ESP_LOGW(TAG, "the boiler answers UNKNOWN-DATAID to ID 1: the setpoint is still sent, and the "
                  "boiler heats at its own flow setting");
}

// The boiler said DATA-INVALID to ID 1, most likely because its range and flow_min_dc..flow_max_dc
// disagree. Said ONCE, with the value; why it still counts as sent is ot_control_io_heard_t's.
void otth_warn_invalid_id1(bool heard, uint16_t raw)
{
    static bool warned;
    if (warned || !heard)
        return;
    warned = true;
    const int16_t dc = ot_control_io_f88_dc(raw);
    ESP_LOGW(TAG, "the boiler answers DATA-INVALID to ID 1 (" DC_FMT ", raw 0x%04x): it counts as "
                  "sent; compare flow_min_dc..flow_max_dc with the boiler's range",
             DC_ARG(dc), (unsigned)raw);
}

// Logged on CHANGE only: a line per second would push everything else out of the ring GET
// /api/log serves. The ID 1 line comes before the status line of the same tick, so the log shows
// the CH-before-ID-1 invariant in order: the setpoint on the wire, THEN the bit.
void otth_report(const ot_control_in_t *in, const ot_control_out_t *out, uint8_t prev_status)
{
    if (in->setpoint_confirmed && in->confirmed_dc != s_logged_dc) {
        ESP_LOGI(TAG, "ID 1 on the wire: " DC_FMT "%s", DC_ARG(in->confirmed_dc),
                 in->confirmed_dc == out->held_setpoint_dc ? "" : " (not the held value)");
        s_logged_dc = in->confirmed_dc;
    }
    if (out->state != s_logged_state) {
        ESP_LOGI(TAG, "control_state %s -> %s (cause %s)", ot_control_state_name(s_logged_state),
                 ot_control_state_name(out->state), ot_control_reason_name(out->cause));
        s_logged_state = out->state;
    }
    if (out->status_high != prev_status)
        ESP_LOGI(TAG, "master status high byte %02x -> %02x (ch=%d dhw=%d, %s, %s)",
                 (unsigned)prev_status, (unsigned)out->status_high,
                 (out->status_high & OT_STATUS_CH_ENABLE) ? 1 : 0,
                 (out->status_high & OT_STATUS_DHW_ENABLE) ? 1 : 0,
                 ot_control_state_name(out->state), ot_control_reason_name(out->reason));
}

void otth_publish(const ot_control_cfg_t *cfg, const ot_control_out_t *out, bool ch_command,
                  const ot_bus_write_state_t *ws, uint32_t now)
{
    ot_control_io_virtual_t v[OT_CONTROL_IO_VIRTUAL_MAX];
    const size_t n = ot_control_io_virtuals(cfg, out, ch_command, ws->id1_seq != 0,
                                            ot_control_io_f88_dc(ws->id1_raw), v);
    for (size_t i = 0; i < n; i++)
        ot_state_set_virtual(v[i].key, v[i].value, now);
}
