// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import type { ComponentChildren } from "preact";
import { useState } from "preact/hooks";
import type { ControlDocument } from "../../api/control";
import { runOperation, setEntity } from "../../api/device";
import { t } from "../../i18n/index.ts";
import {
  bannerText,
  cardText,
  failsafeBanner,
  ownerText,
  parseNumber,
  reasonText,
  sinceHaText,
  stateView,
} from "./model";
import styles from "./Control.module.css";

/**
 * The control card's pieces. Presentational: every decision worth testing is in model.ts -- the
 * rows' words included (cardText(), bannerText()), because which bit a row reads is exactly what
 * a component cannot be asked -- and every write goes through the page's act(), which shows the
 * device's answer.
 */

/**
 * What every control needs from the page: which write is in flight, and the one way to write.
 *
 * `id` is the control's STABLE internal spelling ("ch", "boost", ...), used for `busy === id`
 * comparisons; `label` is its translated display text (aria-label, toast). They used to be the
 * same string (`what`) doubling as both -- they were split so a translated label never
 * changes which control is busy (decoupling the `what`
 * prop).
 */
export interface Writer {
  busy: string | null;
  act: (id: string, label: string, run: () => Promise<void>) => Promise<void>;
}

/** The hints beside the boxes, read once per visit (Control.tsx, loadHints()). */
export interface Hints {
  flow: string | null;
  dhw: string | null;
}

function Row({ label, children }: { label: string; children: ComponentChildren }) {
  return (
    <div class={styles.row}>
      <span class={styles.label}>{label}</span>
      <span class={styles.value}>{children}</span>
    </div>
  );
}

/**
 * An On/Off pair for a switch entity. Two buttons, not a toggle: a toggle sends the opposite of
 * what the page believes, and what it believes may be a second old or unknown. "On" sends true
 * and "Off" sends false whatever is shown -- and writing the value the device already holds is a
 * legitimate write (Home Assistant's keepalive is exactly that).
 */
function SwitchPair({ id, label, entity, value, busy, act }:
  Writer & { id: string; label: string; entity: string; value: boolean | null }) {
  return (
    <span class={styles.pair} role="group" aria-label={label}>
      {[true, false].map((v) => (
        <button
          type="button"
          key={String(v)}
          class={value === v ? styles.pressed : undefined}
          aria-pressed={value === v}
          disabled={busy === id}
          onClick={() => void act(id, label, () => setEntity(entity, v))}
        >
          {v ? t("control.button.on") : t("control.button.off")}
        </button>
      ))}
    </span>
  );
}

/**
 * A temperature box and its button. The one local check is "is it a number" (parseNumber()); the
 * band beside the box is a hint, and 35 °C is sent -- the device's 422 names the band.
 */
function SetpointForm({ id, label, entity, hint, busy, act }:
  Writer & { id: string; label: string; entity: string; hint: string | null }) {
  const [text, setText] = useState("");
  const [notANumber, setNotANumber] = useState(false);

  function submit(e: Event) {
    e.preventDefault();
    const value = parseNumber(text);
    setNotANumber(value === null);
    if (value !== null) void act(id, label, () => setEntity(entity, value));
  }

  return (
    <form class={styles.form} onSubmit={submit}>
      <input
        type="text"
        inputMode="decimal"
        value={text}
        placeholder="°C"
        aria-label={`${label}, °C`}
        disabled={busy === id}
        onInput={(e) => setText((e.target as HTMLInputElement).value)}
      />
      <button type="submit" disabled={busy === id}>{t("control.button.set")}</button>
      {hint && <span class={styles.hint}>{hint}</span>}
      {notANumber && <span class={styles.bad}>{t("control.validation.notANumber")}</span>}
    </form>
  );
}

/** Loud while the failsafe runs; quiet, but present, once it has run since boot. */
export function FailsafeBanner({ doc }: { doc: ControlDocument }) {
  const b = failsafeBanner(doc);
  if (b === null) return null;
  const said = bannerText(b);
  return (
    <div class={`${styles.banner} ${b.active ? styles.bannerActive : styles.bannerPast}`} role="status">
      <strong>{said.title}</strong>
      <span>{said.body}</span>
    </div>
  );
}

export function StatusCard({ doc, busy, act }: Writer & { doc: ControlDocument }) {
  const view = stateView(doc.state);
  const reason = reasonText(doc.reason);
  const since = sinceHaText(doc);
  const seasonLabel = t("control.season.label");
  return (
    <div class="card">
      <p class={`${styles.state} ${styles[view.tone]}`}>{view.label}</p>
      <p class={styles.note}>{view.note}</p>
      {reason && <p class={styles.note}>{reason}</p>}
      <p class={styles.hint}>{ownerText(doc.mode)}{since ? ` ${since}` : ""}</p>
      <Row label={seasonLabel}>
        <SwitchPair id="season" label={seasonLabel} entity="heating_season" value={doc.heating_season}
                    busy={busy} act={act} />
      </Row>
    </div>
  );
}

