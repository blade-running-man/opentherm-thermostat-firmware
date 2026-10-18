// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_control_internal.h"

// THE WATCHDOG, THE HEAT-HOURS ARM AND THE FAILSAFE'S CH DECISION.

// Saturating addition, copied from ot_sensor.c rather than linked: pulling ot_sensor for
// four lines would put its state type and outlier filter into every suite that includes
// ot_control.h, ot_command's among them. Wrapping here would turn an HA dead for 50 days back
// into a fresh one, which is precisely the failure the accumulator exists to prevent.
uint32_t otc_add_sat(uint32_t a, uint32_t b)
{
    return (a > UINT32_MAX - b) ? UINT32_MAX : a + b;
}

static uint32_t heat_limit_h(const ot_control_cfg_t *cfg)
{
    return (uint32_t)cfg->failsafe_heat_days * 24u;
}

// Advances every duration by one step's interval. Returns true when a full powered hour was
// counted from below the limit, i.e. when heat_hours must be written to NVS now.
//
// EVERY DURATION IS AN ACCUMULATOR, never `now - then`: the millisecond clock
// wraps after 49.7 days, and a subtraction across the wrap reads as "HA spoke a moment ago".
//
// The watchdog counts only while the mode LAST OBSERVED is HA -- the mode that was in effect for
// this interval -- and is held at zero in LOCAL: HA cannot feed a watchdog it does not own, so a
// switch to HA must start the count from nothing, not from however long LOCAL lasted.
bool otc_advance(ot_control_t *c, const ot_control_cfg_t *cfg, uint32_t dt_ms)
{
    c->overdue_ms  = c->seen_ha ? otc_add_sat(c->overdue_ms, dt_ms) : 0;
    c->ok_age_ms   = otc_add_sat(c->ok_age_ms, dt_ms);
    c->ch_age_ms   = otc_add_sat(c->ch_age_ms, dt_ms);
    c->dhw_age_ms  = otc_add_sat(c->dhw_age_ms, dt_ms);
    c->fs_ms       = otc_add_sat(c->fs_ms, dt_ms);   // zeroed on entry, read on exit

    // POWERED HOURS since HA last asked for heat -- not wall-clock time, because SNTP may be as
    // dead as HA. Hours the device was off do not count [OWNER]. A late step does not lose
    // hours: several can complete at once.
    c->hh_ms = otc_add_sat(c->hh_ms, dt_ms);
    const uint32_t hours = c->hh_ms / OTC_HOUR_MS;
    if (hours == 0)
        return false;
    c->hh_ms %= OTC_HOUR_MS;
    // Persisted only while below the limit, so a disarmed device stops writing. The count
    // itself goes on in RAM up to 0xFFFF, which is harmless: at or over the limit is disarmed.
    const bool below = c->heat_hours < heat_limit_h(cfg);
    const uint32_t sum = (uint32_t)c->heat_hours + hours;
    c->heat_hours = sum > UINT16_MAX ? UINT16_MAX : (uint16_t)sum;
    return below;
}

// The failsafe's CH verdict and its reason; ID 1 is the failsafe setpoint, set by the caller.
// Row 1 of the ladder already guarantees the season is on.
//
// fs_want is the hysteresis' own memory, kept apart from ch_out, the bit actually sent: the
// invariant can hold the bit down for a step while the hysteresis wants it up, and a
// dead band read from the bit instead of the verdict would then never heat.
bool otc_failsafe_ch(ot_control_t *c, const ot_control_cfg_t *cfg, const ot_control_in_t *in,
                     ot_control_reason_t *reason)
{
    // Bound 2, the DIYLESS July: no heat request from HA within
    // failsafe_heat_days means the house is not in heating season, whatever the switch says.
    if (c->heat_hours >= heat_limit_h(cfg)) {
        c->fs_want = false;
        *reason    = OT_CONTROL_REASON_FS_DISARMED;
        return false;
    }
    // No fresh room source: heat blind at the failsafe setpoint -- a freeze costs
    // the house more than a week of gas. DO NOT apply the minimum cycle here: it belongs to the
    // hysteresis, and test_a_room_source_lost_mid_failsafe_heats_blind_at_once
    // pins the letter.
    if (!in->room_fresh) {
        c->fs_want = true;
        *reason    = OT_CONTROL_REASON_FS_BLIND;
        return true;
    }
    // +-0.3 K around the target, both edges inclusive; inside the band the last verdict holds.
    const int32_t target = cfg->failsafe_room_target_dc;
    if (in->room_dc <= target - 3)
        c->fs_want = true;
    else if (in->room_dc >= target + 3)
        c->fs_want = false;
    // failsafe_min_cycle_s is both the minimum on and the minimum off time, measured on the bit
    // actually sent -- whoever last moved it, HA included: the boiler's wear does not care.
    if (c->fs_want != c->ch_out && c->ch_age_ms < (uint32_t)cfg->failsafe_min_cycle_s * 1000u) {
        *reason = OT_CONTROL_REASON_MIN_CYCLE;
        return c->ch_out;
    }
    *reason = c->fs_want ? OT_CONTROL_REASON_FS_ROOM_COLD : OT_CONTROL_REASON_FS_ROOM_WARM;
    return c->fs_want;
}
