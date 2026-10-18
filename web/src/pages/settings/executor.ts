// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The controller's settings, between GET /api/config and the card's boxes.
//
// Pure, like model.ts beside it: tests/executor.test.ts reaches every rule. The card holds two
// copies of its boxes -- `seed`, what the device said when the boxes were filled, and `form`, what
// is on screen -- and every rule below is a comparison of the two. That is what "sent only when
// changed" means: the settings page sends an executor setting only when the box's value differs
// from the device's; changed
// by the owner, against what the owner was shown. NOT against the device's newest document: an
// untouched box holding an old value would then differ from a value somebody else has written
// since, and the page would put the old one back.
//
// Relative imports carry the .ts extension so node can load this module (tests/harness.ts).

import { ApiError } from "../../api/client.ts";
import {
  EXECUTOR_KEYS,
  type DeviceConfig,
  type ExecutorKey,
  type ExecutorPatch,
} from "../../api/config.ts";
import { t } from "../../i18n/index.ts";
import type { MsgKey } from "../../i18n/index.ts";
import { explainError, type Explanation } from "./errors.ts";

/** Every box is text, because that is what an <input> holds (model.ts, SettingsForm). */
export type ExecutorForm = Record<ExecutorKey, string>;

export interface ExecutorCard {
  seed: ExecutorForm;
  form: ExecutorForm;
}

export type ExecutorPatchResult =
  | { ok: true; patch: ExecutorPatch }
  | { ok: false; key: ExecutorKey; problem: string };

/** Tenths of a degree on the wire and degrees in the box: the `_dc` suffix. */
function isTenths(key: ExecutorKey): boolean {
  return key.endsWith("_dc");
}

/**
 * The box labels. Each is unique on the page, because TextField derives its id from it.
 *
 * A function, not an object -- Phase B's t() reads the reactive locale signal, so this must be
 * called at render/build time and never cached at module load (i18n/index.ts's t()).
 */
export const executorLabel = (key: ExecutorKey): string =>
  t(`settings.executor.label.${key}` as MsgKey);

export function executorFormFrom(cfg: DeviceConfig): ExecutorForm {
  const form = {} as ExecutorForm;
  for (const key of EXECUTOR_KEYS)
    form[key] = isTenths(key) ? (cfg[key] / 10).toFixed(1) : String(cfg[key]);
  return form;
}

/**
 * A box's text as the whole number the device stores, or null when there is none.
 *
 * The one judgement this page makes, for the reason patchFromForm() judges the port: the value
 * leaves as a JSON number, and "45.25" has no honest number of tenths -- rounding it would send a
 * value nobody typed. Degrees take at most one decimal, a comma allowed (the Russian keypad);
 * the rest are whole numbers. Whether the number is LEGAL -- 60..7200, the half-degree grid, the
 * band -- is ot_config_apply()'s answer, and its 422 says so. DO NOT copy those bounds here: a
 * copy stricter than the device refuses what the device would take, with no way round it.
 */
export function parseExecutorField(key: ExecutorKey, text: string): number | null {
  const t = text.trim();
  if (isTenths(key)) {
    const m = /^(\d{1,4})(?:[.,](\d))?$/.exec(t);
    return m ? Number(m[1]) * 10 + Number(m[2] ?? "0") : null;
  }
  return /^\d{1,5}$/.test(t) ? Number(t) : null;
}

/**
 * THE one rule for "the owner has not changed this box", used by the patch AND the rebase: the
 * same text as the seed, or the same number written differently ("40" over "40.0"). DO NOT give
 * either function a rule of its own. The rebase once compared text alone, so a box retyped "40"
 * over "40.0" -- nothing to send, said the patch -- kept "40" under a newer document, and then
 * sent 400 back over another writer's value: the stale write this exists to prevent.
 */
function unchanged(key: ExecutorKey, text: string, seedText: string): boolean {
  if (text.trim() === seedText.trim()) return true;
  const value = parseExecutorField(key, text);
  return value !== null && value === parseExecutorField(key, seedText);
}

/**
 * The body of the card's POST: the keys whose box the owner changed, and nothing else. An
 * untouched box is skipped before its value is sent, so what the page loaded can never be sent
 * back. `{}` means there is nothing to send, and the card does not send it -- /api/config answers
 * an empty document with 422 no-document.
 */
