<script setup>
import { data } from './entities.data'
const g = data.groups
const dash = '—'
const show = (v) => (v === null || v === undefined || v === '' ? dash : v)
const range = (e) => (e.min === null && e.max === null ? dash : `${show(e.min)} – ${show(e.max)}`)
const opts = (e) => (e.options ? e.options.join(' · ') : dash)
const yes = (b) => (b ? 'yes' : 'no')
</script>

# Entities

Every entity the firmware exposes, as it appears over REST, over MQTT, in Home Assistant discovery
and in the web UI. This build lists **{{ data.total }}** entities, **{{ data.writable }}** of them
writable; **{{ data.byDataId.length }}** distinct OpenTherm Data-IDs carry them, and
**{{ data.synthetic.length }}** are synthetic.

::: info How this table is produced
This page is not written by hand. It is rendered at site build time from
`web/src/api/entities.ts`, which `tools/generate_registry.py` generates from
`tools/opentherm_ids.py` — the one entity list the whole firmware is built from (see
[Entity registry](/development/entity-registry)). The loader is `docs/reference/entities.data.ts`.
Change the table, build the firmware (the pre-build hook regenerates the outputs), and the next
docs build shows the change.
:::

The **key** is the stable identifier: it names the entity in `/api/entities/{key}` and in the MQTT
topics (`<prefix>/<key>/state`, `<prefix>/<key>/set`), and it forms the Home Assistant unique id
together with the device id, so renaming an entity never re-keys it. The **name** is the English
display name Home Assistant shows; the web UI also has German, Dutch and Ukrainian names for every
entity. **Data-ID** is the OpenTherm message the value travels in (see
[The OpenTherm frame](/protocol/frame)); several entities can share one Data-ID when the frame
packs two bytes or a flag byte. A dash in the Data-ID column marks a **synthetic** entity: an
input or an output of the executor, never on the wire. **Codec** is how the 16-bit DATA-VALUE is
parsed — `f88` is the signed 8.8 fixed-point, `u8_hb`/`u8_lb` one byte of the two. **Category**
`diagnostic` places the entity in Home Assistant's diagnostic section.

The table describes the OpenTherm specification, not a particular boiler. Which Data-IDs a
boiler actually answers is discovered at runtime; an entity the boiler does not support reads
`unsupported` and is never announced to Home Assistant (see [Boiler](/boiler/)).

## Sensors

Numeric readings, in Data-ID order; the synthetic ones the executor reports come last.

<table>
<thead><tr><th>Key</th><th>Name</th><th>Data-ID</th><th>Codec</th><th>Unit</th><th>Device class</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.sensor" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.dataId) }}</td><td><code>{{ e.codec }}</code></td><td>{{ show(e.unit) }}</td><td>{{ show(e.deviceClass) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>

## Binary sensors

One bit of a flag byte each — the boiler's status (ID 0 low byte), its configuration (ID 3), its
fault flags (ID 5), the remote-parameter flags (ID 6) and the override priorities (ID 100) — plus
the CH demand bit the executor actually sends.

<table>
<thead><tr><th>Key</th><th>Name</th><th>Data-ID</th><th>Device class</th><th>Category</th></tr></thead>
<tbody>
<tr v-for="e in g.binary" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.dataId) }}</td><td>{{ show(e.deviceClass) }}</td><td>{{ show(e.entityCategory) }}</td></tr>
</tbody>
</table>

## Numbers

The writable values. **Range** is the registry's write bound; for the two whose boiler publishes
its own limits (ID 56 from ID 48, ID 57 from ID 49) the boiler's answer replaces it at runtime.
**Read back** says whether the value is also polled from the boiler; a row that is not read back
is a write-only frame, whose value is what the firmware last sent. Which of these Home Assistant
may command, and which the firmware keeps to itself, is decided in one place (see
[Commands](/protocol/commands)).

<table>
<thead><tr><th>Key</th><th>Name</th><th>Data-ID</th><th>Range</th><th>Unit</th><th>Read back</th></tr></thead>
<tbody>
<tr v-for="e in g.number" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ show(e.dataId) }}</td><td>{{ range(e) }}</td><td>{{ show(e.unit) }}</td><td>{{ yes(e.readable) }}</td></tr>
</tbody>
</table>

## Switches

The executor's inputs. None is a Data-ID of its own: together with the executor's state they
decide the master bits of ID 0. That byte is held once, in the executor, and is not an entity
itself — its inputs are these switches and what it decided is reported by the synthetic sensors
and enumerations (see [Executor](/architecture/executor)).

<table>
<thead><tr><th>Key</th><th>Name</th><th>Icon</th></tr></thead>
<tbody>
<tr v-for="e in g.switch" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td><code>{{ show(e.icon) }}</code></td></tr>
</tbody>
</table>

## Enumerations

What the executor decided, rendered as one of a fixed set of options. The options are listed in
the executor's own order; the value over REST and MQTT is the option, never its index.

<table>
<thead><tr><th>Key</th><th>Name</th><th>Options</th><th>Device class</th></tr></thead>
<tbody>
<tr v-for="e in g.enum" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ opts(e) }}</td><td>{{ show(e.deviceClass) }}</td></tr>
</tbody>
</table>
