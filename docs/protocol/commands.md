# Commands

How a write request — a registry key and a number — becomes either an OpenTherm frame on the bus
or a command to the executor, and every way it can be refused. One component decides all of it:
`components/ot_command/`, pure and host-tested. The transports only parse the request and
translate the answer; neither REST nor MQTT holds a check of its own, because a check duplicated on
the surface eventually diverges from the one in the depths, and diverges silently.

```text
REST  POST /api/entities/<key>  {"value": …}      MQTT  <prefix>/<key>/set  <number>
        │ ot_wire_parse_entity_write()                    │ ot_mqtt_decide()  (retained / non-number refused)
        ▼                                                 ▼
  ot_command_check(key, value, OT_ORIGIN_WEB)       ot_command_check(key, value, OT_ORIGIN_HA)
        │                                                 │
        ├── FRAME   ──► ot_bus_write(id, raw)             ├── FRAME   ──► (unreachable: OT_CMD_NOT_FOR_HA)
        └── CONTROL ──► ot_thermostat_control_apply()     └── CONTROL ──► ot_thermostat_control_apply()
                        (the final answer)                                (the final answer)
```

The transports themselves are described on [REST API](/interfaces/rest-api) and
[MQTT](/interfaces/mqtt).

## Two entry points

| Function | Who calls it | What it decides |
| --- | --- | --- |
| `ot_command_check(key, value, origin, cfg, out)` | every writer: REST with `OT_ORIGIN_WEB`, MQTT with `OT_ORIGIN_HA` | everything: key, writability, the boiler's support, bounds, ownership; returns a tagged result, FRAME or CONTROL |
| `ot_command_encode(key, value, out)` | only `ot_command_check()` (and its own suite) | the frame path: writability, support, bounds, codec |

**Do not** call `ot_command_encode()` from a writer's surface: it knows no owner, so a write
through it would bypass the ownership refusal. And the executor's own re-sends of ID 1 and ID 56
go through neither: the state model's "unsupported" flag never clears, so the encoder would refuse
every re-send forever once two stray UNKNOWN-DATAID answers raised it, and since the CH bit never
rises before the held ID 1 has gone out, CH would never rise again. The executor encodes those two
values itself, from tenths of a degree straight into f8.8, with `ot_control_io_dc_f88()` (see
[Executor](/architecture/executor)).

A tagged result rather than two functions means no caller can reach the frame path for an entity
that is an executor input.

## Origins

`ot_origin_t` has two values: `OT_ORIGIN_WEB` (0) for REST and the web UI, `OT_ORIGIN_HA` (1)
for MQTT. **Anything that is not `OT_ORIGIN_HA` is judged as the web**, never the other way round:
Home Assistant's rights include CH in HA mode, and an unknown origin must not get them.

## Two kinds of row

Which path a key takes follows from the registry row's executor command (`control` in
`tools/opentherm_ids.py`):

- **Control rows** — `ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`, `heating_season`.
  The value becomes a command to the executor (`OT_CMD_OUT_CONTROL`), which holds it, persists it
  and decides what reaches the boiler. Three of them are synthetic switches with no Data-ID; two
  are real Data-IDs (1 and 56) the executor writes itself.
