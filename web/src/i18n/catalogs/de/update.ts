// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Firmware Update page.
import type { Message } from "../../format.ts";

export const update = {
  "update.title": "OTA-Update",
  "update.instructions":
    "Wählen Sie eine Firmware-.bin-Datei zum Hochladen aus. Das Gerät startet nach einem " +
    "erfolgreichen Update automatisch neu.",
  "update.fileAria": "Firmware-.bin-Datei zum Hochladen",
  "update.uploading": "Firmware wird hochgeladen…",
  "update.complete": "Upload abgeschlossen! Gerät startet neu…",
  "update.uploadFailedStatus": "Upload fehlgeschlagen: {statusText}",
  "update.toastDone": "Firmware hochgeladen!",
  "update.toastFailed": "Upload fehlgeschlagen",
  "update.failed": "Upload fehlgeschlagen — Verbindung verloren (Gerät startet möglicherweise neu)",
} satisfies Partial<Record<string, Message>>;
