// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The control card's reading of GET /api/control and of the executor's entities:
// what each state means in words, which bits went to the boiler, and when the
// card must ask again.
//
// Pure: no DOM and no network, and only TYPES from the api modules except the generated entity
// table, which has no imports -- so node loads this file and tests/model.test.ts reaches all of
// it. The words are this page's; the state, reason and cause NAMES are the device's
// (components/ot_control/ot_control_names.c), and a name this page does not know is shown as the
// device spelled it, never dropped.
//
// DO NOT decide here what the device will accept. Which write the executor takes in which mode is
// ot_command_check()'s answer (the order of refusals in components/ot_command/include/
// ot_command.h); a copy here would drift from it silently. The card sends, and shows the
// device's refusal in the device's words (errors.ts).

import type { ControlDocument } from "../../api/control.ts";
import { ENTITIES } from "../../api/entities.ts";
import type { StateValue } from "../../api/ws.ts";
import { t, tp } from "../../i18n/index.ts";
import type { MsgKey } from "../../i18n/index.ts";

/**
 * A runtime check that the answer IS the document, so that a firmware answering something else
 * produces a notice instead of a render that dereferences `undefined`.
 *
 * It must accept EVERY real document -- tests/model.test.ts feeds it the renderer's output as
 * test/test_api pins it -- so it checks only what the card cannot draw without: the blocks and
 * the scalars it reads. Names are strings, not the known unions: a state added by a later
 * firmware must reach stateView() and be shown, not refused here. `null` inside a block is the
 * renderer's "absent" and is not checked either.
 */
export function isControlDocument(v: unknown): v is ControlDocument {
  const obj = (x: unknown): x is Record<string, unknown> =>
    typeof x === "object" && x !== null && !Array.isArray(x);
  if (!obj(v)) return false;
  const { dhw, boost, failsafe } = v;
  return v.schema === 1
    && typeof v.mode === "string" && typeof v.state === "string"
    && typeof v.reason === "string" && typeof v.cause === "string"
    && typeof v.heating_season === "boolean" && typeof v.status_high === "number"
    && typeof v.held_setpoint_dc === "number" && typeof v.watchdog_overdue_s === "number"
    && obj(dhw) && typeof dhw.enable === "boolean"
    && obj(boost) && typeof boost.active === "boolean"
    && obj(failsafe) && typeof failsafe.count === "number";
}

export type Tone = "ok" | "idle" | "warn" | "alarm";
export interface StateView {
  label: string;
  tone: Tone;
  /** One sentence on what the state does to the boiler. */
  note: string;
}

// A Map, not an object literal: stateView("constructor") must not find Object's own property.
// Only the TONE lives in code; the label/note text is the token.state.* catalog key (moved
// verbatim from here in Phase B -- i18n/catalogs/en/tokens.ts's header comment).
const STATE_TONE = new Map<string, Tone>([
  ["season_off", "idle"],
  ["boost", "ok"],
  ["local", "ok"],
  ["ha_waiting", "warn"],
  ["failsafe", "alarm"],
  ["ha", "ok"],
]);

export function stateView(state: string): StateView {
  const tone = STATE_TONE.get(state);
  if (tone === undefined) {
    return {
      label: t("token.state.unknown.label", { state }),
      tone: "warn",
      note: t("token.state.unknown.note"),
    };
  }
  return {
    label: t(`token.state.${state}.label` as MsgKey),
    tone,
    note: t(`token.state.${state}.note` as MsgKey),
  };
}

// What the CH bit is doing (ot_control.h, ot_control_reason_t). Keys are the device's own
// spelling; the catalog holds the sentence (token.reason.*).
const REASON_KEYS = new Map<string, MsgKey>([
  ["await_setpoint", "token.reason.await_setpoint"],
  ["fs_disarmed", "token.reason.fs_disarmed"],
  ["fs_blind", "token.reason.fs_blind"],
  ["fs_room_cold", "token.reason.fs_room_cold"],
  ["fs_room_warm", "token.reason.fs_room_warm"],
  ["min_cycle", "token.reason.min_cycle"],
]);

// Why the state is failsafe. The two share one vocabulary on the wire (api/control.ts).
const CAUSE_KEYS = new Map<string, MsgKey>([
  ["watchdog", "token.cause.watchdog"],
  ["ha_blind", "token.cause.ha_blind"],
]);

/**
 * What the CH bit is doing, in words; null for "none". Preserves the original fallback chain:
 * a reason not in REASON_KEYS may still be one of CAUSES's names (the two share one vocabulary
 * on the wire), and only then is it shown as the device spelled it (token.reason.other).
 */
export function reasonText(reason: string): string | null {
  if (reason === "none") return null;
  const k = REASON_KEYS.get(reason) ?? CAUSE_KEYS.get(reason);
  return k ? t(k) : t("token.reason.other", { reason });
}

