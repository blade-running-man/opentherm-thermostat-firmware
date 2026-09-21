// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Common, app-wide action labels.
import type { Message } from "../../format.ts";

export const common = {
  "common.set": "Задати",
  "common.refresh": "Оновити",
  "common.save": "Зберегти",
  "common.cancel": "Скасувати",
  "common.loading": "Завантаження…",
  "common.retry": "Повторити",
} satisfies Partial<Record<string, Message>>;
