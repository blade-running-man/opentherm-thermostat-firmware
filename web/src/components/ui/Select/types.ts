// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

export interface SelectProps {
  /** A visible field label. Omit for a bare, label-less select (e.g. the nav language switcher);
   *  pass `ariaLabel` instead so the control still names itself to assistive tech. */
  label?: string;
  /** Accessible name used when there is no visible `label`. Ignored when `label` is present. */
  ariaLabel?: string;
  value: string | number;
  options: Array<{ value: string | number; label: string }>;
  onChange: (v: number | string) => void;
  /** Same meaning as TextField's: a store that refuses every write leaves no control live. */
  disabled?: boolean;
}
