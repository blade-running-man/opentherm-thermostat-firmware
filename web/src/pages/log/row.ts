// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// One log row, apart from the DOM: its class attribute and the four cells it shows.
//
// Pure, so tests/row.test.ts reaches all of it -- Log.tsx cannot be tested, because it imports a
// CSS module and node cannot load one (tests/harness.ts). The class attribute is the part worth
// keeping out of the JSX: a CSS module is a plain object holding a name per rule the stylesheet
// DEFINES, so styles.I for a rule nobody wrote is `undefined`, and interpolating that into a
// template string puts the literal word "undefined" in the DOM.
//
// Relative imports carry the .ts extension so node can load this module (tests/harness.ts).

import { t } from "../../i18n/index.ts";
import { matchTemplate, LOG_TEMPLATES } from "../../i18n/firmwareText.ts";
import { formatUptime, type LogLine } from "./parse.ts";

/** A CSS module as imported: a class name per rule the stylesheet defines, nothing for the rest. */
export type ClassMap = Readonly<Record<string, string | undefined>>;

/** The row as the page renders it. Every field is a string, so nothing can reach the DOM absent. */
export interface LogRow {
  /** The <div>'s class attribute. */
  class: string;
  /** The level letter, "" for a line that is not in the logger's format (parse.ts). */
  level: string;
  /** Uptime as h:mm:ss.mmm, "" when the line carried no stamp. */
  stamp: string;
  tag: string;
  text: string;
}

/**
 * The row: `.line` plus the level's own rule when the stylesheet has one, and the four cells.
 *
 * The names are FILTERED rather than interpolated, which is the fix and not a precaution: a rule
 * the stylesheet does not define is `undefined` here, and `class="line undefined"` is what three
 * of the five levels rendered until this function existed. Filtering makes the attribute right by
 * construction, so Log.module.css is free to colour the levels it has something to say about and
 * to leave the rest alone -- the alternative, a rule per level written only to keep the lookup
 * fed, is a stylesheet that breaks silently the day a level is added.
 *
 * The level letter is a cell of its own for the reason Log.module.css gives: colour is never the
 * only carrier of a level, and E and W differ by colour alone without it.
 */
export function logRow(styles: ClassMap, line: LogLine): LogRow {
  const level = line.level;
  return {
    class: [styles.line, level === null ? undefined : styles[level]]
      .filter((name): name is string => typeof name === "string" && name !== "")
      .join(" "),
    level: level ?? "",
    stamp: line.ms === null ? "" : formatUptime(line.ms),
    tag: line.tag ?? "",
    text: translateLogText(line),
  };
}

/**
 * Best-effort translation of one line's text (i18n/firmwareText.ts's LOG_TEMPLATES); a line that
 * matches no known firmware format is returned unchanged (English passthrough -- ESP-IDF's own
 * internal lines and any firmware string not yet in the registry). The registry is filtered to
 * entries with no tag or the SAME tag as this line before matching, so a fixed phrase owned by
 * one component (e.g. ot_bus's "boiler is answering") cannot cross-match a same-looking line
 * logged by a different one under a different tag.
 */
function translateLogText(line: LogLine): string {
  const candidates = LOG_TEMPLATES.filter((entry) => !entry.tag || entry.tag === line.tag);
  const m = matchTemplate(line.text, candidates);
  return m ? t(m.key, m.params) : line.text;
}
