// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useState } from "preact/hooks";
import type { Availability } from "../../api/entities";
import {
  getEntities,
  getState,
  writeEntity,
  type ApiEntityMeta,
  type ApiEntityState,
} from "../../api/registry";
import { toast } from "../../components/Toast";
import { Notice } from "../settings/sections";
import type { Explanation } from "../settings/errors";
import { ageLevel, formatAge } from "../boiler/decode";
import { entityName } from "../../i18n/entityName.ts";
import { t } from "../../i18n/index.ts";
import type { MsgKey } from "../../i18n/index.ts";
import { explainStateFailure, explainWriteFailure } from "./errors";
import styles from "./State.module.css";

/**
 * State — the entity registry with value, availability and age, and writing by hand.
 *
 * THE PAGE IS DELIBERATELY MINIMAL. The full table with filters, search and export is future
 * work; here there is exactly enough to check a write to the boiler by hand and see what came
 * of it. Filters must not be added here "since there is a table anyway": their place and
 * their look are settled later together with the rest of the interface.
 *
 * REST IS READ, NOT THE SOCKET, and that is not an oversight. The `/ws` frame carries a flat
 * "key → scalar" map where null means "availability is not ok" and nothing more (api/ws.ts):
 * neither which availability it is nor how old the value is follows from it. Telling
 * `unsupported` from `invalid` — the very thing the state model draws a line between — cannot be done over
 * the socket, and on this page that is the main thing. The socket serves the connection badge
 * in the navigation; the table asks `/api/state`.
 *
 * There is not one privileged endpoint here: the same three routes are
 * available to curl on the same terms.
 */

/** The value poll period. The same as on the boiler page, and for the same reason. */
const POLL_MS = 2000;

/**
 * The metadata re-read period.
 *
 * The registry within one firmware build is constant, but the bounds in it are not:
 * `ot_state` substitutes the boiler's values as soon as it answers IDs 48 and 49, and that
 * can happen after the page has already been opened. Once every half minute is rare enough
 * not to spend the network on a document of sixty entities, and frequent enough that an
 * input field is not left with the table's bounds until the page is reloaded.
 */
const META_POLL_MS = 30000;

