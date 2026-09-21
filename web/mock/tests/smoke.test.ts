// web/mock/tests/smoke.test.ts
import { eq, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";

const s = initialState();
eq(s.scenario.provisioned, true, "default is provisioned");
eq(s.scenario.passwordSet, false, "default has no password");
eq(s.scenario.mode, "local", "default mode is local");
eq(s.heldSetpointDc, s.config.local_ch_setpoint_dc, "held setpoint seeded from local setpoint");
eq(typeof s.uptimeMs, "number", "uptime is a number");

report("mock/smoke");
