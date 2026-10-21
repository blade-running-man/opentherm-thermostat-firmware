// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The pipes of the heating scene (pixelBoiler.ts), painted onto its canvas: steel pipe lit from
// the upper left, couplings along the straight runs, swept elbows, and brass union nuts where a
// pipe screws onto the boiler — the fittings a real installation is built from. The water shows
// through the pipe only as a glow along its middle and as packets of light gliding along it, in
// the water's colour, and the pipe's outline is tinted the same way, so a pipe reads hot or cold
// even between packets. The look is the ComfoAir scene's ducts, scaled down to water pipes.

import { litOffset, packetsAt, rasterize, type PipePixel, type PipeRun, type PipeSample } from "./pixelPipe";

/** The scene's one drawing primitive: a w×h block of `color` at (x, y), rounded to whole pixels. */
export type Px = (x: number, y: number, color: string, alpha: number, w?: number, h?: number) => void;

export const P_OUT = "#0e1016";
// The cylinder's tones from the lit side round to the shaded one; index 0 faces the light.
export const PIPE_TONES = ["#c9d0dc", "#aab2c0", "#8c95a5", "#6f7889", "#5a6273", "#474e5d"];
// The gas line is painted yellow, as gas pipework is by code — the one pipe that is not steel-grey.
export const GAS_TONES = ["#fff4a3", "#f6dc4c", "#dcbb2b", "#b5961d", "#8b7215", "#5f4d0d"];
export const RING_TONES = ["#e4e9f0", "#c3cad5", "#a3abb9", "#848d9e", "#6a7383", "#3c4250"];
export const BRASS_TONES = ["#fff0a6", "#f0cd68", "#d3a83d", "#b08526", "#8a6419", "#5e430f"];

export const PIPE_HALF = 2;           // a 5px pipe
const RING_HALF = 3;                  // couplings and nuts stand a pixel proud of it
const TAIL = 5;

export const tone = (tones: readonly string[], lit: number) =>
  tones[Math.max(0, Math.min(tones.length - 1, Math.round(((1 - lit) / 2) * (tones.length - 1))))];

/**
 * One cross-section of a cylinder `half` pixels either side of the sample, toned by how lit each
 * point is; the outermost pixel is the outline. Only for STRAIGHT runs — the couplings and the
 * nuts; a pipe's elbows go through rasterize().
 */
function section(px: Px, s: PipeSample, half: number, tones: readonly string[]): void {
  for (let k = -half; k <= half; k += 0.5) {
    const x = s.x + s.nx * k, y = s.y + s.ny * k;
    if (Math.abs(k) > half - 0.75) { px(x, y, P_OUT, 1); continue; }
    px(x, y, tone(tones, litOffset(s, k) / (half - 1)), 1);
  }
}

/** A pipe ready to paint: its run, its body rasterized once, and where its fittings go. */
export interface Pipe {
  run: PipeRun;
  body: PipePixel[];
  /** Couplings every `ringPitch` samples from `ringFrom`, clear of the elbows (0 = none). */
  ringPitch: number;
  ringFrom: number;
  /** A brass union nut at the run's start, its end, or both — where it screws onto a fitting. */
  nutStart: boolean;
  nutEnd: boolean;
  /** The body's tones, lit side first: steel by default, yellow for the gas line. */
  tones: readonly string[];
}

/** rasterize() is far too slow to run every frame, so the body is computed here, once. */
export function pipe(run: PipeRun, look: Partial<Omit<Pipe, "run" | "body">> = {}): Pipe {
  return {
    run, body: rasterize(run, PIPE_HALF),
    ringPitch: look.ringPitch ?? 0, ringFrom: look.ringFrom ?? 0,
    nutStart: look.nutStart ?? false, nutEnd: look.nutEnd ?? false, tones: look.tones ?? PIPE_TONES,
  };
}

/** The bare pipe with its outline and centre tinted by what flows in it. Paint the back runs first. */
export function drawPipe(px: Px, p: Pipe, water: string, inten: number): void {
  for (const b of p.body) {
    if (b.edge) { px(b.x, b.y, P_OUT, 1); px(b.x, b.y, water, 0.55 * inten); } else px(b.x, b.y, tone(p.tones, b.lit), 1);
  }
  for (const s of p.run.samples) px(s.x, s.y, water, 0.35 * inten);
  const { samples, total } = p.run;
  if (p.ringPitch > 0) {
    for (let i = p.ringFrom; i < total - 3; i += p.ringPitch) {
      if (samples.slice(Math.max(0, i - 5), i + 6).some((s) => s.bend)) continue;
      drawRing(px, samples, i, RING_TONES);
    }
  }
  if (p.nutStart) drawRing(px, samples, 2, BRASS_TONES);
  if (p.nutEnd) drawRing(px, samples, total - 2, BRASS_TONES);
}

/**
 * A coupling or a union nut: three samples of ring a pixel proud of the pipe, with a dark seam
 * either side where the pipe butts into it.
 */
function drawRing(px: Px, samples: PipeSample[], i: number, tones: readonly string[]): void {
  for (const j of [i - 2, i + 2]) {
    const s = samples[j];
    if (s) for (let k = -PIPE_HALF + 1; k <= PIPE_HALF - 1; k++) px(s.x + s.nx * k, s.y + s.ny * k, P_OUT, 0.55);
  }
  for (let j = i - 1; j <= i + 1; j++) if (samples[j]) section(px, samples[j], RING_HALF, tones);
}

/** The water moving through: short bright packets with a fading tail, the head leading. */
export function drawPackets(px: Px, p: Pipe, water: string, phase: number, inten: number, count: number): void {
  for (const head of packetsAt(phase, count, p.run.total)) {
    for (let t = 0; t < TAIL; t++) {
      const s = p.run.samples[head - t];
      if (!s) break;
      const a = (1 - t / TAIL) * inten;
      for (let k = -1; k <= 1; k++) px(s.x + s.nx * k, s.y + s.ny * k, water, k === 0 ? a : a * 0.6);
    }
  }
}
