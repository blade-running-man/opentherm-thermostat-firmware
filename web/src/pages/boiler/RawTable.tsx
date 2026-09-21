// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { ENTITIES } from "../../api/entities";
import type { OtRawId } from "../../api/otRaw";
import {
  ageLevel,
  asF88,
  asS16,
  asU16,
  flagBits,
  formatAge,
  hex16,
  hiByte,
  loByte,
} from "./decode";
import { entityName } from "../../i18n/entityName.ts";
import { t } from "../../i18n/index.ts";
import styles from "./Boiler.module.css";

/**
 * A row per Data-ID, a column per reading — why all of them at once is written in decode.ts.
 *
 * The name is captioned next to the number, but it is taken FROM THE GENERATED registry: a
 * hand-written table of names here would be the second entity list CLAUDE.md forbids. An
 * unknown number has no name, and that is a dash, not a guess.
 */

// One Data-ID can carry several entities -- ID 5 carries seven. We show every name
// comma-separated: picking one would mean lying confidently.
//
// A synthetic entity (dataId null) is filtered out BEFORE grouping: it is not on
// the wire, so it has no place in a table of what the wire said -- and grouped as is, every
// one of them would land under the key "null".
//
// Grouped by (key, name) pairs rather than the resolved name: the DE/NL/UK overlay
// (entityName()) reads the `locale` signal, so it must be looked up at render time, in
// nameForId(), not once at module load.
const ENTITIES_BY_ID = ENTITIES.reduce<Record<number, { key: string; name: string }[]>>(
  (acc, e) => {
    if (e.dataId === null) return acc;
    (acc[e.dataId] ??= []).push({ key: e.key, name: e.name });
    return acc;
  },
  {}
);

export function nameForId(dataId: number): string | null {
  const entities = ENTITIES_BY_ID[dataId];
  return entities ? entities.map((e) => entityName(e.key, e.name)).join(", ") : null;
}

/** An answer arrived, but it says "no such ID" or "the data is invalid" — worth highlighting. */
function isAnomaly(type: string): boolean {
  return type !== "read-ack" && type !== "write-ack";
}

function Row({ row }: { row: OtRawId }) {
  const level = ageLevel(row.age_ms);
  return (
    <tr class={styles[level]}>
      <td class={styles.num}>{row.id}</td>
      <td class={styles.name}>{nameForId(row.id) ?? "—"}</td>
      <td class={isAnomaly(row.type) ? styles.anomaly : undefined}>
        <code class={styles.token}>{row.type}</code>
      </td>
      <td class={styles.num}>
        <code class={styles.token}>{hex16(row.raw)}</code>
        <span class={styles.sub}>{asU16(row.raw)}</span>
      </td>
      <td class={styles.num}>{asF88(row.raw)}</td>
      <td class={styles.num}>{asU16(row.raw)}</td>
      <td class={styles.num}>{asS16(row.raw)}</td>
      <td class={styles.num}>
        {hiByte(row.raw)} / {loByte(row.raw)}
      </td>
      {/* Monospaced and of constant width: one and the same bit stands in one column across
          every row, so a flag that has come up is visible without reading the values. */}
      <td>
        <code class={styles.bits}>{flagBits(row.raw)}</code>
      </td>
      <td class={styles.num}>
        {formatAge(row.age_ms)}
        <span class={styles.sub}>{t("boiler.raw.answers", { count: row.count })}</span>
      </td>
    </tr>
  );
}

export function RawTable({ ids }: { ids: OtRawId[] }) {
  // The endpoint is being written in parallel with this page, and its answer is checked by
  // nothing. If `ids` arrives as something other than an array, .map brings the whole render
  // down -- a white screen instead of the page whose very job is to show that something is
  // wrong with the firmware.
  const rows = Array.isArray(ids) ? ids : [];

  if (rows.length === 0)
    return <p class={styles.hint}>{t("boiler.raw.empty")}</p>;

  return (
    // A wrapper with its own horizontal scroll: ten columns do not fit on a phone, and the
    // page as a whole must not be dragged sideways — the counter header would go with it.
    <div class={styles.scroller}>
      <table class={styles.table}>
        <thead>
          <tr>
            <th>{t("boiler.raw.col.id")}</th>
            <th>{t("boiler.raw.col.name")}</th>
            <th>{t("boiler.raw.col.type")}</th>
            <th>{t("boiler.raw.col.raw")}</th>
            <th>{t("boiler.raw.col.f88")}</th>
            <th>{t("boiler.raw.col.u16")}</th>
            <th>{t("boiler.raw.col.s16")}</th>
            <th>{t("boiler.raw.col.hiLo")}</th>
            <th>{t("boiler.raw.col.flags")}</th>
            <th>{t("boiler.raw.col.age")}</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row) => (
            <Row key={row.id} row={row} />
          ))}
        </tbody>
      </table>
    </div>
  );
}
