// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The control card's reading of GET /api/control and of the executor's entities.
//
// Run: node src/pages/control/tests/model.test.ts
//
// The documents below are the firmware's own output, copied from test/test_api/test_api.cpp,
// which pins ot_api_render_control() byte for byte. A runtime check that refuses one of them
// would blank the card on a healthy device, so every shape the renderer can print is here.

import { setLocale } from "../../../i18n/index.ts";
setLocale("en"); // model.ts now reads t(); pin the English wording

import {
  EXECUTOR_ENTITY_KEYS,
  bandText,
  bannerText,
  cardText,
  causeText,
  chCommand,
  chFromEntity,
  controlFingerprint,
  executorStarted,
  failsafeBanner,
  formatDc,
  formatSeconds,
  isControlDocument,
  ownerText,
  parseNumber,
  reasonText,
  sentBits,
  shownChCommand,
  sinceHaText,
  stateView,
} from "../model.ts";
import type { ControlDocument } from "../../../api/control.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// test_control_renders_byte_for_byte: season_off with a boost still running and the DHW bit
// alone in the status byte.
const PINNED =
  "{\"schema\":1,\"mode\":\"local\",\"state\":\"season_off\",\"reason\":\"none\","
  + "\"cause\":\"none\",\"heating_season\":false,\"status_high\":2,\"held_setpoint_dc\":500,"
  + "\"dhw\":{\"enable\":true,\"setpoint_dc\":505},"
  + "\"boost\":{\"active\":true,\"setpoint_dc\":500,\"remaining_s\":3540},"
  + "\"failsafe\":{\"count\":2,\"last_duration_s\":61},\"watchdog_overdue_s\":7,"
  + "\"stack_hwm\":1184}";
// test_control_absent_values_are_null_not_zero.
const ABSENT = PINNED
  .replace("\"setpoint_dc\":505}", "\"setpoint_dc\":null}")
  .replace("{\"active\":true,\"setpoint_dc\":500,\"remaining_s\":3540}",
           "{\"active\":false,\"setpoint_dc\":null,\"remaining_s\":null}")
  .replace("\"stack_hwm\":1184}", "\"stack_hwm\":null}");
// test_control_names_the_failsafe_and_its_cause.
const FAILSAFE = PINNED.replace(
  "\"mode\":\"local\",\"state\":\"season_off\",\"reason\":\"none\",\"cause\":\"none\"",
  "\"mode\":\"ha\",\"state\":\"failsafe\",\"reason\":\"fs_disarmed\",\"cause\":\"watchdog\"");
// ot_control_state_name() prints "unknown" for a value outside its table.
const UNKNOWN = PINNED.replace("\"state\":\"season_off\"", "\"state\":\"unknown\"");
// The fifth shape, not pinned by test_api but printed by the same renderer: before
// ot_thermostat_start() has initialised the executor, ot_thermostat_control_get() memsets the
// document (components/ot_thermostat/ot_thermostat.c), and a task that never ran leaves the step
// output at zero -- state 0 (season_off), held 0, no stack measured.
const ZEROED =
  "{\"schema\":1,\"mode\":\"local\",\"state\":\"season_off\",\"reason\":\"none\","
  + "\"cause\":\"none\",\"heating_season\":false,\"status_high\":0,\"held_setpoint_dc\":0,"
  + "\"dhw\":{\"enable\":false,\"setpoint_dc\":null},"
  + "\"boost\":{\"active\":false,\"setpoint_dc\":null,\"remaining_s\":null},"
  + "\"failsafe\":{\"count\":0,\"last_duration_s\":0},\"watchdog_overdue_s\":0,"
  + "\"stack_hwm\":null}";
// Home Assistant mode, before and after its first accepted CH command.
const HA_WAITING = PINNED.replace("\"mode\":\"local\",\"state\":\"season_off\"",
                                  "\"mode\":\"ha\",\"state\":\"ha_waiting\"");
const HA = PINNED.replace("\"mode\":\"local\",\"state\":\"season_off\"", "\"mode\":\"ha\",\"state\":\"ha\"");

