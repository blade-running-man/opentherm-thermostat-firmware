// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Common, app-wide action labels.
import type { Message } from "../../format.ts";

export const common = {
  "common.set": "Instellen",
  "common.refresh": "Vernieuwen",
  "common.save": "Opslaan",
  "common.cancel": "Annuleren",
  "common.loading": "Laden…",
  "common.retry": "Opnieuw",
} satisfies Partial<Record<string, Message>>;
