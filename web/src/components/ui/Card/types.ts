// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { ComponentChildren } from "preact";

export interface CardProps {
  title: string;
  subtitle?: string;
  children: ComponentChildren;
}
