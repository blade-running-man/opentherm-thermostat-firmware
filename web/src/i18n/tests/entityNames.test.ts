// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The generated entity-name overlay covers exactly the registry, in every language.
//
// The generator already refuses to emit a mismatched table (render_entity_names_ts.py), so
// this is the web-side echo of that guard: it catches a hand-edit of the generated file, and
// pins that its English column is the same string entities.ts carries -- the two are rendered
// from one source and must not drift.
//
// Run: node src/i18n/tests/entityNames.test.ts

import { ENTITIES } from "../../api/entities.ts";
import { ENTITY_NAMES } from "../entityNames.generated.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

const registryKeys = ENTITIES.map((e) => e.key).sort();
const overlayKeys = Object.keys(ENTITY_NAMES).sort();

eq(overlayKeys, registryKeys, "the overlay covers exactly the registry keys");

for (const e of ENTITIES) {
  const row = ENTITY_NAMES[e.key];
  ok(row !== undefined, `${e.key} is in the overlay`);
  if (!row) continue;
  eq(row.en, e.name, `${e.key} en column equals entities.ts name`);
  for (const loc of ["en", "de", "nl", "uk"] as const) {
    ok(row[loc].trim().length > 0, `${e.key} has a non-empty ${loc} name`);
  }
}

report("i18n/entityNames");
