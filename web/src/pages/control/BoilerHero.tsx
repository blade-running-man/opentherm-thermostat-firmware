// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

import { useEffect, useRef } from "preact/hooks";
import { deviceState } from "../../api/device";
import { t } from "../../i18n/index.ts";
import { PixelBoiler } from "./pixelBoiler";
import type { StateValue } from "../../api/ws";
import styles from "./BoilerHero.module.css";

/**
 * BoilerHero — the animated boiler at the top of the Control page.
 *
 * READS ONLY. It shows what the boiler is doing from the entity registry carried on /ws
 * (deviceState) and writes nothing: the controls below it keep their own tested wiring. Reading
 * deviceState.value here subscribes the component, so every frame that changes a shown entity
 * re-renders it, and the effect pushes the values into the scene. No entity is hand-listed -- the
 * keys are the generated registry's (CLAUDE.md: one entity list, ever).
 *
 * ONE CANVAS. The burner and the water-circulation loop share a single pixel grid (pixelBoiler.ts)
 * -- the flame lives inside the boiler that the water circles, so it is one machine, not two
 * overlaid drawings. Preact re-renders only the numbers. `null` renders "--", never 0.
 */

/** A boolean entity is "on" only when strictly true -- null/absent is not off-by-accident. */
function isOn(v: StateValue): boolean {
  return v === true;
}

function num(v: StateValue): number | null {
  return typeof v === "number" ? v : null;
}

function fmt(v: number | null, digits: number, unit = ""): string {
  return v === null ? "—" : v.toFixed(digits) + unit;
}

/** control_state is a synthetic string entity ("local", "boost", "failsafe", ...). */
function stateLabel(v: StateValue): string {
  if (typeof v !== "string" || v === "") return "—";
  return v.replace(/_/g, " ").toUpperCase();
}

export function BoilerHero() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sceneRef = useRef<PixelBoiler | null>(null);

  // Reading the signal subscribes this component; a /ws frame that moves any shown value re-renders.
  const values = deviceState.value;
  const flameOn = isOn(values["flame"]);
  const mod = num(values["modulation"]);
  const flow = num(values["flow_temperature"]);
  const ret = num(values["return_temperature"]);
  const bar = num(values["ch_pressure"]);
  const chOn = isOn(values["ch_active"]);
  const dhwOn = isOn(values["dhw_active"]);
  const state = stateLabel(values["control_state"]);

  useEffect(() => {
    if (!canvasRef.current) return;
    const scene = new PixelBoiler(canvasRef.current);
    sceneRef.current = scene;
    scene.start();
    return () => {
      scene.stop();
      sceneRef.current = null;
    };
  }, []);

  useEffect(() => {
    sceneRef.current?.setInputs({
      chActive: chOn, dhwActive: dhwOn, flame: flameOn,
      flowTemp: flow, returnTemp: ret, modulation: mod,
    });
  }, [chOn, dhwOn, flameOn, flow, ret, mod]);

  return (
    <section class={styles.hero} aria-label={t("control.hero.ariaLabel")}>
      {/* Synthwave backdrop: starfield sky, a glowing horizon, and a perspective grid receding
          into the distance. Behind the scene; the boiler is drawn on a transparent canvas, so the
          grid shows through its gaps. Decorative. */}
      <div class={styles.backdrop} aria-hidden="true">
        <div class={styles.stars} />
        <div class={styles.horizon} />
        <div class={styles.grid} />
      </div>

      <div class={styles.status}>
        <span class={styles.state}>{state}</span>
        <span class={`${styles.pill} ${chOn ? styles.pillOn : ""}`}>
          {chOn ? t("control.hero.heatingOn") : t("control.hero.heatingOff")}
        </span>
        <span class={`${styles.pill} ${dhwOn ? styles.pillOn : ""}`}>
          {dhwOn ? t("control.hero.dhwOn") : t("control.hero.dhwOff")}
        </span>
      </div>

      {/* The scene (burner + circulation loop) is decorative -- the pump state is in the pills and
          the flow/return numbers -- so the canvas is aria-hidden; the labels are real text over it. */}
      <div class={styles.furnace}>
        <canvas ref={canvasRef} class={styles.scene} aria-hidden="true" />
        <span class={`${styles.tag} ${styles.flowTag}`}>{t("control.hero.tag.flow")} ▶</span>
        <span class={`${styles.tag} ${styles.retTag}`}>◀ {t("control.hero.tag.return")}</span>
        <span class={`${styles.tag} ${styles.radTag}`}>{t("control.hero.tag.rad")}</span>
        <span class={`${styles.tag} ${styles.sinkTag}`}>{t("control.hero.tag.sink")}</span>
        <span class={`${styles.tag} ${styles.coldTag}`}>{t("control.hero.tag.cold")} ▶</span>
        <span class={styles.plate}>◈ {t("control.hero.tag.boiler")}</span>
        {!flameOn && <span class={styles.standby}>{t("control.hero.tag.standby")}</span>}
      </div>

      <div class={styles.stats}>
        <div class={styles.stat}>
          <span class={`${styles.statVal} ${styles.flowVal}`}>{fmt(flow, 1, "°")}</span>
          <span class={styles.statLabel}>{t("control.hero.flow")}</span>
        </div>
        <div class={styles.stat}>
          <span class={`${styles.statVal} ${styles.retVal}`}>{fmt(ret, 1, "°")}</span>
          <span class={styles.statLabel}>{t("control.hero.return")}</span>
        </div>
        <div class={styles.stat}>
          <span class={`${styles.statVal} ${styles.modVal}`}>{fmt(mod, 0, "%")}</span>
          <span class={styles.statLabel}>{t("control.hero.modulation")}</span>
        </div>
        <div class={styles.stat}>
          <span class={`${styles.statVal} ${styles.barVal}`}>{fmt(bar, 1)}</span>
          <span class={styles.statLabel}>{t("control.hero.bar")}</span>
        </div>
      </div>
    </section>
  );
}
