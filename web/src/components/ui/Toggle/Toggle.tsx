// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { ToggleProps } from "./types";

export function Toggle({
  label,
  checked,
  onChange,
  disabled,
}: ToggleProps) {
  return (
    <label>
      <input
        type="checkbox"
        checked={checked}
        disabled={disabled}
        onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
      />
      {label}
    </label>
  );
}
