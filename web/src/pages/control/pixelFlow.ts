// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The heating loop's dynamics — the pure maths behind the circulation drawn by pixelBoiler.ts:
// how fast the water moves, how bright the loop is, how hot the radiator glows. No DOM here, so
// the node runner can pin it (tests/pixelFlow.test.ts). The pipes' geometry is pixelPipe.ts.
//
// PHYSICS NOTE. OpenTherm has no "water is flowing" signal for the heating circuit — the motion is
// a VISUAL indication of demand, not a measured rate. So the loop runs when the pump does
// (ch_active) and speeds up with modulation; without it, it crawls and dims rather than stopping.

export interface LoopInputs {
  chActive: boolean;
  dhwActive: boolean;
  flame: boolean;
  flowTemp: number | null;
  returnTemp: number | null;
  modulation: number | null;
}

/**
 * Loop speed as a fraction, 0..1. The pump (ch_active) drives it and modulation speeds it up; with
 * the pump off it crawls — a slow drift while the burner is lit (e.g. DHW), a slower one in standby
 * — but never freezes hard, so the loop always reads as a circuit, not a dead diagram.
 */
export function speed(inp: LoopInputs): number {
  if (inp.chActive) {
    const m = Math.max(0, Math.min(100, inp.modulation ?? 60));
    return 0.35 + 0.65 * (m / 100);
  }
  if (inp.flame) return 0.14;
  return 0.05;
}

/** Overall brightness of the loop by state: bright pumping, dim in DHW/standby. */
export function intensity(inp: LoopInputs): number {
  if (inp.chActive) return 1;
  if (inp.dhwActive) return 0.35; // heating loop idle while hot water is the demand
  if (inp.flame) return 0.5;
  return 0.28; // standby: dim, not gone
}

/** Radiator/pipe glow by temperature — the same curve as BoilerHero's old pipeOpacity. */
export function tempGlow(temp: number | null): number {
  if (temp === null || !Number.isFinite(temp)) return 0.28;
  return Math.max(0.28, Math.min(1, (temp - 20) / 55));
}
