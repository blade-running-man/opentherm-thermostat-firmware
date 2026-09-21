// web/mock/tests/scenarios.test.ts
import { eq, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";
import { applyScenario, readScenario } from "../scenarios.ts";

const s = initialState();
const r = applyScenario(s, { mode: "ha", passwordSet: true, boilerFault: true });
eq(r.status, 200, "scenario applied");
eq(s.scenario.mode, "ha", "mode switched");
eq(s.scenario.passwordSet, true, "password flag switched");
eq(s.scenario.boilerFault, true, "fault switched");
eq(s.config.control_mode, 1, "control_mode reflected to 1 for ha");
eq(s.config.heating_season, s.scenario.heatingSeason, "heating_season reflected to config");

eq(applyScenario(s, { nope: 1 } as unknown as Record<string, unknown>).status, 400, "unknown knob rejected");

const cur = readScenario(s).body as { mode: string };
eq(cur.mode, "ha", "readScenario reflects state");

report("mock/scenarios");
