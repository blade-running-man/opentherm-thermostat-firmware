// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The controller's settings: what the card sends, and what it never sends.
//
// Run: node src/pages/settings/tests/executor.test.ts
//
// The rule pinned here: the settings page sends no executor
// setting unless changed: the section sends the controller's settings ONLY WHEN CHANGED -- changed by
// the owner, against what the owner was shown. An untouched box is never sent, whatever the
// device holds now, because a box holding what the page loaded is exactly the stale
// control_mode that decision was written against.

import { ApiError } from "../../../api/client.ts";
import { CONFIG_UNCHANGED } from "../../../api/secrets.ts";
import { EXECUTOR_KEYS, type DeviceConfig } from "../../../api/config.ts";
import { setLocale } from "../../../i18n/index.ts";
import {
  executorFormFrom,
  executorPatch,
  parseExecutorField,
  rebaseExecutor,
  refusalOutcome,
  refusedKeys,
  reloadLoaded,
  settleExecutor,
  type ExecutorCard,
} from "../executor.ts";
import { eq, ok, report } from "./harness.ts";

setLocale("en"); // executor.ts now reads t(); pin the English wording

// What GET /api/config answers (ot_wire_render_config(), components/ot_wire/ot_wire_config.c).
const DEVICE: DeviceConfig = {
  wifi_ssid: "Kitchen 2G",
  wifi_psk: CONFIG_UNCHANGED,
  mqtt_host: "192.168.50.10",
  mqtt_port: 1883,
  mqtt_user: "opentherm",
  mqtt_password: CONFIG_UNCHANGED,
  topic_prefix: "opentherm/a4c1385f2b90",
  ha_discovery: true,
  device_name: "Термостат OpenTherm",
  tz: "MSK-3",
  ntp_server: "pool.ntp.org",
  dhw_enable: true,
  control_mode: 0,
  heating_season: true,
  watchdog_s: 900,
  failsafe_setpoint_dc: 450,
  failsafe_room_target_dc: 180,
  failsafe_heat_days: 3,
  failsafe_min_cycle_s: 600,
  flow_min_dc: 400,
  flow_max_dc: 700,
  room_mqtt_enable: false,
  room_mqtt_role: 1,
  room_mqtt_stale_s: 900,
  room_mqtt_ha_forwarded: false,
  local_ch_enable: true,
  local_ch_setpoint_dc: 450,
  dhw_setpoint_dc: 0,
  ui_password: CONFIG_UNCHANGED,
  ui_password_set: true,
  read_only: false,
  wifi_known_good: true,
};

// The five keys POST /api/config refuses by name with 422 read-only-field: EXECUTOR_OWNED in
// components/ot_wire/ot_wire_config.c, copied verbatim. One of them in a body fails the WHOLE save.
const EXECUTOR_OWNED = ["local_ch_enable", "local_ch_setpoint_dc", "dhw_enable", "dhw_setpoint_dc",
                        "heating_season"];

function fresh(cfg: DeviceConfig = DEVICE): ExecutorCard {
  const seed = executorFormFrom(cfg);
  return { seed, form: { ...seed } };
}

// --- the boxes, as the device's document fills them ------------------------------------------

{
  const { seed } = fresh();
  eq(seed.failsafe_setpoint_dc, "45.0", "a temperature shows in degrees with one decimal: 450 tenths");
  eq(seed.failsafe_room_target_dc, "18.0", "the room target the same way");
  eq(seed.flow_min_dc, "40.0", "and the band");
  eq(seed.watchdog_s, "900", "seconds stay seconds, as the device's refusal sentence names them");
  eq(seed.control_mode, "0", "the mode is the document's number, 0 local or 1 Home Assistant");
  eq(Object.keys(seed).sort(), [...EXECUTOR_KEYS].sort(), "a box for each of the eight, and no more");
}

// --- what a box's text is as the number the device stores ------------------------------------

eq(parseExecutorField("flow_min_dc", "45"), 450, "whole degrees become tenths");
eq(parseExecutorField("flow_min_dc", "45.5"), 455, "one decimal is a tenth");
eq(parseExecutorField("flow_min_dc", "45,5"), 455, "a comma is what the Russian keypad gives");
eq(parseExecutorField("flow_min_dc", " 45.5 "), 455, "a pasted value keeps its spaces out");
for (const bad of ["45.25", "", "  ", "-5", "4e1", "45.", ".5", "abc"])
  eq(parseExecutorField("flow_min_dc", bad), null,
     `degrees ${JSON.stringify(bad)} are not sent: there is no honest number of tenths for it`);
eq(parseExecutorField("watchdog_s", "900"), 900, "a whole number is itself");
for (const bad of ["1.5", "0x10", "", "-60", "9e2"])
  eq(parseExecutorField("watchdog_s", bad), null, `${JSON.stringify(bad)} is not a whole number`);
