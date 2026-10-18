# ot_command — a registry key plus a number turned into an OpenTherm frame, or refused

`ot_command` is the one place where the legality of a write is decided: writability, the
boiler's support of the write ID, the bounds, the codec, and ownership between Home Assistant
and the local thermostat. It turns an accepted write into either an OpenTherm frame for the bus
or a command for the executor; every refusal, whatever the transport, is decided here and
nowhere else.

## Purpose

A writer — the REST/web interface, or Home Assistant over MQTT — arrives with a registry key
and a number. This component answers one question: may that write happen, and if so, in what
form? A duplicated check on a transport surface would one day diverge silently from the one in
the depths, so all writers pass through a single entry point (`ot_command_check()`), and no
other code decides whether a write is legal.

## Responsibility

What it owns:

- **The single decision point for every write.** REST/web (`OT_ORIGIN_WEB`) and Home Assistant
  over MQTT (`OT_ORIGIN_HA`) both come through `ot_command_check()`; nothing else decides a
  write.
- **Validation.** Unknown key, read-only entity, out-of-range value (including NaN and
  infinity), a boiler that answered UNKNOWN-DATAID twice for the write ID, and part-byte codecs
  that cannot be written whole.
- **Ownership.** A web write while `control_mode` is HA is `OT_CMD_OWNED_BY_HA`; a Home
  Assistant write while `control_mode` is LOCAL is `OT_CMD_OWNED_BY_LOCAL`; Home Assistant
  turning the heating season on is `OT_CMD_SEASON_ON_IS_LOCAL`; Home Assistant writing a raw
  OpenTherm frame row (an entity with no control command) is `OT_CMD_NOT_FOR_HA`.
- **The FRAME/CONTROL split.** For a plain OpenTherm row the accepted result is a frame; for a
  registry row that names a control command (`ch_enable`, `ch_setpoint`, `dhw_enable`,
  `dhw_setpoint`, `heating_season`) it is a command for the executor (`ot_control`).

What it does NOT do:

- **It does not send the frame.** The caller passes the frame to `ot_bus_write()` itself — the
  bus queue and its eviction rule belong to the bus.
- **It does not parse request bodies.** "A value from outside" and "operation parameters" are
  `ot_wire`'s `ot_wire_value_t` / `ot_wire_params_t`, kept there deliberately (a linking
  measurement — see Notes). The gluing happens in `ot_http`.
- **It is not the final word on ownership.** `ot_command_check()` is the EARLY answer; the mode
  may flip between this call and the caller's later `ot_control_apply()`, which re-checks under
  its own lock. The caller must handle a refusal from `ot_control_apply()` too.
- **It does not depend on hardware or on `ot_config`.** Configuration arrives as plain values in
  `cfg`. The whole component is verified on the host.

## Public API

Header: `include/ot_command.h`. Types: `ot_command_err_t`, `ot_command_frame_t`,
`ot_command_out_kind_t` (`OT_CMD_OUT_FRAME` / `OT_CMD_OUT_CONTROL`), `ot_command_out_t`.
`ot_origin_t`, `ot_control_cfg_t` and `ot_control_cmd_t` come in from `ot_control.h`.

| Function | Contract |
| --- | --- |
| `ot_command_check(key, value, origin, cfg, out)` | The entry point for **every writer**, carrying the origin. Validates, decides ownership, and returns a tagged `out` (FRAME → `ot_bus_write()`, CONTROL → `ot_control_apply()`). Any task; reads `ot_state` (which takes `ot_lock` inside) and nothing else. NULL `key`/`cfg`/`out` → `OT_CMD_UNKNOWN_KEY`. On refusal `*out` is untouched. Returns any `ot_command_err_t`; the caller maps `OWNED_BY_HA` / `OWNED_BY_LOCAL` / `SEASON_ON_IS_LOCAL` to HTTP 409. |
| `ot_command_encode(key, value, out)` | `ot_command_check()`'s frame path — validates and encodes one OpenTherm write frame. **Not to be called from a writer's surface**: it knows no owner, so a write through it would bypass the ownership check and its 409. A synthetic row (`data_id -1`) is `OT_CMD_NOT_WRITABLE` regardless of its writable flag — it has no Data-ID to put in a frame. On any refusal `*out` is untouched. Nothing is allocated; `out` belongs to the caller. |
| `ot_command_strerror(e)` | A literal fit for a response body. Never NULL. |

