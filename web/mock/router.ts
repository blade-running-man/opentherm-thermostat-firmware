// web/mock/router.ts
// The single pure dispatcher. Transport-agnostic: it takes a parsed MockRequest and the mutable
// BoilerState and returns a MockResponse. plugin.ts adapts Node req/res to this. Gate order
// mirrors the firmware: CSRF (415) before policy (401/403), then routing. The DNS-rebind Host
// gate is intentionally skipped in the mock (dev, localhost only).
import type { BoilerState, MockRequest, MockResponse } from "./model.ts";
import { checkPolicy, parseBasic, csrfOk, type PolicyCtx } from "./auth.ts";
import { renderState, renderEntities, renderEntity, writeEntity } from "./entities.ts";
import { getConfig, postConfig, provision, wifiScan, getLog } from "./config.ts";
import { getControl, runOp, getOtRaw } from "./control.ts";
import { applyScenario, readScenario } from "./scenarios.ts";
import { TicketStore } from "./ws.ts";

export interface Deps { tickets: TicketStore; nowMs: () => number; }

function json(status: number, body: unknown): MockResponse { return { status, body }; }

export function handle(req: MockRequest, s: BoilerState, deps: Deps): MockResponse {
  const { method, path } = req;
  const hasBody = req.body !== undefined;

  // Mock-only dev/test control channel. NOT a firmware route, so it is deliberately routed
  // BEFORE the firmware CSRF/auth gates: a test must be able to flip scenario knobs (including
  // scenario.passwordSet and scenario.provisioned themselves) regardless of the auth state it
  // is simulating. Gating it would make the harness unable to reach the states it exists to test.
  if (path.startsWith("/__mock/")) {
    if (path === "/__mock/scenario") {
      if (method === "GET") return readScenario(s);
      if (method === "POST") return applyScenario(s, (req.body as Record<string, unknown>) ?? {});
    }
    return json(404, { error: "not found" });
  }

  // CSRF gate (415) — before auth, as the firmware does.
  if (!csrfOk(method, hasBody, req.headers["content-type"])) {
    return json(415, { error: "unsupported media type" });
  }

  // Auth context from the scenario + Authorization header.
  const authenticated = parseBasic(req.headers["authorization"], s.scenario.password);
  const ctx: PolicyCtx = {
    provisioned: s.scenario.provisioned, passwordSet: s.scenario.passwordSet, authenticated,
  };
  const verdict = checkPolicy(method, path, ctx);
  if (verdict === 401) {
    return { status: 401, headers: { "WWW-Authenticate": 'Basic realm="opentherm"' }, body: { error: "ticket required" } };
  }
  if (verdict === 403) return json(403, { error: "forbidden" });

  if (method === "GET") {
    if (path === "/api/state") return json(200, renderState(s));
    if (path === "/api/entities") return json(200, renderEntities(s));
    if (path.startsWith("/api/entities/")) return renderEntity(s, decodeURIComponent(path.slice("/api/entities/".length)));
    if (path === "/api/control") return getControl(s);
    if (path === "/api/ot/raw") return getOtRaw(s);
    if (path === "/api/config") return getConfig(s);
    if (path === "/api/wifi/scan") return wifiScan(s);
    if (path === "/api/log") return getLog(s);
    return json(404, { error: "not found" });
  }

  if (method === "POST") {
    if (path === "/api/ws-ticket") return json(200, deps.tickets.issue(deps.nowMs()));
    if (path === "/api/config") return postConfig(s, req.body);
    if (path === "/api/provision") return provision(s, req.body);
    if (path.startsWith("/api/entities/")) {
      const key = decodeURIComponent(path.slice("/api/entities/".length));
      const v = (req.body as { value?: number | boolean } | undefined)?.value;
      if (v === undefined) return json(400, { error: "missing value" });
      return writeEntity(s, key, v);
    }
    if (path.startsWith("/api/ops/")) {
      const name = decodeURIComponent(path.slice("/api/ops/".length));
      return runOp(s, name, (req.body as Record<string, unknown>) ?? {}, deps.nowMs());
    }
    if (path === "/update") return json(200, { ok: true }); // OTA stub
    return json(404, { error: "not found" });
  }

  return json(404, { error: "not found" });
}
