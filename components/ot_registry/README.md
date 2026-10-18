# ot_registry — lookup over the generated entity registry

## Purpose

`ot_registry` is the read-only lookup layer over the firmware's single list of entities. It turns
keys, indices and Data-IDs into `const ot_entity_t *` records and iterators. It holds no state and
stores nothing of its own — every record it returns points into a static table compiled into the
firmware.

## Responsibility

Owns:

- The lookup API over the entity table `OT_ENTITIES[]`: by index, by key, key→index, and Data-ID
  iteration.
- The small derived predicates a value renderer needs (is-boolean, enum option walking).
- Access to the poll ring (`OT_POLL_IDS[]`).

Does NOT do:

- **It does not own the list.** `include/registry_generated.h` is generated output and carries a
  `DO NOT EDIT` banner. It is produced by `tools/generate_registry.py` from the entity definitions
  in `tools/opentherm_ids.py`. To change the entities, their kinds, codecs, poll ring, bounds,
  units, etc., edit `tools/opentherm_ids.py` (and the generator if the record shape changes) and
  regenerate — never edit the header. An edit made directly in the header is silently overwritten
  by the next build.
- No decoding of frame payloads, no state storage, no JSON projection, no scheduling. This
  component is pure lookup.
- No hash table or secondary index — see Implementation.

## Public API

All functions are declared in `include/ot_registry.h`. Every returned pointer leads into the
header's static data: do not free it, it lives for the program's lifetime. All functions are pure,
stateless, and safe from any task and from an ISR.

| Function | Contract |
| --- | --- |
| `uint16_t ot_registry_count(void)` | Number of entities (`OT_ENTITY_COUNT`). |
| `const ot_entity_t *ot_registry_at(uint16_t index)` | Record at `index`, or `NULL` if out of range. |
| `const ot_entity_t *ot_registry_by_key(const char *key)` | Record for `key`, or `NULL` if the key is absent or `key == NULL`. |
| `int ot_registry_index_of(const char *key)` | Position of `key`, or `-1` if absent (or `key == NULL`). Stable within one firmware build; used as the index into the parallel value arrays that track each entity's state. |
| `int ot_registry_first_for_id(uint8_t data_id)` | First position whose entity reads `data_id`, or `-1`. Start of the per-ID iteration. |
| `int ot_registry_next_for_id(uint8_t data_id, int after)` | Next position after `after` reading `data_id`, or `-1` when there are no more. |
| `bool ot_registry_is_boolean(const ot_entity_t *e)` | True when the entity's value is boolean (`kind` is `OT_KIND_BINARY` or `OT_KIND_SWITCH`); `false` for `NULL`. Decided by kind, not codec. |
| `uint8_t ot_registry_option_count(const ot_entity_t *e)` | Number of options of an `OT_KIND_ENUM` entity; `0` for every other kind and for `NULL`. |
| `const char *ot_registry_option(const ot_entity_t *e, unsigned index, size_t *len)` | The `index`-th enum option, NOT NUL-terminated (length into `*len`, which may be `NULL`); print with `"%.*s"`. `NULL` when not an enum or `index` is past the last option. |
| `uint16_t ot_registry_poll_count(void)` | Length of the poll ring (`OT_POLL_ID_COUNT`). |
| `uint8_t ot_registry_poll_at(uint16_t index)` | Poll ring Data-ID at `index`, or `0` if out of range. |

### Why lookup-by-ID returns an iterator

One Data-ID carries several entities — ID 5 is six flags plus the manufacturer code, ID 0 is six
status flags. Returning the first record found would never fill in the rest, so per-ID access is an
iterator:

```c
for (int i = ot_registry_first_for_id(id); i >= 0;
     i = ot_registry_next_for_id(id, i))
    ...
```

## Implementation

Key files:

- `ot_registry.c` (105 lines) — the entire implementation.
- `include/ot_registry.h` — the public contract.
- `include/registry_generated.h` — the generated table (do not edit).
- `CMakeLists.txt` — registers the component with `SRCS ot_registry.c`, `INCLUDE_DIRS include`.

Generated table shape (`registry_generated.h`):

- `ot_entity_kind_t`: `OT_KIND_SENSOR`, `OT_KIND_BINARY`, `OT_KIND_NUMBER`, `OT_KIND_SWITCH`,
  `OT_KIND_ENUM`.