export function executorPatch(seed: ExecutorForm, form: ExecutorForm): ExecutorPatchResult {
  const patch: ExecutorPatch = {};
  for (const key of EXECUTOR_KEYS) {
    if (unchanged(key, form[key], seed[key])) continue;
    const value = parseExecutorField(key, form[key]);
    if (value === null)
      return {
        ok: false,
        key,
        problem: t("settings.executor.problem", {
          label: executorLabel(key),
          kind: t(isTenths(key) ? "settings.executor.kind.decimal" : "settings.executor.kind.whole"),
        }),
      };
    patch[key] = value;
  }
  return { ok: true, patch };
}

/**
 * A newer document under boxes the owner may be half-way through editing. An untouched box
 * (unchanged()) takes the device's value; a touched one keeps what was typed; the new document
 * becomes the seed either way, so a typed value the device now holds stops counting as a change.
 * Called whenever the page reloads the document -- after the broker card's save, or this card's.
 */
export function rebaseExecutor(card: ExecutorCard, cfg: DeviceConfig): ExecutorCard {
  const seed = executorFormFrom(cfg);
  const form = {} as ExecutorForm;
  for (const key of EXECUTOR_KEYS)
    form[key] = unchanged(key, card.form[key], card.seed[key]) ? seed[key] : card.form[key];
  return { seed, form };
}

/**
 * After a save the device took: the boxes that were sent become the baseline, as they were SENT --
 * a box the owner typed into while the request was in flight is still a change. The reload that
 * follows (rebaseExecutor()) then lays the device's own values over them, including a narrowed
 * band's repair of the stored LOCAL setpoint.
 */
export function settleExecutor(card: ExecutorCard, sent: ExecutorForm,
                               keys: readonly ExecutorKey[]): ExecutorCard {
  const seed = { ...card.seed };
  for (const key of keys) seed[key] = sent[key];
  return { seed, form: card.form };
}

/**
 * The boxes a refusal points at. POST /api/config names what it refused in `field`
 * (ApiError.field): the JSON key when a value was not a number at all, and otherwise the
 * refusal's name, ot_config_err_name() (components/ot_config/ot_config_strerror.c). The sentence
 * shown is the device's own; this only decides which boxes to mark beside it.
 *
 * "range" names no field -- ot_config_apply() returns a code, not a name -- so it marks what was
 * sent: a stored number is already in range, so one of those is the one that is not.
 */
export function refusedKeys(field: string | null, sent: readonly ExecutorKey[]): ExecutorKey[] {
  if (field === null) return [];
  const key = EXECUTOR_KEYS.find((k) => k === field);
  if (key !== undefined) return [key];
  switch (field) {
    case "flow":
      return ["flow_min_dc", "flow_max_dc"];
    case "failsafe":
      return ["failsafe_setpoint_dc", "flow_min_dc", "flow_max_dc"];
    case "mode-needs-broker":
      return ["control_mode"];
    case "range":
      return [...sent];
    // The five keys the executor owns, refused by name (EXECUTOR_OWNED, ot_wire_config.c). It
    // marks no box because none of them HAS a box here: EXECUTOR_KEYS and EXECUTOR_OWNED are
    // disjoint, so this card cannot send one and cannot be told this. Moving a key across that
    // line -- the executor giving one up, or taking one of these eight -- is what would make it
    // reachable, and then the answer is still none: the box it names is on the Control page.
    case "read-only-field":
      return [];
    default:
      return [];
  }
}

/**
 * A save the device did not take: the notice, in the device's words (errors.ts), and the boxes to
 * mark beside it. Only an ApiError names boxes -- silence refused nothing, so it marks none.
 */
export function refusalOutcome(err: unknown, sent: readonly ExecutorKey[]):
  { failure: Explanation; bad: ExecutorKey[] } {
  return {
    failure: explainError(err),
    bad: err instanceof ApiError ? refusedKeys(err.field, sent) : [],
  };
}

/**
 * After the card saves, the page reloads the document into `loaded` ONLY (Settings.tsx). NOT the
 * page's load(): that re-seeds the broker form too, and an unsaved broker edit would vanish under
 * a save of a different card. The Wi-Fi hook hears the document first (`note`, its noteConfig()),
 * before the page's state changes, for the ordering that hook's comments guard. A failed reload
 * changes nothing: the save itself succeeded, and the next load shows the rest.
 */
export async function reloadLoaded(get: () => Promise<DeviceConfig>,
                                   note: (cfg: DeviceConfig) => void,
                                   set: (cfg: DeviceConfig) => void): Promise<void> {
  try {
    const cfg = await get();
    note(cfg);
    set(cfg);
  } catch {
    // Nothing to do: see above.
  }
}