/** Why the state is failsafe, in words; null for "none" -- every state but failsafe. */
export function causeText(cause: string): string | null {
  if (cause === "none") return null;
  const k = CAUSE_KEYS.get(cause);
  return k ? t(k) : t("token.cause.other", { cause });
}

/**
 * Who owns the CH and hot-water commands, from the document's `mode`: the one place the device
 * says it. Words only -- nothing on the card is disabled on this account (the DO NOT above).
 */
export function ownerText(mode: string): string {
  return t(mode === "ha" ? "token.mode.ha" : "token.mode.local");
}

/**
 * The bits of the master status the device last ASKED the boiler for -- the ID 0 high byte,
 * `status_high` -- CH enable in bit 0 and DHW enable in bit 1 (OpenTherm v2.2, ID 0). Beside the
 * command they show the invariant at work: CH commanded, and not yet asked for, while the new
 * flow setpoint goes out (reason await_setpoint).
 */
export function sentBits(statusHigh: number): { ch: boolean; dhw: boolean } {
  return { ch: (statusHigh & 1) !== 0, dhw: (statusHigh & 2) !== 0 };
}

/** On, off, or a dash for what nobody has reported yet. */
export function onOff(v: boolean | null): string {
  return v === null ? "—" : t(v ? "control.onoff.on" : "control.onoff.off");
}

/** The rows whose words are the document's alone; parts.tsx only draws them. */
export interface CardText {
  chAsked: string;
  dhwAsked: string;
  held: string;
  dhwSetpoint: string;
  /** The running boost, or null when none runs. */
  boost: string | null;
}

/**
 * Here rather than in the component, because "which bit does the CH row read" is exactly what a
 * component cannot be asked by a test: the CH row is bit 0 and the hot-water row bit 1, and a
 * card that swapped them would look perfectly reasonable.
 */
export function cardText(doc: ControlDocument): CardText {
  const bits = sentBits(doc.status_high);
  return {
    chAsked: onOff(bits.ch),
    dhwAsked: onOff(bits.dhw),
    held: formatDc(doc.held_setpoint_dc),
    dhwSetpoint: doc.dhw.setpoint_dc === null
      ? t("control.dhw.unset")
      : formatDc(doc.dhw.setpoint_dc),
    boost: doc.boost.active
      ? t("control.boost.running",
          { sp: formatDc(doc.boost.setpoint_dc), time: formatSeconds(doc.boost.remaining_s) })
      : null,
  };
}

/**
 * Whether the executor has started at all.
 *
 * ot_thermostat_control_get() memsets the document while the executor is not initialised, and a
 * thermostat task that failed to start leaves the step output zeroed for good
 * (components/ot_thermostat/ot_thermostat.c) -- a document that reads as an ordinary season_off
 * while every write answers 503. Two fields tell that apart, and BOTH are needed: once
 * ot_control_init() has run, the held setpoint is otc_bound()'s and never below flow_min_dc
 * (100 tenths at the very lowest, OT_CONFIG_FLOW_DC_MIN), and a measured stack means the task
 * has run. A card that showed the zeroed document as a state would say "Heating season off,
 * nothing is asked for" about a device that cannot ask for anything at all.
 */
export function executorStarted(doc: ControlDocument): boolean {
  return doc.held_setpoint_dc !== 0 || doc.stack_hwm !== null;
}

/**
 * The CH command of whoever owns it -- LOCAL's switch or Home Assistant's
 * (ot_control_ch_command(), components/ot_control/ot_control.c) -- from the ch_enable entity.
 * null until the socket has reported it: GET /api/control does not carry the command.
 */
export function chCommand(values: Record<string, StateValue>): boolean | null {
  const v = values.ch_enable;
  return typeof v === "boolean" ? v : null;
}

/**
 * The CH command to SHOW. The socket carries it while it is open; with the socket down the card
 * polls GET /api/entities/ch_enable beside the document (refresh.ts) and shows THAT -- never the
 * socket's last value, which stopped changing when the socket did and would state a command the
 * owner may have changed since, with nothing to say it is old. null is a dash: not known yet.
 */
export function shownChCommand(live: boolean, values: Record<string, StateValue>,
                               polled: boolean | null): boolean | null {
  return live ? chCommand(values) : polled;
}

/** The command inside GET /api/entities/ch_enable's document; null when the answer is not one. */
export function chFromEntity(answer: unknown): boolean | null {
  const obj = (x: unknown): x is Record<string, unknown> =>
    typeof x === "object" && x !== null && !Array.isArray(x);
  if (!obj(answer) || !obj(answer.value)) return null;
  const v = answer.value.value;
  return typeof v === "boolean" ? v : null;
}

