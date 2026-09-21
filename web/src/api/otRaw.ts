// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// GET /api/ot/raw — everything the bus has already received from the boiler, with not one
// interpretation applied.
//
// This is a diagnostic endpoint, not entities: it reports the state of the wire — how many
// poll cycles have gone by, whether the slave answers at all, and which sixteen-bit word
// arrived last for each Data-ID. That is exactly why it is NOT produced by the entity
// generator and is not a second entity list: it holds no names, no units and no codecs —
// only the ID numbers the boiler named itself.
//
// Nor is it privileged (CLAUDE.md, "The web interface has no privileged endpoint"): the
// Boiler page is an ordinary client, and curl gets exactly the same document.
//
// A separate module rather than lines in client.ts: that one is already up against the
// 350-line ceiling. request() is imported from there, so a failure arrives here as the
// same ApiError.

import { request, runOperation } from "./client";

/**
 * The response type in the boiler's frame — the three MsgType bits from the header, handed
 * over as a string.
 *
 * The union enumerates what the endpoint promises and serves as documentation; the page
 * prints the value as it is and will not break if the firmware adds a ninth string. These
 * are protocol tokens, not text for a human, and they must not be translated: they are
 * looked up in the OpenTherm specification and in the device log by those same letters.
 *
 * `unknown-dataid` and `data-invalid` are ANSWERS, not the absence of one: the boiler said
 * "I have no such ID" or "the data is invalid". A row with such a type is meaningful, its
 * `raw` is usually zero, and confusing it with silence is not allowed.
 */
export type OtRawType =
  | "read-ack"
  | "write-ack"
  | "data-invalid"
  | "unknown-dataid"
  | "read-data"
  | "write-data"
  | "invalid-data"
  | "reserved";

/** The last answer for one Data-ID. */
export interface OtRawId {
  /** The Data-ID number, 0..255. No name — names live in the generated entity list. */
  id: number;
  type: OtRawType;
  /** Sixteen bits of the data field, 0..65535. pages/boiler/decode.ts computes the readings. */
  raw: number;
  /** How many milliseconds ago this answer arrived. */
  age_ms: number;
  /** How many answers have arrived for this ID since start-up. */
  count: number;
}

/**
 * A snapshot of the bus state.
 *
 * `ids` holds only those Data-IDs that got at least one answer, in ascending `id` order.
 * An empty array with `answering: false` is the normal and most frequent state while
 * debugging, not a fault in the page.
 */
export interface OtRawSnapshot {
  uptime_ms: number;
  cycles: number;
  ok: number;
  failed: number;
  overdue: number;
  answering: boolean;
  /**
   * The share of samples over ~2 ms on which the OpenTherm input was ACTIVE, as a percentage.
   *
   * It answers the single design question that is otherwise settled with a meter:
   * the idle line is a low level, so a connected but silent boiler is expected to
   * show 0 here. A steady 100 means the input polarity is inverted.
   */
  in_duty: number;
  /** Scan identifiers done and total; a zero total means no scan is running. */
  scan_done: number;
  scan_total: number;
  ids: OtRawId[];
}

export function getOtRaw(): Promise<OtRawSnapshot> {
  return request("/api/ot/raw");
}

/**
 * Asks the device to toggle the OpenTherm line slowly for 20 seconds.
 *
 * Needed for exactly one thing: checking with a multimeter that the adapter's output
 * stage is alive. A frame takes 34 ms once a second and a needle cannot see it; two
 * seconds in each state it can see.
 *
 * There are no frames during the test, and a boiler starved of frames (its master silent)
 * reads a shorted thermostat and demands heat: master silence longer than five
 * seconds is treated as a shorted thermostat. The boiler will
 * most likely fire up. That is expected, and warning about it is the button's job.
 *
 * An operation, not an endpoint of its own: `POST /api/ops/linetest`. The parameters are
 * passed explicitly even though the firmware knows the same defaults — the numbers sit next
 * to the button text that names them, and the two cannot drift apart silently.
 */
export function startLineTest(): Promise<void> {
  return runOperation("linetest", { duration_ms: 20000, half_period_ms: 2000 });
}

/**
 * Starts a sweep of all Data-IDs 0..127: ask each one once.
 *
 * About four minutes. The mandatory ID 0 still goes out every other turn, so 128
 * identifiers cost 256 conversations. Read-only: the sweep produces no writes.
 *
 * An operation, not an endpoint of its own: `POST /api/ops/scan`. The range is passed
 * explicitly, because "all of 0..127" is the page's statement about what it is asking for,
 * not a default the firmware is free to narrow one day.
 */
export function startScan(): Promise<void> {
  return runOperation("scan", { from: 0, to: 127 });
}
