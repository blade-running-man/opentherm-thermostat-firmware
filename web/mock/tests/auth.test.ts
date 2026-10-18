// web/mock/tests/auth.test.ts
import { eq, report } from "../../src/pages/settings/tests/harness.ts";
import { checkPolicy, parseBasic, csrfOk, type PolicyCtx } from "../auth.ts";

const open: PolicyCtx = { provisioned: true, passwordSet: false, authenticated: false };
eq(checkPolicy("GET", "/api/state", open), "allow", "GET allowed when no password");
eq(checkPolicy("POST", "/api/config", open), "allow", "bootstrap POST config allowed no-password");
eq(checkPolicy("POST", "/api/entities/ch_setpoint", open), "allow", "bootstrap entity write allowed");

const locked: PolicyCtx = { provisioned: true, passwordSet: true, authenticated: false };
eq(checkPolicy("GET", "/api/state", locked), 401, "GET needs auth when password set");
const authed: PolicyCtx = { provisioned: true, passwordSet: true, authenticated: true };
eq(checkPolicy("GET", "/api/state", authed), "allow", "authenticated GET allowed");
eq(checkPolicy("POST", "/api/config", authed), "allow", "authenticated POST allowed");
eq(checkPolicy("POST", "/api/config", locked), 401, "unauth POST when password set => 401");

const unprov: PolicyCtx = { provisioned: false, passwordSet: false, authenticated: false };
eq(checkPolicy("POST", "/api/provision", unprov), "allow", "provision allowed when unprovisioned");
eq(checkPolicy("POST", "/api/config", unprov), 403, "other writes forbidden when unprovisioned");
eq(checkPolicy("GET", "/api/state", unprov), "allow", "GET allowed unprovisioned no-password");

// bus-halting op always needs a password.
eq(checkPolicy("POST", "/api/ops/linetest", open), 403, "linetest forbidden without a password");
eq(checkPolicy("POST", "/api/ops/scan", open), "allow", "scan allowed without password (bootstrap)");

// non-POST write methods are 403 in every state (a true ot_policy mirror).
eq(checkPolicy("DELETE", "/api/config", open), 403, "DELETE bootstrap write => 403");
eq(checkPolicy("PUT", "/api/config", open), 403, "PUT bootstrap write => 403");
eq(checkPolicy("PUT", "/api/state", authed), 403, "PUT never allowed even authenticated");
eq(checkPolicy("DELETE", "/api/provision", unprov), 403, "DELETE provision when unprovisioned => 403");
eq(checkPolicy("PATCH", "/api/config", authed), 403, "PATCH => 403");

eq(parseBasic("Basic " + Buffer.from("admin:admin").toString("base64"), "admin"), true, "correct basic");
eq(parseBasic("Basic " + Buffer.from("admin:nope").toString("base64"), "admin"), false, "wrong basic");
eq(parseBasic(undefined, "admin"), false, "missing header");
eq(parseBasic("Bearer x", "admin"), false, "non-basic scheme");

eq(csrfOk("POST", true, "application/json"), true, "json body ok");
eq(csrfOk("POST", true, "text/plain"), false, "non-json body rejected");
eq(csrfOk("POST", true, "application/json; charset=utf-8"), true, "json with charset ok");
eq(csrfOk("GET", false, undefined), true, "GET no body ok");
eq(csrfOk("POST", false, undefined), true, "bodyless POST ok (e.g. ws-ticket)");

report("mock/auth");
