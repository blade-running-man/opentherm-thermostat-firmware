// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_control_internal.h"

// THE BOOST, ladder row 2: "heat at this flow setpoint for this long, then give the
// bit back". The boost has no re-send of its own; it is subsumed by the held-ID-1 re-send
// cadence. Its validation lives here, in pure code.
//
// A boost is RAM only and never restored after a reboot: a bounded request made by a person is
// not a setting, and the reboot is exactly the moment nobody is watching.

#define MS_PER_MINUTE 60000u

// Wrap-safe "has moment t come yet". uint32 monotonic milliseconds wrap after 49.7 days, and a
// plain `now >= deadline` ends every boost started in the minutes before the wrap on its first
// step: its deadline lands after the wrap, numerically small. The signed difference is right
// anywhere within +-24.8 days of t, and the longest boost is eight hours.
//
// DO NOT "simplify" this to a plain comparison. test_the_millisecond_wrap_does_not_end_a_boost_
// early is the test that notices, and nothing on the bench would.
static bool reached(uint32_t now_ms, uint32_t t_ms)
{
    return (int32_t)(now_ms - t_ms) >= 0;
}

ot_control_err_t ot_control_boost_start(ot_control_t *c, const ot_control_cfg_t *cfg,
                                        int16_t setpoint_dc, uint32_t minutes, uint32_t now_ms)
{
    // In the order of test_the_refusals_are_answered_in_order: the most fundamental "no" first.
    // Mode and season as the EXECUTOR last saw them, not the snapshot's: the snapshot is built
    // outside the lock, and a boost accepted on a stale LOCAL one would outlive HA mode and come
    // back with LOCAL, heating for up to eight hours. Only the setpoint's
    // bounds come from the snapshot.
    if (c->seen_ha)
        return OT_CONTROL_OWNED_BY_HA;
    // Refused with a reason, not accepted as a no-op: a running boost on the panel over a
    // boiler that row 1 keeps cold would be a lie.
    if (!c->seen_season)
        return OT_CONTROL_SEASON_IS_OFF;
    // Minutes BEFORE any multiplication: UINT32_MAX minutes times 60000 wraps into a small,
    // plausible duration, and it must be refused as what it is.
    if (minutes == 0 || minutes > OT_CONTROL_BOOST_MAX_MINUTES)
        return OT_CONTROL_BAD_MINUTES;
    // The same bounds as every CH setpoint: a boost is a CH command with a deadline.
    if (setpoint_dc < cfg->flow_min_dc || setpoint_dc > cfg->flow_max_dc)
        return OT_CONTROL_OUT_OF_RANGE;

    // A start REPLACES a running boost: a new setpoint and a fresh deadline. A changed setpoint
    // changes the held ID 1, which step() then asks for at once -- no flag needed here.
    c->boost_active      = true;
    c->boost_setpoint_dc = setpoint_dc;
    c->boost_expires_ms  = now_ms + minutes * MS_PER_MINUTE;   // may wrap; reached() does not mind
    return OT_CONTROL_OK;
}

void ot_control_boost_cancel(ot_control_t *c)
{
    c->boost_active      = false;
    c->boost_setpoint_dc = 0;
    c->boost_expires_ms  = 0;
}

bool ot_control_boost_active(const ot_control_t *c)
{
    return c->boost_active;
}

// 0 when no boost runs: cancel() and init() both zero it.
int16_t ot_control_boost_setpoint_dc(const ot_control_t *c)
{
    return c->boost_setpoint_dc;
}

uint32_t ot_control_boost_remaining_s(const ot_control_t *c, uint32_t now_ms)
{
    // DO NOT drop the !boost_active guard as redundant. A cancelled boost has expires_ms == 0,
    // and reached(now, 0) is true only while (int32_t)now >= 0 -- the first 24.8 days of uptime.
    // After that, without the guard, a dead boost reports seven weeks left.
    // test_a_fresh_or_cancelled_boost_stays_dead_past_2_to_the_31 is the test that notices.
    if (!c->boost_active || reached(now_ms, c->boost_expires_ms))
        return 0;
    // Rounded UP, so a running boost never reads 0 before its deadline. At most eight hours of
    // milliseconds plus 999: no overflow.
    return (c->boost_expires_ms - now_ms + 999u) / 1000u;
}

// Called by step() before the ladder, so the step that reaches the deadline is already LOCAL: a
// boost ends AT its deadline, not one step after it. On a boost that is not running this cancels
// nothing, which is why it needs no guard of its own -- the ladder reads the flag, not the time.
// Nor does the 24.8-day wrap matter: with no boost the deadline is 0 (init() and cancel() leave
// it so), and reached(now, 0) flips between yes and no every 24.8 days of uptime -- a yes
// rewrites the zeroes already there, a no runs nothing.
void otc_boost_expire(ot_control_t *c, uint32_t now_ms)
{
    if (reached(now_ms, c->boost_expires_ms))
        ot_control_boost_cancel(c);
}
