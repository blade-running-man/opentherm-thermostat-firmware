// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Readings of the raw 16-bit word in the boiler's answer.
//
// WHY THERE ARE EIGHT OF THEM AND NONE IS CALLED THE RIGHT ONE. In an OpenTherm frame the
// value is sixteen bits with no type marker whatsoever: the same 0x1440 is 20.25 °C as f8.8,
// 5184 as u16, and "bits 6 and 10 are set" as flags. Only the Data-ID table knows which
// codec is correct, and this page shows EVERYTHING the boiler answered, identifiers absent
// from that table included — they are what it exists for. The codec of an unknown ID cannot
// be guessed, but a plausible reading is recognised by eye instantly: 20.25 in the f8.8
// column is a temperature, 0x0301 in the flags column is two meaningful bits, 65535 in u16
// is "the sensor is not answering". So all the columns are shown at once, side by side.
//
// DO NOT introduce a HAND-WRITTEN table of Data-ID names or codecs here. The single entity
// list is produced by the generator from `tools/opentherm_ids.py` (CLAUDE.md, "One entity
// list, ever"); a hand-written second list in the frontend is precisely what that rule
// forbids, and it will drift from the first the day the generator adds something. Names come
// from the generated `api/entities.ts` — that is how RawTable.tsx labels them. Codecs are
// NOT carried over from there: an identifier unknown to that table has no codec at all, and
// showing eight readings only for the unknown ones would mean reading one and the same page
// in two different ways.

/** Fresher than this — the row is highlighted. Less than the bus poll period, hence "now". */
export const FRESH_MS = 5000;

/** Older than this — the row is dimmed: an answer exists, but not a recently confirmed one. */
export const STALE_MS = 30000;

/**
 * Coercion to sixteen bits WITHOUT a sign.
 *
 * The value comes from JSON and nobody has checked it — the firmware is a debug build and
 * changes in parallel with this page. A bitwise AND turns a fractional, a negative and an
 * out-of-range number alike into the sixteen bits that are the only ones with a meaning;
 * without it toFixed and toString(16) would return NaN and a string of unknown length, and
 * the whole table would slide at once.
 */
function norm(raw: number): number {
  return raw & 0xffff;
}

export function asU16(raw: number): number {
  return norm(raw);
}

/** Two's complement: the top bit is the sign. */
export function asS16(raw: number): number {
  const u = norm(raw);
  return u > 32767 ? u - 65536 : u;
}

/**
 * f8.8 — a SIGNED fixed-point value, the low byte being the fraction.
 *
 * Signed, and that is the one mistake that is easy to make here: the outside temperature in
 * winter arrives as 0xFFB0, an unsigned reading would show 255.69, and the page would lie
 * about overheating while it is freezing outdoors. The OpenTherm 2.2 specification defines
 * f8.8 as exactly a signed fixed point.
 */
export function asF88(raw: number): string {
  return (asS16(raw) / 256).toFixed(2);
}

export function hiByte(raw: number): number {
  return norm(raw) >> 8;
}

export function loByte(raw: number): number {
  return norm(raw) & 0xff;
}

export function hex16(raw: number): string {
  return `0x${norm(raw).toString(16).toUpperCase().padStart(4, "0")}`;
}

/**
 * Sixteen bits as the string `0000 0010 . 1000 0001`.
 *
 * In nibbles and with a dot between the bytes, because this is read by eye and what is
 * looked for in it is ONE bit: on half the flag Data-IDs the high byte is the boiler's
 * state and the low byte its faults, and without a separator the positions have to be
 * counted with a finger. The string width is constant, so in a monospaced font one and the
 * same bit stands in one and the same column across every row of the table, and a bit that
 * has come up is visible without reading the values.
 */
export function flagBits(raw: number): string {
  const u = norm(raw);
  const nibble = (shift: number) => ((u >> shift) & 0xf).toString(2).padStart(4, "0");
  return `${nibble(12)} ${nibble(8)} . ${nibble(4)} ${nibble(0)}`;
}

export type AgeLevel = "fresh" | "normal" | "stale";

export function ageLevel(ageMs: number): AgeLevel {
  if (ageMs < FRESH_MS) return "fresh";
  if (ageMs > STALE_MS) return "stale";
  return "normal";
}

/**
 * Age in words. Three ranges, because different things are of interest: below a second the
 * milliseconds matter (is the poll keeping up), below a minute the tenths, and beyond that
 * only the bare fact "this data has not been refreshed for a while", where precision is
 * of no use.
 */
export function formatAge(ageMs: number): string {
  const ms = Math.max(0, Math.round(ageMs));
  if (ms < 1000) return `${ms} ms`;
  if (ms < 60000) return `${(ms / 1000).toFixed(1)} s`;
  const total = Math.round(ms / 1000);
  return `${Math.floor(total / 60)} min ${total % 60} s`;
}
