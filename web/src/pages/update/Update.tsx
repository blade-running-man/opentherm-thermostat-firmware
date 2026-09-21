// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useState } from "preact/hooks";
import { toast } from "../../components/Toast";
import { t } from "../../i18n/index.ts";
import styles from "./Update.module.css";

export function UpdatePage(_props: { path?: string }) {
  const [uploading, setUploading] = useState(false);
  const [progress, setProgress] = useState("");

  async function handleUpload(file: File) {
    setUploading(true);
    setProgress(t("update.uploading"));
    try {
      const form = new FormData();
      form.append("update", file);
      const res = await fetch("/update", { method: "POST", body: form });
      if (res.ok) {
        setProgress(t("update.complete"));
        toast(t("update.toastDone"));
      } else {
        setProgress(t("update.uploadFailedStatus", { statusText: res.statusText }));
        toast(t("update.toastFailed"));
      }
    } catch {
      setProgress(t("update.failed"));
      toast(t("update.toastFailed"));
    }
    setUploading(false);
  }

  return (
    <div class={styles.page}>
      <h1>{t("nav.update")}</h1>

      <div class="card">
        <h2 class={styles.cardHeading}>{t("update.title")}</h2>
        <p class={styles.hint}>{t("update.instructions")}</p>
        <input
          type="file"
          accept=".bin,.bin.gz"
          disabled={uploading}
          aria-label={t("update.fileAria")}
          onChange={(e) => {
            const file = (e.target as HTMLInputElement).files?.[0];
            if (file) void handleUpload(file);
          }}
        />
        {/* role=status + aria-live so a screen reader announces "uploading / complete / failed";
            it is the only live region on this page and the upload is its most destructive action. */}
        {progress && (
          <p class={styles.progress} role="status" aria-live="polite">{progress}</p>
        )}
      </div>
    </div>
  );
}
