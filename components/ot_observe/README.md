# ot_observe — the raw view over all 128 OpenTherm Data-IDs

A diagnostic table of what the bus has already heard from the boiler: the latest response,
type, raw sixteen bits, age and count for every Data-ID in the 0..127 identifier space. It is
the raw identifier space, not the state model. `ot_state` is indexed by the entity registry and
knows the *meaning* of values; `ot_observe` knows only the *identifier* and the sixteen bits as
they are.

## Responsibility

Owns:
- One in-RAM slot per Data-ID (`OT_OBSERVE_IDS == 128`): `seen`, latest message `type`, `raw`,
  `last_ms`, `count`.
- Recording a response, looking a slot up, counting seen IDs, and rendering the whole table plus
  a bus-stats header as a JSON document (the Data-ID sweep / raw view).

Does NOT do:
- **Never becomes a source of entities.** The single entity list is produced by the generator
  (`tools/opentherm_ids.py`); from this table a human merely learns what is worth adding to it.
- Does not know the meaning of any value — no decoding, no units, no registry lookup.
- Does not depend on ESP-IDF or on `ot_bus`. Time and the header stats arrive as arguments
  (filled by the caller from `ot_bus_stats` / `ot_master_input_duty()`), keeping it pure and
  host-buildable.
- Does not persist. It is RAM-only and does not survive a reboot: diagnostics, not configuration.
  A saved copy would quickly diverge from the boiler.

## Public API

| Function / type | Contract |
| --- | --- |
| `ot_observe_t` | The table: `ot_observe_entry_t e[OT_OBSERVE_IDS]`. |
| `ot_observe_entry_t` | One slot: `bool seen`, `uint8_t type` (`ot_msg_type_t` of the latest response), `uint16_t raw`, `uint32_t last_ms`, `uint32_t count`. |
| `ot_observe_header_t` | Document header, filled by the caller from bus stats: uptime, cycles, ok/failed/overdue counts, `answering`, `in_duty`, the failure breakdown (`timeout`, `frame_error`, `parity_error`), `rx_edges`, `pins_shorted`, `scan_done`/`scan_total`. |
| `void ot_observe_reset(ot_observe_t *o)` | Zeroes the whole table (`memset`). |
| `void ot_observe_record(ot_observe_t *o, uint8_t data_id, ot_msg_type_t type, uint16_t raw, uint32_t now_ms)` | Records a response into slot `data_id`. `data_id >= OT_OBSERVE_IDS` is discarded silently (a frame from a faulty slave, not the caller's error — crashing over it is not allowed). Increments `count` (saturating at `UINT32_MAX`). |
| `const ot_observe_entry_t *ot_observe_get(const ot_observe_t *o, uint8_t data_id)` | Returns a pointer into internal storage, or `NULL` if `data_id` is outside the table. Do not free. |
| `uint32_t ot_observe_seen_count(const ot_observe_t *o)` | Number of Data-IDs seen at least once. |
| `size_t ot_observe_render_json(const ot_observe_t *o, const ot_observe_header_t *h, uint32_t now_ms, char *out, size_t cap)` | Renders the header plus one object per seen ID into `out`. snprintf semantics: returns the REQUIRED size without the terminating NUL, never overflows the buffer, always leaves it a valid string. A caller that ran out of room can then allocate exactly what is needed instead of guessing a margin. |

## Implementation

Two hand-written files, both small and well under the 350-line ceiling:

- `include/ot_observe.h` — the contract and the structs. `OT_OBSERVE_IDS` is 128 because a
  Data-ID is eight bits but the spec defines only 0..127; anything above is discarded.
- `ot_observe.c` — the four operations plus two static helpers.

Key implementation points:

- **`type_word()`** maps each `ot_msg_type_t` to a JSON string. `OT_MSG_UNKNOWN_DATAID`
  ("I do not have such a thing") and `OT_MSG_DATA_INVALID` ("I do, but I have nothing to say
  right now") are kept as **different** words — merging them would lose half the diagnostics.
  The `switch` covers every enumerator, with a trailing `return "?"`.
- **`appendf()`** (a `printf`-format-checked variadic helper) appends with snprintf semantics,
  accumulating the REQUIRED length in `*need` even after the buffer is full; it writes only while
  there is room left for the terminating NUL. `*need` is what `ot_observe_render_json` returns.
- **Output order is ascending Data-ID** — the array traversal order is the output order —
  because the table is read by eye and jumping rows ruin it. Only `seen` slots are emitted.
- **`age = now_ms - e->last_ms`** is deliberately unsigned: the wrap-around survives the
  millisecond overflow after ~49 days, where a signed subtraction would yield a negative age and
  an untrustworthy table.

Invariants / DO NOT:
- **DO NOT** turn this table into a source of entities. The generated list is the single source.
- `count` saturates rather than wraps: `if (e->count < UINT32_MAX) e->count++;`.
- `ot_observe_render_json` writes `out[0] = '\0'` when `cap > 0`, so the buffer is always a
  string even on truncation.

## Tests

Host suite: `test/test_ot_observe/test_ot_observe.cpp` (`pio test -e native -f test_ot_observe`).
The component is pure and framework-free, so the host build compiles the same source the device
runs. Its only build dependency is `ot_frame` (for `ot_msg_type_t`).

## Notes

- CMake: `idf_component_register(SRCS "ot_observe.c" INCLUDE_DIRS "include" REQUIRES ot_frame)`.
- The component name in the design/layout docs is described as "raw and over all 128 IDs",
  explicitly contrasted with `ot_state` (the registry-indexed, meaning-aware state model).
- The header stats (`ot_observe_header_t`) are passed in rather than pulled, precisely so the
  component never has to depend on `ot_bus` and stays host-buildable; the failure breakdown is
  kept split (a timeout = not a single input edge, i.e. dead receive path or a silent boiler;
  a frame error = edges that did not come together: polarity, timings, interference) because
  summing them into one number would lose the only fork there is.