/**
 * The executor's own entities: the registry's synthetic rows (data_id null), taken from
 * the generated table -- never listed here, which would be the second entity list CLAUDE.md
 * forbids.
 */
export const EXECUTOR_ENTITY_KEYS: readonly string[] = ENTITIES
  .filter((e) => e.dataId === null)
  .map((e) => e.key);

/**
 * A string that changes whenever one of the executor's entities does, and when it changes the
 * card asks for GET /api/control again. That is how the card stays live through /ws: the socket
 * carries those entities as they change, and the document's reason, cause and countdowns --
 * which no entity carries -- are picked up by the card's slow poll.
 */
export function controlFingerprint(values: Record<string, StateValue>): string {
  return JSON.stringify(EXECUTOR_ENTITY_KEYS.map((k) => values[k] ?? null));
}

export interface FailsafeBanner {
  active: boolean;
  /** causeText() while active; null otherwise. */
  cause: string | null;
  /** Entries since this boot (RAM only). */
  count: number;
  /** The last COMPLETED failsafe; null while one runs, because the number is the previous one's. */
  lastDurationS: number | null;
}

/**
 * The failsafe banner, or null when there is nothing to say. The counters stay on the card after
 * the failsafe ends, because the moment it fires is the moment nobody is looking
 * ("Visibility").
 */
export function failsafeBanner(doc: ControlDocument): FailsafeBanner | null {
  const active = doc.state === "failsafe";
  if (!active && doc.failsafe.count === 0) return null;
  return {
    active,
    cause: active ? causeText(doc.cause) : null,
    count: doc.failsafe.count,
    lastDurationS: active ? null : doc.failsafe.last_duration_s,
  };
}

/** The banner's two lines. In words here, so the count of one reads as one (checklist 3.6). */
export function bannerText(b: FailsafeBanner): { title: string; body: string } {
  const word = tp("failsafe.entries", b.count);
  const count = t("control.failsafe.count", { count: b.count, word });
  const last = b.lastDurationS !== null
    ? t("control.failsafe.last", { duration: formatSeconds(b.lastDurationS) }) : "";
  return {
    title: t(b.active ? "control.failsafe.active" : "control.failsafe.ran"),
    body: `${b.cause ? `${b.cause} ` : ""}${count}${last}.`,
  };
}

/**
 * What `watchdog_overdue_s` means in the state the document reports, in words; null in LOCAL
 * mode, where it is held at zero and there is nothing to age (ot_control.h).
 *
 * IT IS NOT ALWAYS AN AGE. ot_control_apply() zeroes it on each accepted HA CH command
 * (heard()), otc_advance() counts it from the entry into HA mode while none has arrived, and
 * ot_control_init() restores it from RTC memory across a soft reset. Only `ha` is reached by way
 * of an accepted command (it needs ha_heard, which heard() sets), so only there is the count "how
 * long ago". In ha_waiting the card once said "Last accepted CH command … ago" about a command
 * that had never been sent.
 */
export function sinceHaText(doc: ControlDocument): string | null {
  if (doc.mode !== "ha") return null;
  const counted = formatSeconds(doc.watchdog_overdue_s);
  if (doc.state === "ha") return t("control.sinceHa.ha", { counted });
  if (doc.state === "ha_waiting") return t("control.sinceHa.waiting", { counted });
  return t("control.sinceHa.other", { counted });
}

/** Tenths of a degree in words; null is a dash, never 0 °C. */
export function formatDc(dc: number | null): string {
  return dc === null ? "—" : `${(dc / 10).toFixed(1)} °C`;
}

export function formatSeconds(total: number): string {
  const s = Math.max(0, Math.round(total));
  if (s < 60) return `${s} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${s % 60} s`;
  const m = Math.floor(s / 60);
  return `${Math.floor(m / 60)} h ${m % 60} min`;
}

/**
 * A box's text as a number, or null when it is not one -- the card's one local check, as in
 * State.tsx's WriteForm: JSON.stringify(NaN) is "null", and the device would answer a bad body
 * instead of saying what is wrong with the value. The BOUNDS are not checked: 35 °C is sent, and
 * the device's 422 names the band.
 */
export function parseNumber(text: string): number | null {
  const t = text.trim().replace(",", ".");
  return /^[-+]?\d+(\.\d+)?$/.test(t) ? Number(t) : null;
}

/** "40.0 … 70.0 °C": a hint beside a box, never a check; null unless both ends are known. */
export function bandText(lo: number | null | undefined, hi: number | null | undefined): string | null {
  if (lo === null || lo === undefined || hi === null || hi === undefined) return null;
  return `${lo.toFixed(1)} … ${hi.toFixed(1)} °C`;
}
