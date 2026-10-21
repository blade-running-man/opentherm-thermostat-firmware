# Home Assistant discovery

With `ha_discovery` on (the default), the device announces every entity to Home Assistant through
MQTT discovery, so it appears as one device with its sensors, switches and numbers and no YAML.
This page covers what each document contains, how it is produced, and when each one is owed; the
topics and the publisher it rides on are on [MQTT](/interfaces/mqtt), and setting it up on the Home
Assistant side is on [Home Assistant](/guide/home-assistant).

Three pieces, along the usual seam:

| Piece | Where | What it decides |
| --- | --- | --- |
| The generator | `tools/render_discovery.py` | **What** each entity is in Home Assistant: its component, keys, gating and bounds — one templated document per entity, written into `components/ot_ha/include/ha_generated.h` |
| `ot_ha` | `components/ot_ha` (pure, host-tested) | **Which** documents are owed now (`ot_ha_want`, the plan), and the **bytes** of each (six tokens filled at runtime) |
| `ot_mqtt_link` | `components/ot_mqtt_link` | Publishes the bytes from its one task, retained, QoS 1 |

The documents are generated from the same single entity list as everything else
(`tools/opentherm_ids.py`, see [Entity registry](/development/entity-registry)), so Home Assistant,
REST, MQTT and the web UI cannot disagree about which entities exist. `ha_generated.h` is an output:
fix the generator, not the file.

## Topics and identifiers

Every document goes to

```
homeassistant/<component>/<device_id>/<object_id>/config
```

| Part | Value |
| --- | --- |
| `homeassistant` | `OT_HA_DISCOVERY_PREFIX` — Home Assistant's default, where it listens |
| `<component>` | `sensor`, `binary_sensor`, `switch`, `number` or `button` |
| `<device_id>` | Twelve lower-case hex digits of the MAC — the node id |
| `<object_id>` | The registry key, or `<key>_off` for the season's button |

| Key | Value | Why |
| --- | --- | --- |
| `uniq_id` | `<device_id>_<object_id>` | Never the device name: renaming must not re-key an entity |
| `def_ent_id` | `<component>.opentherm_<object_id>` | A stable entity id in Home Assistant |
| `dev.ids` | `[<device_id>]` | |
| `dev.cns` | `[["mac", "aa:bb:cc:dd:ee:ff"]]` | Home Assistant merges devices by connection; the colons of a MAC live here, never in a topic |
| `dev.name` | `device_name` | The owner's name for the device, the only thing a rename changes |
| `dev.mdl`, `dev.sw` | the project name and the firmware version | |
| `dev.cu` | `http://<ip>/` | The configuration URL — only when the IP is a dotted quad |
| `o` | `{"name":"opentherm-thermostat","sw":…}` | The origin block |

