// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { connection } from "../../api/device";
import { t } from "../../i18n/index";
import styles from "./ConnectionBadge.module.css";

// Colour is never the only carrier: the label says the same thing in words. A red dot on
// its own is unreadable to anyone who cannot separate it from the green one, and this
// badge is the only thing on screen that distinguishes live readings from stale ones.
const LABEL_KEYS = {
  open: "conn.connected",
  connecting: "conn.connecting",
  closed: "conn.disconnected",
} as const;

const TITLE_KEYS = {
  open: "conn.connected.title",
  connecting: "conn.connecting.title",
  closed: "conn.disconnected.title",
} as const;

export function ConnectionBadge() {
  const status = connection.value;
  return (
    <span
      class={`${styles.badge} ${styles[status]}`}
      title={t(TITLE_KEYS[status])}
      role="status"
      aria-live="polite"
    >
      <span class={styles.dot} aria-hidden="true" />
      {t(LABEL_KEYS[status])}
    </span>
  );
}
