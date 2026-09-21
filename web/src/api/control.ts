// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// GET /api/control: what the executor is doing and why.
//
// A transcription of the one renderer, ot_api_render_control() (components/ot_api/
// ot_api_control.c), whose host suite test/test_api pins the spellings. The control card
// (pages/control) reads it, and checks at run time that an answer is this document before it
// draws one (isControlDocument(), pages/control/model.ts).
//
// A separate module rather than lines in client.ts, for the reason api/otRaw.ts gives: client.ts
// is at the 350-line ceiling. request() comes from client.ts, so a failure is the same ApiError.

import { request } from "./client";

/**
 * The ladder's rows (STATE_NAMES, components/ot_control/ot_control_names.c), which are also the
 * option strings of the control_state entity. "unknown" only for a value outside that table,
 * which a healthy device never prints.
 */
export type ControlState =
  | "season_off" | "boost" | "local" | "ha_waiting" | "failsafe" | "ha" | "unknown";

/** REASON_NAMES in the same file; `reason` and `cause` share the vocabulary. */
export type ControlReason =
  | "none" | "watchdog" | "ha_blind" | "fs_disarmed" | "fs_blind" | "fs_room_cold"
  | "fs_room_warm" | "min_cycle" | "await_setpoint" | "unknown";

/**
 * The document. Temperatures are integer tenths (`_dc`), as in /api/config. An absent value is
 * `null`, never 0: no boost is not a boost whose time is up, an unset DHW setpoint is not 0 °C,
 * an unmeasured stack is not an untouched one.
 */
export interface ControlDocument {
  /** OT_API_CONTROL_SCHEMA: this document's shape, not the registry's schema. */
  schema: 1;
  /** The control_mode entity's spellings -- NOT /api/config's 0/1. */
  mode: "local" | "ha";
  state: ControlState;
  /** What the CH bit is doing. */
  reason: ControlReason;
  /**
   * Why the state is failsafe, recomputed every step (track_failsafe() in ot_control.c):
   * "watchdog" or "ha_blind" while that cause is live, else "none". "none" also occurs IN failsafe
   * when the state is held only by the latch -- the acute cause cleared and it awaits an
   * accepted HA ch_enable. Always "none" in every non-failsafe state. Narrower than `reason`.
   */
  cause: "none" | "watchdog" | "ha_blind";
  heating_season: boolean;
  /** The ID 0 high byte the device last ASKED for -- not what the boiler does. */
  status_high: number;
  /** The held ID 1: always valid, CH on or off. */
  held_setpoint_dc: number;
  dhw: { enable: boolean; setpoint_dc: number | null };
  /** Both numbers are null unless a boost runs: a leftover would describe a boost nobody started. */
  boost:
    | { active: true; setpoint_dc: number; remaining_s: number }
    | { active: false; setpoint_dc: null; remaining_s: null };
  /** Since this boot. */
  failsafe: { count: number; last_duration_s: number };
  watchdog_overdue_s: number;
  /** Bytes of the thermostat task's stack never used; null until the task has measured itself. */
  stack_hwm: number | null;
}

export function getControl(): Promise<ControlDocument> {
  return request("/api/control");
}
