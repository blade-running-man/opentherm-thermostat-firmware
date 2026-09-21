# ot_ticket — one-shot, short-lived tickets for the `/ws` handshake (pure, host-tested)

A tiny authorization mechanism for the WebSocket handshake: a browser cannot reliably
send an HTTP Basic `Authorization` header on `new WebSocket()`, so the socket instead
presents a ticket that was issued over an ordinary, password-protected REST endpoint
(`POST /api/ws-ticket`) and is redeemed exactly once at the handshake.

## Responsibility

Owns:
- A fixed-size, caller-owned table of ticket slots (`ot_ticket_table_t`).
- The issue / redeem / reset logic: placing a ready value, spending it exactly once, and
  the TTL and eviction rules.

Does NOT do:
- **Generate** ticket values. Randomness arrives as an argument (`esp_random()` on device,
  fixed in tests), so the table cannot invent tickets from itself.
- **Own storage.** Nothing is allocated; the table belongs to the caller.
- **Read the clock.** Time (`now_ms`) is passed in on every call — the component is pure.
- **Persist.** The table lives in RAM only and MUST NOT survive a reboot: a saved ticket
  would outlive a change of the interface password.
- **Enforce access.** What actually protects the endpoint is the access policy on
  `POST /api/ws-ticket` (same password as everything else, in `ot_http_policy.c`), not this
  table.

## Public API

All functions are pure over the passed-in table and clock. Failure is always a plain
`bool`; `false` from `redeem` means "do not open the socket" and never needs a reason.

| Symbol | Contract |
| --- | --- |
| `void ot_ticket_reset(ot_ticket_table_t *t)` | Zero the whole table (`memset` to 0). |
| `bool ot_ticket_issue(ot_ticket_table_t *t, const char *value, uint32_t now_ms)` | Put a ready `value` (must be exactly `OT_TICKET_LEN` chars) into a slot. `false` and table unchanged if malformed. A slot is free if empty or expired; if none is free, the oldest live ticket is evicted. Returns `true` on placement. |
| `bool ot_ticket_redeem(ot_ticket_table_t *t, const char *value, uint32_t now_ms)` | Spend a ticket: `true` exactly once per issued value and only within `OT_TICKET_TTL_MS` of issuance. Marks the slot not-live on success. Malformed value → `false`. |

Types and constants (`include/ot_ticket.h`):

- `OT_TICKET_SLOTS` = 14 — twice the server's client sessions (`OT_HTTP_MAX_CLIENTS` = 7):
  seven that can be in use at once plus seven for abandoned tickets awaiting TTL expiry.
- `OT_TICKET_LEN` = 32 — 128 bits of randomness in hex; unguessable within the TTL.
- `OT_TICKET_TTL_MS` = 30000 — 30-second lifetime.
- `ot_ticket_slot_t` — `{ char value[OT_TICKET_LEN + 1]; uint32_t issued_ms; bool live; }`.
- `ot_ticket_table_t` — `{ ot_ticket_slot_t s[OT_TICKET_SLOTS]; }`.

## Implementation

Key files:
- `include/ot_ticket.h` — the contract, plus the long rationale comments (why tickets
  exist, why the table size is the answer to overflow, why 14 slots).
- `ot_ticket.c` — the whole logic (~68 lines).
- `CMakeLists.txt` — `idf_component_register(SRCS "ot_ticket.c" INCLUDE_DIRS "include")`.

Lifecycle: `reset` → `issue(value)` places the value, stamps `issued_ms`, sets `live` →
`redeem(value)` matches a live, unexpired slot, clears `live` (one-shot), returns `true`.

Internal helpers:
- `expired(slot, now_ms)` — `(uint32_t)(now_ms - issued_ms) > OT_TICKET_TTL_MS`.
- `well_formed(value)` — non-NULL and `strlen == OT_TICKET_LEN`.

Invariants and details:
- **Overflow-safe timing.** Age is computed as unsigned subtraction `now_ms - issued_ms`,
  which stays correct across the 32-bit millisecond counter wrap (~49 days), both in
  `expired()` and in the eviction "oldest" comparison.
- **Issue slot choice.** First pass takes the first free (empty or expired) slot; only if
  all are live does it evict, choosing the largest age (oldest issue time). Eviction of the
  oldest LIVE ticket targets the one most likely abandoned, and beats refusing issuance
  (which would lock a legitimate owner out for the full 30 s).
- **Constant-time redeem.** The value comparison ORs together per-character XOR differences
  over all `OT_TICKET_LEN` bytes with no early exit. Deliberate: there is nothing meaningful
  to time here, but the habit costs nothing and avoids a pattern that is dangerous elsewhere.
- **One-shot.** A successful redeem sets `live = false`; a second redeem of the same value
  returns `false`.
- **Copy includes the terminator.** `issue` copies `OT_TICKET_LEN + 1` bytes (the NUL), so
  `value` must be a proper C string of exactly that length (enforced by `well_formed`).

DO NOT:
- Persist the table across reboots (a stale ticket would outlive a password change).
- Add a smarter eviction policy expecting it to prevent overflow — the table size, not the
  policy, is the protection; the policy only decides what to do when the table is full.

## Tests

No dedicated host suite is referenced within this component directory. The header states
the component is pure and host-tested (randomness and time injected as arguments); any
Unity suite lives under `test/` outside this directory and was not inspected.

## Notes

- Rationale for the whole mechanism: browsers do not reliably attach HTTP Basic auth to a
  WebSocket handshake, so a separate one-shot ticket bridges the same-password REST world to
  the socket.
- Purity is a security property here too: because randomness is an argument, the table
  cannot fabricate a valid ticket on its own.
- The header's own comment stresses that the endpoint's real guard is the access policy in
  `ot_http_policy.c` (same password as the rest of the API), not this table.
