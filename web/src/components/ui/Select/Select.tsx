// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { SelectProps } from "./types";

export function Select({
  label,
  ariaLabel,
  value,
  options,
  onChange,
  disabled,
}: SelectProps) {
  // No visible label -> no id to bind a <label> to; the select names itself via aria-label instead.
  const id = label ? label.toLowerCase().replace(/\s+/g, "-") : undefined;
  return (
    <div class="form-group">
      {label ? <label htmlFor={id}>{label}</label> : null}
      <select
        id={id}
        aria-label={label ? undefined : ariaLabel}
        value={value}
        disabled={disabled}
        onChange={(e) => {
          const raw = (e.target as HTMLSelectElement).value;
          const opt = options.find((o) => String(o.value) === raw);
          onChange(opt && typeof opt.value === "number" ? Number(raw) : raw);
        }}
      >
        {options.map((opt) => (
          <option key={opt.value} value={opt.value}>
            {opt.label}
          </option>
        ))}
      </select>
    </div>
  );
}