eq(parseExecutorField("watchdog_s", "5"), 5,
   "a number the device will refuse (below 60) is still SENT: the bounds are ot_config's, and "
   + "its 422 says them");

// --- only what the owner changed ----------------------------------------------------------------

{
  const c = fresh();
  const r = executorPatch(c.seed, c.form);
  eq(r, { ok: true, patch: {} }, "an untouched card has nothing to send");
}

{
  const c = fresh();
  c.form.watchdog_s = "600";
  eq(executorPatch(c.seed, c.form), { ok: true, patch: { watchdog_s: 600 } },
     "one changed box sends that one key and nothing else");
}

{
  const c = fresh();
  c.form.control_mode = "1";
  c.form.failsafe_setpoint_dc = "50.5";
  eq(executorPatch(c.seed, c.form), { ok: true, patch: { control_mode: 1, failsafe_setpoint_dc: 505 } },
     "the mode as its number and a temperature in tenths");
}

{
  const c = fresh();
  c.form.flow_min_dc = "40";
  eq(executorPatch(c.seed, c.form), { ok: true, patch: {} },
     "\"40\" over \"40.0\" is the same number written differently, not a change");
}

{
  const c = fresh();
  c.form.flow_max_dc = "70.25";
  const r = executorPatch(c.seed, c.form);
  ok(!r.ok && r.key === "flow_max_dc" && r.problem.length > 0,
     "a changed box that is not a number stops the save before anything is sent, and names itself");
}

{
  // The two sentences, word for word: browser checklist item 4.6 quotes the first.
  const c = fresh();
  c.form.failsafe_setpoint_dc = "45.25";
  eq(executorPatch(c.seed, c.form), {
    ok: false,
    key: "failsafe_setpoint_dc",
    problem: "Failsafe flow setpoint (°C): a temperature in degrees, with at most one decimal is needed.",
  }, "a temperature box asks for degrees with one decimal");
  const w = fresh();
  w.form.watchdog_s = "1.5";
  eq(executorPatch(w.seed, w.form), {
    ok: false,
    key: "watchdog_s",
    problem: "Watchdog (s): a whole number is needed.",
  }, "and a count of seconds asks for a whole number");
}

{
  // Every box changed at once: the body still carries only the eight, never an owned key and
  // never a broker key.
  const c = fresh();
  c.form = {
    control_mode: "1", watchdog_s: "600", failsafe_setpoint_dc: "50.0",
    failsafe_room_target_dc: "19.0", failsafe_heat_days: "5", failsafe_min_cycle_s: "900",
    flow_min_dc: "35.0", flow_max_dc: "75.0",
  };
  const r = executorPatch(c.seed, c.form);
  ok(r.ok, "a card with every box changed is sendable");
  if (r.ok) {
    const keys = Object.keys(r.patch);
    eq(keys.sort(), [...EXECUTOR_KEYS].sort(), "all eight, because all eight changed");
    eq(EXECUTOR_OWNED.filter((k) => keys.includes(k)), [],
       "none of the five the executor owns: /api/config refuses each by name and the save fails "
       + "whole; the Control page writes them through POST /api/entities/<key>");
  }
}

// --- the stale-write case the rule exists for ----------------------------------------------------

{
  // The card was filled in LOCAL mode. Somebody else -- curl, another tab -- then switched the
  // device to Home Assistant. The owner, on the old card, changes only the watchdog.
  const c = fresh();
  c.form.watchdog_s = "600";
  eq(executorPatch(c.seed, c.form), { ok: true, patch: { watchdog_s: 600 } },
     "the untouched mode box, still showing LOCAL, is NOT sent: sending it would switch the "
     + "device back to LOCAL behind the other writer's back");

  const moved = rebaseExecutor(c, { ...DEVICE, control_mode: 1, flow_max_dc: 650 });
  eq(moved.form.control_mode, "1", "a reload shows the other writer's mode in the untouched box");
  eq(moved.form.flow_max_dc, "65.0", "and every other untouched box follows the device");
  eq(moved.form.watchdog_s, "600", "while the box the owner is editing keeps what was typed");
  eq(executorPatch(moved.seed, moved.form), { ok: true, patch: { watchdog_s: 600 } },
     "and the owner's edit is still the only change");
}

{
  // A box retyped "40" over "40.0" holds the number the owner was shown, written differently. It
  // is untouched -- executorPatch() already says so -- and must follow a newer document like any
  // untouched box: kept as typed, it would send 400 back over another writer's 450.
  const c = fresh();
  c.form.flow_min_dc = "40";
  const moved = rebaseExecutor(c, { ...DEVICE, flow_min_dc: 450 });
  eq(moved.form.flow_min_dc, "45.0",
     "a box holding the shown number written differently follows the device after a reload");
  eq(executorPatch(moved.seed, moved.form), { ok: true, patch: {} },
     "so it never sends the old number back over the other writer's");
}