Entity names are the canonical English names from the registry. The web UI's German, Dutch and
Ukrainian names are a web-only overlay and never reach discovery, so Home Assistant stays English
(see [Web UI](/interfaces/web-ui#languages)).

## Which component for which entity

`build_docs()` in `render_discovery.py` decides it from the entity's kind **and** its executor
command — not from the kind alone:

| Entity | Component | Gated | Notes |
| --- | --- | --- | --- |
| `ch_setpoint` | `number` | yes | `min`/`max` from the executor's flow band at runtime, step 0.5, box mode |
| `dhw_setpoint` | `number` | yes | `min`/`max` from the boiler's own bounds at runtime, step 1 |
| `ch_enable`, `dhw_enable` | `switch` | yes | Command `1`/`0`, state `true`/`false` |
| `heating_season` | `binary_sensor` **and** `button` | button only | The state, plus an off-only button `heating_season_off` that publishes `0` |
| A readable binary or switch entity | `binary_sensor` | no | `pl_on` `true`, `pl_off` `false` |
| An enum (`control_state`, …) | `sensor` | no | `dev_cla: enum` with `ops` listing every option |
| Any other readable or synthetic entity | `sensor` | no | `dev_cla`, `stat_cla`, `unit_of_meas` when the registry has them |
| A write-only frame row (IDs 14, 16, 24, 124, 126) | none | — | No state to show, and Home Assistant may not write a frame |

Two documents share the `heating_season` entity. The season is a binary sensor plus an off button,
never a switch, because Home Assistant may turn the heating season **off** but never on — turning it
on is a local decision (`OT_CMD_SEASON_ON_IS_LOCAL`). A switch would offer an "on" that is always
refused. `control_state` also carries `json_attr_t` pointing at `<prefix>/control_state/attributes`,
the executor's `reason` and `cause`.

Command payloads are the numbers `ot_command` reads (`1`, `0`, a setpoint); state payloads are
`ot_api`'s spelling (`true`, `false`, the option text, `None`). No document asks Home Assistant to
retain a command — a retained command would be refused anyway (see
[MQTT](/interfaces/mqtt#the-retained-command-refusal)).

### An expanded document

`ch_setpoint` for a device `aabbccddeeff` with a flow band of 40–70 °C, abbreviated keys as
published:

```json
{"name":"CH setpoint","uniq_id":"aabbccddeeff_ch_setpoint",
 "def_ent_id":"number.opentherm_ch_setpoint",
 "avty":[{"t":"opentherm/aabbccddeeff/status"},{"t":"opentherm/aabbccddeeff/control/owner"}],
 "avty_mode":"all","ic":"mdi:thermometer",
 "dev":{"ids":["aabbccddeeff"],"cns":[["mac","aa:bb:cc:dd:ee:ff"]],"name":"Boiler",
        "mdl":"opentherm-thermostat","sw":"1.0.0","cu":"http://192.168.1.20/"},
 "o":{"name":"opentherm-thermostat","sw":"1.0.0"},
 "stat_t":"opentherm/aabbccddeeff/ch_setpoint/state",
 "cmd_t":"opentherm/aabbccddeeff/ch_setpoint/set",
 "min":40,"max":70,"step":0.5,"mode":"box","unit_of_meas":"°C","dev_cla":"temperature"}
```

## Availability

Every document lists `<prefix>/status` as its availability topic (`avty_t`). The **gated**
documents — the two switches, the two numbers and the season button — list two topics instead,
`<prefix>/status` and `<prefix>/control/owner`, with `avty_mode: all`. The owner topic reads `online`
only while `control_mode` is HA, so in LOCAL mode Home Assistant shows those controls unavailable and
never publishes their commands. A gated document always carries both topics **and** `all`: either one
alone is an entity that is never unavailable. The read-only entities, the season's binary sensor
included, are available in every mode.

## The six tokens

`render_discovery.py` writes each document as compact JSON with six placeholders; `ot_ha_render()`
(`components/ot_ha/ot_ha.c`) fills them on the device. A token is exactly `{`, one capital letter and
`}` — compact JSON never contains that outside a string, and the generator's tests forbid a brace in
any static string.

| Token | Filled with | Example |
| --- | --- | --- |
| `{P}` | The topic prefix, JSON-escaped, inside a string | `opentherm/aabbccddeeff` |
| `{I}` | The device id, the unique-id stem | `aabbccddeeff` |
| `{D}` | The device block, a JSON object | `{"ids":["aabbccddeeff"],…}` |
| `{O}` | The origin block, a JSON object | `{"name":"opentherm-thermostat","sw":"1.0.0"}` |
| `{L}` | The lower bound in tenths, as a JSON number | `400` → `40`, `455` → `45.5` |
| `{H}` | The upper bound, likewise | `700` → `70` |

`{L}` and `{H}` are read only by a bounded document. An unknown capital letter is copied as literal
text, never guessed, and the tests catch a token left in a rendered document. `expand()` in the
generator is the Python statement of the same substitutions; the Python tests expand every document
with it, and the C suite `test_ot_ha` pins `ot_ha_render()` to the same bytes.

### Why templates and not documents built on the device

The shape of each document — which keys, which component, gated or not — is fixed per entity at
build time and can be tested exhaustively on the host, including against Home Assistant's own
schemas. What only the device knows (its id, MAC, name, address, version and the current bounds) is
exactly the six tokens. Building documents in C would put the shape where only a flashed device can
check it.

## When each document is owed

### WAIT, WANT, DROP

`ot_ha_want()` decides per document, from the discovery flag and the entity's availability:

| Discovery | Document | Availability | Decision |
| --- | --- | --- | --- |
| off | any | any | **DROP** |
| on | not read from the boiler (synthetic entities, `ch_setpoint`, the switches) | any | **WANT** |
| on | read from the boiler (`wire`) | `unknown` (never answered) | **WAIT** |
| on | `wire` | `unsupported` (UNKNOWN-DATAID) | **DROP** |
| on | `wire` | `ok` or `invalid` | **WANT** |

- **WAIT** keeps an entity the boiler has not yet answered for out of Home Assistant entirely. An
  entity that appears on every boot and vanishes two minutes later is worse than one that appears
  once.
- **DROP** is published as an **empty retained payload** on the document's topic, which deletes the
  entity in Home Assistant. A boiler that answers UNKNOWN-DATAID for an ID loses that entity cleanly;
  discovery switched off clears every entity an earlier boot left retained under `homeassistant/`.
- `invalid` is still WANT: the boiler supports the ID and simply has no data right now.
- `wire` marks a row whose Data-ID is **read** from the boiler. `ch_setpoint` is not one — ID 1 is
  write-only, its unsupported flag never clears, and it must not hide the setpoint Home Assistant
  commands — and neither are the switches or the season, which never touch the wire. `dhw_setpoint`
  **is** one (ID 56 is read back), so it waits for the boiler's first answer and is dropped from a
  boiler that does not support ID 56: it is the one control entity that can disappear.

### The plan

`ot_ha_plan_t` remembers, per connection, what Home Assistant has been told for each document —
nothing, the config, or the empty payload — and the bounds the config carried. `ot_ha_plan_next()`
returns the lowest-index document still owed:

- a WANT document whose config was never sent, or a bounded one whose sent bounds differ from the
  current ones — so a changed flow band re-offers `ch_setpoint` alone;
- a DROP document whose empty payload was not yet sent.

A WAIT document is never offered. The publisher sends at most eight messages a pass, documents
last (see [MQTT](/interfaces/mqtt#publishing)), and marks each one settled with `ot_ha_plan_done()`
once esp-mqtt accepted it; a failed publish stays owed.

The plan is reset — every wanted document re-offered, every dropped one re-cleared — on:

| Trigger | Why |
| --- | --- |
| A new broker connection | A broker that just accepted us holds nothing we can trust |
| `homeassistant/status` = `online` | Home Assistant restarted and may have lost its retained view |
| The device's IP changed | `configuration_url` names it |
| A changed prefix, device name or discovery flag | Part of the broker fingerprint: the client restarts, which is a new connection |

Home Assistant's birth message feeds nothing to the watchdog and its `offline` is ignored; both are
explained on [MQTT](/interfaces/mqtt#inbound-messages).

### Bounds

| Document | `bounds` | Where `{L}`/`{H}` come from |
| --- | --- | --- |
| `ch_setpoint` | `FLOW` | The executor's `flow_min_dc`/`flow_max_dc` (`ot_ha_bounds()`) |
| `dhw_setpoint` | `STATE` | `ot_state_bounds()` — the boiler's own bounds once it has answered ID 48, the registry's until then |
| all others | `NONE` | — |

A static 10–90 °C on `ch_setpoint` would let Home Assistant send a value the firmware then refuses;
the runtime band means the number box can only offer what the executor accepts. A bounded document with
`lo > hi` is refused rather than published.

## Guards against a silently deleted entity

Home Assistant reports nothing when it refuses a discovery document — the entity simply never
appears, or a whole device disappears. The guards in `ot_ha` and the generator exist for that:

- **Overflow returns 0, never a truncated document.** A half-object retained under `homeassistant/`
  is a broken entity Home Assistant keeps. The glue logs the object id (rate-limited) and publishes
  nothing; the document is retried only when its inputs change. `OT_HA_DOC_MAX` (1536 bytes) is
  proven against the worst-case document by `test_ot_ha`.
- **The device id must be exactly twelve lower-case hex characters.** Home Assistant ignores a
  topic segment with anything outside `[a-zA-Z0-9_-]`, so a MAC with colons would publish to a topic
  it never reads; a shorter id would re-key every entity.
- **`configuration_url` is a dotted quad or absent.** A URL Home Assistant's validator refuses takes
  the whole device block — every entity — with it.
- **One gate, `usable()`,** at the top of both entry points rejects a NULL field or an empty prefix
  (which would put state topics at `/`). `""` for optional metadata is legal and omits the key.
- **Escaped strings are bounded** (`OT_HA_ESCAPE_MAX`, 64): the prefix and name go through the one
  JSON escaper, with the length checked first because the escaper drops overflow silently.

The generator carries five rules of its own, each with a test in `tools/tests/test_discovery.py`:

1. `options` needs `device_class: enum` and forbids a unit and a state class.
2. A pair Home Assistant reads together is published whole or not at all: a command-bearing
   component always has its command topic, a gated one both availability topics and `all`.
3. Topic segments are `[a-zA-Z0-9_-]` only.
4. `configuration_url` has a scheme, or is absent.
5. Absent metadata is omitted, never published as `""` — an empty string is a unit, a class, a name.

## Tests

| Suite | What it pins |
| --- | --- |
| `tools/tests/test_discovery.py` | The five rules; only the control entities are gated; the season is a binary sensor plus an off button; payload spellings; no retained command; the two runtime-bounded numbers; write-only frame rows have no document and every other readable row has one; `wire` marks exactly the rows read from the boiler; topic layout; ids never use the name; braces are JSON or tokens; `ha_generated.h` on disk matches what the generator renders |
| `tools/tests/test_ha_schema.py` | Hands every expanded document, with full and bare context, to Home Assistant's **own** `DISCOVERY_SCHEMA` for its component, and checks that HA refuses what rules 1 and 4 forbid |
| `test/test_ot_ha` | The C renderer: the tokens, the guards, the worst case against `OT_HA_DOC_MAX` |
| `test/test_ot_ha_plan` | WAIT/WANT/DROP and the plan |

`test_ha_schema.py` needs a Python with `homeassistant` installed and is skipped otherwise. Home
Assistant's pytest plugin breaks plain tests, so run it with plugin autoloading off:

```sh
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 "$HA_PY" -m pytest tools/tests/test_ha_schema.py -q
```

See [Testing](/development/testing) for the rest of the gates.

Source: [`components/ot_ha/README.md`](../../components/ot_ha/README.md),
[`tools/render_discovery.py`](../../tools/render_discovery.py).
