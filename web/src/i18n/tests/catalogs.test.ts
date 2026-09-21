// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The catalog parity guard -- the check tsc cannot do. `Record<MsgKey, Message>` already
// forces the key SET to match; this pins the things a type does not: no empty values, the
// same {placeholders} in every language (a dropped interpolation is a silent runtime hole),
// the same string-vs-plural SHAPE per key, and -- for a plural key -- every plural category
// the language's own Intl.PluralRules requires plus `other`.
//
// Run: node src/i18n/tests/catalogs.test.ts

import { catalogs } from "../catalogs/index.ts";
import { en } from "../catalogs/en.ts";
import type { Message } from "../format.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

type Locale = keyof typeof catalogs;
const LOCALES = Object.keys(catalogs) as Locale[];
const KEYS = Object.keys(en) as Array<keyof typeof en>;

function forms(msg: Message): string[] {
  return typeof msg === "string" ? [msg] : Object.values(msg) as string[];
}

function tokens(msg: Message): string[] {
  const set = new Set<string>();
  for (const f of forms(msg)) for (const m of f.matchAll(/\{(\w+)\}/g)) set.add(m[1]);
  return [...set].sort();
}

// --- identical key sets -------------------------------------------------------------------
const enKeys = KEYS.map(String).sort();
for (const loc of LOCALES) {
  eq(Object.keys(catalogs[loc]).sort(), enKeys, `${loc} has exactly the English key set`);
}

// --- no empty values ----------------------------------------------------------------------
for (const loc of LOCALES) {
  for (const key of KEYS) {
    const msg = catalogs[loc][key];
    ok(
      forms(msg).every((f) => f.trim().length > 0),
      `${loc} "${String(key)}" has no empty form`
    );
  }
}

// --- same shape and same placeholders as English ------------------------------------------
for (const key of KEYS) {
  const enMsg = en[key];
  const enIsPlural = typeof enMsg !== "string";
  const enTokens = tokens(enMsg);
  for (const loc of LOCALES) {
    const msg = catalogs[loc][key];
    ok(
      (typeof msg !== "string") === enIsPlural,
      `${loc} "${String(key)}" matches English's string-vs-plural shape`
    );
    eq(tokens(msg), enTokens, `${loc} "${String(key)}" keeps English's placeholders`);
  }
}

// --- plural keys cover every category the locale requires ---------------------------------
for (const key of KEYS) {
  if (typeof en[key] === "string") continue;
  for (const loc of LOCALES) {
    const msg = catalogs[loc][key] as Exclude<Message, string>;
    const required = new Set(
      new Intl.PluralRules(loc).resolvedOptions().pluralCategories
    );
    required.add("other");
    for (const cat of required) {
      ok(
        typeof msg[cat as keyof typeof msg] === "string",
        `${loc} "${String(key)}" supplies the "${cat}" plural form`
      );
    }
  }
}

report("i18n/catalogs");
