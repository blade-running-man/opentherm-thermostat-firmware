// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The push transport: one socket to the device, reconnecting on its own.
//
// The design puts
// a single endpoint here -- `/ws` -- which fixes its contract: the
// full state on connect, deltas afterwards. Polling 60 entities over REST was the
// alternative and it is what this file exists to avoid.
//
// This file knows about sockets and nothing about boilers. Entity keys are opaque
// strings on purpose: CLAUDE.md forbids a second entity list, and giving the payload a
// hand-written shape here would be exactly that. `api/device.ts` holds the values, and
// `api/entities.ts` -- generated from tools/opentherm_ids.py -- will type them.

import { request } from "./client";

/**
 * One entity value on the wire, mirroring `ot_value_t`
 * (components/ot_state/include/ot_state.h).
 *
 * `null` means the entity exists and has no value to show: the device renders it for
 * every availability other than `ok` -- not asked yet, or the boiler answered
 * `data-invalid` (ID 5 with no faults, ID 27 with no outside sensor) or `unknown-dataid`.
 * DO NOT treat it as "delete this key": an entity that drops out of the model is a bug,
 * not a message, and deleting the key would make the UI render a gap where it should
 * render "--".
 */
export type StateValue = number | boolean | string | null;

/**
 * Frames from the device.
 *
 * `state` and `delta` carry the same shape -- a flat map of entity key to value, the same
 * values `GET /api/state` reports and not one field more -- and differ only in
 * completeness, so the client has one merge path for both. `state` arrives once per
 * connection and replaces everything; `delta` carries only what changed.
 *
 * The device sends nothing else on this socket, and this type is why: anything not shaped
 * like one of these three is dropped by isInboundFrame below, silently and for ever. The
 * firmware end is components/ot_http/ot_http_ws_push.c.
 *
 * `ping` exists so silence is distinguishable from a dead socket; see SILENCE_TIMEOUT_MS.
 */
export type InboundFrame =
  | { type: "state"; values: Record<string, StateValue> }
  | { type: "delta"; values: Record<string, StateValue> }
  | { type: "ping" };

// There is no outbound frame type, and that is the contract rather than an omission: this
// socket only ever receives. Commands go over REST, because a command accepted here would
// be an endpoint the REST API does not have -- the privileged path forbidden by the rule
// that the web UI has no handle of its own.
// See setEntity/runOperation in api/device.ts.

export type ConnectionStatus = "connecting" | "open" | "closed";

const WS_PATH = "/ws";
const TICKET_PATH = "/api/ws-ticket";

/**
 * The handshake ticket.
 *
 * `new WebSocket()` cannot set headers, so the UI password cannot be attached to the
 * handshake the way it is attached to every other request. The device therefore serves a
 * short-lived single-use ticket over the ordinary authenticated REST API, and the socket
 * presents it in the query string -- the only place a browser can put it.
 *
 * It is fetched on EVERY connect, reconnect included: the ticket is spent by the handshake
 * that used it (components/ot_ticket/ot_ticket.h), so reusing one is refused with 401.
 */
interface TicketResponse {
  ticket: string;
  ttl_ms: number;
}

async function fetchTicket(): Promise<string> {
  const answer = await request<TicketResponse>(TICKET_PATH, { method: "POST" });
  // The device is contracted to answer 32 hex characters. Checking is not paranoia: an
  // empty string would open `/ws?ticket=` and fail the handshake with no way to tell that
  // from a network fault, and the reconnect loop would hide it for ever.
  if (typeof answer?.ticket !== "string" || answer.ticket === "") {
    throw new Error("the device issued no ticket");
  }
  return answer.ticket;
}

const BACKOFF_MIN_MS = 1_000;
const BACKOFF_MAX_MS = 30_000;

// The device is contracted to send something at least every 15 s -- a delta if anything
// changed, a `ping` if not. A socket that goes quiet for longer than this is treated as
// dead and reopened.
//
// This exists because a TCP connection dropped by a NAT box or a sleeping AP stays "open"
// to the browser indefinitely: no `close` event fires and the UI silently shows stale
// readings. DO NOT lower this below roughly twice the device's keepalive interval, or a
// single late frame will cause a reconnect loop.
const SILENCE_TIMEOUT_MS = 35_000;

function socketUrl(ticket: string): string {
  const url = new URL(WS_PATH, location.href);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  url.searchParams.set("ticket", ticket);
  return url.toString();
}

/**
 * True for anything shaped like a frame we could act on.
 *
 * JSON.parse succeeding proves nothing: `"hello"`, `42` and `{}` all parse cleanly and
 * none of them is a frame. Without this check the InboundFrame type is a claim the
 * runtime does not keep, and a consumer that trusts `frame.values` reads undefined.
 */
function isInboundFrame(value: unknown): value is InboundFrame {
  if (typeof value !== "object" || value === null) return false;
  const type = (value as { type?: unknown }).type;
  if (type === "ping") return true;
  if (type !== "state" && type !== "delta") return false;
  const values = (value as { values?: unknown }).values;
  return typeof values === "object" && values !== null;
}

