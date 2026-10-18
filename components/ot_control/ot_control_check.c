// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_control.h"

// OWNERSHIP AND BOUNDS OF ONE COMMAND, stateless.
//
// A file of its own because it has two callers with different needs: ot_command_check()
// asks it for the early answer, with no executor at hand, and ot_control_apply() asks it again for
// the final one. Kept apart from apply(), a suite that only needs the answer links one object.
//
// The order of refusals is ONE rule, stated once at ot_command_check() in ot_command.h: what is
// wrong with the request whoever sends it -- its representation, the boiler's own refusals -- is
// asked there, BEFORE this function; then ownership; then the executor's own bounds. This
// function is the last two steps, and its questions come in that order, each pinned by a test:
//   1. is it a command at all;
//   2. whose is it -- before this command's own bounds, because those are the owner's to learn: a
//      caller is not told the bounds of a command it may not send
//      (test_ownership_is_answered_before_the_value);
//   3. is the value inside this command's own bounds.
ot_control_err_t ot_control_check(const ot_control_cfg_t *cfg, ot_origin_t origin,
                                  ot_control_cmd_t cmd, int16_t value)
{
    const bool is_bool = cmd == OT_CONTROL_CMD_CH_ENABLE || cmd == OT_CONTROL_CMD_DHW_ENABLE ||
                         cmd == OT_CONTROL_CMD_SEASON;
    if (!is_bool && cmd != OT_CONTROL_CMD_CH_SETPOINT && cmd != OT_CONTROL_CMD_DHW_SETPOINT)
        return OT_CONTROL_OUT_OF_RANGE;

    // THE SEASON IS THE PERSON'S MASTER KILL, not a control value HA owns. So the web writes it
    // 0 or 1 in EITHER mode: a person must always be able to
    // say "off", even while HA owns the boiler, and "on" is the person's decision too. HA may send
    // 0 in HA mode -- its summer logic is bound 1 of the failsafe -- and never 1. In LOCAL
    // mode HA owns nothing, the season's 0 included: LOCAL means the person drives, and HA's
    // summer logic does not reach in. When two refusals apply (HA, LOCAL, 1), ownership wins.
    // test_the_season_truth_table pins all eight cells.
    //
    // Anything that is not OT_ORIGIN_HA is judged as the web. DO NOT invert that into "anything
    // not WEB is HA": HA's rights include CH in HA mode, and an unknown origin must not get them.
    const bool ha_mode = cfg->mode == OT_CONTROL_MODE_HA;
    if (origin == OT_ORIGIN_HA) {
        if (!ha_mode)
            return OT_CONTROL_OWNED_BY_LOCAL;
    } else if (ha_mode && cmd != OT_CONTROL_CMD_SEASON) {
        return OT_CONTROL_OWNED_BY_HA;
    }

    if (is_bool && value != 0 && value != 1)
        return OT_CONTROL_OUT_OF_RANGE;
    // HA may turn the season off, never on [OWNER]: its summer logic is bound 1 of the failsafe
    // and a bound the bounded party can lift is not a bound.
    if (cmd == OT_CONTROL_CMD_SEASON && origin == OT_ORIGIN_HA && value == 1)
        return OT_CONTROL_SEASON_ON_IS_LOCAL;
    // Refused, not clamped. DO NOT clamp "to be helpful": an HA automation that
    // keeps being refused feeds no watchdog and lands in failsafe, which is the right outcome for
    // a broken automation; a clamp would hide it behind a plausible flow temperature.
    if (cmd == OT_CONTROL_CMD_CH_SETPOINT &&
        (value < cfg->flow_min_dc || value > cfg->flow_max_dc))
        return OT_CONTROL_OUT_OF_RANGE;
    // 0 is the store's "unset"; accepted, it would switch the reconciliation off unseen. The
    // upper bound is ID 48's, asked by ot_command_encode() first. DO NOT copy it in here:
    // a second copy of an entity's limits is the second list of entities CLAUDE.md forbids.
    if (cmd == OT_CONTROL_CMD_DHW_SETPOINT && value <= 0)
        return OT_CONTROL_OUT_OF_RANGE;
    return OT_CONTROL_OK;
}
