// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The pure formatter: {token} substitution and plural-category selection.
//
// Run: node src/i18n/tests/format.test.ts

import { format, substitute } from "../format.ts";
import { en } from "../catalogs/en.ts";
import { uk } from "../catalogs/uk.ts";
import { eq, report } from "../../pages/settings/tests/harness.ts";

// --- substitute ---------------------------------------------------------------------------
eq(substitute("Write: {name}", { name: "Flow" }), "Write: Flow", "fills a named token");
eq(substitute("no tokens here"), "no tokens here", "a template with no token is untouched");
eq(substitute("{a} and {b}", { a: "x" }), "x and {b}", "an unfilled token is left verbatim");
eq(substitute("{n}"), "{n}", "no params leaves every token verbatim");
eq(substitute("{n} left", { n: 5 }), "5 left", "a number param is stringified");

// --- format, string messages --------------------------------------------------------------
eq(format("Set", "en"), "Set", "a plain string passes through");
eq(format("Write: {name}", "en", { name: "X" }), "Write: X", "a string message substitutes");

// --- format, plural messages (English: one/other) -----------------------------------------
eq(format(en["failsafe.entries"], "en", undefined, 1), "entry", "en count 1 -> one");
eq(format(en["failsafe.entries"], "en", undefined, 0), "entries", "en count 0 -> other");
eq(format(en["failsafe.entries"], "en", undefined, 2), "entries", "en count 2 -> other");
eq(
  format({ one: "{count} item", other: "{count} items" }, "en", undefined, 3),
  "3 items",
  "count is exposed to the plural template as {count}"
);

// --- format, plural messages (Ukrainian: one/few/many/other) ------------------------------
eq(format(uk["failsafe.entries"], "uk", undefined, 1), "запис", "uk count 1 -> one");
eq(format(uk["failsafe.entries"], "uk", undefined, 2), "записи", "uk count 2 -> few");
eq(format(uk["failsafe.entries"], "uk", undefined, 5), "записів", "uk count 5 -> many");
eq(format(uk["failsafe.entries"], "uk", undefined, 21), "запис", "uk count 21 -> one");

report("i18n/format");
