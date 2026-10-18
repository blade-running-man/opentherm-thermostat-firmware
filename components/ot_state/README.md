# ot_state — the decoded state model over the registry: value, availability, timestamps

## Purpose

The read-side model of what the boiler said about every entity in the registry:
decoded values indexed by registry position, three-valued availability, per-consumer
"changed" marks, read bounds, and unsupported Data-IDs.

This is not the bus's raw table. That raw table keeps the last sixteen bits for each of
the 128 Data-IDs and serves diagnostics — it has neither a codec nor a key. `ot_state`
holds decoded, keyed values and knows no identifier that is absent from the registry.
One does not replace the other.

## Responsibility

Owns:

- The decoded value of every registry entity: number or boolean, plus availability and the
  arrival timestamp.
- The three-valued availability model (`OK` / `INVALID` / `UNSUPPORTED`) plus the
  "not asked yet" `UNKNOWN`, keeping DATA-INVALID and UNKNOWN-DATAID distinct even though
  both arrive as the value `0x0000`.
- The unsupported-Data-ID verdict, reached only after two UNKNOWN-DATAID answers in a row.
- Read bounds, keyed by source Data-ID, that displace the table constants once the boiler
  reports its own range.
- Per-consumer (web, MQTT) dirty marks, set only on an actual change.
- The single write seam for synthetic entities (those whose `data_id` is negative) whose
  values come from the executor, not the wire.

Does NOT do:

