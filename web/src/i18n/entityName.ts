// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The localized display name of an entity. Reads the `locale` signal, so a component calling
// it in render re-renders on a language switch. Standalone in Phase A -- the pages that show
// entity names now wire it in Phase B: State.tsx and the Boiler page's RawTable.tsx both call it.
//
// The table is generated from the registry + tools/entity_names_i18n.py; English is the
// canonical registry name, which is also what `entities.ts` carries, so the fallback below can
// never actually miss for a real key -- it exists for a key the caller invents.

import { ENTITY_NAMES } from "./entityNames.generated";
import { locale } from "./index";

/** The entity's name in the current UI language. `fallback` is used only for an unknown key
 *  (pass the entity's English `name` from entities.ts, or the key itself). */
export function entityName(key: string, fallback: string): string {
  return ENTITY_NAMES[key]?.[locale.value] ?? fallback;
}
