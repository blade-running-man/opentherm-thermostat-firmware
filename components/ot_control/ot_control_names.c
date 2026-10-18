// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_control.h"

// The wire spellings, and nothing else in this file: they are the option strings of the generated
// control_state entity and the attributes published with it, so a typo here renames a
// state in the owner's Home Assistant recorder. Designated initialisers tie each string to its
// enumerator rather than to a position, so an enumerator added in the middle cannot shift them.
//
// DO NOT return NULL for an unknown value: every caller prints the result straight into JSON.

static const char *const STATE_NAMES[OT_CONTROL_STATE_COUNT] = {
    [OT_CONTROL_SEASON_OFF] = "season_off",
    [OT_CONTROL_BOOST]      = "boost",
    [OT_CONTROL_LOCAL]      = "local",
    [OT_CONTROL_HA_WAITING] = "ha_waiting",
    [OT_CONTROL_FAILSAFE]   = "failsafe",
    [OT_CONTROL_HA]         = "ha",
};

static const char *const REASON_NAMES[OT_CONTROL_REASON_COUNT] = {
    [OT_CONTROL_REASON_NONE]           = "none",
    [OT_CONTROL_REASON_WATCHDOG]       = "watchdog",
    [OT_CONTROL_REASON_HA_BLIND]       = "ha_blind",
    [OT_CONTROL_REASON_FS_DISARMED]    = "fs_disarmed",
    [OT_CONTROL_REASON_FS_BLIND]       = "fs_blind",
    [OT_CONTROL_REASON_FS_ROOM_COLD]   = "fs_room_cold",
    [OT_CONTROL_REASON_FS_ROOM_WARM]   = "fs_room_warm",
    [OT_CONTROL_REASON_MIN_CYCLE]      = "min_cycle",
    [OT_CONTROL_REASON_AWAIT_SETPOINT] = "await_setpoint",
};

// The cast to unsigned folds "negative" into "too large": an enum is an int, and a garbage value
// read out of a corrupted struct can be either.
const char *ot_control_state_name(ot_control_state_t s)
{
    return (unsigned)s < OT_CONTROL_STATE_COUNT ? STATE_NAMES[s] : "unknown";
}

const char *ot_control_reason_name(ot_control_reason_t r)
{
    return (unsigned)r < OT_CONTROL_REASON_COUNT ? REASON_NAMES[r] : "unknown";
}
