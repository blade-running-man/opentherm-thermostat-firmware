// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The room-MQTT source's four settings: what the card sends, and what it never sends.
//
// Run: node src/pages/settings/tests/room.test.ts
//
// Mirrors executor.test.ts's rule: sent ONLY WHEN CHANGED -- changed by the owner, against what
// the owner was shown, never against the device's newest document (the stale-write case at the
// bottom is the same one executor.test.ts pins).

import { ApiError } from "../../../api/client.ts";
import { CONFIG_UNCHANGED } from "../../../api/secrets.ts";
import { ROOM_KEYS, type DeviceConfig } from "../../../api/config.ts";
import { setLocale } from "../../../i18n/index.ts";
import {
  parseRoomStale,
  rebaseRoom,
  roomFormFrom,
  roomPatch,
  roomRefusalOutcome,
  roomRefusedKeys,
  settleRoom,
  type RoomCard,
} from "../room.ts";
import { eq, ok, report } from "./harness.ts";

setLocale("en"); // room.ts now reads t(); pin the English wording

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

function fresh(cfg: DeviceConfig = DEVICE): RoomCard {
  const seed = roomFormFrom(cfg);
  return { seed, form: { ...seed } };
}

// --- the boxes, as the device's document fills them ------------------------------------------

{
  const { seed } = fresh();
  eq(seed.room_mqtt_enable, false, "the toggle shows the device's own boolean");
  eq(seed.room_mqtt_role, "1", "the role shows as the device's number, as text for the Select");
  eq(seed.room_mqtt_stale_s, "900", "seconds stay seconds");
  eq(seed.room_mqtt_ha_forwarded, false, "the second toggle shows the device's own boolean too");
  eq(Object.keys(seed).sort(), [...ROOM_KEYS].sort(), "a box for each of the four, and no more");
}

// --- what a box's text is as the number the device stores ------------------------------------

eq(parseRoomStale("900"), 900, "a whole number is itself");
eq(parseRoomStale(" 60 "), 60, "a pasted value keeps its spaces out");
for (const bad of ["", "  ", "-5", "4e1", "45.5", "abc", "0x10"])
  eq(parseRoomStale(bad), null, `${JSON.stringify(bad)} is not a whole number`);
eq(parseRoomStale("5"), 5,
   "a number the device will refuse (below 10) is still SENT: the bounds are ot_config's, and "
   + "its 422 says them");

// --- only what the owner changed ----------------------------------------------------------------

{
  const c = fresh();
  eq(roomPatch(c.seed, c.form), { ok: true, patch: {} }, "an untouched card has nothing to send");
}

{
  const c = fresh();
  c.form.room_mqtt_enable = true;
  eq(roomPatch(c.seed, c.form), { ok: true, patch: { room_mqtt_enable: true } },
     "flipping the toggle sends that one key and nothing else");
}

{
  const c = fresh();
  c.form.room_mqtt_role = "0";
  eq(roomPatch(c.seed, c.form), { ok: true, patch: { room_mqtt_role: 0 } },
     "the role enum maps Room/Ambient's 1/0 straight through, as a number");
}

{
  const c = fresh();
  c.form.room_mqtt_stale_s = "60";
  eq(roomPatch(c.seed, c.form), { ok: true, patch: { room_mqtt_stale_s: 60 } },
     "the stale window sends the number it holds");
}

{
  const c = fresh();
  c.form.room_mqtt_enable = true;
  c.form.room_mqtt_role = "0";
  c.form.room_mqtt_stale_s = "60";
  c.form.room_mqtt_ha_forwarded = true;
  const r = roomPatch(c.seed, c.form);
  ok(r.ok, "a card with every box changed is sendable");
  if (r.ok)
    eq(Object.keys(r.patch).sort(), [...ROOM_KEYS].sort(), "all four, because all four changed");
}

{
  const c = fresh();
  c.form.room_mqtt_stale_s = "0900";
  ok(roomPatch(c.seed, c.form).ok, "a leading zero still parses to the same number");
  eq(roomPatch(c.seed, c.form), { ok: true, patch: {} },
     "\"0900\" over \"900\" is the same number written differently, not a change");
}

