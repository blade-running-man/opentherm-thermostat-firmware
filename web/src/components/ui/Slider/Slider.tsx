// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { SliderProps } from "./types";

export function Slider({
  label,
  min,
  max,
  value,
  onChange,
  step,
  unit,
}: SliderProps) {
  const id = label.toLowerCase().replace(/\s+/g, "-");
  return (
    <div class="form-group">
      <label htmlFor={id}>
        {label}: {value}{unit ?? ""}
      </label>
      <input
        id={id}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onInput={(e) => onChange(+(e.target as HTMLInputElement).value)}
      />
    </div>
  );
}
