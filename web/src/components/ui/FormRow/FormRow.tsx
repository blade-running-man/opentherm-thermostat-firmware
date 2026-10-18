// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { FormRowProps } from "./types";

export function FormRow({
  children,
  style,
}: FormRowProps) {
  return <div class="form-row" style={style}>{children}</div>;
}
