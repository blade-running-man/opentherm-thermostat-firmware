// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { ComponentChildren } from "preact";

export interface ButtonProps {
  children: ComponentChildren;
  variant?: "primary" | "danger" | "default";
  onClick?: () => void | Promise<void>;
  disabled?: boolean;
  loading?: boolean;
}
