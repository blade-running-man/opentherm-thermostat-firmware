<script setup>
import { data } from '../reference/entities.data'
</script>

# Entity registry

Every entity the firmware exposes — over REST, over MQTT, in Home Assistant discovery and in the
web UI — comes from **one** table, `tools/opentherm_ids.py`. There is no second, hand-written
entity list anywhere in the tree, and there must never be one: a second list is how four surfaces
end up disagreeing while each of them still agrees with itself. This page covers the table, the
generator and its outputs, the entity kinds, the synthetic entities, and how to add an entity end
to end.

## The pipeline

```text
tools/opentherm_ids.py            the one table (pure Python data)
tools/entity_names_i18n.py        DE/NL/UK display names, keyed by entity key
        │
        ▼
tools/generate_registry.py        the driver: where each output is written, --check
  └─ tools/registry_model.py      loads, validates and flattens the table into one Entity list
       ├─ render_c.py             ─► components/ot_registry/include/registry_generated.h
       ├─ render_ts.py            ─► web/src/api/entities.ts
       ├─ render_discovery.py     ─► components/ot_ha/include/ha_generated.h
       └─ render_entity_names_ts.py ─► web/src/i18n/entityNames.generated.ts
```

- **`tools/opentherm_ids.py`** declares every entity as data, in three tables (`VALUES`, `FLAGS`,
  `VIRTUALS`), plus `SCHEMA_VERSION` and `POLL_IDS`, the poll ring computed from the table. It
  describes the **OpenTherm specification**, not a particular boiler: what a boiler actually
  supports is discovered at runtime from its UNKNOWN-DATAID answers (see [Bus](/protocol/bus)),
  and that never lands here — otherwise the firmware would stop being one binary for any boiler.
  The read/write direction of every row was checked against §5.4 of the OpenTherm 2.2
  specification, the one source that marks every ID R-, -W or RW.
- **`tools/registry_model.py`** turns the three table shapes into one flat list of `Entity` rows.
  Flattening happens there and only there, so every renderer walks the same list and no two
  outputs can drift by one of them learning a table shape the other does not know. It also
  refuses a duplicate key across the tables and an unknown control name. The C enums are numbered
  by `KINDS` and `CODECS` tuple order — append only.
- **`tools/generate_registry.py`** owns the command line and the output paths. It deliberately
  writes `web/src/api/entities.ts` and nothing else under `web/src/api/`: the device configuration
  types (`DeviceConfig`, `ConfigPatch` in `web/src/api/config.ts`) are hand-written, because the
  table knows nothing about Wi-Fi, the broker or the password, and generating into that module
  would erase them on the first run.

### The outputs

| Output | Renderer | What it is | Read by |
| --- | --- | --- | --- |
| `components/ot_registry/include/registry_generated.h` | `render_c.py` | `OT_ENTITIES[OT_ENTITY_COUNT]` (`ot_entity_t`), the kind and codec enums, `OT_POLL_IDS`, `OT_SCHEMA_VERSION`, `OT_OPTION_SEP` | `ot_registry`, and through it the state model, `ot_api`, `ot_command`, the bus scheduler |
| `web/src/api/entities.ts` | `render_ts.py` | `ENTITIES` (key, name, kind, Data-ID, codec, unit, device class, icon, category, readable, writable, min, max, options), the derived `EntityKey` and `EntityKind`, `Availability`, `StateEntry`, `SCHEMA_VERSION` | the SPA; this site's entity tables |
| `components/ot_ha/include/ha_generated.h` | `render_discovery.py` | `OT_HA_DOCS[]`: one templated discovery document per Home Assistant entity, with its component, object id, gating, `wire` flag and bounds source | `ot_ha`, which fills six tokens at runtime |
| `web/src/i18n/entityNames.generated.ts` | `render_entity_names_ts.py` | `ENTITY_NAMES`: every entity's display name in English, German, Dutch and Ukrainian | the SPA's language switcher (`web/src/i18n/entityName.ts`) |

Every output carries a `GENERATED … DO NOT EDIT` banner and is committed. **Fix the table or the
generator, never the output**: a hand edit is a second list of entities, and the next build
silently undoes it. The key union in `entities.ts` is derived from the array
(`(typeof ENTITIES)[number]["key"]`), never listed a second time.

`ot_api` prints registry strings raw through `%s`, so it carries no JSON escaper.
`test_no_registry_string_needs_json_escaping` makes that safe by banning, at the entrance to the
table, any string that would need escaping (a quote, a backslash, a control character).

### The translation overlay cannot drift