{
  // The owner typed the value the device now holds: after the reload it is not a change.
  const c = fresh();
  c.form.watchdog_s = "600";
  const moved = rebaseExecutor(c, { ...DEVICE, watchdog_s: 600 });
  eq(executorPatch(moved.seed, moved.form), { ok: true, patch: {} },
     "a typed value the device already holds is nothing to send");
}

{
  // Saved: the box that was sent is the new baseline; a box typed into during the request is not.
  const c = fresh();
  c.form.watchdog_s = "600";
  const sent = { ...c.form };
  const during: ExecutorCard = { seed: c.seed, form: { ...c.form, failsafe_heat_days: "5" } };
  const settled = settleExecutor(during, sent, ["watchdog_s"]);
  eq(executorPatch(settled.seed, settled.form), { ok: true, patch: { failsafe_heat_days: 5 } },
     "after a save the sent box is no longer a change, and one typed during the request still is");
}

{
  // The SAME box typed into again while its own save was in flight: 600 went out, the box now
  // says 700. The baseline is what was sent, never the box as it stands when the answer lands.
  const c = fresh();
  c.form.watchdog_s = "600";
  const sent = { ...c.form };
  const during: ExecutorCard = { seed: c.seed, form: { ...c.form, watchdog_s: "700" } };
  const settled = settleExecutor(during, sent, ["watchdog_s"]);
  eq(executorPatch(settled.seed, settled.form), { ok: true, patch: { watchdog_s: 700 } },
     "a box retyped during its own save is still a change after it");
}

// --- what the card does with the device's answer ------------------------------------------------

{
  const refusal = new ApiError(422, "the failsafe setpoint (failsafe_setpoint_dc) must lie between "
    + "flow_min_dc and flow_max_dc", "failsafe");
  const out = refusalOutcome(refusal, ["failsafe_setpoint_dc"]);
  eq(out.bad, ["failsafe_setpoint_dc", "flow_min_dc", "flow_max_dc"],
     "a refusal marks the boxes its field names (browser checklist 4.4)");
  eq([out.failure.status, out.failure.detail], [422, refusal.message],
     "and its notice carries the device's own sentence");
  eq(refusalOutcome(new ApiError(422, "out of range", "range"), ["watchdog_s"]).bad, ["watchdog_s"],
     "\"range\" marks what was sent");
  eq(refusalOutcome(new TypeError("Failed to fetch"), ["watchdog_s"]).bad, [],
     "silence marks no box: nothing was refused");
}

{
  // After the card saves, the page reloads the document for `loaded` alone (Settings.tsx): the
  // Wi-Fi hook hears it first, then the page's state -- the order noteConfig() exists for.
  const calls: string[] = [];
  await reloadLoaded(async () => ({ ...DEVICE, watchdog_s: 600 }),
                     (cfg) => calls.push(`note ${cfg.watchdog_s}`),
                     (cfg) => calls.push(`set ${cfg.watchdog_s}`));
  eq(calls, ["note 600", "set 600"], "the reload reaches the hook and then `loaded`, with the new document");
  const none: string[] = [];
  await reloadLoaded(async () => { throw new TypeError("Failed to fetch"); },
                     () => none.push("note"), () => none.push("set"));
  eq(none, [], "a reload that fails changes nothing: the save itself succeeded");
}

// --- which boxes a refusal points at -----------------------------------------------------------

eq(refusedKeys("watchdog_s", ["watchdog_s"]), ["watchdog_s"],
   "a JSON key the wire layer refused (not a number) is that box");
eq(refusedKeys("flow", ["flow_min_dc"]), ["flow_min_dc", "flow_max_dc"],
   "\"flow\" is the band: flow_min_dc must be below flow_max_dc");
eq(refusedKeys("failsafe", ["flow_max_dc"]), ["failsafe_setpoint_dc", "flow_min_dc", "flow_max_dc"],
   "\"failsafe\" is the setpoint against the band, either side of it");
eq(refusedKeys("mode-needs-broker", ["control_mode"]), ["control_mode"],
   "Home Assistant mode needs a broker: the mode box");
eq(refusedKeys("range", ["watchdog_s", "failsafe_heat_days"]), ["watchdog_s", "failsafe_heat_days"],
   "\"range\" names no field, and a stored number is already in range, so it is one of those sent");
eq(refusedKeys("read-only", ["watchdog_s"]), [],
   "a store written by a newer firmware is the page's problem, not a box's");
eq(refusedKeys("read-only-field", ["watchdog_s"]), [],
   "\"read-only-field\" names one of the five the executor owns (EXECUTOR_OWNED), which have no "
   + "box on this card -- unreachable while the two lists stay disjoint, and answered anyway");
eq(refusedKeys(null, ["watchdog_s"]), [], "no field named, no box marked");

report("settings/executor");
