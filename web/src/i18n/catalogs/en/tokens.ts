// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Firmware token labels -- the "firmware i18n" core. The
// firmware emits stable English tokens (components/ot_control/ot_control_names.c) and the web
// maps each to a label/sentence here; the Map/lookup KEYS the pages match on stay the device's
// own spelling (control/model.ts), only the returned prose becomes a t() call.
//
// EN values below are moved VERBATIM from pages/control/model.ts's STATES/REASONS/CAUSES maps
// and ownerText(), and from pages/state/State.tsx's AVAILABILITY map -- do NOT reword them, the
// Control/State task's pinning tests assert this exact English. `error.detail.*` are the
// firmware's own error sentences (ot_command_strerror(), components/ot_command/ot_command.c),
// matched by firmwareText.ts's DETAIL_TEMPLATES; kept here because they are firmware-contract
// prose, like the rest of this file, not page copy.
import type { Message } from "../../format.ts";

export const tokens = {
  // Control states (components/ot_control/ot_control_names.c). Label + one-sentence note.
  "token.state.season_off.label": "Heating season off",
  "token.state.season_off.note": "No heat is asked for, whatever the CH switch says. Hot water is not affected.",
  "token.state.boost.label": "Boost",
  "token.state.boost.note": "A timed boost holds its flow setpoint and asks for heat until it ends.",
  "token.state.local.label": "Local control",
  "token.state.local.note": "The device's own CH switch and flow setpoint drive the boiler.",
  "token.state.ha_waiting.label": "Waiting for Home Assistant",
  "token.state.ha_waiting.note": "Home Assistant owns the boiler and has sent no CH command since it took over. CH stays off until it does, or until the watchdog expires.",
  "token.state.failsafe.label": "Failsafe",
  "token.state.failsafe.note": "Home Assistant has gone silent or blind, and the device is running its own failsafe.",
  "token.state.ha.label": "Home Assistant in control",
  "token.state.ha.note": "Home Assistant's commands drive the boiler.",
  // Unknown-state fallback; {state} is the device's own spelling, kept verbatim.
  "token.state.unknown.label": "Unknown state \"{state}\"",
  "token.state.unknown.note": "This page does not know the state the device reports; the device's own name for it is shown.",

  // Reasons (ot_control.h, ot_control_reason_t).
  "token.reason.await_setpoint": "CH is wanted; the device waits for the new flow setpoint to reach the boiler before it asks for heat.",
  "token.reason.fs_disarmed": "CH is held off: Home Assistant has not asked for heat within failsafe_heat_days.",
  "token.reason.fs_blind": "No fresh room temperature: heating blind at the failsafe setpoint.",
  "token.reason.fs_room_cold": "The room fell below the failsafe target: heating.",
  "token.reason.fs_room_warm": "The room reached the failsafe target: CH is off.",
  "token.reason.min_cycle": "The CH bit is held for the failsafe minimum cycle time.",
  "token.reason.other": "Reason: {reason}",

  // Causes of failsafe.
  "token.cause.watchdog": "No CH command from Home Assistant for longer than watchdog_s.",
  "token.cause.ha_blind": "Home Assistant's own room sensor has gone stale while its commands keep arriving.",
  "token.cause.other": "Cause: {cause}",

  // Mode ownership sentences (control/model.ts ownerText()).
  "token.mode.ha": "Home Assistant owns the CH and hot-water commands (control_mode ha). A write from this page is still sent, and the device's answer is shown.",
  "token.mode.local": "This device's own controls own the CH and hot-water commands (control_mode local): this page, or any REST client. Home Assistant's commands are refused.",

  // Availability (components/ot_api/ot_api.c; State page).
  "token.avail.ok": "available",
  "token.avail.invalid": "invalid data",
  "token.avail.unsupported": "not supported by the boiler",
  "token.avail.unknown": "no answer yet",

  // Firmware error detail sentences (ot_command_strerror(), components/ot_command/ot_command.c;
  // ot_http_control.c). Matched by firmwareText.ts's DETAIL_TEMPLATES; on no match the device's
  // English sentence is shown unchanged (firmwareText.ts's header comment).
  "error.detail.no_entity": "no such entity",
  "error.detail.read_only": "entity is read-only",
  "error.detail.out_of_range": "value out of range",
  "error.detail.unsupported_id": "boiler does not support this data-id",
  "error.detail.owned_ha": "owned by Home Assistant: {1}",
  "error.detail.owned_local": "owned by the thermostat: {1}",
  "error.detail.season_off_boost": "heating_season is off: a boost would not heat",
} satisfies Record<string, Message>;
