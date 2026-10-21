<script setup>
import { data } from '../reference/entities.data'
// Message names come from the one map in the loader (SPEC_NAMES), never a copy kept here.
const n = (id) => data.specNames[id] ?? '—'
</script>

# Intergas Kombi Kompakt HRE

The owner's boiler, and the one the firmware runs on: an Intergas Kombi Kompakt HRE combi boiler
with no storage tank. This page records what that appliance answers over OpenTherm, its fault
codes, and the panel parameters that bound what the firmware can ask of it.

::: info A property of this appliance, not the contract
Nothing here is built into the firmware. The registry describes the OpenTherm specification, and
support is discovered at runtime (see [The boiler's OpenTherm interface](/boiler/)). Another
boiler — or this model with other firmware or other panel settings — will answer differently.
Re-derive these facts for your unit; do not treat them as the contract.
:::

## Wiring

The OpenTherm pair is on connector **X4, terminals 11–12**. Terminals 6–7 are the on/off
thermostat input and must stay **open**: a jumper or an old thermostat there stops OpenTherm.
Terminals 8–9 are the outdoor sensor. A voltmeter does not tell these apart — the on/off input is
fed 24 V DC too — so identify the terminals from the boiler's manual and measure only to rule out
mains voltage ([Conversation with the boiler](/hardware-verification/02-boiler-conversation)).

## Identity

The values on this page are the owner's observations of this unit — survey readings and probe
results — not values the firmware checks or relies on. ID 3 was observed answering `0x41AD`: the
flag byte `0x41` reports *DHW present* and no storage tank, and the low byte is **MemberID 173**.
A fault-code table belongs to a MemberID, so the one below is tied to 173 and is not applied to
other boilers.

## Supported Data-IDs

The sweep (`POST /api/ops/scan`) only **reads** — no code path in it produces a write — so what
this boiler answers splits into two lists: its answers to reads, and its answers to the writes the
firmware makes at start and on request.

### Answers to reads

A read sweep of 0..127 found every ID answered. These answer a read with something other than
UNKNOWN-DATAID:

| ID | Message | Answer | Observed |
| --- | --- | --- | --- |
| 0 | {{ n(0) }} | READ-ACK | — |
| 3 | {{ n(3) }} | READ-ACK | `0x41AD` (DHW present, MemberID 173) |
| 5 | {{ n(5) }} | DATA-INVALID | supported; no fault present |
| 6 | {{ n(6) }} | READ-ACK | `0x0101`: transfer and write enabled for the DHW setpoint only |
| 9 | {{ n(9) }} | READ-ACK | — |
| 17 | {{ n(17) }} | READ-ACK | — |
| 18 | {{ n(18) }} | READ-ACK | about 1.5 bar |
| 19 | {{ n(19) }} | READ-ACK | — |
| 25 | {{ n(25) }} | READ-ACK | e.g. 43.0 °C |
| 26 | {{ n(26) }} | READ-ACK | e.g. 34.4 °C |
| 27 | {{ n(27) }} | DATA-INVALID | no outdoor sensor connected |
| 48 | {{ n(48) }} | READ-ACK | `0x4128`: DHW range 40–65 °C |
| 56 | {{ n(56) }} | READ-ACK | 60.0 °C |
| 100 | {{ n(100) }} | READ-ACK | — |
| 113 | {{ n(113) }} | READ-ACK | — |
| 114 | {{ n(114) }} | READ-ACK | — |
| 116 | {{ n(116) }} | READ-ACK | — |
| 120 | {{ n(120) }} | READ-ACK | — |
| 123 | {{ n(123) }} | READ-ACK | — |

Apart from ID 0, these are the 18 IDs the poll ring keeps on this unit after pruning, so a full
pass drops to about half a minute. IDs 5 and 27 must read `invalid` with a `null` value, never
`ok` and `0`: that is the live check that DATA-INVALID is not taken for data.

Of the IDs the poll ring asks, the owner's survey records **15, 28, 33, 49, 57, 117, 118, 119, 121,
122, 125 and 127** as answering UNKNOWN-DATAID, marked `unsupported` and pruned, one log line each.

::: warning The code's note names fewer
The comment on ring compaction in `components/ot_bus_sched/ot_bus_sched.c` cites a probe of this
boiler run without the position correction it justifies. It names **8** IDs that dropped out of
the ring — 15, 28, 49, 57, 117, 119, 121, 125 — and their successors 17, 33, 56, 100, 118, 120,
122 and 127 as **missing from the log a turn later**: skipped by the uncorrected position, not
asked. So the note says nothing about how 33, 118, 122 and 127 answer; that they answer
UNKNOWN-DATAID rests on the survey alone. Re-check the 12-ID list on the unit before relying on it.
:::

### Answers to writes

| ID | Message | Answer | Written by |
| --- | --- | --- | --- |
| 1 | {{ n(1) }} | WRITE-ACK | the executor |
| 2 | {{ n(2) }} | WRITE-ACK | the introduction at bus start: `0x0000` |
| 16 | {{ n(16) }} | WRITE-ACK | a hand write |
| 24 | {{ n(24) }} | WRITE-ACK | a hand write |
| 126 | {{ n(126) }} | WRITE-ACK | the introduction at bus start: `0x0101` |

ID 124 ({{ n(124) }}) is written at bus start too, but the survey records no answer for it.

### What this means for the firmware

- **ID 14 (maximum relative modulation) is recorded as not supported**, so there is no modulation
  ceiling to set. ID 14 is write-only and never polled; the verdict rests on the sweep's *reads*
  of it, and a boiler may refuse a read of an ID whose writes it accepts. Two such sweeps mark it
  `unsupported` until a reboot, after which a write to it is refused `409`
  (see [The bus](/protocol/bus#the-sweep)). A write to ID 14 has not been recorded.
- **ID 57 (MaxTSet) is not supported, and neither is ID 49.** The CH flow ceiling exists only in
  the panel configuration (below), invisible over the bus and clamping silently. The bounds of
  `max_ch_setpoint` stay the registry's 30–90 °C, because their source, ID 49, never answers with
  a value (only an `ok` reply replaces the bounds).
- **ID 48 is supported**, so the DHW range of `dhw_setpoint` is the boiler's own **40–65 °C**, not
  the registry's 30–80 °C. It matches the panel.
- **ID 27 is DATA-INVALID**: no outdoor sensor, so weather compensation needs an external source.
  **ID 28 (return temperature) is not supported.**
- **IDs 16 and 24 accept writes** but are not exposed to Home Assistant: a room value sent once
  and never refreshed is worse than none.

## Fault codes

The OEM fault code is the low byte of ID 5, published as a number (`oem_fault_code`). It is
assumed to match the code on the panel display; that can only be confirmed during a real fault.
All of these are **locking faults**, cleared with the panel's **Reset** key once the cause is dealt
with; the last locking fault is readable and clearable at the panel.

| Code | Meaning |
| --- | --- |
| 0 | sensor failure during the self-test |
| 1 | temperature too high: air in the system, pump not turning, low flow |
| 2 | sensors S1 and S2 swapped |
| 4 | no flame signal: gas closed, low pressure, condensate, ignition or earthing |
| 5 | poor flame signal |
| 6 | flame detection error |
| 8 | incorrect fan speed |
| 10–14 | sensor S1 (flow) failure |
| 20–24 | sensor S2 (return) failure |
| 27 | outdoor sensor short circuit |
| 29, 30 | gas valve relay faulty |

## Panel parameters that bound what the firmware asks

Over OpenTherm the firmware writes ID 1 (the flow setpoint) and ID 56 (the DHW setpoint). The
effective floor and ceiling of the flow are **panel-only, invisible over the bus and silent**: a
room that will not warm up should be checked at the panel before the executor is debugged.

| Parameter | Meaning | Range / factory |
| --- | --- | --- |
| Max CH flow temperature (user menu) | the silent ceiling over ID 1 | 30–90 °C / 80 °C |
| DHW temperature (user menu) | overridden by ID 56 | 40–65 °C / 60 °C |
| **`E`** | minimum flow temperature with an OpenTherm thermostat; **not readable over OpenTherm** | 10–60 °C / **40 °C** |
| `E.` | reaction to an OpenTherm demand below `E`: `0` ignore it, `1` heat at `E`, `2` heat at maximum | — / 1 |
| `5.` | the maximum flow temperature settable from the panel; ID 1 is clamped to it | 30–90 °C / 90 °C |
| `5` | the minimum flow temperature of the heat curve | 10 °C…`5.` / 25 °C |
| `P` | anti-cycling time during CH | 0–15 min / 5 min |
| `3`, `3.`, `4` | maximum CH power, maximum modulating-pump capacity, maximum DHW power | per model |
| `o.` | eco days; `0` lets OpenTherm switch the tap comfort | 0–10 / 3 |

The consequences:

- With `E.` = 1 (factory), an ID 1 below 40 °C is served at 40 °C, not obeyed. The firmware obeys
  a low request; the boiler may not, and from outside the two are indistinguishable. Set the
  firmware's `flow_min` at or above `E` ([Configuration](/guide/configuration)).
- A flow setpoint above the panel's maximum is clamped by the boiler, silently. The firmware's
  `flow_max` should not exceed it.
- The DHW bit in the ID 0 high byte does **not** turn the tap off. On this boiler it switches tap
  comfort, and only when the panel is in Eco with `o.` = 0.
