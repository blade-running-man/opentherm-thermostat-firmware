// web/mock/evolve.ts
// The dynamic boiler model. One tick advances the simulated boiler by dtMs. Time constants
// are deliberately accelerated (TAU_S below) so a developer sees the flow reach setpoint in
// ~30 s rather than the boiler's real several minutes. This is a dev aid, not fidelity.
import type { BoilerState, ControlState, ControlReason, ControlCause } from "./model.ts";

const TAU_S = 20;          // exponential time constant for flow, seconds (accelerated)
const ROOM_TAU_S = 120;    // room temperature is slower
const FLAME_ON_MARGIN_DC = 20;

function approach(cur: number, target: number, dtS: number, tau: number): number {
  const k = 1 - Math.exp(-dtS / tau);
  return cur + (target - cur) * k;
}

export function tick(prev: BoilerState, dtMs: number): BoilerState {
  const s: BoilerState = structuredClone(prev);
  const dtS = dtMs / 1000;
  s.uptimeMs += dtMs;
  s.cycles += 1;

  const heating = s.chEnable && s.scenario.heatingSeason && !s.scenario.failsafe;
  // Flame lights when heating is demanded and flow is below setpoint by a margin.
  s.flame = s.scenario.answering && heating && s.flowDc < s.heldSetpointDc - FLAME_ON_MARGIN_DC;

  const flowTarget = s.flame ? s.heldSetpointDc : s.roomDc;
  s.flowDc = Math.round(approach(s.flowDc, flowTarget, dtS, TAU_S));
  s.returnDc = Math.round(approach(s.returnDc, s.flowDc - 150, dtS, TAU_S));
  s.modulationPct = s.flame
    ? Math.max(0, Math.min(100, Math.round((s.heldSetpointDc - s.flowDc) / 4)))
    : 0;

  // Room drifts up while the room is being heated, else decays toward outdoor.
  const roomTarget = heating && s.flowDc > s.roomDc ? s.roomDc + 30 : s.outdoorDc;
  s.roomDc = Math.round(approach(s.roomDc, roomTarget, dtS, ROOM_TAU_S));

  s.dhwFlowDc = Math.round(approach(s.dhwFlowDc, s.dhwEnable ? s.dhwSetpointDc : s.roomDc, dtS, TAU_S));

  if (s.scenario.answering) s.ok += 1; else { s.failed += 1; s.overdue += 1; }

  // Boost timer.
  if (s.boostActive) {
    s.boostRemainingS = Math.max(0, s.boostRemainingS - dtS);
    if (s.boostRemainingS === 0) s.boostActive = false;
  }

  // Watchdog: in HA mode the simulated HA is "blind" (never writes), so overdue climbs.
  if (s.scenario.mode === "ha") s.watchdogOverdueS += dtS;
  else s.watchdogOverdueS = 0;

  applyLadder(s, dtS);
  return s;
}

// The control ladder (V2 spec §2), reduced to what the UI shows. Order matters: the first
// matching branch wins, mirroring the executor's precedence.
function applyLadder(s: BoilerState, dtS: number): void {
  let state: ControlState; let reason: ControlReason = "none"; let cause: ControlCause = "none";
  if (!s.scenario.heatingSeason) { state = "season_off"; }
  else if (s.scenario.failsafe) {
    state = "failsafe";
    reason = s.scenario.mode === "ha" ? "fs_blind" : "fs_disarmed";
    cause = s.scenario.mode === "ha" ? "watchdog" : "none";
    if (s.controlState !== "failsafe") { s.failsafeCount += 1; s.lastFailsafeDurationS = 0; }
    s.lastFailsafeDurationS += dtS; // integrate real elapsed seconds, not a flat +1 per tick
  }
  else if (s.boostActive) { state = "boost"; }
  else if (s.scenario.mode === "ha") {
    if (s.watchdogOverdueS >= s.config.watchdog_s) { state = "failsafe"; reason = "fs_blind"; cause = "watchdog"; }
    else if (s.heldSetpointDc <= 0) { state = "ha_waiting"; reason = "await_setpoint"; }
    else { state = "ha"; }
  }
  else { state = "local"; }
  s.controlState = state; s.reason = reason; s.cause = cause;
}
