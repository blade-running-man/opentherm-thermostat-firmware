// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Firmware Update page.
import type { Message } from "../../format.ts";

export const update = {
  "update.title": "OTA-update",
  "update.instructions":
    "Selecteer een firmware-.bin-bestand om te uploaden. Het apparaat start na een geslaagde " +
    "update automatisch opnieuw op.",
  "update.fileAria": "Firmware-.bin-bestand om te uploaden",
  "update.uploading": "Firmware uploaden…",
  "update.complete": "Upload voltooid! Apparaat start opnieuw op…",
  "update.uploadFailedStatus": "Upload mislukt: {statusText}",
  "update.toastDone": "Firmware geüpload!",
  "update.toastFailed": "Upload mislukt",
  "update.failed": "Upload mislukt — verbinding verbroken (apparaat start mogelijk opnieuw op)",
} satisfies Partial<Record<string, Message>>;
