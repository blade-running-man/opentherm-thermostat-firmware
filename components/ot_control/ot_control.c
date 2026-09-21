// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <string.h>

#include "ot_control_internal.h"

// THE LADDER, THE EDGES AND THE STEP. The watchdog and the failsafe's
// CH verdict are ot_control_failsafe.c's, the boost is ot_control_boost.c's, and every command
// enters through ot_control_apply.c; this file owns what the boiler is told once a second.

#define DHW_RETRY_MS  60000u   // a differing readback is rewritten at most once a minute
#define DHW_MAX_TRIES 3u       // unanswered writes of one target before giving up on it

// Quantised to 0.5 degrees, then inside [flow_min, flow_max]. In that order, because the
// bounds are the half that keeps the boiler safe: a bound that is not a multiple of 5 is held as
// it is (test_the_bounds_win_over_the_quantisation). 5 is odd, so there are no ties to round;
// a negative value rounds toward zero and is clamped away anyway. apply() uses it too, to put a
// CH setpoint on the store's grid before persisting it.
int16_t otc_bound(const ot_control_cfg_t *cfg, int32_t dc)
{
    int32_t q = (dc + 2) / 5 * 5;
    if (q < cfg->flow_min_dc)
        q = cfg->flow_min_dc;
    if (q > cfg->flow_max_dc)
        q = cfg->flow_max_dc;
    return (int16_t)q;
}

// Forgets HA's command at an entry into HA ownership. HA's setpoint falls back to the
// failsafe setpoint rather than to "none": there is no "no setpoint" state, and if HA's
// first command is CH_ENABLE = 1 alone, the boiler heats at the one value the owner configured
// for "HA said nothing about temperature".
void otc_forget_ha(ot_control_t *c, const ot_control_cfg_t *cfg)
{
    c->ha_heard       = false;
    c->ha_ch_enable   = false;
    c->ha_setpoint_dc = cfg->failsafe_setpoint_dc;
    c->fs_latched     = false;
}

// The edges, observed HERE ONLY: step() calls this; apply() and boost_start() never do,
// because their snapshot is built outside the lock and may predate what the executor has already
// seen -- taken as an edge, a stale mode would be taken back on the next step as an entry, and
// HA's accepted command forgotten. The entries into HA ownership are
// LOCAL -> HA and the season off -> on while in HA mode; boot in HA mode is init()'s; leaving
// failsafe is apply()'s, because it is a command, not an edge.
//
// EDGES ARE SAMPLED, once a step. A flip HA -> LOCAL -> HA, or season on -> off -> on, that
// starts and ends between two steps is never seen and so is not an entry: an HA command at most
// one step old survives it. That is the boundary, and it is harmless. DO NOT build edge counters
// for it.
//
// The season going OFF is not an entry: row 1 outranks every HA row while it lasts, and the way
// back on is an entry of its own, so nothing HA sent before or during a season-off survives it.
// HA's clock is not touched by either edge -- its commands are accepted with the season off, so
// the watchdog stays meaningful through the summer.
//
// ANY mode edge ends a boost. A boost cannot exist while the executor has seen HA --
// boost_start() refuses then -- so the HA -> LOCAL half has nothing to end today; it is there so no
// later path lets a boost outlive a change of owner. DO NOT end it on the season going off as
// well: a boost with the season off is an overlap the ladder resolves.
static void observe(ot_control_t *c, const ot_control_cfg_t *cfg)
{
    const bool ha = cfg->mode == OT_CONTROL_MODE_HA;
    bool entry = false;
    if (ha != c->seen_ha) {
        entry = ha;
        ot_control_boost_cancel(c);
        c->seen_ha = ha;
    }
    if (cfg->heating_season != c->seen_season) {
        entry = entry || (cfg->heating_season && ha);
        c->seen_season = cfg->heating_season;
    }
    if (entry)
        otc_forget_ha(c, cfg);
}

void ot_control_init(ot_control_t *c, const ot_control_cfg_t *cfg,
                     const ot_control_restore_t *r, uint32_t now_ms)
{
    memset(c, 0, sizeof *c);
    // A persisted DHW setpoint must survive a reboot even on a boiler that never answers
    // the ID 56 readback (there is nothing to reconcile against there, so dhw_due() would otherwise
    // never re-send it). Arm one write, exactly as an accepted DHW_SETPOINT command does via
    // dhw_reopen -> dhw_unread_once. Harmless on a readback boiler: it writes once, then the
    // readback reconciles (and if it already agrees, the agree branch consumes the arm with no
    // write); bounded by DHW_MAX_TRIES either way.
    if (cfg->dhw_setpoint_set)
        c->dhw_unread_once = true;
    c->seen_ha      = cfg->mode == OT_CONTROL_MODE_HA;
    c->seen_season  = cfg->heating_season;
    c->last_step_ms = now_ms;
    otc_forget_ha(c, cfg);   // boot in HA mode is an entry; in LOCAL, harmless
    if (r) {
        // The overdue survives a soft reset so that a reboot loop cannot hold ha_waiting for ever
        // booted in LOCAL, the first step zeroes it -- HA owned no watchdog there. The
        // part-hour survives for the other bound: a loop faster than an hour never completes one,
        // and would never disarm.
        c->overdue_ms = r->overdue_valid ? r->overdue_ms : 0;
        c->heat_hours = r->heat_hours;
        c->hh_ms      = r->hh_valid ? r->hh_ms : 0;
    }
    c->held_dc     = otc_bound(cfg, cfg->failsafe_setpoint_dc);
    c->ch_age_ms   = UINT32_MAX;   // no change known: the first failsafe decision is not held
}