export function HeatingCard({ doc, ch, hints, busy, act }:
  Writer & { doc: ControlDocument; ch: boolean | null; hints: Hints }) {
  const text = cardText(doc);
  const chLabel = t("control.ch.label");
  const flowLabel = t("control.flow.label");
  return (
    <div class="card">
      <h2 class={styles.title}>{t("control.heating.title")}</h2>
      <Row label={t("control.ch.commandRow")}>
        <SwitchPair id="ch" label={chLabel} entity="ch_enable" value={ch} busy={busy} act={act} />
      </Row>
      <Row label={t("control.ch.askedRow")}>{text.chAsked}</Row>
      <Row label={t("control.flow.heldRow")}>{text.held}</Row>
      <Row label={t("control.flow.newRow")}>
        <SetpointForm id="flow" label={flowLabel} entity="ch_setpoint" hint={hints.flow}
                      busy={busy} act={act} />
      </Row>
      <p class={styles.hint}>{t("control.ch.explain")}</p>
    </div>
  );
}

export function DhwCard({ doc, hints, busy, act }: Writer & { doc: ControlDocument; hints: Hints }) {
  const text = cardText(doc);
  const dhwLabel = t("control.dhw.label");
  const dhwSetpointLabel = t("control.dhw.setpointLabel");
  return (
    <div class="card">
      <h2 class={styles.title}>{dhwLabel}</h2>
      <Row label={t("control.dhw.commandRow")}>
        <SwitchPair id="dhw" label={dhwLabel} entity="dhw_enable" value={doc.dhw.enable}
                    busy={busy} act={act} />
      </Row>
      <Row label={t("control.dhw.askedRow")}>{text.dhwAsked}</Row>
      <Row label={dhwSetpointLabel}>{text.dhwSetpoint}</Row>
      <Row label={t("control.dhw.newSetpointRow")}>
        <SetpointForm id="dhwSetpoint" label={dhwSetpointLabel} entity="dhw_setpoint" hint={hints.dhw}
                      busy={busy} act={act} />
      </Row>
    </div>
  );
}

export function BoostCard({ doc, hints, busy, act }: Writer & { doc: ControlDocument; hints: Hints }) {
  const [setpoint, setSetpoint] = useState("");
  const [minutes, setMinutes] = useState("");
  const [notANumber, setNotANumber] = useState(false);
  const running = cardText(doc).boost;
  const boostLabel = t("control.boost.label");
  const stopLabel = t("control.boost.stopLabel");

  function start(e: Event) {
    e.preventDefault();
    const sp = parseNumber(setpoint);
    const min = parseNumber(minutes);
    setNotANumber(sp === null || min === null);
    // BOTH parameters, in one call: the device has no default for either (op_boost,
    // components/ot_http/ot_http_ops.c), and the boxes start empty for the same reason.
    if (sp !== null && min !== null)
      void act("boost", boostLabel, () => runOperation("boost", { setpoint: sp, minutes: min }));
  }

  return (
    <div class="card">
      <h2 class={styles.title}>{boostLabel}</h2>
      {running !== null ? (
        <Row label={t("control.boost.runningRow")}>
          {running}
          <button type="button" disabled={busy === "stopBoost"}
                  onClick={() => void act("stopBoost", stopLabel, () => runOperation("boost_off"))}>
            {t("control.button.stop")}
          </button>
        </Row>
      ) : (
        <p class={styles.hint}>{t("control.boost.none")}</p>
      )}
      <form class={styles.form} onSubmit={start}>
        <input type="text" inputMode="decimal" value={setpoint} placeholder="°C"
               aria-label={`${t("control.boost.setpointAria")}, °C`} disabled={busy === "boost"}
               onInput={(e) => setSetpoint((e.target as HTMLInputElement).value)} />
        <input type="text" inputMode="numeric" value={minutes} placeholder="min"
               aria-label={t("control.boost.lengthAriaMinutes")} disabled={busy === "boost"}
               onInput={(e) => setMinutes((e.target as HTMLInputElement).value)} />
        <button type="submit" disabled={busy === "boost"}>{t("control.button.start")}</button>
        {hints.flow && <span class={styles.hint}>{hints.flow}</span>}
        {notANumber && <span class={styles.bad}>{t("control.boost.bothNeeded")}</span>}
      </form>
      <p class={styles.hint}>{t("control.boost.explain")}</p>
    </div>
  );
}