{
  const c = fresh();
  c.form.room_mqtt_stale_s = "abc";
  const r = roomPatch(c.seed, c.form);
  ok(!r.ok && r.key === "room_mqtt_stale_s" && r.problem.length > 0,
     "a changed box that is not a number stops the save before anything is sent, and names itself");
  eq(r, { ok: false, key: "room_mqtt_stale_s", problem: "Stale after (s): a whole number is needed." },
     "the sentence, word for word");
}

// --- the stale-write case the rule exists for ----------------------------------------------------

{
  // The card was filled with the source off. Somebody else -- curl, another tab -- enables it.
  // The owner, on the old card, changes only the stale window.
  const c = fresh();
  c.form.room_mqtt_stale_s = "60";
  const moved = rebaseRoom(c, { ...DEVICE, room_mqtt_enable: true, room_mqtt_role: 0 });
  eq(moved.form.room_mqtt_enable, true, "a reload shows the other writer's toggle in the untouched box");
  eq(moved.form.room_mqtt_role, "0", "and every other untouched box follows the device");
  eq(moved.form.room_mqtt_stale_s, "60", "while the box the owner is editing keeps what was typed");
  eq(roomPatch(moved.seed, moved.form), { ok: true, patch: { room_mqtt_stale_s: 60 } },
     "and the owner's edit is still the only change");
}

{
  // A box retyped "0900" over "900" holds the number the owner was shown, written differently. It
  // is untouched, and must follow a newer document like any untouched box.
  const c = fresh();
  c.form.room_mqtt_stale_s = "0900";
  const moved = rebaseRoom(c, { ...DEVICE, room_mqtt_stale_s: 1200 });
  eq(moved.form.room_mqtt_stale_s, "1200",
     "a box holding the shown number written differently follows the device after a reload");
  eq(roomPatch(moved.seed, moved.form), { ok: true, patch: {} },
     "so it never sends the old number back over the other writer's");
}

{
  // Saved: the box that was sent is the new baseline; a box typed into during the request is not.
  const c = fresh();
  c.form.room_mqtt_stale_s = "60";
  const sent = { ...c.form };
  const during: RoomCard = { seed: c.seed, form: { ...c.form, room_mqtt_ha_forwarded: true } };
  const settled = settleRoom(during, sent, ["room_mqtt_stale_s"]);
  eq(roomPatch(settled.seed, settled.form), { ok: true, patch: { room_mqtt_ha_forwarded: true } },
     "after a save the sent box is no longer a change, and one typed during the request still is");
}

// --- what the card does with the device's answer ------------------------------------------------

{
  const refusal = new ApiError(422, "room_mqtt_stale_s must lie between 10 and 65535", "range");
  const out = roomRefusalOutcome(refusal, ["room_mqtt_stale_s"]);
  eq(out.bad, ["room_mqtt_stale_s"], "\"range\" marks what was sent");
  eq([out.failure.status, out.failure.detail], [422, refusal.message],
     "and its notice carries the device's own sentence");
  eq(roomRefusalOutcome(new TypeError("Failed to fetch"), ["room_mqtt_stale_s"]).bad, [],
     "silence marks no box: nothing was refused");
}

eq(roomRefusedKeys("room_mqtt_enable", ["room_mqtt_enable"]), ["room_mqtt_enable"],
   "a JSON key the wire layer refused (not a boolean) is that box");
eq(roomRefusedKeys("range", ["room_mqtt_role", "room_mqtt_stale_s"]),
   ["room_mqtt_role", "room_mqtt_stale_s"],
   "\"range\" names no field, and a stored number is already in range, so it is one of those sent");
eq(roomRefusedKeys("read-only", ["room_mqtt_stale_s"]), [],
   "a store written by a newer firmware is the page's problem, not a box's");
eq(roomRefusedKeys(null, ["room_mqtt_stale_s"]), [], "no field named, no box marked");

report("settings/room");
