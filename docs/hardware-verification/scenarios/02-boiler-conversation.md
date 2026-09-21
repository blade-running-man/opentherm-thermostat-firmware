# 02 — Conversation with the boiler

This scenario proves the firmware really talks to the boiler: the input polarity is right, the
first frames decode, and the registry and state model fill with *real* boiler answers rather
than merely looking filled. It ends at the Boiler page, which must label what it knows and invent
nothing.

Have the curl harness and the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md) running throughout — the bus must never fall
silent while you do this. The device must already be on the network
([`01-first-bring-up.md`](01-first-bring-up.md)).

> **SAFETY — before connecting the boiler.** If the boiler has never been wired to this device,
> walk the "No link at all" section below **first** to rule out mains voltage on the terminals. A
> switched-live thermostat terminal puts 230 V AC onto a shield input that has no mains isolation.

## 0. Input polarity — measure it, do not believe it

The resting polarity was derived from someone else's library, not from a measurement.

- [ ] With a meter on shield contact **D2**, the boiler connected but silent: expect a level near
      zero.
- [ ] If the level there is the supply voltage, **flip `ot_in_inverted` in the board descriptor**
      and rebuild.

> **DO NOT** go looking for a bug in `ot_decode` before this check. If not a single frame
> decodes, the first thing to try is flipping `ot_in_inverted`.

## 1. Without the boiler — the bus task runs

- [ ] The log shows `ot_master: in=<in> out=<out> (inverted), tick 100 us` and
      `ot_bus: bus task started, <N> ids in the poll ring`.
- [ ] With no boiler, `boiler is not answering (timeout) after 5 tries; polling continues` — and
      it keeps polling.
- [ ] An unanswered conversation cycles at ~1020 ms (900 ms receive window + 100 ms pause), well
      inside the 1150 ms deadline.