ok([ABSENT, FAILSAFE, UNKNOWN, HA_WAITING, HA].every((v) => v !== PINNED),
   "the variants really differ from the pinned document (a stale anchor would copy it)");

const doc = (json: string) => JSON.parse(json) as ControlDocument;

// --- the runtime check accepts every real document -------------------------------------------

for (const [name, json] of [["pinned", PINNED], ["absent values", ABSENT], ["failsafe", FAILSAFE],
                            ["unknown state", UNKNOWN], ["before the executor runs", ZEROED]])
  ok(isControlDocument(JSON.parse(json)), `a real document is accepted: ${name}`);

for (const [name, value] of [
  ["null", null], ["a string", "hello"], ["a number", 42], ["an array", []], ["an empty object", {}],
  ["schema 2", { ...doc(PINNED), schema: 2 }],
  ["dhw null", { ...doc(PINNED), dhw: null }],
  ["status_high as a string", { ...doc(PINNED), status_high: "2" }],
  ["held_setpoint_dc as a string", { ...doc(PINNED), held_setpoint_dc: "500" }],
  ["heating_season as a number", { ...doc(PINNED), heating_season: 1 }],
  ["boost.active as a string", { ...doc(PINNED), boost: { active: "yes" } }],
  ["dhw.enable as a string", { ...doc(PINNED), dhw: { enable: "on", setpoint_dc: null } }],
  ["failsafe.count as a string", { ...doc(PINNED), failsafe: { count: "2", last_duration_s: 0 } }],
] as const)
  ok(!isControlDocument(value), `not a control document: ${name}`);
// Everything the card draws from: without any one of them it would dereference undefined.
for (const key of ["mode", "state", "reason", "cause", "heating_season", "status_high",
                   "held_setpoint_dc", "watchdog_overdue_s", "dhw", "boost", "failsafe"])
  ok(!isControlDocument({ ...doc(PINNED), [key]: undefined }), `not a control document: no ${key}`);

// --- the state, in words ------------------------------------------------------------------------

for (const state of ["season_off", "boost", "local", "ha_waiting", "failsafe", "ha"]) {
  const v = stateView(state);
  ok(v.label.length > 0 && v.note.length > 0 && !v.label.includes(state),
     `${state} has a label of its own and a sentence`);
}
eq(stateView("failsafe").tone, "alarm", "the failsafe is the loudest state");
eq(stateView("ha_waiting").tone, "warn", "waiting for Home Assistant is a warning: CH is off");
eq(stateView("season_off").tone, "idle", "the season off is quiet: it is the owner's own off");
eq(["local", "boost", "ha"].map((s) => stateView(s).tone), ["ok", "ok", "ok"],
   "the three states that do what somebody asked are ok, Home Assistant in control included");
ok(stateView("sleeping").label.includes("sleeping") && stateView("sleeping").tone === "warn",
   "a state this page does not know is shown as the device spelled it, not dropped");
ok(stateView("constructor").label.includes("constructor"),
   "and an object's own property name is not mistaken for a known state");

eq(reasonText("none"), null, "no reason, no line");
for (const reason of ["await_setpoint", "fs_disarmed", "fs_blind", "fs_room_cold", "fs_room_warm",
                      "min_cycle"])
  ok((reasonText(reason) ?? "").length > 0 && !(reasonText(reason) ?? "").startsWith("Reason:"),
     `the reason ${reason} has a sentence`);
eq(reasonText("new_reason"), "Reason: new_reason", "an unknown reason is shown as spelled");
eq(causeText("none"), null, "no cause outside the failsafe");
ok((causeText("watchdog") ?? "").includes("watchdog_s"), "the watchdog names its setting");
ok((causeText("ha_blind") ?? "").length > 0, "ha_blind has a sentence");
eq(causeText("odd"), "Cause: odd", "an unknown cause is shown as spelled");

ok(ownerText("ha") !== ownerText("local") && ownerText("ha").includes("Home Assistant owns"),
   "who owns the commands comes from the document's mode, in words");

// --- has the executor started at all -----------------------------------------------------------

