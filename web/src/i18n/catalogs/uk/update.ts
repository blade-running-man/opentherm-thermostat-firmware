// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Firmware Update page.
import type { Message } from "../../format.ts";

export const update = {
  "update.title": "OTA-оновлення",
  "update.instructions":
    "Виберіть файл прошивки .bin для завантаження. Після успішного оновлення пристрій " +
    "автоматично перезавантажиться.",
  "update.fileAria": "Файл прошивки .bin для завантаження",
  "update.uploading": "Завантаження прошивки…",
  "update.complete": "Завантаження завершено! Пристрій перезавантажується…",
  "update.uploadFailedStatus": "Помилка завантаження: {statusText}",
  "update.toastDone": "Прошивку завантажено!",
  "update.toastFailed": "Помилка завантаження",
  "update.failed": "Помилка завантаження — з'єднання втрачено (можливо, пристрій перезавантажується)",
} satisfies Partial<Record<string, Message>>;
