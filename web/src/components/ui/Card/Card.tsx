// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import styles from "./Card.module.css";
import type { CardProps } from "./types";

export function Card({
  title,
  subtitle,
  children,
}: CardProps) {
  return (
    <div class="card">
      <h2 class={styles.title}>{title}</h2>
      {subtitle && (
        <p class={styles.subtitle}>{subtitle}</p>
      )}
      {children}
    </div>
  );
}
