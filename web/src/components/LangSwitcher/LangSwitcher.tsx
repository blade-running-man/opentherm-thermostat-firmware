// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The language switcher. Built and type-checked in Phase A as a standalone component; Phase B
// mounted it in Nav.tsx, so it is now part of every page's chrome.
//
// It reads `locale.value` in render, so it re-renders on a switch, and writes through
// setLocale, which is the one place persistence and <html lang> are kept in step.

import { Select } from "../ui/Select";
import { LOCALES, locale, setLocale, t, type Locale } from "../../i18n/index";

export function LangSwitcher() {
  return (
    <Select
      ariaLabel={t("nav.language")}
      value={locale.value}
      options={LOCALES.map((l) => ({ value: l.code, label: l.short }))}
      onChange={(v) => setLocale(v as Locale)}
    />
  );
}
