// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

export interface SliderProps {
  label: string;
  min: number;
  max: number;
  value: number;
  onChange: (v: number) => void;
  step?: number;
  unit?: string;
}
