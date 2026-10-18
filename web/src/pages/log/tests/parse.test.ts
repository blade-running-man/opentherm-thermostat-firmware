// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// GET /api/log, read one line at a time.
//
// Run: node src/pages/log/tests/parse.test.ts
//
// The lines are ESP-IDF's, as the device's log hook captures them (components/ot_log): the
// trailing newline stripped, every byte above 0x7F escaped as \u00XX by ot_log_render().

import { formatUptime, parseLine, parseLog, recoverUtf8 } from "../parse.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

eq(parseLine("I (12345) ot_thermostat: time zone MSK-3"),
   { level: "I", ms: 12345, tag: "ot_thermostat", text: "time zone MSK-3" },
   "the level, the milliseconds since boot, the tag and the text");
eq(parseLine("E (100) ws: the snapshot did not fit in the buffer -- socket closed").level, "E",
   "an error");
eq(parseLine("W (5) wifi:mode : sta (a4:c1:38:5f:2b:90)"),
   { level: "W", ms: 5, tag: "wifi", text: "mode : sta (a4:c1:38:5f:2b:90)" },
   "the Wi-Fi driver writes no space after its tag, and the tag ends at the first colon");
eq(["E", "W", "I", "D", "V"].map((l) => parseLine(`${l} (1) t: x`).level), ["E", "W", "I", "D", "V"],
   "all five ESP-IDF levels, debug and verbose included: a build may raise the level");
eq(parseLine("Guru Meditation Error: Core  0 panic'ed"),
   { level: null, ms: null, tag: null, text: "Guru Meditation Error: Core  0 panic'ed" },
   "a line that is not in the logger's format is kept whole, never dropped");
eq(parseLine(""), { level: null, ms: null, tag: null, text: "" }, "an empty line is a line");
eq(parseLine("\u001b[0;32mI (1) t: x\u001b[0m"), { level: "I", ms: 1, tag: "t", text: "x" },
   "colour codes, if a build ever turns them on, are not part of the line");

// --- the escaped bytes --------------------------------------------------------------------------

{
  const name = "Термостат OpenTherm";
  const escaped = String.fromCharCode(...new TextEncoder().encode(name));
  ok(escaped !== name, "the device's escaping turns each UTF-8 byte into its own character");
  eq(recoverUtf8(escaped), name, "and they are decoded back when they are UTF-8");
  eq(parseLine(`I (7) ot_net: device name ${escaped}`).text, `device name ${name}`,
     "inside a line too");
  const cut = String.fromCharCode(...new TextEncoder().encode("Термостат")).slice(0, -1);
  eq(recoverUtf8(cut), cut,
     "a line the 160-byte slot cut through a letter is shown as it came, not guessed at");
}
eq(recoverUtf8("plain ascii"), "plain ascii", "ASCII is untouched");
eq(recoverUtf8("café"), "café", "a lone byte that is not UTF-8 is untouched");
eq(recoverUtf8("already Юникод"), "already Юникод", "text that is already Unicode is untouched");
{
  // U+00D0 then U+019E: truncated to bytes they would be D0 9E, a valid UTF-8 letter. A line
  // holding any character above U+00FF was never a run of escaped bytes, so it is left alone.
  const mixed = String.fromCharCode(0xd0, 0x19e);
  eq(recoverUtf8(mixed), mixed, "a line with a character above U+00FF is not decoded at all");
}

// --- the document --------------------------------------------------------------------------------

eq(parseLog([]), [], "an empty ring is a device that has just booted, not a failure");
eq(parseLog(["I (1) a: b", "x"])?.map((l) => l.text), ["b", "x"], "one entry per line, in order");
eq(parseLog("I (1) a: b"), null, "a string is not the document");
eq(parseLog([1, 2]), null, "nor is an array of numbers");
eq(parseLog({ lines: [] }), null, "nor an object");

eq(formatUptime(0), "0:00:00.000", "since boot, not wall time");
eq(formatUptime(12345), "0:00:12.345", "seconds and milliseconds");
eq(formatUptime(3723004), "1:02:03.004", "hours, minutes, seconds");
eq(formatUptime(90000000), "25:00:00.000",
   "past a day the hours go on: the stamp is time since boot, not a time of day");

report("log/parse");
