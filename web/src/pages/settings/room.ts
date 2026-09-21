// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The room-MQTT source's four settings (docs/ha-room-source.md), between GET /api/config and the
// card's boxes.
//
// Pure, like executor.ts beside it, and built the same way: tests/room.test.ts reaches every
// rule. The card holds two copies of its boxes -- `seed`, what the device said when the boxes
// were filled, and `form`, what is on screen -- and every rule below is a comparison of the two,
// so that "sent only when changed" (the same reason ExecutorPatch exists, api/config.ts) holds
// here too. NOT against the device's newest document, for the same reason executor.ts gives: an
// untouched box holding an old value would then differ from a value somebody else has written
// since, and the page would put the old one back.
//
// Unlike the executor's eight, these four are not refused by name -- POST /api/config takes them
// like any other field -- and two of them are booleans, not text: a Toggle's `checked` is a
// boolean already, so this form keeps them as booleans rather than forcing them through the
// text-box round trip the executor's number boxes need.
//
// Relative imports carry the .ts extension so that node can load this module when a suite runs it
// directly (pages/settings/tests/harness.ts).

import { ApiError } from "../../api/client.ts";
import {
  ROOM_KEYS,
  type DeviceConfig,
  type RoomKey,
  type RoomPatch,
} from "../../api/config.ts";
import { t } from "../../i18n/index.ts";
import type { MsgKey } from "../../i18n/index.ts";
import { explainError, type Explanation } from "./errors.ts";

/** The two boolean keys, whose box is a Toggle and whose form value is a boolean already. */
type RoomBoolKey = "room_mqtt_enable" | "room_mqtt_ha_forwarded";
const ROOM_BOOL_KEYS: readonly RoomBoolKey[] = ["room_mqtt_enable", "room_mqtt_ha_forwarded"];

function isBoolKey(key: RoomKey): key is RoomBoolKey {
  return (ROOM_BOOL_KEYS as readonly RoomKey[]).includes(key);
}

/**
 * Two booleans (Toggle's own type) and two text boxes: `room_mqtt_role` is a Select whose value
 * is one of two strings ("0"/"1"), and `room_mqtt_stale_s` is a TextField holding a whole number
 * of seconds -- the same "text is what an <input> holds" reason executor.ts's ExecutorForm gives.
 */
export interface RoomForm {
  room_mqtt_enable: boolean;
  room_mqtt_role: string;
  room_mqtt_stale_s: string;
  room_mqtt_ha_forwarded: boolean;
}

export interface RoomCard {
  seed: RoomForm;
  form: RoomForm;
}

export type RoomPatchResult =
  | { ok: true; patch: RoomPatch }
  | { ok: false; key: RoomKey; problem: string };

/**
 * The box labels. Each is unique on the page, because TextField/Select derive their id from it.
 *
 * A function, not an object -- t() reads the reactive locale signal (executor.ts's
 * executorLabel(), the same reason).
 */
export const roomLabel = (key: RoomKey): string => t(`settings.room.label.${key}` as MsgKey);

export function roomFormFrom(cfg: DeviceConfig): RoomForm {
  return {
    room_mqtt_enable: cfg.room_mqtt_enable,
    room_mqtt_role: String(cfg.room_mqtt_role),
    room_mqtt_stale_s: String(cfg.room_mqtt_stale_s),
    room_mqtt_ha_forwarded: cfg.room_mqtt_ha_forwarded,
  };
}

/**
 * `room_mqtt_stale_s`'s text as the whole number of seconds the device stores, or null when there
 * is none. Whether the number is LEGAL -- 10..65535 (docs/ha-room-source.md) -- is
 * ot_config_apply()'s answer, and its 422 says so; DO NOT copy that bound here, for the same
 * reason parseExecutorField() does not copy the executor's (executor.ts).
 */
export function parseRoomStale(text: string): number | null {
  const t = text.trim();
  return /^\d{1,5}$/.test(t) ? Number(t) : null;
}

/** `room_mqtt_role`'s text as the whole number the device stores: 0 or 1, whole and unsigned. */
function parseRoomRole(text: string): number | null {
  const t = text.trim();
  return /^\d{1,5}$/.test(t) ? Number(t) : null;
}