// first match wins. ha_waiting is row 4 only while the watchdog has not expired (
// "or becomes failsafe when the watchdog expires"), and HA can be blind only once it has spoken
// ("HA's commands are still fresh"). fs_latched keeps a failsafe until an accepted HA
// CH_ENABLE with its cause gone (apply()); a latched failsafe always has ha_heard or an expired
// watchdog, so row 4 never sees one.
static ot_control_state_t ladder(const ot_control_t *c, const ot_control_cfg_t *cfg, bool expired)
{
    if (!cfg->heating_season)
        return OT_CONTROL_SEASON_OFF;
    if (!c->seen_ha)
        return c->boost_active ? OT_CONTROL_BOOST : OT_CONTROL_LOCAL;
    if (!c->ha_heard && !expired)
        return OT_CONTROL_HA_WAITING;
    if (c->fs_latched || expired || c->blind)
        return OT_CONTROL_FAILSAFE;
    return OT_CONTROL_HA;
}

// Entries, exits and the cause of a failsafe. The duration is the fs_ms
// accumulator, not exit - entry, for the wrap's sake. A dead HA is reported as dead even when it
// was also blind: an expired watchdog means HA's commands are not fresh, so the fresh-commands rule does not apply.
//
// fs_cause is RECOMPUTED every step the failsafe holds, not latched with the state. A
// failsafe entered while blind and then held only by the latch -- the source recovered and
// HA feeds the watchdog with a CH_SETPOINT but has not re-sent CH_ENABLE -- must stop claiming
// HA_BLIND: neither acute cause holds, so the cause is NONE (the state alone says "failsafe,
// latched, awaiting an accepted HA CH_ENABLE"). A stale HA_BLIND there misreports control_state's
// cause attribute to the owner. No enum names "latched" and inventing one would rewrite the
// generated reason options; NONE is the honest "no acute cause right now".
static void track_failsafe(ot_control_t *c, ot_control_state_t prev, ot_control_state_t s,
                           bool expired)
{
    if (s != OT_CONTROL_FAILSAFE) {
        if (prev == OT_CONTROL_FAILSAFE)
            c->last_failsafe_duration_s = c->fs_ms / 1000u;
        return;
    }
    if (prev != OT_CONTROL_FAILSAFE) {
        c->failsafe_count++;
        c->fs_ms   = 0;
        c->fs_want = c->ch_out;   // the hysteresis starts from what the boiler was last told
    }
    c->fs_latched = true;
    c->fs_cause   = expired  ? OT_CONTROL_REASON_WATCHDOG
                  : c->blind ? OT_CONTROL_REASON_HA_BLIND
                             : OT_CONTROL_REASON_NONE;
}

// ID 56 reconciliation: written when the readback differs, at most once a minute, never
// while unset. A new target is a new write and goes out at once. After DHW_MAX_TRIES writes the
// readback has not agreed with, this target is given up on until the readback agrees, the target
// changes, or a person writes it again: a boiler that stores whole degrees reads 50.3 back as
// 50.0 for ever, and without the cap that is 1440 writes a day into what may be its flash.
//
// An accepted DHW_SETPOINT (apply()) reopens its target once the store has it -- matched by value,
// because the snapshot lags the command by a step or more, and the OLD target must not be written
// meanwhile. With no readback there is nothing to reconcile against, so nothing is retried; but a
// command a person just gave is carried out once rather than never.
//
// DO NOT compare with a tolerance instead: a half-degree change would then never be written.
static bool dhw_due(ot_control_t *c, const ot_control_cfg_t *cfg, const ot_control_in_t *in)
{
    if (!cfg->dhw_setpoint_set)
        return false;
    if (cfg->dhw_setpoint_dc != c->dhw_target_dc) {
        c->dhw_target_dc = cfg->dhw_setpoint_dc;
        c->dhw_tries     = 0;
        c->dhw_age_ms    = UINT32_MAX;
    }
    if (c->dhw_reopen && c->dhw_reopen_dc == c->dhw_target_dc) {
        c->dhw_reopen      = false;
        c->dhw_tries       = 0;
        c->dhw_age_ms      = UINT32_MAX;
        c->dhw_unread_once = true;
    }
    if (in->dhw_readback_valid && in->dhw_readback_dc == c->dhw_target_dc) {
        c->dhw_tries       = 0;
        c->dhw_unread_once = false;
        return false;
    }
    if (!in->dhw_readback_valid && !c->dhw_unread_once)
        return false;
    if (c->dhw_tries >= DHW_MAX_TRIES || c->dhw_age_ms < DHW_RETRY_MS)
        return false;
    c->dhw_unread_once = false;
    c->dhw_tries++;
    c->dhw_age_ms = 0;
    return true;
}