export function StatePage(_props: { path?: string }) {
  const [meta, setMeta] = useState<ApiEntityMeta[] | null>(null);
  const [values, setValues] = useState<Record<string, ApiEntityState>>({});
  const [failure, setFailure] = useState<Explanation | null>(null);
  // Separate from meta: showing an empty table before the first answer is not allowed —
  // empty here would mean "the registry is empty", not "the page has not asked yet".
  const [asked, setAsked] = useState(false);
  const [wrote, setWrote] = useState<Explanation | null>(null);

  useEffect(() => {
    let alive = true;
    let inFlight = false;

    async function poll() {
      if (inFlight) return;
      inFlight = true;
      try {
        const doc = await getEntities();
        if (alive) setMeta(doc.entities);
      } catch (err) {
        if (alive) setFailure(explainStateFailure(err));
      } finally {
        inFlight = false;
        if (alive) setAsked(true);
      }
    }

    void poll();
    const timer = setInterval(() => void poll(), META_POLL_MS);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    let alive = true;
    // Requests do not overlap each other: with a device that has dropped off, fetch hangs
    // until the browser's timeout, and setInterval would have queued up a dozen requests.
    let inFlight = false;

    async function poll() {
      if (inFlight) return;
      inFlight = true;
      try {
        const doc = await getState();
        if (!alive) return;
        setValues(doc.state);
        setFailure(null);
      } catch (err) {
        // The last values are NOT wiped: readings three seconds old are more use than an
        // empty table, and that the link is gone is said in words right next to them.
        if (alive) setFailure(explainStateFailure(err));
      } finally {
        inFlight = false;
      }
    }

    void poll();
    const timer = setInterval(() => void poll(), POLL_MS);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);

  // The outcome of the last write. The entity name goes into the headline, because the
  // message sits above the table and the table has sixty rows: "the value is out of bounds"
  // without a name does not say which field was refused.
  function noteWrite(key: string, what: Explanation | null) {
    if (what === null) {
      setWrote(null);
      return;
    }
    const found = meta?.find((e) => e.key === key);
    const name = found ? entityName(found.key, found.name) : key;
    setWrote({ ...what, headline: `${name}: ${what.headline}` });
  }

  return (
    <div class={styles.page}>
      <h1>{t("state.title")}</h1>

      {failure && <Notice what={failure} />}
      {wrote && <Notice what={wrote} />}

      {!asked && meta === null && !failure && (
        <div class="card">{t("state.loading")}</div>
      )}

      {meta !== null && (
        <div class="card">
          <p class={styles.hint}>{t("state.hint", { seconds: POLL_MS / 1000 })}</p>
          <div class={styles.scroller}>
            <table class={styles.table}>
              <thead>
                <tr>
                  <th>{t("state.col.entity")}</th>
                  <th>{t("state.col.value")}</th>
                  <th>{t("state.col.availability")}</th>
                  <th>{t("state.col.age")}</th>
                  <th>{t("state.col.write")}</th>
                </tr>
              </thead>
              <tbody>
                {meta.map((e) => (
                  <Row key={e.key} meta={e} value={values[e.key]} onWrite={noteWrite} />
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
    </div>
  );
}

/**
 * Availability in words, with all four values distinguishable.
 *
 * `unsupported` and `invalid` are DIFFERENT diagnoses, and collapsing them into a single "no
 * data" would throw away the unsupported/invalid distinction: the first means "the boiler twice said
 * it has no such Data-ID", the second "it supports it, but the data is invalid". The actions
 * behind them differ too, right down to whether to ever write here at all.
 *
 * The labels themselves are `token.avail.*` (i18n/catalogs/en/tokens.ts): they are the same
 * firmware-contract words the Control page's mode text draws on, so they live in the shared
 * token namespace, not a page-local one.
 */
const AVAILABILITY_CLASS: Record<Availability, string> = {
  ok: styles.ok,
  invalid: styles.invalid,
  unsupported: styles.unsupported,
  unknown: styles.unknown,
};

function Row({
  meta,
  value,
  onWrite,
}: {
  meta: ApiEntityMeta;
  value: ApiEntityState | undefined;
  onWrite: (key: string, what: Explanation | null) => void;
}) {
  const availability = value?.availability ?? "unknown";
  const age = value?.age_ms ?? null;

  return (
    <tr>
      <td class={styles.name}>
        {entityName(meta.key, meta.name)}
        {/* The key and the Data-ID under the name: those are what curl, MQTT and the
            OpenTherm specification call the entity by, and without them the table does not
            line up with any of the three. A synthetic row (data_id null) has no Data-ID, and
            "ID " with nothing after it would read as a value that failed to load. */}
        <span class={styles.sub}>
          {meta.data_id === null ? meta.key : `${meta.key} · ID ${meta.data_id}`}
        </span>
      </td>
      <td class={styles.num}>
        <Value value={value} unit={meta.unit} />
      </td>
      <td class={AVAILABILITY_CLASS[availability]}>{t(`token.avail.${availability}` as MsgKey)}</td>
      {/* Only the stale is dimmed. The threshold is the same as on the boiler page
          (decode.ts), because the question is the same: has the boiler confirmed this
          value recently. Freshness is not highlighted — in a sixty-row table almost
          everything would end up highlighted, and highlighting would stop meaning anything. */}
      <td class={`${styles.num} ${age !== null && ageLevel(age) === "stale" ? styles.stale : ""}`}>
        {age === null ? "—" : formatAge(age)}
      </td>
      <td>
        {meta.writable ? (
          <WriteForm meta={meta} onWrite={onWrite} />
        ) : (
          <span class={styles.sub}>{t("state.readOnly")}</span>
        )}
      </td>
    </tr>
  );
}

/**
 * A value or a dash.
 *
 * A dash and not a zero: `value` arrives as null whenever availability is not "ok", and a
 * zero in that spot would mean "it is 0 °C outside" where the boiler said nothing at all
 * (ot_api.c:78-81).
 */
function Value({ value, unit }: { value: ApiEntityState | undefined; unit?: string }) {
  if (!value || value.value === null) return <span class={styles.none}>—</span>;
  if (typeof value.value === "boolean")
    return <span>{value.value ? t("state.value.yes") : t("state.value.no")}</span>;
  return (
    <span>
      {value.value}
      {unit ? ` ${unit}` : ""}
    </span>
  );
}

/**
 * The input field and button for a writable entity.
 *
 * THE BOUNDS ARE NOT CHECKED HERE, and that is a rule, not an omission. The single place
 * where a write's legality is decided is `ot_command` on the device; a check duplicated on
 * the surface will one day drift from the check in the depths, and drift silently
 * (ot_command.h). `min`/`max` end up in the field's attributes as a hint to the browser, but
 * they do not stop the submission: a 422 from the device is the correct answer, and seeing
 * it is more use than not sending the request.
 */
function WriteForm({
  meta,
  onWrite,
}: {
  meta: ApiEntityMeta;
  onWrite: (key: string, what: Explanation | null) => void;
}) {
  const [text, setText] = useState("");
  const [busy, setBusy] = useState(false);
  const [notANumber, setNotANumber] = useState(false);

  function submit(e: Event) {
    e.preventDefault();
    // A comma is what the Russian layout gives on the numeric keypad.
    const value = Number(text.trim().replace(",", "."));
    // The one local check, and it is NOT about the range: only what is not a number at all
    // gets filtered out. JSON.stringify(NaN) yields "null", and the device would receive a
    // bad body instead of a clear refusal about the value.
    if (text.trim() === "" || !Number.isFinite(value)) {
      setNotANumber(true);
      return;
    }
    setNotANumber(false);
    setBusy(true);
    writeEntity(meta.key, value)
      .then(() => {
        toast(t("state.queued", { name: entityName(meta.key, meta.name) }));
        onWrite(meta.key, null);
      })
      .catch((err: unknown) => onWrite(meta.key, explainWriteFailure(err)))
      .finally(() => setBusy(false));
  }

  const bounded = meta.min !== undefined && meta.max !== undefined;

  return (
    <form class={styles.write} onSubmit={submit}>
      <input
        type="number"
        // "any" rather than a step: the device knows the entity's codec and this page does
        // not, and a 0.5 step invented here would refuse a legal integer on a u16 entity.
        step="any"
        min={meta.min}
        max={meta.max}
        value={text}
        disabled={busy}
        aria-label={t("state.write", { name: entityName(meta.key, meta.name) })}
        onInput={(e) => setText((e.target as HTMLInputElement).value)}
      />
      <button type="submit" disabled={busy}>
        {t("state.writeAction")}
      </button>
      {bounded && (
        <span class={styles.sub}>
          {meta.min}…{meta.max}
          {meta.unit ? ` ${meta.unit}` : ""}
        </span>
      )}
      {notANumber && <span class={styles.invalid}>{t("state.notANumber")}</span>}
    </form>
  );
}
