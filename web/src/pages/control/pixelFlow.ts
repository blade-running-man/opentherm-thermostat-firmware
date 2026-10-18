// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The heating-loop geometry and dynamics — the pure maths behind the circulation drawn by
// pixelBoiler.ts. No DOM here, so the node runner can pin it (tests/pixelFlow.test.ts).
//
// The loop is a rectangle laid out like a house heating system: the LEFT side runs up through the
// boiler (where the burner reheats the water), the TOP pipe carries hot water across to the
// RADIATOR on the RIGHT, the right side runs down through the radiator (giving off heat), and the
// BOTTOM pipe returns the cooled water to the boiler. Warm colour on the boiler half (top + left),
// cyan on the radiator half (right + bottom).
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

export interface Seg { x0: number; y0: number; dx: number; dy: number; len: number; start: number }
export interface LoopPath { segs: Seg[]; total: number; warmDist: number }

/**
 * The circuit as an axis-aligned polyline through the given points, in the direction the water
 * travels (last point should equal the first, closing the loop). `warmDist` is the distance from
 * the start up to which the water is still hot (the FLOW half); past it, cyan (the RETURN half).
 * The renderer places the boiler/radiator on the path and decides which parts are "inside" from
 * their boxes, so this stays a plain geometry helper.
 */
export function buildPath(points: Array<[number, number]>, warmDist: number): LoopPath {
  const segs: Seg[] = [];
  let start = 0;
  for (let i = 0; i < points.length - 1; i++) {
    const [x0, y0] = points[i];
    const [x1, y1] = points[i + 1];
    const len = Math.abs(x1 - x0) + Math.abs(y1 - y0);
    segs.push({ x0, y0, dx: Math.sign(x1 - x0), dy: Math.sign(y1 - y0), len, start });
    start += len;
  }
  return { segs, total: start, warmDist };
}

/** Point (and travel direction) at a distance along the loop; wraps around. */
export function locate(path: LoopPath, dist: number) {
  let d = ((dist % path.total) + path.total) % path.total;
  for (let i = 0; i < path.segs.length; i++) {
    const s = path.segs[i];
    if (d <= s.len) return { x: s.x0 + s.dx * d, y: s.y0 + s.dy * d, dx: s.dx, dy: s.dy, seg: i };
    d -= s.len;
  }
  const s = path.segs[path.segs.length - 1];
  return { x: s.x0 + s.dx * s.len, y: s.y0 + s.dy * s.len, dx: s.dx, dy: s.dy, seg: path.segs.length - 1 };
}

/** Warm (the FLOW half, up to warmDist) vs cyan (the RETURN half, after it). */
export function warmAt(path: LoopPath, dist: number): boolean {
  return (((dist % path.total) + path.total) % path.total) < path.warmDist;
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

/** Evenly spaced packet distances around the loop for a given phase. */
export function packetDists(phase: number, count: number, total: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < count; i++) out.push((((phase + (i / count) * total) % total) + total) % total);
  return out;
}