void ot_control_step(ot_control_t *c, const ot_control_cfg_t *cfg, const ot_control_in_t *in,
                     uint32_t now_ms, ot_control_out_t *out)
{
    const uint32_t dt = now_ms - c->last_step_ms;   // unsigned: right across the wrap
    c->last_step_ms = now_ms;

    // The interval belongs to the mode that was in effect during it, so the clocks advance
    // BEFORE the edges are observed. DO NOT swap these two lines: the first step in HA mode would
    // then count the last LOCAL second against HA's watchdog.
    const bool hour_done = otc_advance(c, cfg, dt);
    observe(c, cfg);
    otc_boost_expire(c, now_ms);
    c->blind = in->ha_forwarded_stale;

    const bool expired = c->overdue_ms >= (uint32_t)cfg->watchdog_s * 1000u;
    const ot_control_state_t prev = c->state;
    const ot_control_state_t s    = ladder(c, cfg, expired);
    track_failsafe(c, prev, s, expired);

    // What the state wants. season_off and ha_waiting keep CH down and hold ID 1 where it is:
    // harmless, because the CH bit overrides the setpoint (OpenTherm v2.2 §5.2).
    ot_control_reason_t reason = OT_CONTROL_REASON_NONE;
    bool    want   = false;
    int32_t target = c->held_dc;
    switch (s) {
    case OT_CONTROL_BOOST:
        want   = true;
        target = c->boost_setpoint_dc;
        break;
    case OT_CONTROL_LOCAL:
        want   = cfg->local_ch_enable;
        target = cfg->local_ch_setpoint_dc;
        break;
    case OT_CONTROL_FAILSAFE:
        want   = otc_failsafe_ch(c, cfg, in, &reason);
        target = cfg->failsafe_setpoint_dc;
        break;
    case OT_CONTROL_HA:
        want   = c->ha_ch_enable;
        target = c->ha_setpoint_dc;
        break;
    default:
        break;
    }

    // THE INVARIANT: CH never RISES unless the bus has carried the held ID 1 since
    // it last changed. `confirmed` means "the last ID 1 the bus reported is ours", so a foreign
    // value on the wire un-confirms it too. A bit already up is not dropped for a new setpoint:
    // that would cost a burner cycle for every slider move, and the invariant is about rising.
    const int16_t held = otc_bound(cfg, target);
    if (held != c->held_dc) {
        c->held_dc   = held;
        c->confirmed = false;
    }
    if (in->setpoint_confirmed) {
        c->confirmed = in->confirmed_dc == held;
        if (c->confirmed)
            c->ok_age_ms = 0;
    }
    bool ch = want;
    if (want && !c->ch_out && !c->confirmed) {
        ch     = false;
        reason = OT_CONTROL_REASON_AWAIT_SETPOINT;
    }
    if (ch != c->ch_out) {
        c->ch_out      = ch;
        c->ch_age_ms   = 0;
    }

    out->status_high = (uint8_t)((ch ? OT_STATUS_CH_ENABLE : 0u) |
                                 (cfg->dhw_enable ? OT_STATUS_DHW_ENABLE : 0u));
    out->held_setpoint_dc = c->held_dc;
    // THE CADENCE: every step until the bus confirms the held value -- on a change, before
    // CH rises, and after a request the task had to skip because a hand write was pending --
    // then again once OT_CONTROL_RESEND_MS has passed since the last confirmation.
    out->send_setpoint     = !c->confirmed || c->ok_age_ms >= OT_CONTROL_RESEND_MS;
    out->send_dhw_setpoint = dhw_due(c, cfg, in);
    out->state             = s;
    out->reason            = reason;
    out->cause             = s == OT_CONTROL_FAILSAFE ? c->fs_cause : OT_CONTROL_REASON_NONE;
    out->overdue_ms        = c->overdue_ms;
    out->hh_ms             = c->hh_ms;
    out->persist_heat_hours = hour_done || c->hh_dirty;
    c->hh_dirty            = false;
    out->heat_hours        = c->heat_hours;
    out->failsafe_count    = c->failsafe_count;
    out->last_failsafe_duration_s = c->last_failsafe_duration_s;
    c->state = s;
}

// By the snapshot's mode, not by seen_ha: the caller asks with the same snapshot it stepped with,
// and a mode flip not yet stepped is HA's the moment ot_control_apply() would treat it as HA's.
bool ot_control_ch_command(const ot_control_t *c, const ot_control_cfg_t *cfg)
{
    return cfg->mode == OT_CONTROL_MODE_HA ? c->ha_ch_enable : cfg->local_ch_enable;
}
