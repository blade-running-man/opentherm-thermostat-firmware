# ot_ha — Home Assistant discovery documents and when each one is owed

## Purpose

This component answers two questions about Home Assistant MQTT discovery and nothing else:

1. **Which** discovery documents Home Assistant is owed right now
   (`ot_ha_want` / `ot_ha_wants` / `ot_ha_plan_*`).
2. What the **bytes** of each document are (`ot_ha_topic` / `ot_ha_render`).

The document templates are generated from the single project-wide entity list; this component
fills six tokens at runtime and decides timing. It is **pure**: no FreeRTOS, no ESP-IDF, no
allocation. It reads `ot_state` (each call takes the state lock internally) and `ot_registry`, and
writes only into caller-supplied buffers and the caller's plan struct. The impure esp-mqtt glue
that actually publishes the rendered bytes lives in the `ot_mqtt_link` component and calls this one
on its own task.

## Responsibility

**Owns:**
- The read side of the generated discovery table (`ot_ha_doc_count`, `ot_ha_doc_at`).
- Rendering a document's topic and body from a borrowed context (`ot_ha_topic`, `ot_ha_render`),
  filling the six tokens `{P} {I} {D} {O} {L} {H}`.
- The WAIT / WANT / DROP decision per document (`ot_ha_want`, `ot_ha_wants`).
- The runtime bounds of the bounded documents (`ot_ha_bounds`).
- The per-connection plan of what has been told to Home Assistant and what is owed next
  (`ot_ha_plan_reset`, `ot_ha_plan_next`, `ot_ha_plan_done`).
- The safety guards that keep a malformed value from silently deleting an entity or a whole device
  in Home Assistant: the device-id must be twelve lower-case hex characters, `configuration_url`
  must be built from a dotted-quad IP or omitted, overflow returns 0 rather than a truncated
  document, and a bounded document with `lo_dc > hi_dc` is refused.

**Does NOT do:**
- Decide **what** an entity is in Home Assistant — its shape, keys, control gating and which take
  runtime bounds. That is fixed by the discovery generator that produces `ha_generated.h`.
- Publish anything, own a task, or touch MQTT, timers or FreeRTOS. The `ot_mqtt_link` component
  does the I/O.
- Allocate. It writes only into the caller's buffers and plan struct.

## Public API (`include/ot_ha.h`)

