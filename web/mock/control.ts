// web/mock/control.ts
// The executor's /api/control document, the four /api/ops, and the /api/ot/raw snapshot.
// Boost/linetest/scan mutate the model; refusals mirror the firmware (409 wrong mode / already
// running, 422 bad params). getOtRaw synthesises a plausible raw table from the driven Data-IDs.
import type { BoilerState, MockResponse } from "./model.ts";

export function getControl(s: BoilerState): MockResponse {
  const dhwSet = s.dhwEnable ? s.dhwSetpointDc : null;
  const statusHigh = (s.chEnable ? 0x01 : 0) | (s.dhwEnable ? 0x02 : 0);
  return {
    status: 200,
    body: {
      schema: 1, mode: s.scenario.mode, state: s.controlState, reason: s.reason, cause: s.cause,
      heating_season: s.scenario.heatingSeason, status_high: statusHigh,
      held_setpoint_dc: s.heldSetpointDc,
      dhw: { enable: s.dhwEnable, setpoint_dc: dhwSet },
      boost: s.boostActive
        ? { active: true, setpoint_dc: s.boostSetpointDc, remaining_s: Math.round(s.boostRemainingS) }
        : { active: false, setpoint_dc: null, remaining_s: null },
      failsafe: { count: s.failsafeCount, last_duration_s: Math.round(s.lastFailsafeDurationS) },
      watchdog_overdue_s: Math.round(s.watchdogOverdueS),
      stack_hwm: 4096,
    },
  };
}

export function runOp(s: BoilerState, name: string, params: Record<string, unknown>, nowMs: number): MockResponse {
  switch (name) {
    case "boost": {
      if (s.scenario.mode === "ha") return { status: 409, body: { error: "boost is a local action" } };
      if (!s.scenario.heatingSeason) return { status: 409, body: { error: "heating season is off" } };
      const setpoint = params.setpoint, minutes = params.minutes;
      if (typeof setpoint !== "number" || typeof minutes !== "number") {
        return { status: 422, body: { error: "boost needs both setpoint and minutes" } };
      }
      if (minutes < 1 || minutes > 480) return { status: 422, body: { error: "minutes outside 1..480", field: "minutes" } };
      const spDc = Math.round(setpoint * 10);
      if (spDc < s.config.flow_min_dc || spDc > s.config.flow_max_dc) {
        return { status: 422, body: { error: "setpoint outside flow bounds", field: "setpoint" } };
      }
      s.boostActive = true; s.boostSetpointDc = spDc; s.boostRemainingS = minutes * 60;
      s.heldSetpointDc = spDc;
      return { status: 202, body: { running: true } };
    }
    case "boost_off":
      s.boostActive = false; s.boostRemainingS = 0;
      return { status: 202, body: { running: true } };
    case "linetest": {
      if (nowMs < s.lineTestUntilMs) return { status: 409, body: { error: "line test already running" } };
      const dur = Number(params.duration_ms ?? 20000), half = Number(params.half_period_ms ?? 2000);
      if (dur < 1 || dur > 30000) return { status: 422, body: { error: "duration_ms outside 1..30000", field: "duration_ms" } };
      if (half < 100 || half > 5000) return { status: 422, body: { error: "half_period_ms outside 100..5000", field: "half_period_ms" } };
      s.lineTestUntilMs = nowMs + dur;
      return { status: 202, body: { running: true } };
    }
    case "scan": {
      const from = Number(params.from ?? 0), to = Number(params.to ?? 127);
      if (from < 0 || to > 127 || from > to) return { status: 422, body: { error: "scan range outside 0..127" } };
      s.scanTotal = to - from + 1; s.scanDone = s.scanTotal;
      return { status: 202, body: { running: true } };
    }
    default:
      return { status: 404, body: { error: "no such operation" } };
  }
}

export function getOtRaw(s: BoilerState): MockResponse {
  const ids = [
    { id: 0, type: "read-ack", raw: (s.chEnable ? 0x0100 : 0) | (s.flame ? 0x0008 : 0), age_ms: 400, count: s.cycles },
    { id: 1, type: "write-ack", raw: Math.round(s.heldSetpointDc / 10 * 256), age_ms: 400, count: s.cycles },
    { id: 25, type: "read-ack", raw: Math.round(s.flowDc / 10 * 256), age_ms: 400, count: s.cycles },
  ];
  return {
    status: 200,
    body: {
      uptime_ms: s.uptimeMs, cycles: s.cycles, ok: s.ok, failed: s.failed, overdue: s.overdue,
      answering: s.scenario.answering, in_duty: s.flame ? 60 : 5,
      scan_done: s.scanDone, scan_total: s.scanTotal, ids,
    },
  };
}
