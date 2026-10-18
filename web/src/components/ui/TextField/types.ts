// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

export interface TextFieldProps {
  label: string;
  value: string | number;
  onChange: (v: string) => void;
  type?: string;
  placeholder?: string;
  /**
   * True for a value the machine reads: a key, an SSID, a hostname, a topic. Applies
   * VERBATIM_INPUT, whose comment says which failure each attribute prevents. Opt-in rather
   * than the default because a device name IS prose and should keep its capitalisation.
   */
  verbatim?: boolean;
  /** A sentence under the field, wired to the input with aria-describedby. */
  note?: string;
  autocomplete?: string;
  disabled?: boolean;
}
