// web/mock/tests/evolve.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";
import { tick } from "../evolve.ts";

// Heating on + lit: flow climbs toward held setpoint over successive ticks.
let s = initialState();
s.chEnable = true; s.heldSetpointDc = 600; s.flowDc = 300; s.flame = true;
const before = s.flowDc;
for (let i = 0; i < 30; i++) s = tick(s, 1000);
ok(s.flowDc > before, "flow rises while heating");
ok(s.flowDc <= 600, "flow does not overshoot the setpoint");

// Heating off: flow decays toward room, flame goes out.
let c = initialState();
c.chEnable = false; c.flowDc = 600; c.roomDc = 205;
for (let i = 0; i < 60; i++) c = tick(c, 1000);
ok(c.flowDc < 600, "flow cools when heating is off");
eq(c.flame, false, "flame is out when heating is off");

// HA mode with a blind controller drives the watchdog overdue counter up.
let h = initialState();
h.scenario.mode = "ha"; h.watchdogOverdueS = 0;
h = tick(h, 1000);
ok(h.watchdogOverdueS >= 1, "watchdog counts up in HA mode");

// Forced failsafe scenario lands the ladder in failsafe.
let f = initialState();
f.scenario.mode = "ha"; f.scenario.failsafe = true;
f = tick(f, 1000);
eq(f.controlState, "failsafe", "failsafe scenario reaches the failsafe state");

// Failsafe duration integrates real elapsed seconds (dt-aware), counted once per episode.
let fd = initialState();
fd.scenario.mode = "ha"; fd.scenario.failsafe = true;
for (let i = 0; i < 5; i++) fd = tick(fd, 1000);
eq(fd.failsafeCount, 1, "one failsafe episode counted");
eq(Math.round(fd.lastFailsafeDurationS), 5, "duration integrates 5 seconds over 5 one-second ticks");

// Season off short-circuits to season_off.
let o = initialState();
o.scenario.heatingSeason = false;
o = tick(o, 1000);
eq(o.controlState, "season_off", "season off => season_off state");

report("mock/evolve");
