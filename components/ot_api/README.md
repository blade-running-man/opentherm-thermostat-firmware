# ot_api — the registry and the state, projected into JSON

## Purpose

The single output path off the device. REST, MQTT and the web UI are all clients of this one
API: it renders the generated entity registry and the read-side state model (`ot_state`) into
JSON, and renders the executor's control document. Nothing leaves the device except through these
renderers, so the three transports can never disagree about how a value is printed.

## Responsibility

Owns:

- The JSON shape of the entity metadata (`/api/entities`), the entity values (`/api/state`), a
  single entity (`/api/entity/<key>`), the `/ws` push frame, a bare per-entity value (used for
  MQTT state payloads), and the executor's control document (`/api/control`).
- The one place where a value's printed form is decided (`emit_bare_value`), so REST, `/ws` and
  MQTT can never disagree about a switch, an enum, or an absent value.
- Consistent snapshots: each document is rendered whole under `ot_lock()`.

Deliberately does NOT do:

- No I/O, no HTTP, no sockets — it fills caller-owned buffers only.
- No JSON escaping. Every string it prints comes from the generated registry (or from literals /
  `ot_control` names); the quote, backslash and control characters that escaping would change are
  kept out of the registry table upstream by `tools/tests/test_registry.py`, at the point where
  each string is still a single one. **DO NOT** print anything here that did not come from the
  registry.
- No privileged path for any client. If a screen needs a path of its own, the API is wrong.
- `ot_api_control.c` does not reach for the thermostat; it is fed a plain struct so the component
  stays pure and host-testable (an `ot_thermostat.h` include would drag FreeRTOS in).

## Public API

All renderers follow **snprintf semantics**: they return the REQUIRED size (excluding the
terminating NUL), never overflow the buffer, and always leave it a valid string. A caller that
ran out of room must answer 500, not hand out truncated JSON — truncated JSON looks valid right
up to the attempt to parse it. The buffer is the caller's; the snapshot is taken under
`ot_lock()`.

### `ot_api.h`

| Function | Contract |
| --- | --- |
| `size_t ot_api_render_entities(char *out, size_t cap)` | Registry metadata (key, name, data_id, writable, unit, device_class, entity_category, options, min/max). No values. Wraps `{"schema":N,"entities":[...]}`. |
| `size_t ot_api_render_state(char *out, size_t cap, uint32_t now_ms)` | Every entity's value: `{availability,value,age_ms}` keyed by entity key. `{"schema":N,"state":{...}}`. |
| `size_t ot_api_render_entity(const char *key, char *out, size_t cap, uint32_t now_ms)` | One entity, metadata + value. Returns **0** if the key is not in the registry — caller answers 404, not an empty document. |
| `size_t ot_api_render_frame(const char *type, const uint32_t *mask, char *out, size_t cap)` | A `/ws` frame `{"type":...,"values":{key:value,...}}`. `mask==NULL` → every entity (the snapshot a new socket gets); otherwise a delta — registry index `i` is bit `i%32` of `mask[i/32]`, and the array holds `(OT_ENTITY_COUNT+31)/32` words (a shorter array is read past its end). `type` is printed raw — pass a literal. The frame shape matches the client's `web/src/api/ws.ts`. |
| `size_t ot_api_render_value(uint16_t index, char *out, size_t cap)` | One entity's bare value (for MQTT state payloads). `index` is the registry position; returns **0** with nothing written if out of range. |

### `ot_api_control.h`

- `size_t ot_api_render_control(const ot_api_control_t *c, char *out, size_t cap)` — renders the
  `GET /api/control` document. Pure: fed an `ot_api_control_t` gathered by the task layer
  (`ot_thermostat_control_get()`); this only formats it.
- `ot_api_control_t` — the executor snapshot: `mode`, `state`, `reason`, `cause`,
  `heating_season`, `status_high` (what the task last handed the bus — asked, not confirmed by the
  boiler), `held_setpoint_dc` (the setpoint held on OpenTherm Data-ID 1), DHW enable/setpoint,
  boost active/setpoint/remaining, failsafe count/last-duration, `watchdog_overdue_s`,
  `stack_known`/`stack_hwm`. Temperatures are integer tenths of a degree (`_dc` suffix) — no float
  is formatted, so there is no rounding or `nan` to guard.
