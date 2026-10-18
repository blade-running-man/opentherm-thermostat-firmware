// web/mock/tests/entities.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";
import { renderState, renderEntities, renderEntity, renderWsValues, writeEntity } from "../entities.ts";

const s = initialState();

const doc = renderState(s);
eq(doc.schema, 1, "state schema is 1");
eq(doc.state["control_mode"].value, "local", "enum renders option string not index");
eq(doc.state["ch_enable"].value, true, "switch renders boolean");
ok(doc.state["ch_setpoint"].availability === "ok", "ch_setpoint available");

// unavailable entity: value must be null. (member_id has no field in the model.)
eq(doc.state["member_id"].availability, "unknown", "unmodeled entity is unknown");
eq(doc.state["member_id"].value, null, "non-ok availability yields null value");

const list = renderEntities(s);
eq(list.schema, 1, "entities schema is 1");
const chSet = list.entities.find((e) => e.key === "ch_setpoint");
eq(chSet?.data_id, 1, "ch_setpoint data_id is 1 (snake_case)");

const flat = renderWsValues(s);
eq(typeof flat["ch_enable"], "boolean", "ws value is a bare scalar");

const okw = writeEntity(s, "ch_setpoint", 60);
eq(okw.status, 202, "in-range write is accepted");
ok("queued" in (okw.body as object) || "applied" in (okw.body as object), "202 body is queued/applied");

eq(writeEntity(s, "fault", true).status, 405, "read-only entity write is 405");
eq(writeEntity(s, "nope", 1).status, 404, "unknown entity write is 404");
eq(writeEntity(s, "ch_setpoint", 5).status, 422, "out-of-range write is 422 (min is 10)");

// The executor owns ch_enable/ch_setpoint once Home Assistant holds control.
const ha = initialState();
ha.scenario.mode = "ha";
eq(writeEntity(ha, "ch_setpoint", 55).status, 409, "ch_setpoint is HA-owned in HA mode");

const single = renderEntity(s, "ch_setpoint");
eq(single.status, 200, "single entity ok");
eq(renderEntity(s, "nope").status, 404, "unknown single entity 404");

report("mock/entities");
