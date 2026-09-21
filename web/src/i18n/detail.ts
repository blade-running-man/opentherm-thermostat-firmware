// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Best-effort translation of a firmware error detail sentence; English passthrough on no match
// (firmwareText.ts's DETAIL_TEMPLATES documents why a stable machine code does not exist to key
// off instead). Every page's errors.ts calls this on the device's `detail` string.
//
// This wrapper -- not firmwareText.ts itself -- imports t(), because t() reads the reactive
// locale signal (i18n/index.ts, @preact/signals) and firmwareText.ts must stay pure/node-safe.
import { matchTemplate, DETAIL_TEMPLATES } from "./firmwareText.ts";
import { t } from "./index.ts";

export function translateDetail(message: string): string {
  const m = matchTemplate(message, DETAIL_TEMPLATES);
  return m ? t(m.key, m.params) : message;
}