- **Frame rows** — every other writable entity (the plain OpenTherm writes such as IDs 14, 16, 24,
  57, 124, 126). The value is encoded into one frame (`OT_CMD_OUT_FRAME`) and queued on the bus
  with `ot_bus_write()`, evicting any pending write (see [The bus](/protocol/bus#a-queue-of-one)).
  **Only the web may write them.**

Which entities are writable, and their table bounds, is on [Entities](/reference/entities).

## The order of refusals

The order is one rule, stated once in `ot_command.h`: first what is wrong with the request
**whoever sends it**, because no change of owner cures it; then **who may send it**; then the
executor's own bounds, which are the owner's to learn — a caller is not told the bounds of a
command it may not send.

For a **control row**:

1. the key exists and is writable;
2. the boiler supports the write Data-ID — **not asked for `ch_setpoint`** (below);
3. the value converts into the executor's `int16_t` (below);
4. for `dhw_setpoint`, the boiler's own bounds, through `ot_command_encode()`, on the value that
   will be **persisted** (tenths divided by ten, not the float that came in);
5. `ot_control_check()`: ownership first, then the executor's bounds — 0 or 1 for a switch,
   `flow_min`..`flow_max` for `ch_setpoint`, and no 0 (the store's "unset") for `dhw_setpoint`.

For a **frame row**: `ot_command_encode()` judges the request (key, writability, a Data-ID to put
in a frame, support, NaN, bounds, codec), and **only then** an `OT_ORIGIN_HA` request is refused
with `OT_CMD_NOT_FOR_HA`. So an HA write of an out-of-range value is `OUT_OF_RANGE`, not
`NOT_FOR_HA`.

Consequences, pinned by `test_the_order_of_refusals_is_one_rule`: a web write in HA mode answers
`ch_enable = 0.5` and `dhw_setpoint = 99` with `OUT_OF_RANGE`, but `ch_enable = 2` and
`ch_setpoint = 99` with `OWNED_BY_HA`. The same `ch_setpoint = 99` is a `409` in HA mode and a
`422` in LOCAL mode.

### Ownership

| Mode | Web may write | Home Assistant may write |
| --- | --- | --- |
| LOCAL | every control row; every frame row | nothing (`OWNED_BY_LOCAL`), the season included |
| HA | `heating_season` only (0 or 1); every frame row | `ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`; `heating_season` **0 only** |

The season is the person's master switch, not a control value Home Assistant owns: the web may
always turn it off, and on, in either mode. Home Assistant may turn it off in HA mode, because its
summer logic is a bound of the failsafe, and never on (`SEASON_ON_IS_LOCAL`): a bound the bounded
party can lift is not a bound. When two refusals apply (HA origin, LOCAL mode, value 1), ownership
wins. The mode, the ladder and the watchdog an accepted HA command feeds are on
[Executor](/architecture/executor).

### Why Home Assistant may send no frame

Home Assistant drives the **executor**, never the bus. An HA frame write would hold the bus's one
write slot and defer the executor's ID 1 re-send, and while the held ID 1 is deferred the CH bit
cannot rise — the failsafe's included. So `OT_CMD_NOT_FOR_HA` holds in **both** modes: the entity
is nobody's, and no change of owner cures it. Its natural HTTP code would be `409`, but no HTTP
route can reach it, because REST always writes with `OT_ORIGIN_WEB`. Discovery follows the same
line: the write-only frame rows get no Home Assistant entity at all, and readable frame rows are
announced read-only ([Home Assistant discovery](/interfaces/home-assistant-discovery)).

There is deliberately no mode check on frame rows for the web: a room setpoint or a maximum
modulation is nobody's, and refusing the web in HA mode would take a working control away.

### Why `ch_setpoint` skips the support check

The executor holds the CH setpoint and sends ID 1 itself every 10 s; those replies feed
`ot_state` like any other, so two stray UNKNOWN-DATAID answers would raise a flag that never
clears. Asked here, the flag would block no frame — the executor keeps sending the held value —
and would only freeze it: every setpoint refused until a reboot, and a Home Assistant automation
that refreshes only the setpoint starving the watchdog into failsafe. `dhw_setpoint` keeps the
check (no DHW value is persisted that the boiler would refuse) and carries the same latent risk;
dropping the line would not lift it, because the DHW bounds path asks the same flag.

## Bounds

- **NaN and infinity first.** No comparison with NaN is true, so `value < lo || value > hi` would
  pass it, and f8.8 encodes it as 0 °C. Both are `OUT_OF_RANGE` before any comparison. From REST
  this is unreachable (the JSON parser rejects them), but a computed float from another caller is
  guarded here.
- **The boiler's bounds replace the table's.** A frame row's bounds come from `ot_state_bounds()`:
  the registry's `min_value`..`max_value`, displaced once the boiler has answered the row's
  `bounds_from` Data-ID with an `ok` reply — upper bound from the high byte (`s8_hb`), lower from
  the low byte (`s8_lb`). `dhw_setpoint` (ID 56) takes them from ID 48 and `max_ch_setpoint`
  (ID 57) from ID 49. Until then, or on a boiler that does not support the source ID, the table
  stands.
- **No bounds is a refusal.** A writable entity whose table declares no bounds is `OUT_OF_RANGE`,
  fail-closed: otherwise a new writable row without bounds would switch the range check off
  unnoticed. `test_every_writable_entity_declares_bounds` holds every writable row with a Data-ID
  to having them; the synthetic switches are bounded by `ot_control` instead.
- **The CH setpoint is bounded by configuration**, `flow_min`..`flow_max`, not by the table or by
  ID 57. Out of range is **refused, not clamped**: a refused Home Assistant automation feeds no
  watchdog and ends in failsafe, which is the right outcome for a broken automation, whereas a
  clamp would hide it behind a plausible flow temperature. Home Assistant is not expected to hit
  it, because discovery publishes the runtime bounds.
- **Codec limits.** `u16` must lie in 0..65535 and `s16` in −32768..32767; the half-word codecs
  are refused (see [The frame](/protocol/frame#writing-is-whole-word-only)).

### Conversion for the executor

The executor speaks `int16_t`: tenths of a degree for a temperature, the number itself for a
switch. A temperature is converted with `lroundf(value * 10.0f)` **in float**: 45.05 arrives as
the float 45.0499992, whose float product rounds back to 450.5 and gives 451, what the client
wrote; in double the product stays 450.4999 and gives 450. Over every x.x5 in ±1000 the float form
is right 20 000 times of 20 000 and the double form wrong 8 000 times. A switch value must be a
whole number (`0.5` is neither on nor off and is refused rather than guessed); a whole number
other than 0 or 1 is passed on for `ot_control` to refuse, so its two callers cannot disagree.
Anything without an `int16_t` is `OUT_OF_RANGE`, never wrapped.

## Refusal codes

| Code | Meaning | REST | MQTT |
| --- | --- | --- | --- |
| `OT_CMD_UNKNOWN_KEY` | no such key in the registry | `404` | refused |
| `OT_CMD_NOT_WRITABLE` | the entity is read-only, or a synthetic row on the frame path | `405` | refused |
| `OT_CMD_HALF_WORD_CODEC` | the codec covers half a word | `405` | refused |
| `OT_CMD_UNSUPPORTED_BY_BOILER` | the boiler answered UNKNOWN-DATAID twice for the write ID | `409` | refused |
| `OT_CMD_OUT_OF_RANGE` | outside the bounds, NaN, no `int16_t`, a fractional switch | `422` | refused |
| `OT_CMD_OWNED_BY_HA` | a web write while the mode is HA | `409` | — |
| `OT_CMD_OWNED_BY_LOCAL` | a Home Assistant write while the mode is LOCAL | — | refused |
| `OT_CMD_SEASON_ON_IS_LOCAL` | Home Assistant tried to turn the season on | — | refused |
| `OT_CMD_NOT_FOR_HA` | Home Assistant tried to write a frame row | — | refused |

`405` and not `403` for a read-only entity: writing it is never allowed to anyone; it is a property
of the entity, not of the caller. `409` for an unsupported ID, not `404`: the entity exists, it is
this boiler that does not accept it. `422`, not `400`: the body was understood and the value
rejected. The ownership codes are `409`, not `422`: they say who may write, not what is wrong with
the value. The REST switch ends in an explicit `500` for a code it does not know, because the board
build has no `-Wall` and the host build that has `-Werror=switch` never compiles `ot_http`.

Before the command layer, REST answers `404` for a path that names no entity and `400` for a body
that is not `{"value": <number | boolean>}`. A boolean is kept apart from a number while parsing
and handed on as 1 or 0. MQTT refuses a retained command and a payload that is not a number before
the command layer. An MQTT refusal has no reply topic: it is counted and logged (at most one line a
minute, with the count held back, and never the payload, because `GET /api/log` is readable by
anyone who can reach the device).

## The final answer

`ot_command_check()` is the **early** answer. The mode can flip between it and the moment the
command lands, so for a control row the caller hands the result to
`ot_thermostat_control_apply()`, which re-checks ownership under the executor's lock against the
mode it last observed and stamps the watchdog in the same critical section. Its refusals:

| Refusal | REST |
| --- | --- |
| owned by HA / by LOCAL, season on is LOCAL | `409` |
| out of range | `422` |
| the thermostat task is not running | `503` |
| the configuration store refused the change | `409` for a store written by a newer firmware, `500` otherwise |

Accepted answers are `202 Accepted`, never `200`: nothing is confirmed by the boiler yet.

- A **frame** answers `{"queued": {"data_id": …, "raw": …}}`. The queue holds one write and the
  next evicts it, so "queued" is all that is known.
- A **control** command answers `{"applied": {"command": …}}` with no value echoed: the
  executor quantises and bounds a CH setpoint where it uses it, so the request's number is not
  necessarily the one kept. The kept value is read from `GET /api/config`, and what the boiler is
  told shows in `GET /api/control`.

## Tests

`test/test_ot_command` covers `ot_command_encode()` (bounds, NaN, codecs, the boiler's own bounds
winning over the table, every writable entity declaring bounds); `test/test_ot_command_check`
covers the origin path and the order of refusals; the season truth table is pinned in
`test/test_ot_control_check`. All are pure host suites: the
component's dependencies (`ot_registry`, `ot_state`, `ot_frame`, `ot_control`) pull in no
ESP-IDF. On the boiler: [Writes and refusals](/hardware-verification/03-writes-and-refusals).
Component contract: [`components/ot_command/README.md`](../../components/ot_command/README.md).