export interface DeviceSocketHandlers {
  onFrame: (frame: InboundFrame) => void;
  onStatus: (status: ConnectionStatus) => void;
}

/**
 * A self-healing connection to `/ws`.
 *
 * Owns exactly one socket at a time and reopens it with exponential backoff. Callers do
 * not reconnect, retry or inspect the socket -- they call start() once and read frames.
 */
export class DeviceSocket {
  private handlers: DeviceSocketHandlers;
  private socket: WebSocket | null = null;
  private retryMs = BACKOFF_MIN_MS;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private silenceTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = true;
  // `socket` is only assigned after the ticket request resolves, so it cannot be the
  // guard against a second connect() -- a visibilitychange landing mid-request would
  // see null and open a parallel socket. This flag covers the gap.
  private connecting = false;

  constructor(handlers: DeviceSocketHandlers) {
    this.handlers = handlers;
  }

  start(): void {
    if (!this.stopped) return;
    this.stopped = false;
    document.addEventListener("visibilitychange", this.onVisibilityChange);
    void this.connect();
  }

  stop(): void {
    this.stopped = true;
    document.removeEventListener("visibilitychange", this.onVisibilityChange);
    this.clearTimers();
    // Detach before closing: the handler would otherwise schedule a reconnect for a
    // socket we are deliberately shutting down.
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      socket.onclose = null;
      socket.close();
    }
    this.handlers.onStatus("closed");
  }

  private async connect(): Promise<void> {
    if (this.stopped || this.socket || this.connecting) return;
    this.connecting = true;
    this.handlers.onStatus("connecting");
    try {
      const ticket = await fetchTicket();
      // stop() may have run while the ticket request was in flight. Opening now would
      // leave a socket nobody owns and nobody closes.
      if (this.stopped) return;
      this.openSocket(ticket);
    } catch {
      // A refused or unanswered ticket is exactly as fatal to this connection as a refused
      // handshake, and it is reported the same way: the device is not reachable right now.
      // Backoff applies, so a device that is off does not turn into a request flood.
      this.handlers.onStatus("closed");
      this.scheduleReconnect();
    } finally {
      this.connecting = false;
    }
  }

  private openSocket(ticket: string): void {
    const socket = new WebSocket(socketUrl(ticket));
    this.socket = socket;

    socket.onopen = () => {
      this.retryMs = BACKOFF_MIN_MS;
      this.handlers.onStatus("open");
      this.armSilenceTimer();
    };

    socket.onmessage = (event: MessageEvent<string>) => {
      this.armSilenceTimer();
      let parsed: unknown;
      try {
        parsed = JSON.parse(event.data);
      } catch {
        return;
      }
      // A malformed or unrecognised frame is the device's bug, and dropping it is right:
      // the next full state on reconnect repairs whatever this one would have carried.
      // The silence timer was already restarted above, so a device emitting only garbage
      // still counts as alive -- it is reachable, just wrong, and reconnecting in a loop
      // would not improve it.
      if (!isInboundFrame(parsed)) return;
      this.handlers.onFrame(parsed);
    };

    socket.onclose = () => {
      this.socket = null;
      this.clearTimers();
      this.handlers.onStatus("closed");
      this.scheduleReconnect();
    };

    // `error` is always followed by `close`, so reconnecting is left to onclose alone --
    // handling both would double the backoff on every failure.
    socket.onerror = () => {};
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.reconnectTimer) return;
    // Jitter keeps several tabs from reconnecting in lockstep, which matters here more
    // than usual: the device serves seven client sessions in total, sockets holding /ws
    // and ordinary fetches alike (OT_HTTP_MAX_CLIENTS, components/ot_http/ot_http_internal.h).
    const delay = this.retryMs * (0.5 + Math.random() * 0.5);
    this.retryMs = Math.min(this.retryMs * 2, BACKOFF_MAX_MS);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      void this.connect();
    }, delay);
  }

  private armSilenceTimer(): void {
    if (this.silenceTimer) clearTimeout(this.silenceTimer);
    this.silenceTimer = setTimeout(() => {
      // close() rather than reconnecting here: onclose is the single place that reopens,
      // and routing through it keeps the backoff and status reporting in one path.
      this.socket?.close();
    }, SILENCE_TIMEOUT_MS);
  }

  private clearTimers(): void {
    if (this.silenceTimer) clearTimeout(this.silenceTimer);
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.silenceTimer = null;
    this.reconnectTimer = null;
  }

  // A laptop that wakes from sleep finds a dead socket and, with backoff already at 30 s,
  // would leave the user looking at stale readings for half a minute. Coming back to the
  // tab is a strong signal that someone is about to read it, so the wait is reset.
  private onVisibilityChange = (): void => {
    if (this.stopped || document.hidden || this.socket || this.connecting) return;
    this.retryMs = BACKOFF_MIN_MS;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    void this.connect();
  };
}