`entity_names_i18n.py` is a second table keyed by entity key — exactly the shape that drifts. It
does not, because `render_entity_names_ts.py` refuses to generate unless the overlay's key set is
exactly the registry's: no missing key, no orphan, no empty name. That refusal fails generation
and so the firmware build. The overlay has no English column: English is the table's `name`, the
one Home Assistant sees through discovery, and nothing of the other three reaches the firmware, so
Home Assistant stays English whatever the UI shows. `web/src/i18n/tests/entityNames.test.ts`
echoes the guard on the emitted file.

## When it runs

`tools/regenerate.py` is a PlatformIO `pre:` hook on both board environments, so generation runs
before every firmware build (see [Building](/development/building)). It can also be run by hand:

```sh
python3 tools/generate_registry.py           # write the four outputs
python3 tools/generate_registry.py --check   # exit non-zero, naming each stale output
```

## Entity kinds and fields

The table has three row shapes; the model maps them to five generated kinds.

| Table | One row is | Generated kind |
| --- | --- | --- |
| `Value` | a numeric field of one DATA-VALUE, parsed by one codec | `number` if `writable`, else `sensor` |
| `Flag` | one bit of a flag byte (read-only) | `binary` |
| `Virtual` | a synthetic entity with no Data-ID | its own `kind`: `switch`, `sensor`, `binary` or `enum` |

A flag byte becomes one entity per bit because Home Assistant cannot show eight meanings inside
one number.

**`Value` fields:** `data_id`, `key`, `name`, `codec`, `unit`, `device_class`, `state_class`,
`icon`, `entity_category`, `readable` (takes part in the poll ring), `writable`, `write_id` (the
Data-ID of the write when it differs), `min_value`, `max_value`, `default`, `bounds_from` (a
Data-ID whose answer replaces the bounds at runtime — its `s8_hb` is the upper, its `s8_lb` the
lower, the shape of IDs 48 and 49), and `control` (the executor command a write is, or `None` for
a plain frame write).

**`Flag` fields:** `data_id`, `key`, `name`, `high_byte`, `bit`, `device_class`, `icon`,
`entity_category`, `readable`.

