// web/mock/ws.ts
// Ticket store for the /ws handshake and the push-frame builders. Tickets are 32 hex chars,
// single-use, 30 s TTL, capped at 14 live slots (mirrors OT_TICKET_* in the firmware). The push
// frames match ot_api_render_frame: {"type":"state"|"delta"|"ping","values":{...}} (no ping values).
import { randomTicket } from "./wsframe.ts";

export const OT_TICKET_TTL_MS = 30000;
const OT_TICKET_SLOTS = 14;

interface Slot { ticket: string; expiresAtMs: number; }

export class TicketStore {
  private slots: Slot[] = [];
  issue(nowMs: number): { ticket: string; ttl_ms: number } {
    this.slots = this.slots.filter((s) => s.expiresAtMs > nowMs);
    if (this.slots.length >= OT_TICKET_SLOTS) this.slots.shift();
    const ticket = randomTicket();
    this.slots.push({ ticket, expiresAtMs: nowMs + OT_TICKET_TTL_MS });
    return { ticket, ttl_ms: OT_TICKET_TTL_MS };
  }
  redeem(ticket: string, nowMs: number): boolean {
    const i = this.slots.findIndex((s) => s.ticket === ticket);
    if (i < 0) return false;
    const slot = this.slots[i];
    this.slots.splice(i, 1); // single-use: spend it regardless
    return slot.expiresAtMs > nowMs;
  }
}

type Scalar = number | boolean | string | null;
export function stateFrame(values: Record<string, Scalar>): string { return JSON.stringify({ type: "state", values }); }
export function deltaFrame(values: Record<string, Scalar>): string { return JSON.stringify({ type: "delta", values }); }
export function pingFrame(): string { return JSON.stringify({ type: "ping" }); }

// Compute the changed keys between two flat value maps, for delta frames.
export function diffValues(prev: Record<string, Scalar>, next: Record<string, Scalar>): Record<string, Scalar> {
  const out: Record<string, Scalar> = {};
  for (const k of Object.keys(next)) if (prev[k] !== next[k]) out[k] = next[k];
  return out;
}