| Symbol | Contract |
| --- | --- |
| `OT_HA_DOC_MAX` (1536) | The publish buffer size the glue renders into; the host suite proves the worst-case document fits. |
| `OT_HA_TOPIC_MAX` (128) | Discovery topic buffer size. |
| `ot_ha_ctx_t` | BORROWED context: `prefix`, `device_id` (12 lower-case hex), `mac`, `name`, `model`, `sw_version`, `ip`. Every pointer must outlive the call. `""` means "unknown" — the key is then omitted; `NULL` is illegal. |
| `ot_ha_doc_count()` | Number of documents (`OT_HA_DOC_COUNT`, 68). |
| `ot_ha_doc_at(index)` | The document at `index`, or `NULL` past the end. |
| `ot_ha_topic(doc, ctx, out, cap)` | Renders `homeassistant/<component>/<device_id>/<object_id>/config`. Returns bytes written, or **0** when the context is unusable (e.g. device id not 12 hex chars) or it does not fit. |
| `ot_ha_render(doc, ctx, lo_dc, hi_dc, out, cap)` | The document body with its tokens filled. `lo_dc`/`hi_dc` (tenths of a degree) are read only by a bounded document. Returns **0** on refusal (`lo_dc > hi_dc`, unusable ctx) or when it does not fit. |
| `ot_ha_want_t` | `OT_HA_WAIT` (boiler has not answered this row's Data-ID), `OT_HA_WANT` (HA should have it), `OT_HA_DROP` (HA should not). |
| `ot_ha_want(doc, discovery_on, a)` | Per-document decision from discovery on/off and one availability. Pure — availability is an argument. |
| `ot_ha_wants(discovery_on, want[])` | `ot_ha_want()` for every document, availability read from `ot_state`. |
| `ot_ha_bounds(flow_min_dc, flow_max_dc, lo_dc[], hi_dc[])` | Fills the runtime bounds arrays: FLOW from the executor's flow band, STATE from `ot_state_bounds()`, 0 for unbounded. |
| `ot_ha_plan_t` | What this connection has told HA: `sent[]` (`OT_HA_SENT_*`), plus the `lo_dc[]`/`hi_dc[]` the sent config carried. A zeroed plan has told nothing. |
| `OT_HA_SENT_NOTHING / _CONFIG / _EMPTY` | Per-document sent state. |
| `ot_ha_plan_reset(plan)` | Forget everything sent — a new connection, HA's `online`, or a changed context (new name, prefix or address). Re-offers every wanted document, re-clears every dropped one. |
| `ot_ha_plan_next(plan, want[], lo_dc[], hi_dc[], empty)` | The next document owed, lowest index first, or **-1**. `*empty` = publish an EMPTY retained payload (drop) rather than the config. |
| `ot_ha_plan_done(plan, index, empty, lo_dc, hi_dc)` | Mark `index` settled for these inputs (published, or refused by `ot_ha_render` and retried only when inputs change). Out-of-range ignored. |

## Implementation

### Files
- `include/ot_ha.h` — the contract.
- `include/ha_generated.h` — GENERATED, **DO NOT EDIT**. Holds `ot_ha_doc_t`, `OT_HA_DOCS[]`,
  `OT_HA_DOC_COUNT` (68), `ot_ha_bounds_t`, and the discovery/origin constants
  (`OT_HA_DISCOVERY_PREFIX` = `"homeassistant"`, `OT_HA_ORIGIN_NAME` = `"opentherm-thermostat"`).
- `ot_ha.c` — the bytes: topic and body rendering, the token switch, the guards.
- `ot_ha_plan.c` — the timing: `ot_ha_want*`, `ot_ha_bounds`, and the plan.
- `CMakeLists.txt`.

### The generated document (`ot_ha_doc_t`)
Each row carries: `entity` (index into the entity registry), `component`
(`sensor`/`binary_sensor`/`switch`/`number`/`button`), `object_id`, `gated` (available only while
Home Assistant owns the controls), `wire` (read from the boiler — wait for an answer, drop if
unsupported), `bounds` (`OT_HA_BOUNDS_NONE`/`_FLOW`/`_STATE`), and `body` (the templated document).
Two rows share the same `entity` index (a `heating_season` binary_sensor and a `heating_season_off`
button).

### The six tokens
`ot_ha_render` walks the template; a token is exactly `{` + one capital letter + `}`:
- `{P}` — the topic prefix (escaped).
- `{I}` — the device id (the unique-id stem).
- `{D}` — the device block (`put_device`): `ids` = device id, optional `cns` = MAC, escaped `name`,
  optional `mdl`/`sw`, and `cu` = `http://<ip>/` only when `ip` is a dotted quad.
- `{O}` — the origin block (`put_origin`): `OT_HA_ORIGIN_NAME` plus optional `sw`.
- `{L}` / `{H}` — the low/high bound as a JSON number in tenths (`put_dc`: 455 → 45.5), read only
  when `doc->bounds != OT_HA_BOUNDS_NONE`.

An unknown capital letter is copied as literal text, not guessed — the tests catch a token left in
a rendered document. **DO NOT** widen the token test to "any capital letter": a guessed meaning
would pass unnoticed.

### WAIT / WANT / DROP (`ot_ha_want`)
- Discovery off → `DROP` for every document (clears entities an earlier boot retained under
  `homeassistant/`; a device the owner excluded must not linger).
- Not a wire document → `WANT` whenever discovery is on.
- Wire document, by availability: `UNKNOWN` → `WAIT`, `UNSUPPORTED` → `DROP`, `OK`/`INVALID` →
  `WANT` (supported, data may just be absent right now). Rationale: an entity that appears on every
  boot and vanishes two minutes later is worse than one that appears once.

### The plan (`ot_ha_plan_next`)
Lowest index first. A `WANT` document is owed while **stale** — never sent as CONFIG, or bounded
and its sent bounds differ from the current ones (so a changed flow band re-offers `ch_setpoint`
alone). A `DROP` document is owed while its `sent` is not yet `EMPTY` (an empty retained payload
deletes the entity in HA). A `WAIT` document is never offered.

### Invariants / DO NOTs
- **Overflow returns 0, never a truncated document.** `writer_t` (a private bounded-append sink)
  remembers overflow; `ot_ha_topic`/`ot_ha_render` return 0 if it tripped. A half-object published
  under `homeassistant/` is a broken entity HA keeps. This sink is deliberately private and shaped
  to this failure mode; other renderers in the project (e.g. the REST projection, which truncates
  for a still-readable reply) keep their own sinks — DO NOT hoist these into one shared type.
- **`OT_HA_ESCAPE_MAX` (64) is ONE number in both places** — the length check and the temp buffer
  (`6 * MAX + 1`, since a control byte escapes to `\u00XX`). As two literals a raised guard would
  silently overflow the buffer, and no test in this suite could see it. `put_escaped` pre-checks
  length because the JSON escaper drops overflow rather than reporting it.
- **`usable()` is the one gate** at the top of both entry points, so nothing downstream needs a NULL
  check. It rejects `NULL` fields and an empty `prefix` (which would publish state topics starting
  at `/`); `""` for optional metadata is legal (the key is then omitted).
- **The device id must be exactly 12 lower-case hex chars** (`hex_id`): HA's topic matcher ignores a
  topic segment with anything outside `[a-zA-Z0-9_-]`, so a MAC written with colons would publish to
  a topic HA never reads — no entity, no error. A shorter id would re-key every existing entity.
- **`configuration_url` is a dotted quad or nothing** (`dotted_quad`): a value HA's URL validator
  refuses takes the whole device block — every entity — with it.
- **Identifiers are the device id and MAC, never the name:** renaming must not re-key anything; HA
  merges devices by the MAC in `cns` (`connections`).
- **Only one JSON escaper** (`ot_json.h`) — DO NOT grow a second.
- **CMake `REQUIRES` must stay `ot_state ot_registry` (PUBLIC), `PRIV_REQUIRES ot_json`.** `mqtt`,
  `esp_timer`, `freertos` must NEVER be added: a host suite includes `ot_ha.h`, and PlatformIO
  compiles every source of a library whose header a suite includes.

## Tests

Host suites `test_ot_ha` (the rendered bytes; builds the worst-case document against
`OT_HA_DOC_MAX`) and `test_ot_ha_plan` (WAIT/WANT/DROP and the plan). The generator-side rules and
the discovery-schema validation live under `tools/tests/`, outside this component.

## Notes

- Failure contract: a render or topic that does not fit returns 0 and is a **DEFECT** — the glue
  logs the key and publishes nothing, never a truncated document.
- The only external dependencies for timing decisions are `ot_state`/`ot_registry`, read inside
  `ot_ha_wants`/`ot_ha_bounds`; `ot_ha_want` itself takes the availability as an argument and is
  fully pure.