**`Virtual` fields:** `key`, `name`, `kind`, `writable`, `control`, `options` (enum only; the
stored value is the option's **index**), `unit`, `device_class`, `state_class`, `icon`,
`entity_category`. There is no `data_id` field at all, so no row can be given one by a slip; the
generator emits `-1`. Virtuals are a third table rather than rows of `VALUES` because a readable
`Value` puts its Data-ID into the poll ring, and there is no Data-ID to give a synthetic row — as
ID 0 it would be decoded from every ID 0 reply.

**Codecs** are named explicitly on every row: `f88` (signed 8.8 fixed point), `u16`, `s16`,
`u8_hb`, `u8_lb`, `s8_hb`, `s8_lb`, plus `flag` for a `Flag` and `none` for a `Virtual`. There is
no field width in the table and cannot be one: the sixteen bits mean different things for
different Data-IDs, and that cannot be derived from the number.

**Controls** are the executor's commands, named in the table and numbered by `CONTROLS` in
`registry_model.py`: `CH_ENABLE` 1, `CH_SETPOINT` 2, `DHW_ENABLE` 3, `DHW_SETPOINT` 4, `SEASON` 5 —
the values of `ot_control_cmd_t` in `components/ot_control/include/ot_control.h`; 0 means none.

Missing metadata renders as `NULL` / `null`, never `""`, so a consumer can tell "no unit" from an
empty one; a missing range renders as `NAN` / `null`, never 0, which would mean a range from zero
to zero.

The table has **{{ data.total }}** entities, **{{ data.writable }}** of them writable,
over **{{ data.byDataId.length }}** Data-IDs; **{{ data.synthetic.length }}** are synthetic. The
full list is [Entities](/reference/entities); the Data-IDs, grouped as the boiler sees them, are
on [Boiler](/boiler/). The ID 0 **high** byte — the master's status flags, CH enable among them —
is deliberately not an entity: it is the executor's own decision, held once in `ot_control`, and
an entity for it would be a second place the same thing lives.

## The synthetic entities

| Key | Kind | Writable | Control | What it is |
| --- | --- | --- | --- | --- |
| `ch_enable` | switch | yes | `CH_ENABLE` | the owner's or HA's heating request; feeds the watchdog |
| `dhw_enable` | switch | yes | `DHW_ENABLE` | hot water on or off; persisted; not watchdog-fed |
| `heating_season` | switch | yes | `SEASON` | the season; HA may only turn it off |
| `control_mode` | enum `local` · `ha` | no | — | who owns the controls |
| `control_state` | enum `season_off` · `boost` · `local` · `ha_waiting` · `failsafe` · `ha` | no | — | the ladder row; reason and cause as attributes |
| `ch_setpoint_effective` | sensor °C | no | — | the ID 1 value actually on the wire |
| `ch_enable_effective` | binary | no | — | the CH bit actually sent |
| `failsafe_count` | sensor | no | — | failsafe entries |
| `last_failsafe_duration_s` | sensor s | no | — | how long the last failsafe lasted |
| `room_temperature_effective` | sensor °C | no | — | the display pick over the room sources |
| `room_source` | enum `none` · `shield` · `mqtt` | no | — | which source that pick came from |

Option strings are the executor's own names in its enum order, because the stored value is the
index and `ot_control` hands in its enum ordinal; `test_virtuals.py` pins that order. The first
nine rows are the executor's and are written by `ot_thermostat_report.c` from `ot_control_io`'s
list; the room pair is written by `ot_thermostat_room.c`. Both go through
`ot_state_set_virtual()`, which refuses a key with a Data-ID (a boiler value arrives only from the
wire), a NaN or infinite value, and an enum index outside the options. What each executor entity
means is on [Executor](/architecture/executor); the room pair on
[Room sources](/device/room-sources).

The CH flow setpoint is the **real** ID 1 entity (`ch_setpoint`, `f88`, `control = CH_SETPOINT`)
and the DHW setpoint the ID 56 entity (`dhw_setpoint`, `bounds_from = 48`,
`control = DHW_SETPOINT`), not synthetic copies. There is no `climate` entity — the firmware has
no control loop to be a thermostat with — and no per-second "command age" entity:
`control_state` already tells `ha`, `ha_waiting` and `failsafe` apart.

## From a row to Home Assistant

The discovery component is decided by `render_discovery.py` from the row's control and
readability, not from the kind alone:

| Row | Home Assistant gets |
| --- | --- |
| `control = SEASON` | a `binary_sensor` for the state and a `button` that sends off — a switch would snap back when turned on |
| `control = CH_SETPOINT` / `DHW_SETPOINT` | a `number`, its bounds filled at runtime (the flow band; the boiler's ID 48) |
| any other control | a `switch` |
| readable, or synthetic | a `sensor`, or a `binary_sensor` for a binary |
| write-only frame row (IDs 14, 16, 24, 124, 126) | no document at all |

Controls are **gated**: available only while Home Assistant owns them. A row read from the boiler
is marked `wire`, so its document waits until the ID has been answered and is dropped once the
boiler says UNKNOWN-DATAID. Home Assistant may write no OpenTherm frame directly; that refusal is
`OT_CMD_NOT_FOR_HA` in `ot_command`. See
[Home Assistant discovery](/interfaces/home-assistant-discovery).

## Adding an entity

1. **Add the row** to `VALUES`, `FLAGS` or `VIRTUALS` in `tools/opentherm_ids.py`, with an
   explicit codec and an English `name`. A readable row's Data-ID joins the poll ring
   automatically; the ring holds at most `OT_BUS_MAX_POLL` = 32 IDs, which `test_registry.py`
   checks. A writable row declares a range.
2. **Add its three translations** under the same key in `tools/entity_names_i18n.py`. Generation
   fails until you do.
3. **Regenerate**: `python3 tools/generate_registry.py`, or build the firmware.
4. **Give a writable row its behaviour.** A plain frame write is legal once the row is writable:
   `ot_command` checks writability, the boiler's support and the bounds (see
   [Commands](/protocol/commands)). A new executor command needs its value appended to
   `ot_control_cmd_t` **and** to `CONTROLS`, with `test_virtuals.py` (`CONTROLS` against its own copy
   of the enum's numbers) and `test_ot_command_check`
   (`test_every_control_row_names_the_command_of_its_key`) pinning the two together, and
   its handling in `ot_control`; a new discovery shape is a change to `render_discovery.py`.
5. **Give a synthetic row a writer** through `ot_state_set_virtual()`, and decide in
   `test_virtuals.py` whether it is an executor row or not: the test requires the two contracted
   sets to exhaust `VIRTUALS`.
6. **Run the gates**: `python3 -m pytest tools/tests -q` (and the HA schema check under `HA_PY`),
   `pio test -e native`, `cd web && npm test` and `npm run build` (see
   [Testing](/development/testing)).

`SCHEMA_VERSION` (1) is the version of the table's shape, emitted as `"schema"` in
`GET /api/entities` and `GET /api/state`.

### Why the key is the contract

The `key` is the stable id: the REST path `/api/entities/{key}`, the MQTT topics
`<prefix>/<key>/state` and `<prefix>/<key>/set`, and the discovery object id and unique id. The
`name` is display text nothing keys on. Renaming a key re-keys the Home Assistant entity and breaks
every automation that names it; renaming a name does not.
