# 05 — Asking the boiler for heat

This is the scenario where the device raises the CH-enable bit for the first time and the boiler
acts on it. It proves the load-bearing safety invariant of the whole firmware: **the CH bit never
rises before the held ID 1 has gone out**, so the boiler is never asked to heat to a value nobody
chose. It also covers the boost, the boiler's own floor, and the fact that in Home Assistant mode
the local page owns none of these commands.

Have the curl harness and the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md) running. The device ships asking for **no
heat**: local mode, heating season off, CH switch off (see
[`01-first-bring-up.md`](01-first-bring-up.md)).

> **SAFETY — stand at the boiler for §1–§4.** Everything here can make the house heat. Do the
> first items with the boiler in sight, and not on a hot afternoon. The witness that something
> reached the boiler is the boiler's own answers on the wire, not a device log line.

## 1. Turning the heat on in local mode

- [ ] `curl -s -X POST http://$OT/api/entities/heating_season -d '{"value":1}'` → `202
      {"applied":{"command":N}}`; `GET /api/control` `state` becomes `local`.
- [ ] `curl -s -X POST http://$OT/api/entities/ch_setpoint -d '{"value":50}'` → `202`; within
      seconds an ID 1 write of 50.0 (`raw` 12800) appears in `/api/ot/raw`, then repeats
      ~every 10 s.
- [ ] **The invariant.** Starting with `ch_enable` off, change the setpoint to a value it is not
      already holding and raise CH back-to-back:
      ```sh
      curl -s -X POST http://$OT/api/entities/ch_setpoint -d '{"value":55}'
      curl -s -X POST http://$OT/api/entities/ch_enable  -d '{"value":1}'
      ```
      Both answer `202`. In `/api/ot/raw` the ID 1 `count` steps to the new `raw` (14080 for 55.0)
      **no later than** the sample where ID 0's `raw & 256` (the CH bit) is set — never the other
      way round. The burner lights.

> If the boiler answers `UNKNOWN-DATAID` to ID 1, the setpoint is still sent, the re-send
> continues, and CH still rises — the log says so once. Likewise for `DATA-INVALID` on ID 1 (then
> compare `flow_min_dc`..`flow_max_dc` with the boiler's own range). Neither is a defect.

## 2. The CH bit waits for the held setpoint

- [ ] Right after the back-to-back change in §1, sample `GET /api/state` twice a second: for a
      second or two `ch_enable` reads `true` while `ch_enable_effective` is still `false`, then
      both are `true`.

> If `ch_enable_effective` is ever `true` **before** the new-value ID 1 write appears, the
> invariant is broken — **stop.** A boiler asked for heat with a setpoint nobody chose is exactly
> what this bit exists to prevent.

## 3. Turning it off, and the band

- [ ] `curl -s -X POST http://$OT/api/entities/heating_season -d '{"value":0}'` drops the CH bit
      within seconds, whatever `ch_enable` says (confirm at the boiler).
- [ ] `curl -s -X POST http://$OT/api/entities/ch_setpoint -d '{"value":35}'` with `flow_min` 40 →
      `422`.

## 4. The floor is the boiler's, not ours

- [ ] Ask a setpoint below the boiler's own minimum-flow parameter (`ch_setpoint` 32, param `E`
      ≈ 40). Read the flow the boiler actually settles at; if it holds 40, **the firmware obeys and
      the boiler does not** — indistinguishable from outside.
- [ ] Record the boiler's parameters `E` and `E.` from its menu (`E.`: `0` ignore, `1` heat at
      minimum, `2` heat at maximum — a `2` turns a low request into a hot house). If `E` ≠ 40, set
      `flow_min` in the config to match.

## 5. ID 1 goes first; an owed ID 56 follows; the readback is real

These prove the frame ordering and the DHW readback. Use a Data-ID this boiler acknowledges — not
ID 14 or ID 57.

- [ ] **ID 1 before an owed ID 56.** With `ch_setpoint` held and re-sent, pick a `dhw_setpoint` the
      boiler is not already holding, then hand-write another writable Data-ID and the DHW setpoint
      back-to-back:
      ```sh
      curl -s -X POST http://$OT/api/entities/room_setpoint  -d '{"value":20}'
      curl -s -X POST http://$OT/api/entities/dhw_setpoint   -d '{"value":50}'
      ```
      Both answer `202` (a `409` or `unknown-dataid` means the item tested nothing — use a Data-ID
      the boiler acks). The ID 56 write is owed and goes out at the **next idle slot** — it is
      never dropped. **DO NOT** send the hand write *after* the DHW one; that can evict the queued
      ID 56.
