// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The circulation loop's maths — the part that runs without a canvas.
//
// Run: node src/pages/control/tests/pixelFlow.test.ts
//
// Pins that the loop always moves and speeds up with the pump/modulation, that its brightness
// follows the boiler's state, and that the radiator glow tracks temperature and clamps.

import { intensity, speed, tempGlow } from "../pixelFlow.ts";
import { eq, ok, report } from "../../settings/tests/harness.ts";

const base = { chActive: false, dhwActive: false, flame: false, flowTemp: null, returnTemp: null, modulation: null };

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

report("pixelFlow");
