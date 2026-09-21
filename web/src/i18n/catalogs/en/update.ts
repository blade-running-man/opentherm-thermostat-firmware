// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Firmware Update page.
import type { Message } from "../../format.ts";

export const update = {
  "update.title": "OTA Update",
  "update.instructions":
    "Select a firmware .bin file to upload. The device will reboot automatically after a " +
    "successful update.",
  "update.fileAria": "Firmware .bin file to upload",
  "update.uploading": "Uploading firmware…",
  "update.complete": "Upload complete! Device is rebooting…",
  "update.uploadFailedStatus": "Upload failed: {statusText}",
  "update.toastDone": "Firmware uploaded!",
  "update.toastFailed": "Upload failed",
  "update.failed": "Upload failed — connection lost (device may be rebooting)",
} satisfies Record<string, Message>;
