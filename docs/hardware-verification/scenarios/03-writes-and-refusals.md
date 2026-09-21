# 03 — Writes and refusals

This is the first scenario that **changes a setting inside the boiler.** It proves two things:
that a write actually reaches the appliance and comes back changed (not just echoed by the
state model), and that every refusal refuses the *right* thing for the *right* reason with the
*right* code — a decision made in the command layer, never on the HTTP surface.

Have the curl harness and the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md) running. The registry and state must already
be populated and settled — walk [`02-boiler-conversation.md`](02-boiler-conversation.md) first,
then **wait ~3 minutes after boot** before starting here: the boiler's own bounds arrive with
the first ID 48 read (≈ a minute), and unsupported IDs are marked only after two full poll-ring
passes.

> **SAFETY.** One item here (the line test, §5–§6) deliberately stops the bus for ~20 s and the
> boiler *will* demand heat during that window. It is the single legitimate exception to the
> never-silent invariant in [00-setup §2](00-setup-and-safety.md) — everywhere else in this
> file the bus must keep turning. Do not run the line test on a hot afternoon and do not leave
> it looping.

## 0. Before touching anything

- [ ] Record the current DHW setpoint so you can restore it in §7:
      `otget http://$OT/api/state | jq '.state.dhw_setpoint'` — expect
      `{"availability":"ok","value":60,…}` (or your unit's value).

## 1. A write reaches the boiler and comes back changed

- [ ] `curl -sS -i -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"value":50}' http://$OT/api/entities/dhw_setpoint`
      → **`202 Accepted`**, body `{"queued":{"data_id":56,"raw":12800}}`.
      `202` not `200` because the frame is *queued*, not yet confirmed by the boiler; `data_id:56`
      is the right ID; `raw:12800` is 50.0 in f8.8 (50 × 256).
- [ ] **A minute later** the read comes back changed:
      `otget http://$OT/api/state | jq '.state.dhw_setpoint.value'` → **`50`**.

> If it reads `50` *immediately*, the state model is echoing the request rather than reporting a
> READ-ACK from the boiler. If it stays old after two minutes while `age_ms` grows, the queued
> write was evicted (the queue holds one write) — repeat with nothing else touching the device.
> If the boiler's own display disagrees with `/api/state`, believe the display and check the
> boiler's min-flow parameter (`E`) and its service menu.

> **DO NOT** "tidy" the `202` into a `200`. A `200` would assert the boiler accepted the value;
> only the READ-ACK a minute later proves that.

## 2. A value above the boiler's own ceiling is refused: 422

- [ ] `curl -sS -i -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"value":70}' http://$OT/api/entities/dhw_setpoint`
      → **`422 Unprocessable Content`**, body `value out of range`.
- [ ] Cross-check the live bounds:
      `otget http://$OT/api/entities/dhw_setpoint | jq '.meta.min, .meta.max'` → **40** / **65**.

> 70 is inside the static table (30…80) but outside *this* boiler's ID 48 range (65/40). A `422`
> here is the only place the boiler's own answer is proven to displace the table constants on the
> write path. A `202` instead means the boiler's bounds were not consulted (ID 48 not read yet,
> or the write path ignores the source). A `400` means the body did not parse — check the
> quoting.

## 3. A read-only entity refuses the method: 405

- [ ] `curl -sS -i -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"value":50}' http://$OT/api/entities/flow_temperature`
      → **`405 Method Not Allowed`**, body `entity is read-only`.

> `405`, not `403`: writing the flow temperature was never allowed *anyone* — it is the wrong
> method on that address, and the same address reads fine. A `403` would be an access-policy
> decision, which this is not. A `202` would mean the flow-temperature entity was marked writable
> in the entity table — a real danger, since the read/write direction comes straight from there.

## 4. An entity this boiler does not implement: 409

- [ ] First confirm the premise:
      `otget http://$OT/api/state | jq '.state.max_ch_setpoint.availability'` must be
      **`"unsupported"`** (this boiler answers UNKNOWN-DATAID to ID 57 — see
      [00-setup §6](00-setup-and-safety.md)). If it reads `"unknown"`, the mark is not set yet;
      wait two ring passes.
- [ ] `curl -sS -i -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"value":70}' http://$OT/api/entities/max_ch_setpoint`
      → **`409 Conflict`**, body `boiler does not support this data-id`.

> `409`, not `404`: ID 57 *is* in the registry — it is the boiler that does not implement it, a
> conflict between what was asked and what this appliance can do. This is a fact about the unit; a
> boiler that implements ID 57 answers `202`. Do not "fix" it.

## 5. Rights on a device with NO password (the item no host test covers)

The wire from the operations endpoint to the rights check exists only on the device. Clear the
interface password (Settings) or use a fresh device for this section.

- [ ] Confirm there is no password: `curl -sS -o /dev/null -w '%{http_code}\n' http://$OT/api/status`
      (no `-u`) → **`200`** (a `401` means one is still set).
- [ ] `curl -sS -i -X POST -H 'Content-Type: application/json' -d '{}' http://$OT/api/ops/linetest`
      → **`403 Forbidden`**, body `operation halts the boiler conversation; set a password first`.
- [ ] `curl -sS -i -X POST -H 'Content-Type: application/json' -d '{"from":0,"to":127}' http://$OT/api/ops/scan`
      → **`202 Accepted`**, body `{"running":true}`.

> The asymmetry is the point. The line test **stops the master** — and by
> [00-setup §2](00-setup-and-safety.md) a silent master makes the boiler demand heat, so a
> password-less line test would let anyone on the network heat the house indefinitely. It
> therefore **always** requires a password. The scan keeps ID 0 going on every other step, so it
> halts nothing and needs no password. A `202` from a password-less line test is exactly the
> defect this catches; a `403` from the scan is the opposite error.

- [ ] **Put the password back before continuing.**

## 6. A malformed operation refused as malformed, a busy one as busy (password on)

- [ ] `curl -sS -i -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"half_period_ms":10}' http://$OT/api/ops/linetest`
      → **`422`**, body `half_period_ms outside 100..5000` (10 ms is below the bus limit; the
      malformed request must be refused as malformed, not as "already running").
- [ ] Start a run, then start another while it runs:
      `curl … -u ":$OTPASS" … -d '{}' http://$OT/api/ops/linetest` → **`202 Accepted`**;
      `sleep 2`; the same command again → **`409 Conflict`**, body `line test already running`.
- [ ] While it runs, the OT OUT line toggles once every two seconds — visible on a multimeter
      across the boiler terminals.
- [ ] After ~20 s the bus resumes on its own — `cycles` in the monitor grows again with no
      request.

> **SAFETY.** The boiler will demand heat during those ~20 s. Expected and unavoidable — that is
> what the line test does.

## 7. Put the boiler back

- [ ] Write back the DHW setpoint recorded in §0 (substitute the actual number):
      `curl -sS -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"value":60}' http://$OT/api/entities/dhw_setpoint`.
- [ ] A minute later `/api/state` shows it again.
- [ ] The interface password is set again (§5 removed it).

## Notes

- The `raw:12800` for 50.0 and the `40`/`65` DHW bounds are this unit's f8.8 encoding and this
  boiler's ID 48 answer; re-derive them for another appliance.
- This scenario does not exercise the live web page or the WebSocket ticket
  ([`04-live-web-and-ui.md`](04-live-web-and-ui.md)), nor asking the boiler for heat via ID 1
  ([`05-asking-for-heat.md`](05-asking-for-heat.md)), nor precedence between writers.