- [ ] **The re-write cap.** Write a `dhw_setpoint` the boiler cannot hold exactly (e.g. 50.5) and
      sample ID 56 for ten minutes: **exactly three** ID 56 writes — the first at once, then one
      after each disagreeing poll read (≥ a minute apart), then none. Not one (the readback never
      reached the executor); not one-a-minute (the boiler's echo of our own write is not a
      readback).
- [ ] **The readback is wired.** With a `dhw_setpoint` written and agreed, change the DHW setpoint
      on the boiler's own panel: within ~70 s an ID 56 `write-ack` appears and the panel shows the
      device's value again. Nothing happening means the readback is not wired.

## 6. Persistence across a power cycle

- [ ] Power-cycle the device: the local values persist, and the boiler resumes after the reboot
      window (see [00-setup §4](00-setup-and-safety.md)).

## 7. The boost — a row of the ladder

- [ ] Season on, local: `curl -s -X POST http://$OT/api/ops/boost -d '{"setpoint":50,"minutes":2}'`
      → `202`; `/api/control` `state` `boost`, `boost.active` true, `remaining_s` near 120. The
      burner lights and the flow climbs to 50; the boost ends by itself at two minutes and the
      state returns to `local`.
- [ ] Season off, the same request → `409` (`heating_season is off: a boost would not heat`).
- [ ] Refusals, season on, each `422` and leaving a running boost unchanged: `setpoint` 95 (out of
      range), `minutes` 0, `minutes` 1.5, `minutes` 481 (`minutes must be a whole number 1..480`).
- [ ] Start a 10-minute boost, `curl -s -X POST http://$OT/api/ops/boost_off` → `202`, and the CH
      bit drops within seconds. A second `boost_off` also answers `202`.
- [ ] Start a boost and power-cycle: `/api/control` shows `boost.active` false, `status_high`
      without the CH bit — the boost is never restored across a reboot.

> Known, not a defect: after a boost ends the boiler **keeps the boost's setpoint** (ID 1 is never
> read back). The held setpoint the executor maintains in normal operation is what closes this —
> the boost is the exception.

## 8. The same controls from the page, in local mode

With `control_mode` 0 and the boiler in sight:

- [ ] Heating season On → the page shows `202` and state "Local control".
- [ ] CH On → `202`; "CH command" on, then "CH asked of the boiler" on within seconds, the burner
      lighting if the boiler follows.
- [ ] A new flow setpoint (55) → `202`, "Flow setpoint held" 55.0 °C, the hint reading the band
      "40.0 … 70.0 °C".
- [ ] A flow setpoint of 35 (below `flow_min`) → the value is sent and refused: headline "Flow
      setpoint: the value was refused", `HTTP 422`, detail `value out of range`; nothing on the
      card changes.
- [ ] Typing `abc` and pressing Set → "not a number" beside the box and **no** request in the
      Network tab.
- [ ] Hot water Off → "Hot water asked" off within seconds (the tap likely runs cold); On right
      after brings it back.
- [ ] Boost 50 °C / 2 min from the page → state "Boost" counting down, ending by itself; a boost
      with season Off → "Boost: refused by the device", `HTTP 409`, `heating_season is off: a boost
      would not heat`.

## 9. In Home Assistant mode the page owns nothing

With `control_mode` `ha` (see [`06-home-assistant.md`](06-home-assistant.md)), no button is
disabled — a write is still sent, and the device's own answer is shown:

- [ ] CH On → "CH: refused by the device", `HTTP 409`, detail `owned by Home Assistant:
      control_mode is ha`. The same for a new flow setpoint, hot water On/Off, a new hot-water
      setpoint, and a boost start.

> Use a hot-water setpoint **inside** the boiler's ID 48 range (50) for this — a 90 would come back
> `422` and prove nothing about ownership.

- [ ] Heating season Off then On from the card in HA mode: the device answers `202` both times (by
      the rule that only the thermostat, never Home Assistant, turns the season on).

## Notes

- The `raw` values (12800 = 50.0, 14080 = 55.0) are this unit's f8.8 encoding; the flow band
  `40.0`..`70.0` is the shipped default.
- Watchdog and failsafe behaviour once Home Assistant is driving is
  [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md); the broker and discovery are
  [`06-home-assistant.md`](06-home-assistant.md).
