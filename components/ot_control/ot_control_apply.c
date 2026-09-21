// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include <string.h>

#include "ot_control_internal.h"

// The effect of every accepted HA CH command: it feeds the watchdog and ends ha_waiting. One
// helper so the two commands that feed cannot drift apart.
static void heard(ot_control_t *c)
{
    c->overdue_ms = 0;
    c->ha_heard   = true;
}

// THE FINAL ANSWER. The httpd and esp-mqtt tasks both run check-then-apply,
// and a mode flip can land between the two; so this re-checks against the snapshot it is handed,
// under the caller's spinlock, and stamps the watchdog in the same critical section.
//
// now_ms is part of the contract and deliberately unused. DO NOT stamp a moment here: the
// watchdog is an accumulator advanced by step(), and a stored moment is how the
// 49-day wrap comes back. The cost is that a command landing between two steps is counted from
// the previous step -- at most one step early on a 900 s deadline.
ot_control_err_t ot_control_apply(ot_control_t *c, const ot_control_cfg_t *cfg,
                                  ot_origin_t origin, ot_control_cmd_t cmd, int16_t value,
                                  uint32_t now_ms, ot_control_persist_t *persist)
{
    (void)now_ms;
    memset(persist, 0, sizeof *persist);
    // OWNERSHIP IS THE EXECUTOR'S, BOUNDS ARE THE SNAPSHOT'S. The snapshot was built outside the
    // lock and may predate a mode flip step() has already seen: a web write snapshotted in LOCAL
    // must not be granted after the executor went HA. So the mode checked is
    // the one the executor last observed; a flip reaches HA's commands one step after it reaches
    // the store (test_ha_is_refused_until_the_step_sees_the_flip).
    //
    // DO NOT observe edges here, as this function once did: a stale snapshot's mode would be taken
    // as an edge, the next step would take it back as an entry, and HA's accepted command would be
    // forgotten. Edges are step()'s alone.
    ot_control_cfg_t seen = *cfg;
    seen.mode = c->seen_ha ? OT_CONTROL_MODE_HA : OT_CONTROL_MODE_LOCAL;
    const ot_control_err_t e = ot_control_check(&seen, origin, cmd, value);
    if (e != OT_CONTROL_OK)
        return e;   // *c untouched

    // On the store's 5 dc grid before it is persisted or used: the store refuses a CH setpoint off
    // it, and the held ID 1 is rounded the same way. check() has bounded it already.
    if (cmd == OT_CONTROL_CMD_CH_SETPOINT)
        value = otc_bound(cfg, value);

    const bool on  = value == 1;
    const bool web = origin != OT_ORIGIN_HA;
    switch (cmd) {
    case OT_CONTROL_CMD_CH_ENABLE:
        if (web) {
            persist->set_local_ch_enable = true;
            persist->local_ch_enable     = on;
            break;
        }
        // LEAVING FAILSAFE IS AN ENTRY into HA ownership: whatever HA said during the
        // failsafe is forgotten, and this CH_ENABLE is the first command of the new ownership.
        // Only a CH_ENABLE does it -- a setpoint or a DHW toggle is not a decision about heat --
        // and only with the cause gone: the watchdog is fed on the next line, and blindness is
        // the last step's input. DO NOT move this below heard(): forget would clear ha_heard.
        if (c->fs_latched && !c->blind)
            otc_forget_ha(c, cfg);
        heard(c);
        c->ha_ch_enable = on;
        // HA asked for heat: the failsafe's arm is renewed. A reset of a count already at
        // 0 is not a change; asking to persist it would write NVS on every keepalive.
        if (on) {
            c->hh_dirty   = c->hh_dirty || c->heat_hours != 0;
            c->heat_hours = 0;
            c->hh_ms      = 0;
        }
        break;
    case OT_CONTROL_CMD_CH_SETPOINT:
        if (web) {
            persist->set_local_ch_setpoint = true;
            persist->local_ch_setpoint_dc  = value;
            break;
        }
        heard(c);
        c->ha_setpoint_dc = value;   // RAM only: quantised and bounded where it is used
        break;
    case OT_CONTROL_CMD_DHW_ENABLE:
        persist->set_dhw_enable = true;
        persist->dhw_enable     = on;
        break;
    case OT_CONTROL_CMD_DHW_SETPOINT:
        persist->set_dhw_setpoint = true;
        persist->dhw_setpoint_dc  = value;
        // A command reopens its target: a person writing it again is asking again.
        c->dhw_reopen    = true;
        c->dhw_reopen_dc = value;
        break;
    case OT_CONTROL_CMD_SEASON:
        persist->set_heating_season = true;
        persist->heating_season     = on;
        break;
    }
    persist->any = persist->set_local_ch_enable || persist->set_local_ch_setpoint ||
                   persist->set_dhw_enable || persist->set_dhw_setpoint ||
                   persist->set_heating_season;
    return OT_CONTROL_OK;
}
