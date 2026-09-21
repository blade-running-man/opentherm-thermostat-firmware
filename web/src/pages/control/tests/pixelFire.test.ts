// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The pixel fire's column math — the part that runs without a canvas.
//
// Run: node src/pages/control/tests/pixelFire.test.ts
//
// The class that strokes the canvas cannot be reached by node's type-stripping runner (no DOM),
// but the frame heights and the modulation scaling are pure and are where a "helpful" edit would
// silently distort the flame. At scale 1 the tops must equal the reference's own MIN/MAX table;
// the modulation scale must only ever shorten, clamp its input, and never kill the fire on a
// missing reading.

import { MAXH, MINH, modScale, nextFrame } from "../pixelFire.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

// --- nextFrame at scale 1, rand()==0 → the reference lower bound (the MIN table) ------------
const lo = nextFrame(1, () => 0);
eq(lo.outer, [4, 7, 8, 10, 10, 8, 7, 4], "outer tops at scale 1, rand 0 == MIN table");
eq(lo.mid, [2, 3, 5, 5, 3, 2], "mid tops == MIN[j]-5 for the six inner columns");
eq(lo.core, [1, 1], "core tops == MIN[k]-9 for the two centre columns");

// --- bounds: with rand just under 1 every top stays within [MIN, MAX+1) ---------------------
const hi = nextFrame(1, () => 0.999999);
let within = true;
for (let i = 0; i < 8; i++) {
  if (hi.outer[i] < MINH[i] || hi.outer[i] >= MAXH[i] + 1) within = false;
}
ok(within, "every outer top lies in [MIN, MAX+1) at scale 1");

// --- scaling only shortens: a lower scale gives a lower top for the same random draw ---------
const calm = nextFrame(0.4, () => 0.9);
const full = nextFrame(1, () => 0.9);
ok(calm.outer[3] < full.outer[3], "0.4 scale is shorter than 1.0 for the same draw");
ok(calm.outer[3] >= 0, "a scaled top never drops below its column base (column 3's base is 0)");

// --- modScale: endpoints exact, clamped, and never dead on a missing reading -----------------
eq(modScale(0), 0.4, "0 % → 0.4 (a low idle flame, not out)");
eq(modScale(100), 1, "100 % → 1.0 (reference height)");
eq(modScale(null), 0.7, "no reading → a mid flame, not zero");
eq(modScale(undefined), 0.7, "undefined reading → the same mid fallback");
ok(modScale(-20) === 0.4, "negative modulation clamps to the floor");
ok(modScale(200) === 1, "over-100 modulation clamps to the ceiling");
ok(modScale(50) > 0.6 && modScale(50) < 0.8, "mid modulation lands mid-scale");
ok(modScale(NaN) === 0.7, "NaN falls back, does not propagate");

report("pixelFire");