eq(executorStarted(doc(ZEROED)), false,
   "held 0 with no stack measured is the document before the executor runs: once it has stepped, "
   + "the held setpoint is at least flow_min_dc, and flow_min_dc is at least 100");
eq(executorStarted(doc(PINNED)), true, "a running executor");
eq(executorStarted(doc(ABSENT)), true, "a running executor whose stack is not measured yet");
eq(executorStarted({ ...doc(ZEROED), stack_hwm: 1184 }), true, "a measured stack is a running task");

// --- what was asked of the boiler, against what was commanded --------------------------------

eq(sentBits(2), { ch: false, dhw: true },
   "the pinned document's status byte is the DHW bit alone: bit 1, not bit 0");
eq(sentBits(1), { ch: true, dhw: false }, "CH is bit 0 of the master status");
eq(sentBits(3), { ch: true, dhw: true }, "both");
eq(sentBits(0), { ch: false, dhw: false }, "neither");

eq(cardText(doc(PINNED)), {
  chAsked: "off",
  dhwAsked: "on",
  held: "50.0 °C",
  dhwSetpoint: "50.5 °C",
  boost: "50.0 °C, 59 min 0 s left",
}, "the pinned document's rows: status byte 2 is hot water asked of the boiler and CH not");
eq(cardText(doc(ABSENT)).dhwSetpoint, "not set: the boiler keeps its own", "an unset DHW setpoint says so");
eq(cardText(doc(ABSENT)).boost, null, "no boost running: no Running row");

eq(chCommand({ ch_enable: true }), true, "the command is the ch_enable entity");
eq(chCommand({ ch_enable: false }), false, "off is off");
eq(chCommand({ ch_enable: null }), null, "an entity with no value is unknown, not off");
eq(chCommand({}), null, "and so is one the socket has not reported yet");
eq(chCommand({ ch_enable: 1 }), null,
   "a number is not how the device spells a switch (the /ws frame prints true/false)");

eq(shownChCommand(true, { ch_enable: true }, false), true, "with /ws open, the socket's value");
eq(shownChCommand(false, { ch_enable: true }, false), false,
   "with /ws down, the value the poll read -- never the socket's last one, which stopped changing");
eq(shownChCommand(false, { ch_enable: true }, null), null, "and a dash until the poll has answered");
eq(chFromEntity({ meta: {}, value: { availability: "ok", value: true, age_ms: 40 } }), true,
   "GET /api/entities/ch_enable carries the command as true/false");
eq(chFromEntity({ value: { availability: "unknown", value: null, age_ms: null } }), null,
   "no value is unknown");
eq(chFromEntity({ value: { availability: "ok", value: 1, age_ms: 40 } }), null,
   "and a number is not how a switch prints, here as in the /ws frame (chCommand() above)");
eq(chFromEntity("not a document"), null, "and so is an answer that is not an entity");

// --- when the card must ask again ------------------------------------------------------------------

for (const key of ["ch_enable", "control_state", "control_mode", "heating_season",
                   "ch_enable_effective", "failsafe_count"])
  ok(EXECUTOR_ENTITY_KEYS.includes(key), `${key} is one of the executor's entities`);
for (const key of ["flow_temperature", "ch_setpoint", "dhw_setpoint"])
  ok(!EXECUTOR_ENTITY_KEYS.includes(key), `${key} is a Data-ID row, not the executor's`);

{
  const before = { control_state: "local", ch_enable: true, flow_temperature: 43.0 };
  eq(controlFingerprint(before), controlFingerprint({ ...before }), "the same values, the same print");
  eq(controlFingerprint({ ...before, flow_temperature: 44.5 }), controlFingerprint(before),
     "a boiler reading changing every second does not make the card ask again");
  ok(controlFingerprint({ ...before, control_state: "boost" }) !== controlFingerprint(before),
     "a state change does");
  ok(controlFingerprint({ ...before, ch_enable_effective: true }) !== controlFingerprint(before),
     "and so does the CH bit going out");
}

// --- the failsafe banner --------------------------------------------------------------------------

