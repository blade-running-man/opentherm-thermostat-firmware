// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The log row: its class attribute and its four cells.
//
// Run: node src/pages/log/tests/row.test.ts
//
// The styles map below is deliberately INCOMPLETE -- narrower than Log.module.css, which also
// dims D and V. A CSS module hands back `undefined` for a rule nobody wrote, so a map with holes
// in it is the case that matters: the class attribute must come out clean whatever the stylesheet
// happens to define, and the suite must not have to be edited every time a rule is added or
// dropped. DO NOT fill the map in to match the stylesheet -- that is the case that already worked.

import { parseLine } from "../parse.ts";
import { logRow, type ClassMap } from "../row.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { setLocale } from "../../../i18n/index.ts";

const LEVELS = ["E", "W", "I", "D", "V"] as const;

/** As Vite builds it: hashed names for the rules that exist, nothing at all for the rest. */
const STYLES: ClassMap = { line: "line_h1", E: "E_h2", W: "W_h3" };

// --- the class attribute -------------------------------------------------------------------------

for (const level of LEVELS) {
  const row = logRow(STYLES, parseLine(`${level} (1) tag: text`));
  ok(!row.class.includes("undefined"),
     `a ${level} line never carries the word "undefined" as a class: the stylesheet need not `
     + "define a rule for every level, and a missing one is dropped, not interpolated");
  ok(row.class.split(" ").every((c) => c.length > 0),
     `a ${level} line's class attribute has no empty name in it`);
}

eq(logRow(STYLES, parseLine("E (1) t: x")).class, "line_h1 E_h2",
   "a level the stylesheet colours gets both names, the row's and the level's");
eq(logRow(STYLES, parseLine("I (1) t: x")).class, "line_h1",
   "a level it does not gets the row's name alone");
eq(logRow(STYLES, parseLine("Guru Meditation Error: Core 0 panic'ed")).class, "line_h1",
   "and a line with no level at all is still a row");
eq(logRow({}, parseLine("E (1) t: x")).class, "",
   "no stylesheet at all is an empty attribute, never \"undefined undefined\"");

// --- the level letter ----------------------------------------------------------------------------
//
// Colour is never the only carrier (Log.module.css): E and W differed by colour alone until the
// letter was given a column of its own.

eq(LEVELS.map((l) => logRow(STYLES, parseLine(`${l} (1) t: x`)).level), [...LEVELS],
   "every level prints its own letter, so the level survives a monochrome screen");
eq(logRow(STYLES, parseLine("Guru Meditation Error")).level, "",
   "a line that is not in the logger's format has no letter to print");

// --- the other three cells -----------------------------------------------------------------------

{
  const row = logRow(STYLES, parseLine("I (3723004) ot_thermostat: time zone MSK-3"));
  eq(row.stamp, "1:02:03.004", "the stamp is uptime, formatted");
  eq(row.tag, "ot_thermostat", "the tag as it came");
  eq(row.text, "time zone MSK-3", "and the message");
}
{
  const row = logRow(STYLES, parseLine("Guru Meditation Error: Core 0 panic'ed"));
  eq(row.stamp, "", "an unparsed line shows no stamp");
  eq(row.tag, "", "and no tag -- null must never reach the DOM as the word \"null\"");
  eq(row.text, "Guru Meditation Error: Core 0 panic'ed", "while its whole text is kept");
}

// --- best-effort log-text translation (i18n/firmwareText.ts's LOG_TEMPLATES) --------------------

{
  setLocale("de");
  const row = logRow(STYLES, parseLine("I (1000) ot_bus: boiler is answering"));
  ok(row.text !== "boiler is answering", "a known log line is translated under a non-English locale");
  setLocale("en"); // restore -- the suites after this one assume the default English locale
}
{
  const row = logRow(STYLES, parseLine("Guru Meditation Error: Core 0 panic'ed"));
  eq(row.text, "Guru Meditation Error: Core 0 panic'ed",
     "an unknown/unparsed line passes through unchanged (English -- no template matches it)");
}

report("log/row");