- [ ] The bus spoke before the network came up (the bus task starts early — check the two
      timestamps' order), and there are no panics or reboots.

## 2. With the boiler — it answers

- [ ] `boiler is answering` appears within the first seconds.
- [ ] IDs **25** (flow), **26** (DHW), **28** (return) give plausible `f8.8` values that change
      over time. A plausible-looking value in `u8`/`u8` where `f8.8` was expected means the wrong
      codec was assumed.
- [ ] ID **3** (slave flags) answers, showing the boiler's capability set.
- [ ] Some IDs answer `UNKNOWN-DATAID` — normal. Write down exactly which; they feed the pruning
      check in §5.

## 3. The whole registry is exposed

- [ ] `otget http://$OT/api/entities | jq '.schema, (.entities|length)'` → `1` and the entity
      count (`OT_ENTITY_COUNT` from `registry_generated.h` — **61** at the time of writing).

> A count *below* that means the document was truncated — but a truncated document fails to parse
> and the route answers `500 registry does not fit`, so a `500` is a defect, not a setting. A
> count *above* it is impossible for the static registry and means the wrong firmware was flashed.

- [ ] A single entity by key: `otget http://$OT/api/entities/flow_temperature | jq .` →
      `{"meta":…,"value":…}`; a non-existent key gives `404`, not an empty document.
- [ ] The query-parameter form behaves the same:
      `otget "http://$OT/api/entities?t=1" | jq '.entities|length'` → the same count. A `404` here
      means the path tail was taken from the raw URI rather than the parsed request path.

## 4. A minute after boot: who answered, who stayed silent

The poll ring holds 30 IDs with the mandatory ID 0 every other turn, so a full pass takes about a
minute.

```sh
otget http://$OT/api/state \
  | jq -r '.state | to_entries[] | "\(.value.availability)\t\(.key)\t\(.value.value)"' | sort
```

- [ ] `ok` stands against IDs **3, 17, 18, 25, 26, 48, 56** (member id, modulation, CH pressure,
      flow temperature, DHW temperature, DHW setpoint max/min, DHW setpoint), with plausible
      values — flow 30…80 °C, pressure ~1.5 bar, DHW setpoint ~60 °C. A wrong scale (flow reading
      11008 instead of 43.00) means the wrong codec in `tools/opentherm_ids.py` — fix the table,
      not the firmware.
- [ ] **The most important item: DATA-INVALID is not taken for data.** On this boiler ID **27**
      (outside temperature) and ID **5** (fault flags) answer `DATA-INVALID`, so both must show
      `invalid` with value `null` — never `ok`+`0`. `unsupported` here also fails: it is a
      different diagnosis. See [00-setup §6](00-setup-and-safety.md).

## 5. Unsupported IDs are marked and thrown out of the ring

The mark is set after **two** consecutive `UNKNOWN-DATAID` answers — two full ring passes —
counted from boot.

- [ ] `otget http://$OT/api/state | jq -r '.state|to_entries[]|select(.value.availability=="unsupported")|.key'`
      lists exactly the boiler's unsupported IDs. On this unit they are **15, 28, 33, 49, 57, 117,
      118, 119, 121, 122, 125, 127** (see [00-setup §6](00-setup-and-safety.md)).
- [ ] The journal carries one "not supported; removed from the poll ring" line per pruned ID.
      **Read these over USB, not `/api/log`** — the journal ring is 80 lines and boot lines roll
      out after an hour (see [00-setup §3](00-setup-and-safety.md)).

> ID 14 (`max_relative_modulation`) cannot appear in this list — it is write-only and never
> polled. None of the §4 answering IDs may end up here; if one does, reboot and repeat (the mark
> is not stored in NVS).

## 6. The poll ring got shorter, the pass got faster

- [ ] At boot the log says the ring holds 30 IDs; after cleanup it holds fewer (18 on this unit:
      3, 5, 6, 9, 17, 18, 19, 25, 26, 27, 48, 56, 100, 113, 114, 116, 120, 123).
- [ ] A full pass drops to ~34 s. Measure the ceiling of `age_ms`:
      ```sh
      for i in 1 2 3 4 5 6; do
        otget http://$OT/api/state | jq '[.state|to_entries[]|select(.value.age_ms!=null)|.value.age_ms]|max'
        sleep 10
      done
      ```
      The max fluctuates but stays under ~35 000 ms; unbounded growth is a ring-cleanup defect.

## 7. Bounds came from the boiler, not the table

- [ ] `otget http://$OT/api/entities/dhw_setpoint | jq '.meta.min, .meta.max'` → **40** and
      **65**. The static table holds 30/80, so these can only come from the boiler's ID 48 answer
      — the one live check that the boiler's bounds displace the table on the read path.
- [ ] For `max_ch_setpoint` (ID 57) the bounds stay **30/90** from the table, because its source
      (ID 49) is unsupported here — the declared `bounds_from` correctly does not fire.

## 8. The Boiler page labels what is known, invents nothing

Open `http://$OT/` and go to the Boiler page.

- [ ] It reads "The boiler answers" (or "The boiler does not answer" with the checklist to walk),
      with counters for cycles, answers, failures and overdue.
- [ ] Known IDs are labelled from the registry (25 "Flow temperature", 18 "Circuit pressure", 56
      "DHW setpoint"); an unknown ID (one not in `tools/opentherm_ids.py`) stays **with a dash**
      and shows all eight interpretations (`f8.8`, `u16`, `s16`, high/low byte as `u8`/`s8`, flags
      by byte). No unknown ID is labelled with a guess.
- [ ] The names on the page match `GET /api/entities`. A mismatch means a second entity list lives
      in the frontend — fix the generator and its output, not the page.

## 9. Robustness over time

- [ ] Over a day of uninterrupted conversation, the `failed`:`cycles` ratio does not grow (the odd
      lost frame is normal), and `overdue` does not grow (growth means the bus task was held past
      1150 ms).
- [ ] Turn WiFi off for an hour: the conversation is unbroken and there are zero reboots.
- [ ] Disconnect the boiler while running and reconnect it: `boiler is not answering` then `boiler
      is answering`, with no reboot.
- [ ] Start a full sweep while running and confirm it costs no cycle:
      `curl -sS -X POST -u ":$OTPASS" -H 'Content-Type: application/json' -d '{"from":0,"to":127}' http://$OT/api/ops/scan`
      — the ~two-minute pass must not interrupt the poll (see [00-setup §2](00-setup-and-safety.md)).

## No link at all — the search order

Symptom: every failure is a timeout, zero frame errors, the input never toggles. Work the causes
in order.

- [ ] **Are those the right terminals?** Ask this first. Relay-thermostat terminals (a dry contact
      or a switched live) are not OpenTherm; a modulating thermostat uses a separate OpenTherm
      pair. **DANGER:** switched-live terminals put 230 V AC on the shield input (a 1N4148 and an
      optocoupler, no mains isolation) — check for AC *before* connecting anything.
- [ ] **Disconnect the shield and measure DC on the boiler terminals.** Expect **15–24 V DC**
      (looks like the bus, not proof); ~0 V is a dry on/off contact; **~230 V AC → DO NOT
      CONNECT.** A voltmeter alone does not tell OpenTherm terminals from an on/off input on every
      boiler (on the Intergas Kombi Kompakt HRE the on/off input X4 6-7 is fed 24 V DC) — measure
      only to rule out 230 V, and identify the terminals from the boiler manual.
- [ ] **Find the OpenTherm terminals in the boiler manual, do not guess from voltage.** On the
      Intergas Kombi Kompakt HRE, connector X4: `6-7` on/off thermostat, `8-9` outdoor sensor,
      **`11-12` OpenTherm** — and 6-7 must stay **open** (a jumper or an old thermostat there stops
      OpenTherm).
- [ ] **Is OpenTherm enabled in the boiler?** Some boilers ship it off (e.g. a Viessmann parameter,
      a Vaillant needing its VR33, a Baxi that must be enabled and forbids switched live at the
      same time).
- [ ] **3.3 V on the shield's VCC** (its logic supply).
- [ ] **Continuity D2 ↔ the OT-in GPIO and D1 ↔ the OT-out GPIO** (see the board descriptor /
      [00-setup §5](00-setup-and-safety.md)).
- [ ] **Swapped IN and OUT — the most common cause.** Verify by swapping the pins in the descriptor
      and one reflash, not by reasoning.
- [ ] **A dry joint** — the second most common cause; resolder the header.

## Notes

- The entity count (61), the pruned ring (18 IDs) and the specific supported/unsupported IDs are
  properties of this firmware build and this boiler. Re-derive them for another unit; the
  supported/unsupported/DATA-INVALID facts for this boiler are collected in
  [00-setup §6](00-setup-and-safety.md).
- This scenario only listens and reads. Writing a setting into the boiler is
  [`03-writes-and-refusals.md`](03-writes-and-refusals.md); asking it for heat is
  [`05-asking-for-heat.md`](05-asking-for-heat.md).
