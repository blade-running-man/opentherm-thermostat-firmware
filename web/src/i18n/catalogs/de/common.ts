// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Common, app-wide action labels.
import type { Message } from "../../format.ts";

export const common = {
  "common.set": "Setzen",
  "common.refresh": "Aktualisieren",
  "common.save": "Speichern",
  "common.cancel": "Abbrechen",
  "common.loading": "Wird geladen…",
  "common.retry": "Wiederholen",
} satisfies Partial<Record<string, Message>>;