eq(failsafeBanner(doc(ABSENT.replace("\"count\":2", "\"count\":0"))), null,
   "never in failsafe since boot and not in it now: no banner");
eq(failsafeBanner(doc(PINNED)), { active: false, cause: null, count: 2, lastDurationS: 61 },
   "not in failsafe now, but twice since boot: the counters stay visible");
{
  const b = failsafeBanner(doc(FAILSAFE));
  ok(b !== null && b.active && b.count === 2 && b.lastDurationS === null
     && (b.cause ?? "").includes("watchdog_s"),
     "in failsafe: active, with its cause, and no 'last one lasted' for a running one");
}
{
  // One failsafe since boot is the common case, and the one browser checklist 3.6 walks.
  const once = failsafeBanner(doc(PINNED.replace("\"count\":2", "\"count\":1")));
  eq(once, { active: false, cause: null, count: 1, lastDurationS: 61 }, "one failsafe since boot shows");
  eq(once === null ? null : bannerText(once), {
    title: "The failsafe has run since this boot",
    body: "1 failsafe entry since this boot; the last one lasted 1 min 1 s.",
  }, "in the words checklist 3.6 quotes");
  const running = failsafeBanner(doc(FAILSAFE.replace("\"count\":2", "\"count\":1")));
  eq(running === null ? null : bannerText(running), {
    title: "Failsafe is active",
    body: "No CH command from Home Assistant for longer than watchdog_s. 1 failsafe entry since this boot.",
  }, "and a running one in the words checklist 3.5 quotes");
}

// --- numbers in words -------------------------------------------------------------------------------

eq(formatDc(505), "50.5 °C", "tenths as degrees");
eq(formatDc(450), "45.0 °C", "one decimal always");
eq(formatDc(null), "—", "null is a dash, never 0 °C");
eq(formatSeconds(0), "0 s", "zero");
eq(formatSeconds(59), "59 s", "under a minute");
eq(formatSeconds(60), "1 min 0 s", "sixty seconds is a minute");
eq(formatSeconds(3540), "59 min 0 s", "the pinned boost: 3540 s");
eq(formatSeconds(3600), "1 h 0 min", "an hour");
eq(formatSeconds(3661), "1 h 1 min", "past an hour the seconds go");
eq(formatSeconds(90000), "25 h 0 min", "a count, not a time of day: 25 h stays 25 h");

// watchdog_overdue_s is zeroed by each accepted HA CH command (ot_control_apply()), counts from the
// entry into HA mode until the first, and is restored from RTC memory after a soft reset
// (ot_control_init()). Only in `ha` is it "since the last accepted command".
eq(sinceHaText(doc(PINNED)), null, "in local mode there is no Home Assistant command to age");
eq(sinceHaText(doc(HA)), "Last accepted CH command from Home Assistant: 7 s ago.",
   "in `ha`: since its last accepted CH command");
{
  const waiting = sinceHaText(doc(HA_WAITING)) ?? "";
  ok(!waiting.includes("Last accepted"), "in ha_waiting no command has been accepted: no 'last' and no 'ago'");
  eq(waiting, "No CH command from Home Assistant accepted since this device started or entered Home "
     + "Assistant mode; the watchdog has counted 7 s (it carries over a restart).",
     "the count said as what it is");
}
eq(sinceHaText(doc(FAILSAFE)),
   "The watchdog has counted 7 s without an accepted CH command from Home Assistant.",
   "any other state in HA mode: the count, with no claim about which command started it");

eq(parseNumber("50"), 50, "a number");
eq(parseNumber("50,5"), 50.5, "with a comma");
eq(parseNumber(" 50.5 "), 50.5, "with spaces");
eq(parseNumber("-5"), -5, "a negative is a number: refusing it is the device's business");
for (const bad of ["", "abc", "0x10", "1e3", "5.", "50 °C"])
  eq(parseNumber(bad), null, `${JSON.stringify(bad)} is not sent: JSON would carry null or a guess`);

eq(bandText(40, 70), "40.0 … 70.0 °C", "a band as a hint");
eq(bandText(undefined, 70), null, "half a band is no hint");

report("control/model");