- Talk to the bus or transport (it is called *from* the bus task's callback).
- Decode Manchester or frames (that is done upstream; it consumes an already parsed
  message `type` + `raw` word).
- Render JSON (that is a separate projection component).
- Return `esp_err_t` from anything — a faulty slave frame must never bring the model down.

## Public API

| Symbol | Contract |
| --- | --- |
| `ot_availability_t` | Enum: `OT_AVAIL_UNKNOWN` (not asked / no answer), `OT_AVAIL_OK`, `OT_AVAIL_INVALID` (data-invalid), `OT_AVAIL_UNSUPPORTED` (unknown-dataid twice). |
| `ot_value_t` | `{ availability, number (NaN unless OK), boolean (meaningful for the binary kind), updated_ms (0 if never) }`. |
| `ot_consumer_t` | `OT_CONSUMER_WEB`, `OT_CONSUMER_MQTT`, `OT_CONSUMER_COUNT` — each keeps its own dirty set. |
| `void ot_state_reset(void)` | Zeroes the model (numbers to NaN); for tests and first start. |
| `void ot_state_apply_dataid(uint8_t data_id, ot_msg_type_t type, uint16_t raw, uint32_t now_ms)` | Applies the boiler's answer to ALL registry entities reading this Data-ID. Called from the bus task — must not block long. |
| `bool ot_state_set_virtual(const char *key, float value, uint32_t now_ms)` | The one way a value reaches a synthetic entity. `false` (and nothing written) if key unknown/NULL, key's `data_id >= 0`, value non-finite, or an enum value that is not a whole valid option index. Any task, not from an ISR. |
| `bool ot_state_get(const char *key, ot_value_t *out)` | Copies the value out. `false` if key not in registry (or `out` NULL); `*out` untouched. |
| `bool ot_state_bounds(const char *key, float *min_out, float *max_out)` | Bounds from the read `bounds_from` Data-ID if seen, else from the entity table. `false` if key missing or the entity declares no range. |
| `bool ot_state_is_unsupported(uint8_t data_id)` | Whether the Data-ID is marked unsupported; the bus reads it to drop the ID from the round. |
| `bool ot_state_take_dirty(ot_consumer_t consumer, int index)` | Takes (returns and clears) the consumer's changed mark for that entity index. |

## Implementation

Single source file `ot_state.c`; header `include/ot_state.h`. Registered (see
`CMakeLists.txt`) requiring `ot_registry`, `ot_frame` and `ot_lock`; the value decoders
come from the codec helpers (`ot_codec.h`).

Module-static state, all under `ot_lock()`:

- `s_values[OT_ENTITY_COUNT]` — the decoded values, indexed by registry position.
- `s_dirty[OT_CONSUMER_COUNT][ceil(OT_ENTITY_COUNT/32)]` — a bitset of changed marks per
  consumer.
- `s_unknown_streak[128]` / `s_unsupported[128]` — the UNKNOWN-DATAID run length and the
  latched verdict, keyed by Data-ID.
- `s_bounds_raw[128]` / `s_bounds_seen[128]` — the last OK raw word per Data-ID, kept raw
  because the bounds codec (signed high byte / signed low byte) is a property of the
  bounds-carrying Data-IDs (48 and 49), not of the entity that reads them.

Key logic:

- **Availability.** `READ_ACK` / `WRITE_ACK` → `OK`; `DATA_INVALID` → `INVALID`;
  `UNKNOWN_DATAID` → `UNSUPPORTED` only once the streak reaches `OT_UNSUPPORTED_STREAK`
  (= 2), else `UNKNOWN`. Any other message type is neither accepted nor fatal: unlock and
  return. A non-UNKNOWN answer resets the streak to 0; there is no de-latching in the
  other direction — the reset is a reboot.
- **Storage / dirty marks.** `store()` is the one write point for both writers. The dirty
  mark is set ONLY on an actual change (availability, boolean, or number, with NaN handled
  explicitly), so a steady value published once a minute does not flood consumers.
  `updated_ms` is renewed on every apply regardless of change.
- **Fan-out.** One Data-ID feeds several entities (ID 5 feeds seven, ID 0 feeds six); the
  apply loop walks `ot_registry_first_for_id` / `ot_registry_next_for_id`. A flag entity
  stores a boolean via `ot_codec_flag`; the others decode a number through the codec
  switch. On non-OK availability, number is NaN and boolean false.
- **Bounds.** Constants from the entity table are displaced by the boiler's own read
  bounds when the entity's `bounds_from` is non-negative and that Data-ID has been seen —
  the upper bound decoded from the raw word's signed high byte, the lower from its signed
  low byte.
- **Virtual set.** Refuses boiler entities (`data_id >= 0` — a value there comes only from
  the wire), non-finite values (a renderer would emit `nan`, not JSON), and out-of-range or
  non-integral enum indices. A boolean is stored both as a boolean AND as 0/1 in `number`
  (not NaN like a flag) so a renderer that does not consult `ot_registry_is_boolean()`
  still emits valid JSON.

Invariants / DO NOT:

- Everything runs under `ot_lock()`, which is recursive, so a renderer may hold a snapshot
  and call `ot_state_get()` inside.
- `ot_state_apply_dataid()` runs on the bus task — DO NOT block for long; while it computes,
  no conversation happens.
- Two UNKNOWN-DATAID answers, not one, before marking unsupported: parity misses some
  corruption and one spoiled frame must not permanently kill a working entity.

## Tests

The model is pure and host-testable. The source pins its behaviour with named host tests
(for example `test_set_virtual_refuses_a_boiler_entity`, which checks that a boiler entity
— ID 0 included — cannot be written through `ot_state_set_virtual`). Those suites live
under the repository's `test/` tree rather than in this component directory.

## Notes

- DATA-INVALID and UNKNOWN-DATAID both arrive as `0x0000` but are different diagnoses:
  DATA-INVALID means "this entity exists, but there is no data right now" (how ID 5 answers
  when there are no faults, and ID 27 when no outside sensor is connected); UNKNOWN-DATAID
  means "I have no such identifier". Collapsing them would show "no faults" or "0 °C
  outside" where neither was said.
- Per-consumer dirty marks (not one shared set) so the web interface collecting a change
  cannot erase it for an MQTT consumer that is momentarily disconnected.
- `ot_state_set_virtual` is the seam for synthetic entities (negative `data_id`): the
  executor's decision handed in by the task layer. Availability becomes OK, the timestamp
  is always renewed, and marks rise only on an actual change.
- Failure philosophy: no function returns `esp_err_t`; an unknown key gives `false`, and a
  Data-ID outside the valid range is silently ignored.