- `ot_codec_kind_t`: `OT_CODEC_F88`, `_U16`, `_S16`, `_U8_HB`, `_U8_LB`, `_S8_HB`, `_S8_LB`,
  `_FLAG`, `_NONE`.
- `ot_entity_t` — one record per entity: `key` (the stable identifier used as API path, MQTT topic
  and object_id), `name`, `kind`, `data_id` (`0..127`, or `-1` for a synthetic row with no
  Data-ID), `codec`, `flag_high_byte`/`flag_bit` (meaningful when `OT_CODEC_FLAG`), `readable`
  (takes part in the poll ring), `writable`, `control` (`0` none, else a control-command code),
  `write_id` (`-1` when it coincides with `data_id`), `bounds_from` (Data-ID of the bounds, `-1`
  when none), `unit`, `device_class`, `state_class`, `icon`, `entity_category`, `options`
  (`"a|b|c"`, `NULL` unless `OT_KIND_ENUM`), and `min_value`/`max_value`/`default_value` (`NaN` when
  the table gives no range).
- `OT_ENTITIES[OT_ENTITY_COUNT]` — the static array (currently `OT_ENTITY_COUNT == 72`).
- `OT_POLL_IDS[OT_POLL_ID_COUNT]` — the poll ring apart from the mandatory ID 0 (currently 30 IDs).
  ID 0 is deliberately absent: the scheduler sends it on every second step, so listing it here would
  double its rate.
- `OT_SCHEMA_VERSION` (currently `1`), `OT_OPTION_SEP` (`'|'`, the enum option separator).

Lookup structures and invariants:

- **Linear search, on purpose.** `ot_registry_index_of` scans `OT_ENTITIES[]` with `strcmp`. The
  registry is under a hundred records and key lookup happens on an HTTP request, not in the boiler
  conversation loop; a hash table would be a third representation of the same list. `by_key` is
  `index_of` plus a dereference.
- **Synthetic rows are never yielded by ID iteration.** The internal `reads_id()` predicate requires
  `e->data_id >= 0 && e->data_id == (int16_t)data_id`. A synthetic row has `data_id == -1`; its value
  is set directly rather than read from the wire, so it must not be returned for any `data_id`.
  - **DO NOT drop the `>= 0` guard, and DO NOT cast `data_id` to `(uint8_t)` inside the compare.**
    The guard looks dead — an `int16_t -1` never equals a promoted `uint8_t` — but it is the one
    thing that keeps a synthetic row out the day the comparison truncates to a byte, where `-1`
    becomes `255`, a legal frame ID. The host test
    `test_a_synthetic_row_is_never_yielded_for_any_data_id` kills only the *pair* of changes (guard
    dropped AND a `(uint8_t)` cast); either change alone still passes it, so review must catch it.
- **`ot_registry_is_boolean` is decided by kind, not codec.** A synthetic switch has `OT_CODEC_NONE`,
  and treating "boolean" as "codec is `OT_CODEC_FLAG`" would print it as `1.00`. Every value renderer
  asks this one predicate, so they cannot disagree about which rows are booleans.
- **Enum options are walked in place, not split into a table.** `option_count` counts `OT_OPTION_SEP`
  occurrences plus one; `option` walks to the `index`-th segment and returns a non-terminated slice
  with its length. An enum has a handful of short options, and a split table would be a second
  representation of the generated string.

## Tests

No dedicated host suite lives in this directory. The synthetic-row invariant above is pinned by the
host test `test_a_synthetic_row_is_never_yielded_for_any_data_id` (it kills the dropped-guard +
`uint8_t`-cast pair). The generator that produces `registry_generated.h` is covered by its own pytest
suite under `tools/tests/`.

## Notes

- **Single list.** REST paths, MQTT topics, Home Assistant discovery and the frontend types are all
  generated from the same entity definitions in `tools/opentherm_ids.py`. This component is only the
  lookup into that one list; to change anything about the entities, fix the generator input, not the
  header.
- **Index stability.** `ot_registry_index_of` returns a position that is stable within one firmware
  build and is used directly as the index into the parallel arrays that hold each entity's value,
  availability and timestamps.
- **ID 0 and the poll ring.** ID 0 (master/slave status) is intentionally excluded from
  `OT_POLL_IDS`; the scheduler emits it every second step, so including it here would double its rate.
- **Synthetic rows.** A synthetic row (`data_id == -1`) has no Data-ID and never appears on the wire;
  its value is set directly rather than decoded from a boiler frame, which is why the Data-ID iterator
  never yields it.
</content>
</invoke>