## Implementation

**`ot_command.c` — the frame path (`ot_command_encode`, `ot_command_strerror`).** Order:
unknown key / NULL out → not writable → synthetic-row guard (`data_id < 0`, refused *before* the
cast that would turn -1 into ID 255) → the WRITE ID (`write_id` if the entity declares one, else
`data_id`) checked unsupported-by-boiler → NaN/infinity → bounds from `ot_state_bounds()` →
codec. Codecs: F88 (saturates at the bounds via `ot_codec_float_to_f88` rather than overflowing),
U16, S16 (each range-checked and rounded half away from zero); anything else is
`OT_CMD_HALF_WORD_CODEC`.

Two guards worth naming:

- **The WRITE ID, not the read ID.** An entity read under one Data-ID and written under another
  must be checked for support under the *write* ID: it may be exactly the write ID the boiler
  rejects.
- **`ot_state_bounds()` returning false is fail-closed** (`OT_CMD_OUT_OF_RANGE`), not "no bounds
  so anything goes". The table declaring `min_value`/`max_value` as NaN must not switch off the
  range check unnoticeably. Every writable entity that carries a Data-ID has table bounds (a test
  enforces this), so the refusal never fires for them today — it waits for the day the generator
  emits one without.

**`ot_command_check.c` — the ownership/translation path (`ot_command_check`).** For a row with
`control == 0` (a plain OpenTherm write) it calls `ot_command_encode()`, then — *after* the
encode, never before — refuses `OT_ORIGIN_HA` with `OT_CMD_NOT_FOR_HA`; otherwise it returns a
FRAME. For a control row it runs: writability → boiler support (skipped for `ch_setpoint`, see
below; kept for `dhw_setpoint`) → `to_control_value()` → for `dhw_setpoint` the boiler's own
bounds via `ot_command_encode()` on the value that will be *persisted* (`v/10`, not the incoming
float, so the executor never stores a DHW setpoint the boiler would refuse) → `ot_control_check(cfg,
origin, cmd, v)`, mapped by `from_control()`; the accepted result is a CONTROL command.

Helpers:

- `to_control_value(cmd, value, out)` — a float from outside into the `int16_t` `ot_control`
  speaks. Rejects non-finite values first (no comparison with NaN is true, and `lroundf(NaN)` is
  unspecified — on the hosts measured it yields 0, i.e. a 0 °C setpoint). Temperatures → tenths
  of a degree via `lroundf(value * 10.0f)` computed **in float** (measured: the float product
  makes 45.05 → 451, the double product makes 450 — do not recompute in double). Switch values
  must be whole (`0.5` is refused rather than guessed); a whole number other than 0/1 is passed
  through for `ot_control` to refuse, so the two callers of `ot_control_check()` cannot disagree
  about it. The `int16_t` cast is guarded (`<= -32768.5f || >= 32767.5f`) so an absurd value is
  refused, never wrapped into something `ot_control` would accept.
- `from_control(e)` — a one-to-one mapping of `ot_control_err_t` onto `ot_command_err_t`. **No
  default branch**, so that a new `ot_control` enumerator is caught by `-Wswitch` at compile time
  rather than swallowed. The boost-only codes (`SEASON_IS_OFF`, `BAD_MINUTES`), which
  `ot_control_check()` never returns, fall through to a fail-closed `OT_CMD_OUT_OF_RANGE`.

**Error enum invariants (`ot_command.h`):** the value-validation codes keep their fixed numbers;
the ownership codes (`OWNED_BY_HA`, `OWNED_BY_LOCAL`, `SEASON_ON_IS_LOCAL`) are appended below
them, and `NOT_FOR_HA` below those. Do not insert new codes above those markers. The ownership
codes mean "who may write", not "what is wrong with the value" — hence HTTP 409, not 422 — and
that is not a promise that the same value is accepted after a mode switch: the executor's own
bounds are asked *after* ownership, so a web `ch_setpoint = 99` is 409 in HA mode and 422 in
LOCAL mode.

