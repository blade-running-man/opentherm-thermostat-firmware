// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Common, app-wide action labels.
import type { Message } from "../../format.ts";

export const common = {
  "common.set": "Set",
  "common.refresh": "Refresh",
  "common.save": "Save",
  "common.cancel": "Cancel",
  "common.loading": "Loading…",
  "common.retry": "Retry",
} satisfies Record<string, Message>;