/**
 * THE one rule for "the owner has not changed this box" for the two text boxes -- role and
 * stale_s -- mirroring executor.ts's unchanged(): the same text as the seed, or the same number
 * written differently. Role has only two legal strings, so this only ever matters for stale_s.
 */
function unchangedText(key: "room_mqtt_role" | "room_mqtt_stale_s", text: string, seedText: string): boolean {
  if (text.trim() === seedText.trim()) return true;
  const parse = key === "room_mqtt_role" ? parseRoomRole : parseRoomStale;
  const value = parse(text);
  return value !== null && value === parse(seedText);
}

function unchanged(key: RoomKey, form: RoomForm, seed: RoomForm): boolean {
  if (isBoolKey(key)) return form[key] === seed[key];
  return unchangedText(key, form[key], seed[key]);
}

/**
 * The body of the card's POST: the keys whose box the owner changed, and nothing else. An
 * untouched box is skipped before its value is sent, so what the page loaded can never be sent
 * back. `{}` means there is nothing to send, and the card does not send it.
 */
export function roomPatch(seed: RoomForm, form: RoomForm): RoomPatchResult {
  const patch: RoomPatch = {};
  for (const key of ROOM_KEYS) {
    if (unchanged(key, form, seed)) continue;
    if (isBoolKey(key)) {
      patch[key] = form[key];
      continue;
    }
    const parse = key === "room_mqtt_role" ? parseRoomRole : parseRoomStale;
    const value = parse(form[key]);
    if (value === null)
      return {
        ok: false,
        key,
        problem: t("settings.room.problem", { label: roomLabel(key) }),
      };
    patch[key] = value;
  }
  return { ok: true, patch };
}

/**
 * A newer document under boxes the owner may be half-way through editing. An untouched box
 * (unchanged()) takes the device's value; a touched one keeps what was typed; the new document
 * becomes the seed either way -- executor.ts's rebaseExecutor(), the same rule.
 */
export function rebaseRoom(card: RoomCard, cfg: DeviceConfig): RoomCard {
  const seed = roomFormFrom(cfg);
  const form = {} as RoomForm;
  for (const key of ROOM_KEYS)
    (form[key] as string | boolean) = unchanged(key, card.form, card.seed) ? seed[key] : card.form[key];
  return { seed, form };
}

/**
 * After a save the device took: the boxes that were sent become the baseline, as they were SENT --
 * a box the owner typed into while the request was in flight is still a change. Mirrors
 * executor.ts's settleExecutor().
 */
export function settleRoom(card: RoomCard, sent: RoomForm, keys: readonly RoomKey[]): RoomCard {
  const seed = { ...card.seed };
  for (const key of keys) (seed[key] as string | boolean) = sent[key];
  return { seed, form: card.form };
}

/**
 * The boxes a refusal points at. POST /api/config names what it refused in `field`
 * (ApiError.field): the JSON key when a value was not the right type at all (`bad_field`,
 * ot_wire_config.c, for the two booleans), and "range" -- naming no field -- for the two numbers
 * out of bounds (ot_config_check_range(), the same generic name the executor's range check uses,
 * ot_config_err_name()). "range" marks what was sent: a stored number is already in range, so one
 * of those is the one that is not.
 */
export function roomRefusedKeys(field: string | null, sent: readonly RoomKey[]): RoomKey[] {
  if (field === null) return [];
  const key = ROOM_KEYS.find((k) => k === field);
  if (key !== undefined) return [key];
  if (field === "range") return [...sent];
  return [];
}

/**
 * A save the device did not take: the notice, in the device's words (errors.ts), and the boxes to
 * mark beside it. Only an ApiError names boxes -- silence refused nothing, so it marks none.
 */
export function roomRefusalOutcome(err: unknown, sent: readonly RoomKey[]):
  { failure: Explanation; bad: RoomKey[] } {
  return {
    failure: explainError(err),
    bad: err instanceof ApiError ? roomRefusedKeys(err.field, sent) : [],
  };
}
