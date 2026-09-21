// web/mock/scenarios.ts
// The dev-only scenario surface: POST /__mock/scenario patches the Scenario knobs, GET returns
// them. NOT part of the firmware contract — it exists so the SPA's edge states (password set,
// HA mode, boiler fault, silent bus, broker down, failsafe) can be forced without hardware.
import type { BoilerState, MockResponse, Scenario } from "./model.ts";

const KNOBS: (keyof Scenario)[] = [
  "provisioned", "passwordSet", "password", "mode", "heatingSeason",
  "boilerFault", "answering", "brokerUp", "failsafe",
];

export function applyScenario(s: BoilerState, patch: Record<string, unknown>): MockResponse {
  for (const k of Object.keys(patch)) {
    if (!KNOBS.includes(k as keyof Scenario)) return { status: 400, body: { error: `unknown knob: ${k}` } };
  }
  for (const [k, v] of Object.entries(patch)) (s.scenario as unknown as Record<string, unknown>)[k] = v;
  // Reflect knobs the model/config also read.
  s.config.heating_season = s.scenario.heatingSeason;
  s.config.control_mode = s.scenario.mode === "ha" ? 1 : 0;
  return { status: 200, body: { ok: true, scenario: s.scenario } };
}

export function readScenario(s: BoilerState): MockResponse { return { status: 200, body: s.scenario }; }