- `OT_API_CONTROL_SCHEMA` (== 1) — the shape version of THIS document, separate from the registry's
  `OT_SCHEMA_VERSION`.

Example control document:

```json
{"schema":1,"mode":"local","state":"boost","reason":"none","cause":"none",
 "heating_season":true,"status_high":3,"held_setpoint_dc":500,
 "dhw":{"enable":true,"setpoint_dc":505},
 "boost":{"active":true,"setpoint_dc":500,"remaining_s":3540},
 "failsafe":{"count":2,"last_duration_s":61},"watchdog_overdue_s":7,"stack_hwm":1184}
```

## Implementation

**Files:**

- `ot_api_sink.h` (private) — the `ot_api_sink_t` accumulator `{out, cap, need}` every renderer
  writes through. Sits beside the sources, not in `include/`, because no other component should
  call it.
- `ot_api.c` — the entity/state/frame/value renderers, plus the shared `ot_api_emit()` /
  `ot_api_terminate()` (defined here because they are used by both source files).
- `ot_api_control.c` — `ot_api_render_control()` and the `mode_name()` helper (which prints
  `"ha"` for HA mode and `"local"` otherwise, matching the registry's spelling of `control_mode`).

**The sink algorithm.** `ot_api_emit(sink, fmt, ...)` does a `vsnprintf` into the remaining room
(`NULL`/0 room once the buffer is full) and always adds the return value to `s->need`. So `need`
accumulates the REQUIRED size even after the buffer is exhausted — the caller learns of a
shortfall from the returned size, never from the content. `ot_api_terminate()` writes the NUL at
`min(need, cap-1)`, leaving a string in every outcome. `ot_api_emit` is
`__attribute__((format(printf,2,3)))`-checked.

**The one value emitter.** `emit_bare_value()` is THE place a value's form is decided, shared by
`/api/state`, `/ws` and MQTT:

- availability != OK → `null` (null, not zero — "no data" and "0 °C" are different statements);
- boolean entity → `true`/`false`;
- enum → the option string, not its index (`emit_option_value`), guarded before the cast to
  unsigned: a NaN, a negative, or a fractional index (e.g. 1.5) renders `null`, never a wrong
  option (the boiler stores an enum sensor's string in Home Assistant, so the option text is what
  must reach it);
- otherwise a number with two decimals (`%.2f`).

**Invariants / DO NOT constraints:**

- Every document is rendered whole under `ot_lock()`; the lock is **recursive** so nested calls
  like `ot_state_bounds()` (which take the lock themselves) keep the snapshot consistent.
- `data_id < 0` (a synthetic row) prints `null`. **DO NOT** print `data_id` with `%u` — an
  `int16_t` −1 becomes 4294967295, which a client would read as a real Data-ID.
- `age_ms` is `now_ms - updated_ms` when `updated_ms` is set, else `null`.
- In the control document, an absent value is `null`, never `0`: boost numbers print only while
  `boost_active`, the DHW setpoint only while `dhw_setpoint_set`, `stack_hwm` only while
  `stack_known` — otherwise the struct carries stale leftovers from a previous boost, an unset
  setpoint, or an unmeasured stack.
- `value_of()` reads `ot_state`; a missing key leaves the value as UNKNOWN/NaN → `null`. All three
  value renderers give the same answer, and the `/ws` frame prints `null` rather than skipping the
  key: a `state` frame replaces the client's whole map, so a skipped key would vanish from it
  instead of reading "no data".

## Tests

Host suite **`test_api`**: the whole component compiles into it, which is why `ot_api_control.c`
must stay pure (no ESP-IDF header). The no-escaping guarantee is pinned upstream by
`tools/tests/test_registry.py` (`test_no_registry_string_needs_json_escaping`), and the enum-cast
chain by `test_an_enum_value_between_two_options_is_refused_and_the_last_one_renders`.

## Notes

- `CMakeLists.txt` requires `ot_state ot_registry ot_lock ot_control`. `ot_control` is a **public**
  require because `ot_api_control.h` carries its enum types; it is pure (no ESP-IDF header) so
  `test_api` links it without a framework. `ot_json` is deliberately NOT required — there is
  nothing to escape.
- Routing every value through `emit_bare_value()` is what makes REST, `/ws` and MQTT agree; the
  `/ws` socket does not format its own values independently.
