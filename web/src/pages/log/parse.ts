// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// GET /api/log, one line at a time (components/ot_log: the last OT_LOG_LINES = 80 lines, each
// cut to 159 bytes, oldest first, as a JSON array of strings).
//
// Pure, so tests/parse.test.ts reaches all of it. A line is ESP-IDF's
// "<level> (<ms since boot>) <tag>: <text>" -- milliseconds since boot, because both boards log
// with CONFIG_LOG_TIMESTAMP_SOURCE_RTOS -- and a line that does
// not look like one is kept whole. DO NOT drop what does not parse: the log is read when something
// is wrong, and a panic's first line is exactly what a strict parser would throw away.

export type LogLevel = "E" | "W" | "I" | "D" | "V";

export interface LogLine {
  level: LogLevel | null;
  ms: number | null;
  tag: string | null;
  text: string;
}

// The tag ends at the first colon, and the space after it is optional: the Wi-Fi driver writes
// "wifi:mode : sta".
const LINE = /^([EWIDV]) \((\d+)\) ([^:]*): ?(.*)$/s;
// Colour codes, should a build turn CONFIG_LOG_COLORS on: framing, not content.
const ANSI = /\u001b\[[0-9;]*m/g;

export function parseLine(raw: string): LogLine {
  const line = recoverUtf8(raw).replace(ANSI, "");
  const m = LINE.exec(line);
  if (!m) return { level: null, ms: null, tag: null, text: line };
  return { level: m[1] as LogLevel, ms: Number(m[2]), tag: m[3], text: m[4] };
}

/**
 * The device escapes every byte above 0x7F as \u00XX (ot_log_render()), so after JSON.parse a
 * UTF-8 letter is two or three characters in U+0080..U+00FF. When a line holds only such
 * characters and they decode as UTF-8, they were UTF-8: decode them. Anything else is left
 * exactly as it came -- a letter cut in half by the 159-byte slot included -- rather than guessed at.
 */
export function recoverUtf8(s: string): string {
  if (!/[\u0080-\u00ff]/.test(s) || /[^\u0000-\u00ff]/.test(s)) return s;
  try {
    return new TextDecoder("utf-8", { fatal: true }).decode(Uint8Array.from(s, (c) => c.charCodeAt(0)));
  } catch {
    return s;
  }
}

/** The whole document, or null when it is not an array of strings. */
export function parseLog(doc: unknown): LogLine[] | null {
  if (!Array.isArray(doc) || !doc.every((l) => typeof l === "string")) return null;
  return doc.map(parseLine);
}

/** Milliseconds since boot as h:mm:ss.mmm -- the stamps are uptime, not a time of day. */
export function formatUptime(ms: number): string {
  const t = Math.max(0, Math.floor(ms));
  const pad = (n: number, w: number) => String(n).padStart(w, "0");
  const h = Math.floor(t / 3_600_000);
  const m = Math.floor(t / 60_000) % 60;
  const s = Math.floor(t / 1000) % 60;
  return `${h}:${pad(m, 2)}:${pad(s, 2)}.${pad(t % 1000, 3)}`;
}
