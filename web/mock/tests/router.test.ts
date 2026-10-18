// web/mock/tests/router.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { initialState } from "../model.ts";
import { handle, type Deps } from "../router.ts";
import { TicketStore } from "../ws.ts";

const deps: Deps = { tickets: new TicketStore(), nowMs: () => 1000 };
const g = (method: string, path: string, extra: Partial<Parameters<typeof handle>[0]> = {}) =>
  handle({ method, path, query: {}, headers: {}, body: undefined, ...extra }, initialState(), deps);

eq(g("GET", "/api/state").status, 200, "state ok");
eq(g("GET", "/api/entities").status, 200, "entities ok");
eq(g("GET", "/api/control").status, 200, "control ok");
eq(g("GET", "/api/log").status, 200, "log ok");
eq(g("GET", "/api/ot/raw").status, 200, "ot/raw ok");
eq(g("GET", "/api/config").status, 200, "config ok");
eq(g("GET", "/api/wifi/scan").status, 200, "wifi scan ok");
eq(g("GET", "/api/entities/ch_setpoint").status, 200, "single entity ok");
eq(g("GET", "/api/entities/nope").status, 404, "unknown entity 404");
eq(g("GET", "/__mock/scenario").status, 200, "scenario read ok");

const tk = g("POST", "/api/ws-ticket", { headers: {} });
eq(tk.status, 200, "ticket issued");
ok(/^[0-9a-f]{32}$/.test((tk.body as { ticket: string }).ticket), "ticket shape");

// CSRF: POST with a body but wrong content-type => 415.
const s1 = initialState();
const badct = handle({ method: "POST", path: "/api/config", query: {}, headers: {}, body: { device_name: "x" } }, s1, deps);
eq(badct.status, 415, "missing json content-type => 415");

// Password set => GET needs auth (401 + WWW-Authenticate).
const s2 = initialState(); s2.scenario.passwordSet = true;
const locked = handle({ method: "GET", path: "/api/state", query: {}, headers: {}, body: undefined }, s2, deps);
eq(locked.status, 401, "locked GET is 401");
ok((locked.headers?.["WWW-Authenticate"] ?? "").includes("Basic"), "401 carries WWW-Authenticate");
const authHeader = "Basic " + Buffer.from("admin:admin").toString("base64");
const okAuth = handle({ method: "GET", path: "/api/state", query: {}, headers: { authorization: authHeader }, body: undefined }, s2, deps);
eq(okAuth.status, 200, "authenticated GET allowed");

// entity write with json content-type => 202 and reflected in the model.
const s3 = initialState();
const w = handle({ method: "POST", path: "/api/entities/ch_setpoint", query: {}, headers: { "content-type": "application/json" }, body: { value: 60 } }, s3, deps);
eq(w.status, 202, "write accepted");
eq(s3.heldSetpointDc, 600, "write reflected in model");

// entity write missing value => 400.
const s3b = initialState();
eq(handle({ method: "POST", path: "/api/entities/ch_setpoint", query: {}, headers: { "content-type": "application/json" }, body: {} }, s3b, deps).status, 400, "missing value => 400");

// ops route.
const s5 = initialState();
eq(handle({ method: "POST", path: "/api/ops/boost", query: {}, headers: { "content-type": "application/json" }, body: { setpoint: 60, minutes: 30 } }, s5, deps).status, 202, "boost op ok");
eq(handle({ method: "POST", path: "/api/ops/nope", query: {}, headers: { "content-type": "application/json" }, body: {} }, s5, deps).status, 404, "unknown op 404");

// scenario endpoint via router.
const s4 = initialState();
const sc = handle({ method: "POST", path: "/__mock/scenario", query: {}, headers: { "content-type": "application/json" }, body: { mode: "ha" } }, s4, deps);
eq(sc.status, 200, "scenario applied via router");
eq(s4.scenario.mode, "ha", "scenario reflected");

// provision. Provision is policy-legal only on an UNPROVISIONED device: once provisioned, the
// firmware policy (ot_http_policy.c lines 80-114, mirrored in auth.ts) refuses re-homing without a
// password — 403, not 401, since no credential exists to satisfy it. The default mock state is
// provisioned, so this exercises the real setup flow from the one state that allows it.
const s6 = initialState(); s6.scenario.provisioned = false;
const prov = handle({ method: "POST", path: "/api/provision", query: {}, headers: { "content-type": "application/json" }, body: { wifi_ssid: "net" } }, s6, deps);
eq(prov.status, 200, "provision ok");
eq(s6.scenario.provisioned, true, "provision flips provisioned true");

// unknown route => 404.
eq(g("GET", "/api/nope").status, 404, "unknown route 404");
eq(g("PUT", "/api/state").status, 403, "PUT is not allowed => 403 (policy)");

report("mock/router");