**The order of refusals is one rule** (stated once, in the header): (1) what is wrong with the
request whoever sends it — key/writability, boiler support, representation, and for
`dhw_setpoint` the boiler's own bounds — because no change of owner cures it; then (2) who may
send it — `ot_control_check()`'s ownership; then (3) the executor's own bounds, which are the
owner's to learn, since a caller is not told the bounds of a command it may not send. So a web
write in HA mode: `ch_enable = 0.5` → `OUT_OF_RANGE`, but `ch_enable = 2` → `OWNED_BY_HA`.

**`ch_setpoint` never returns `UNSUPPORTED_BY_BOILER`.** It is a synthetic control input: the
executor holds it and sends ID 1 itself every 10 s, and those replies feed back into `ot_state`
like any other, so two stray UNKNOWN-DATAID answers would raise an "unsupported" flag that never
clears. Asked here, that flag would block no frame — the executor keeps sending the old value —
and would only freeze the setpoint: every web and HA setpoint refused until a reboot, and a Home
Assistant automation that refreshes only the setpoint could starve the watchdog into failsafe.
`dhw_setpoint` keeps the support check and carries the same latent risk, but dropping the check
would not lift it, since the DHW bounds path asks the same flag anyway.

**Why Home Assistant may write no raw frame.** Home Assistant drives the executor, never the bus.
A raw OpenTherm frame from HA held on the bus's single write slot would defer the executor's ID 1
re-send, and while the held ID 1 is deferred the CH bit could never rise (including the failsafe's).
So a `control == 0` row is `OT_CMD_NOT_FOR_HA` for `OT_ORIGIN_HA` — decided *after* the request
itself is judged, so an HA write of an out-of-range value is still `OT_CMD_OUT_OF_RANGE`, not
this. This refusal lives in the one entry point, never in a transport.

## Tests

Host suite `test_ot_command` covers the two files, including
`test_every_writable_entity_declares_bounds` (every writable entity with a Data-ID has table
bounds) and `test_the_order_of_refusals_is_one_rule`. `ot_command_encode()` also has its own
suite. The component is pure and framework-free by design — the `REQUIRES` are `ot_registry
ot_state ot_frame ot_control`, none of which pull in ESP-IDF, so host suites that link
`ot_command` stay framework-free.

## Notes

- **`ot_control` is a PUBLIC requirement** (`CMakeLists.txt`): `ot_command.h` includes
  `ot_control.h` for `ot_origin_t`, `ot_control_cfg_t` and `ot_control_cmd_t`, so every component
  that includes `ot_command.h` (`ot_http`, `ot_mqtt`) needs its include path. `ot_control` is
  pure — no ESP-IDF header — so host suites linking `ot_command` stay framework-free.
- **Why parse-result types are NOT here (a measured linking rake):** while
  `ot_wire_value_t` / `ot_wire_params_t` lived in this header, `ot_wire` required `ot_command`,
  and the `test_wire_command` suite — a text-parsing test — dragged in `ot_state`, `ot_registry`
  and the whole boiler model, and failed to link on `ot_lock`. Splitting into separate files of
  one component would not help: PlatformIO compiles all the sources of a library together. The
  cut follows responsibility: `ot_wire` is bytes → parsed value, `ot_command` is parsed value +
  registry + state → frame. Do not bring those types back here "so the command layer is in one
  place".
- **NaN is `OUT_OF_RANGE`, deliberately not a new code:** the caller does the same with it as
  with an out-of-bounds value (refuse, do not queue; the HTTP surface answers 422 — "the body was
  understood, the value was rejected", which for NaN is literally true). A separate code would
  force every switch over `ot_command_err_t` to grow a matching branch, and a caller that forgot
  it would fall into a default that means something else. From REST this path is unreachable (the
  JSON float parser rejects NaN and infinity while parsing the body), but the header declares this
  the only place a write's legality is decided, so a future computed float — a controller division
  by zero, an uninitialised sensor value from MQTT — is guarded here.
- **Do not call `ot_command_encode()` from a writer's surface**, and **do not route the
  executor's own re-sends of ID 1 or ID 56 through it**: the state model's "unsupported" flag
  never clears, so it would refuse every re-send forever, and once ID 1 is refused the CH bit
  could never rise again. The executor encodes those two values itself with the f8.8 codec.
