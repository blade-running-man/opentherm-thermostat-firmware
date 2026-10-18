// web/mock/plugin.ts
// The Vite dev-server adapter. Active only when added to the plugins array (mode "mock"). Owns
// one in-memory BoilerState, ticks it every second, serves /api/* via the pure router, and
// pushes state over /ws using a hand-rolled RFC 6455 handshake (no `ws` dependency; push-only).
import type { Plugin, ViteDevServer } from "vite";
import type { IncomingMessage, ServerResponse } from "node:http";
import type { Socket } from "node:net";
import { initialState, type BoilerState, type MockRequest } from "./model.ts";
import { tick } from "./evolve.ts";
import { handle, type Deps } from "./router.ts";
import { renderWsValues } from "./entities.ts";
import { TicketStore, stateFrame, deltaFrame, pingFrame, diffValues } from "./ws.ts";
import { acceptKey, encodeText } from "./wsframe.ts";

type Scalar = number | boolean | string | null;

export function mockPlugin(): Plugin {
  const state: BoilerState = initialState();
  const tickets = new TicketStore();
  const deps: Deps = { tickets, nowMs: () => state.uptimeMs };
  const sockets = new Set<{ socket: Socket; last: Record<string, Scalar> }>();

  return {
    name: "ot-mock-boiler",
    apply: "serve",
    configureServer(server: ViteDevServer) {
      const timer = setInterval(() => {
        Object.assign(state, tick(state, 1000));
        const next = renderWsValues(state);
        for (const c of sockets) {
          try {
            const d = diffValues(c.last, next);
            const frame = Object.keys(d).length ? deltaFrame(d) : pingFrame();
            c.socket.write(encodeText(frame));
            c.last = next;
          } catch { sockets.delete(c); }
        }
      }, 1000);
      server.httpServer?.on("close", () => clearInterval(timer));

      server.middlewares.use((req: IncomingMessage, res: ServerResponse, next: () => void) => {
        const url = new URL(req.url ?? "/", "http://localhost");
        if (!url.pathname.startsWith("/api/") && !url.pathname.startsWith("/__mock/") && url.pathname !== "/update") {
          return next();
        }
        const chunks: Buffer[] = [];
        req.on("data", (c: Buffer) => chunks.push(c));
        req.on("end", () => {
          let body: unknown = undefined;
          if (chunks.length) { try { body = JSON.parse(Buffer.concat(chunks).toString("utf8")); } catch { body = null; } }
          const headers: Record<string, string> = {};
          for (const [k, v] of Object.entries(req.headers)) headers[k.toLowerCase()] = Array.isArray(v) ? v.join(",") : String(v ?? "");
          const query: Record<string, string> = {};
          url.searchParams.forEach((v, k) => { query[k] = v; });
          const mreq: MockRequest = { method: (req.method ?? "GET").toUpperCase(), path: url.pathname, query, headers, body };
          const r = handle(mreq, state, deps);
          res.statusCode = r.status;
          res.setHeader("Content-Type", "application/json");
          if (r.headers) for (const [k, v] of Object.entries(r.headers)) res.setHeader(k, v);
          res.end(JSON.stringify(r.body ?? {}));
        });
      });

      server.httpServer?.on("upgrade", (req: IncomingMessage, socket: Socket) => {
        const url = new URL(req.url ?? "/", "http://localhost");
        if (url.pathname !== "/ws") return; // Vite's own HMR socket handles the rest
        const ticket = url.searchParams.get("ticket") ?? "";
        if (!tickets.redeem(ticket, state.uptimeMs)) {
          socket.write("HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n"); socket.destroy(); return;
        }
        const key = req.headers["sec-websocket-key"];
        if (typeof key !== "string") { socket.destroy(); return; }
        socket.write(
          "HTTP/1.1 101 Switching Protocols\r\n" +
          "Upgrade: websocket\r\nConnection: Upgrade\r\n" +
          `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`,
        );
        const values = renderWsValues(state);
        socket.write(encodeText(stateFrame(values)));
        const entry = { socket, last: values };
        sockets.add(entry);
        socket.on("close", () => sockets.delete(entry));
        socket.on("error", () => sockets.delete(entry));
        socket.on("data", () => { /* SPA is push-only; ignore inbound frames */ });
      });

      server.config.logger.info("  ➜  Mock boiler simulator active on /api and /ws");
    },
  };
}
