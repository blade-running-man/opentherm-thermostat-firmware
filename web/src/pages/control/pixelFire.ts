// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The boiler's pixel fire — a 16×16 canvas of flickering vertical columns.
//
// The algorithm is the owner's reference (CodePen kylewetton/NeRbvz): three layers of
// vertical lines whose tops jump to a random height every frame at ~7 fps, drawn crisp and
// scaled up with image-rendering:pixelated (BoilerHero.module.css). Reproduced here with one
// addition — the flame height scales with the boiler's MODULATION, so an idling burner shows a
// low fire and a burner at full output a tall one.
//
// SPLIT ON PURPOSE. The pure frame math (nextFrame/modScale) is separated from the class that
// touches the canvas so the node test runner — which cannot import the DOM — can pin the
// column heights (tests/pixelFire.test.ts). The class references DOM globals only inside its
// methods, never at module load, so importing this file in a test is safe. DO NOT move the math
// into the class: that would make it untestable, the exact gap a review once found
// for the .tsx layer.

/** Per-column base (bottom) y, in the flipped canvas where y grows upward. */
export const BASE = [2, 1, 0, 0, 0, 0, 1, 2];
/** Per-column ceiling / floor of the random top, before modulation scaling. */
export const MAXH = [7, 9, 11, 13, 13, 11, 9, 7];
export const MINH = [4, 7, 8, 10, 10, 8, 7, 4];

/** The owner's chosen palette (variant "Original"): outer red, mid orange, cream core. */
export const OUTER = "#d14234";
export const MID = "#f2a55f";
export const CORE = "#e8dec5";

/** 7 frames a second, as the reference does — the chunky cadence is part of the look. */
export const FIRE_FPS = 7;

export interface FireFrame {
  outer: number[]; // 8 columns, x = 4..11
  mid: number[]; //   6 columns, x = 5..10
  core: number[]; //  2 columns, x = 7..8
}

/**
 * Maps modulation % to a height scale. A column's top is lifted from its base toward the
 * random ceiling by this factor: 0.4 at 0 % (a low idle flame) to 1.0 at 100 % (the reference
 * height). `null`/non-finite modulation (no reading) falls back to a mid 0.7 rather than a dead
 * fire — the flame's presence is governed by `flame`, not by a missing modulation number.
 */
export function modScale(mod: number | null | undefined): number {
  if (mod === null || mod === undefined || !Number.isFinite(mod)) return 0.7;
  const m = Math.max(0, Math.min(100, mod));
  return 0.4 + 0.6 * (m / 100);
}

/**
 * One frame's column tops. `rand` is injected so a test can make it deterministic; production
 * passes Math.random. At scale 1 with rand()==0 the tops equal the MIN table, i.e. the
 * reference's own lower bound — the scaling only ever shortens, never distorts, the shape.
 */
export function nextFrame(scale: number, rand: () => number = Math.random): FireFrame {
  const lift = (base: number, top: number) => base + (top - base) * scale;
  const outer: number[] = [];
  const mid: number[] = [];
  const core: number[] = [];
  for (let i = 0; i < 8; i++) {
    const a = rand() * (MAXH[i] - MINH[i] + 1) + MINH[i];
    outer.push(lift(BASE[i], a));
  }
  for (let m = 0; m < 6; m++) {
    const j = 1 + m;
    const a = rand() * ((MAXH[j] - 5) - (MINH[j] - 5) + 1) + (MINH[j] - 5);
    mid.push(lift(BASE[j] + 1, a));
  }
  for (let c = 0; c < 2; c++) {
    const k = 3 + c;
    const a = rand() * ((MAXH[k] - 9) - (MINH[k] - 9) + 1) + (MINH[k] - 9);
    core.push(lift(BASE[k], a));
  }
  return { outer, mid, core };
}

// The flame is drawn by the single scene renderer (pixelBoiler.ts) from these tops; this module
// intentionally keeps only the pure maths (nextFrame/modScale) so it stays host-testable.
