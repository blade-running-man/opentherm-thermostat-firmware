// web/mock/auth.ts
// A pure mirror of ot_policy (V2 spec §5 / ot_http_policy.c). Decides allow/401/403 from
// (method, path, ctx). 401 => a credential could satisfy it (WWW-Authenticate); 403 => nothing
// can (wrong state). Bus-halting ops always demand a password.
export interface PolicyCtx { provisioned: boolean; passwordSet: boolean; authenticated: boolean; }
export type PolicyResult = "allow" | 401 | 403;

const BUS_HALTING_OPS = new Set(["/api/ops/linetest"]);
const BOOTSTRAP_WRITES = (p: string) =>
  p === "/api/config" || p === "/api/ws-ticket" ||
  p.startsWith("/api/entities/") || p.startsWith("/api/ops/");

export function checkPolicy(method: string, path: string, ctx: PolicyCtx): PolicyResult {
  const m = method.toUpperCase();
  if (m === "GET" || m === "HEAD") {
    if (!ctx.passwordSet) return "allow";
    return ctx.authenticated ? "allow" : 401;
  }
  // ot_http_policy.c refuses every non-POST write method outright (`if (method != POST) return
  // FORBIDDEN`). Mirror that: PUT/DELETE/PATCH are 403 in every state, never a write.
  if (m !== "POST") return 403;

  // A bus-halting op needs a password unconditionally.
  if (BUS_HALTING_OPS.has(path) && !ctx.passwordSet) return 403;

  if (!ctx.provisioned) {
    if (path !== "/api/provision") return 403;
    if (ctx.passwordSet) return ctx.authenticated ? "allow" : 401;
    return "allow";
  }
  // provisioned, no password: only the named bootstrap writes are allowed; else 403.
  if (!ctx.passwordSet) return BOOTSTRAP_WRITES(path) ? "allow" : 403;
  return ctx.authenticated ? "allow" : 401;
}

export function parseBasic(header: string | undefined, password: string): boolean {
  if (!header || !header.toLowerCase().startsWith("basic ")) return false;
  try {
    const decoded = Buffer.from(header.slice(6).trim(), "base64").toString("utf8");
    const idx = decoded.indexOf(":");
    if (idx < 0) return false;
    return decoded.slice(idx + 1) === password;
  } catch { return false; }
}

// CSRF gate: a non-GET/HEAD request WITH a body must be application/json, else 415.
export function csrfOk(method: string, hasBody: boolean, contentType: string | undefined): boolean {
  const m = method.toUpperCase();
  if (m === "GET" || m === "HEAD" || !hasBody) return true;
  const ct = (contentType ?? "").toLowerCase().split(";")[0].trim();
  return ct === "application/json";
}
