// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { VERBATIM_INPUT } from "./verbatim";
import type { TextFieldProps } from "./types";

export function TextField({
  label,
  value,
  onChange,
  type = "text",
  placeholder,
  verbatim = false,
  note,
  autocomplete,
  disabled,
}: TextFieldProps) {
  // Derived from the label, which makes the label text load-bearing: two fields labelled
  // "Password" on one page produce one id twice, and then clicking either label focuses the
  // first input. That is why the settings page spells its labels out in full.
  const id = label.toLowerCase().replace(/\s+/g, "-");
  const noteId = `${id}-note`;
  return (
    <div class="form-group">
      <label htmlFor={id}>{label}</label>
      <input
        id={id}
        type={type}
        value={value}
        placeholder={placeholder}
        autocomplete={autocomplete}
        disabled={disabled}
        aria-describedby={note ? noteId : undefined}
        {...(verbatim ? VERBATIM_INPUT : {})}
        onInput={(e) => onChange((e.target as HTMLInputElement).value)}
      />
      {note && <small id={noteId} class="field-note">{note}</small>}
    </div>
  );
}
