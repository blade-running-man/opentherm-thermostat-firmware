// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Control page: the executor's card (Control.tsx, parts.tsx, BoilerHero.tsx, model.ts, errors.ts).
//
// EN values for the model.ts entries below are moved VERBATIM from the pre-Phase-B source (the
// pinning test tests/model.test.ts asserts this exact English); the rest is this page's own copy.
import type { Message } from "../../format.ts";

export const control = {
  // The failsafe banner in control/model.ts hand-pluralizes "entry"/"entries"; routed via tp().
  "failsafe.entries": { one: "entry", other: "entries" },

  // Page chrome (Control.tsx).
  "control.title": "Control",
  "control.loading": "Asking the device what it is doing…",
  "control.live": "Live: the device's changes arrive as they happen.",
  "control.poll": "The live connection is down; the card asks every {n} s, the CH command included.",
  "control.stack": "Thermostat task stack headroom: {n} bytes.",
  "control.accepted": "{what}: accepted",

  // "on"/"off" as reported bits (cardText(), model.ts) -- lowercase, distinct from the button
  // labels below.
  "control.onoff.on": "on",
  "control.onoff.off": "off",

  // The failsafe banner (bannerText(), model.ts).
  "control.failsafe.count": "{count} failsafe {word} since this boot",
  "control.failsafe.active": "Failsafe is active",
  "control.failsafe.ran": "The failsafe has run since this boot",
  "control.failsafe.last": "; the last one lasted {duration}",

  // sinceHaText() -- what watchdog_overdue_s means in each state (model.ts's own DO NOT).
  "control.sinceHa.ha": "Last accepted CH command from Home Assistant: {counted} ago.",
  "control.sinceHa.waiting": "No CH command from Home Assistant accepted since this device "
    + "started or entered Home Assistant mode; the watchdog has counted {counted} (it carries "
    + "over a restart).",
  "control.sinceHa.other": "The watchdog has counted {counted} without an accepted CH command "
    + "from Home Assistant.",

  // Heating season (StatusCard).
  "control.season.label": "Heating season",

  // Central heating (HeatingCard).
  "control.heating.title": "Central heating",
  "control.ch.label": "CH",
  "control.ch.commandRow": "CH command",
  "control.ch.askedRow": "CH asked of the boiler",
  "control.flow.label": "Flow setpoint",
  "control.flow.heldRow": "Flow setpoint held",
  "control.flow.newRow": "New flow setpoint",
  "control.ch.explain": "The command is what the owner of the boiler asked for; \"asked of the "
    + "boiler\" is the CH bit the device last sent. They differ for a moment after a new "
    + "setpoint — CH waits until the setpoint has gone out — and whenever the season or the "
    + "failsafe overrides the command.",

  // Hot water (DhwCard).
  "control.dhw.label": "Hot water",
  "control.dhw.commandRow": "Hot water command",
  "control.dhw.askedRow": "Hot water asked of the boiler",
  "control.dhw.setpointLabel": "Hot water setpoint",
  "control.dhw.newSetpointRow": "New hot water setpoint",
  "control.dhw.unset": "not set: the boiler keeps its own",

  // Boost (BoostCard).
  "control.boost.label": "Boost",
  "control.boost.stopLabel": "Stop boost",
  "control.boost.runningRow": "Running",
  "control.boost.running": "{sp}, {time} left",
  "control.boost.none": "No boost is running.",
  "control.boost.bothNeeded": "both boxes need a number",
  "control.boost.setpointAria": "Boost flow setpoint",
  "control.boost.lengthAria": "Boost length",
  // The minutes box's aria-label: the unit lives in the catalog, not appended in code, so it
  // translates with the rest of the sentence instead of leaking the English word "minutes".
  "control.boost.lengthAriaMinutes": "Boost length, minutes",
  "control.boost.explain": "A boost heats at the flow setpoint for the minutes given, then hands "
    + "back to the CH command. The device decides when it may run and says why when it may not.",

  // Shared buttons and validation (parts.tsx).
  "control.button.on": "On",
  "control.button.off": "Off",
  "control.button.set": "Set",
  "control.button.stop": "Stop",
  "control.button.start": "Start",
  "control.validation.notANumber": "not a number",

  // BoilerHero.
  "control.hero.ariaLabel": "Boiler status",
  "control.hero.heatingOn": "▲ Heating",
  "control.hero.heatingOff": "Heating off",
  "control.hero.dhwOn": "▲ Hot water",
  "control.hero.dhwOff": "Hot water off",
  "control.hero.flow": "Flow",
  "control.hero.return": "Return",
  "control.hero.modulation": "Modulation",
  "control.hero.bar": "Bar",
  // Diagram tags over the (aria-hidden) pixel scene. Distinct from the stat labels above: these
  // are the all-caps flow-diagram callouts. The decorative arrow/diamond glyphs stay literal in
  // the JSX, outside these words.
  "control.hero.tag.flow": "FLOW",
  "control.hero.tag.return": "RETURN",
  "control.hero.tag.rad": "RAD",
  "control.hero.tag.sink": "SINK",
  "control.hero.tag.cold": "COLD",
  "control.hero.tag.boiler": "BOILER",
  "control.hero.tag.standby": "Standby",

  // errors.ts -- headlines are this page's own words; details are the device's, wrapped by
  // translateDetail() (i18n/detail.ts) so a known firmware sentence is shown translated and an
  // unknown one degrades to English, never to a guess.
  "error.control.refused": "{what}: refused by the device",
  "error.control.valueRefused": "{what}: the value was refused",
  "error.control.notKept": "{what}: the device could not keep it",
  "error.control.notCarriedOut": "{what}: nothing on the device would carry it out",
  "error.control.passwordNeeded": "The device wants its web-interface password",
  "error.control.writeRefused": "{what}: the device refused this write",
  "error.control.noRoute": "{what}: this firmware build has no such route",
  "error.control.badRequest": "{what}: the device did not understand the request",
  "error.control.notAccepted": "{what}: the device did not accept it",
  "error.control.offlineHeadline": "{what}: no answer from the device",
  "error.control.offlineDetail": "Nothing came back, so whether it took effect is unknown; the "
    + "card shows it once the device answers again. ({reason})",
  "error.control.notStarted": "The executor has not started",
  "error.control.notStartedDetail": "The device answers, but its thermostat task has not run: "
    + "there is no state to show, and every control here would be refused with 503. Past the "
    + "first seconds after a boot this means the task did not start at all, and the Log page "
    + "says why.",
  "error.control.load.noRoute": "This firmware build does not serve GET /api/control",
  "error.control.load.noRouteDetail": "The executor's document is unavailable. ({message})",
  "error.control.load.denied": "The device refused to show its controls",
  "error.control.load.badDocument": "The device's answer is not a control document this page can read",
  "error.control.load.fault": "The device reported a fault",
  "error.control.load.offlineHeadline": "No answer from the device",
  "error.control.load.offlineDetail": "Nothing reached the device, so it has said nothing about "
    + "its controls; the card keeps the last answer it had. ({reason})",
  "error.control.browserSilent": "The browser gave no reason.",
} satisfies Record<string, Message>;
