// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The circulation loop's maths — the part that runs without a canvas.
//
// Run: node src/pages/control/tests/pixelFlow.test.ts
//
// Pins the loop geometry (a rectangle: boiler half warm on the top+left, radiator half cyan on the
// right+bottom), that the loop always moves and speeds up with the pump/modulation, and that the
// radiator glow tracks temperature and clamps.

import {
  buildPath, intensity, locate, packetDists, speed, tempGlow, warmAt,
} from "../pixelFlow.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

const base = { chActive: false, dhwActive: false, flame: false, flowTemp: null, returnTemp: null, modulation: null };

// --- geometry: an axis-aligned polyline, warm for the first half, cyan for the rest ---------
const path = buildPath([[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], 20);
eq(path.segs.length, 4, "four segments for a four-corner closed polyline");
eq(path.total, 40, "perimeter length");
eq(path.warmDist, 20, "warm half is the distance passed in");

eq([locate(path, 0).x, locate(path, 0).y], [0, 0], "distance 0 is the first point (boiler outlet)");
eq([locate(path, 0).dx, locate(path, 0).dy], [1, 0], "leaves heading along the first segment");
eq([locate(path, path.total).x, locate(path, path.total).y], [0, 0], "one full length wraps to the start");

ok(warmAt(path, 0), "hot at the start of the FLOW half");
ok(warmAt(path, 19), "still warm just before warmDist");
ok(!warmAt(path, 21), "cyan just past warmDist (the RETURN half)");
ok(!warmAt(path, 39), "cyan at the end of the loop");

// --- the loop always moves, and the pump + modulation speed it up ---------------------------
ok(speed(base) > 0, "standby still drifts — never a hard stop");
ok(speed({ ...base, flame: true }) > speed(base), "a lit burner drifts faster than standby");
ok(speed({ ...base, chActive: true, modulation: 0 }) > speed({ ...base, flame: true }),
  "the pump running beats a mere lit burner");
ok(speed({ ...base, chActive: true, modulation: 100 })
   > speed({ ...base, chActive: true, modulation: 20 }), "modulation speeds the loop up");
eq(speed({ ...base, chActive: true, modulation: 0 }), 0.35, "pump at 0% is the loop floor 0.35");
ok(Math.abs(speed({ ...base, chActive: true, modulation: 100 }) - 1) < 1e-9, "pump at 100% is 1.0");

// --- brightness by state --------------------------------------------------------------------
eq(intensity({ ...base, chActive: true }), 1, "pumping is full brightness");
eq(intensity({ ...base, dhwActive: true }), 0.35, "DHW dims the heating loop");
eq(intensity({ ...base, flame: true }), 0.5, "a lit burner without the pump is mid");
eq(intensity(base), 0.28, "standby is dim, not gone");

// --- radiator/pipe glow by temperature ------------------------------------------------------
eq(tempGlow(20), 0.28, "cold floor 0.28 at 20 C");
eq(tempGlow(75), 1, "full glow by 75 C");
eq(tempGlow(200), 1, "clamps above the top");
eq(tempGlow(null), 0.28, "no reading -> the cold floor, not zero");
ok(tempGlow(47.5) > 0.4 && tempGlow(47.5) < 0.6, "mid temperature lands mid-glow");

// --- packets evenly spaced around the loop --------------------------------------------------
const dists = packetDists(0, 6, path.total);
eq(dists.length, 6, "six packets");
eq(dists[0], 0, "first packet at the start for phase 0");
ok(dists.every((d) => d >= 0 && d < path.total), "every packet lies on the loop");
ok(Math.abs((dists[1] - dists[0]) - path.total / 6) < 1e-9, "packets are evenly spaced");

report("pixelFlow");
