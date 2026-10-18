// web/mock/tests/control.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";
import { getControl, runOp, getOtRaw } from "../control.ts";

const s = initialState();
const doc = getControl(s).body as Record<string, unknown>;
eq(doc.schema, 1, "control schema is 1");
eq(doc.mode, "local", "mode projected");
eq((doc.boost as { active: boolean }).active, false, "boost inactive by default");
ok(doc.held_setpoint_dc === s.heldSetpointDc, "held setpoint projected in tenths");

const rb = runOp(s, "boost", { setpoint: 65, minutes: 30 }, 0);
eq(rb.status, 202, "boost accepted in local mode");
eq(s.boostActive, true, "boost active after op");
eq(s.boostSetpointDc, 650, "boost setpoint in tenths");

const h = initialState(); h.scenario.mode = "ha";
eq(runOp(h, "boost", { setpoint: 65, minutes: 30 }, 0).status, 409, "boost refused in HA mode");

eq(runOp(s, "boost", { setpoint: 65 }, 0).status, 422, "boost needs both params");

eq(runOp(s, "boost_off", {}, 0).status, 202, "boost_off ok");
eq(s.boostActive, false, "boost cleared");

eq(runOp(s, "linetest", { duration_ms: 20000, half_period_ms: 2000 }, 1000).status, 202, "linetest starts");
eq(runOp(s, "linetest", { duration_ms: 20000, half_period_ms: 2000 }, 1500).status, 409, "linetest already running");

eq(runOp(s, "scan", { from: 0, to: 127 }, 0).status, 202, "scan starts");
const raw = getOtRaw(s).body as { scan_total: number; ids: unknown[]; answering: boolean };
ok(raw.scan_total > 0, "scan total set");
ok(Array.isArray(raw.ids), "ot/raw has ids array");

eq(runOp(s, "nope", {}, 0).status, 404, "unknown op 404");

report("mock/control");
