<script setup>
import { withBase } from 'vitepress'
import { data } from '../reference/entities.data'
// The Data-ID table below is rendered from the generated registry (see /reference/entities):
// every row, entity and read/write mark comes from data.byDataId, never from a hand-written list.
// The message names come from the same loader (SPEC_NAMES there); a registry Data-ID with no
// name fails the build, so no row renders nameless.
const dash = '—'
const how = (g) =>
  g.dataId === 0 ? 'every other step' : [g.read && 'polled', g.write && 'written'].filter(Boolean).join(' · ')
const entitiesHref = withBase('/reference/entities')
const detail = (e) => (e.options ? e.options.join(' · ') : e.unit ?? dash)
const yes = (b) => (b ? 'yes' : 'no')
</script>

# The boiler's OpenTherm interface

How this firmware deals with the boiler: which OpenTherm messages it reads, which it writes, and
what the three messages that carry control — ID 0, ID 1 and ID 56 — are used for. The boiler is
the one OpenTherm slave on the line, and the firmware is its **sole master**: nothing else talks
to the boiler, and the firmware never stops talking (see [The bus](/protocol/bus#the-silence-rule)).
The frame format is on [The OpenTherm frame](/protocol/frame).

## The specification, not a boiler

Everything below describes the **OpenTherm 2.2 specification** as the registry
(`tools/opentherm_ids.py`) names it, not a particular boiler. The read and write direction of
every Data-ID follows the Data-Id overview map in §5.4 of the specification, which marks each one
R- / -W / RW.

What a given boiler actually supports is **discovered at runtime**, so one binary serves any
boiler: an ID it answers UNKNOWN-DATAID twice is marked `unsupported`, dropped from the poll ring
until a reboot, and never announced to Home Assistant; an ID it answers DATA-INVALID is
`invalid` — it exists, but has no data now. Details:
[The bus](/protocol/bus#availability-and-unsupported-ids). What the owner's boiler answers is on
[Intergas Kombi Kompakt HRE](/boiler/intergas-kombi-kompakt-hre).

## Data-IDs

The **{{ data.byDataId.length }}** Data-IDs the registry names, with the entities carried in each
frame. *Polled* means the ID is in the poll ring and read; *written* means at least one of its
entities is written to the boiler. ID 0 is not in the ring: it goes out on every other step on its
own (below). Each key leads to the full [entity table](/reference/entities), with codecs, units
and bounds.

<table>
<thead><tr><th>Data-ID</th><th>Message</th><th>Use</th><th>Entities</th></tr></thead>
<tbody>
<tr v-for="g in data.byDataId" :key="g.dataId">
<td>{{ g.dataId }}</td>
<td>{{ g.message }}</td>
<td>{{ how(g) }}</td>
<td><span v-for="(e, i) in g.entities" :key="e.key"><template v-if="i">, </template><a :href="entitiesHref"><code>{{ e.key }}</code></a> {{ e.name }}</span></td>
</tr>
</tbody>
</table>

Several entities share one Data-ID when its frame packs two bytes (ID 15: capacity and minimum
modulation; IDs 48 and 49: an upper and a lower bound; ID 127: the boiler's product type and
version) or a flag byte (IDs 0, 3, 5, 6, 100). The
poll ring is computed from the table — every Data-ID with a readable entity, ID 0 excepted — so
adding a readable row is all it takes to poll an ID.

## ID 0: the status exchange

ID 0 goes out on **every other conversation**, forever, as a READ-DATA. Its two bytes go in
opposite directions:

- **The high byte is the master's.** The firmware drives two bits: CH enable (`0x01`) and DHW
  enable (`0x02`). This is **the only way the firmware asks the boiler for heat**: with CH enable
  down, the boiler ignores the flow setpoint. The byte is the executor's decision, held once in
  `ot_control` and handed to the bus by `ot_thermostat`, its one writer. It is deliberately **not
  an entity**: showing it as one would make a second place where the same decision lives. Its
  inputs are entities (the switches below) and what it decided is reported by
  `ch_enable_effective`. It is zero after boot: a device that has not decided yet asks for nothing.
- **The low byte is the boiler's**: fault, CH active, DHW active, flame, cooling active and
  diagnostic indication, each its own binary entity.

The DHW bit is not a tap switch. Whether it does anything is the boiler's business; on a combi
boiler it may switch a comfort (pre-heat) function rather than hot water itself.

## ID 1: the flow setpoint

ID 1 (TSet) is **write-only** and owned by the executor. The CH flow setpoint the boiler heats to
is the real ID 1 entity `ch_setpoint`; the executor holds the value and writes it:

- re-asserted **every 10 s**, because OpenTherm gives TSet no lifetime and the master must keep
  refreshing it; on every change; and on every step until the bus confirms it went out;
- through the bus's non-evicting writer, so a re-send never evicts a hand write;
- encoded directly from tenths of a degree into f8.8 by `ot_control_io_dc_f88()`, not through
  the command layer, whose "unsupported" flag
  would otherwise freeze ID 1 forever after two stray UNKNOWN-DATAID answers.

**The CH bit never rises before the held ID 1 has gone out.** A boiler asked for heat with no
TSet heats to a value nobody chose. "Gone out" means the boiler answered the write — any answer,
DATA-INVALID included (see [The bus](/protocol/bus#the-answered-id-1-record)). The value actually
on the wire is reported as `ch_setpoint_effective`. Its bounds are the configured
`flow_min`..`flow_max`, refused rather than clamped. The ladder that decides the value — local,
Home Assistant, failsafe, boost — is on [Executor](/architecture/executor).

## ID 56: the DHW setpoint

ID 56 (TdhwSet) is read **and** written. It is the one value a boiler may keep in non-volatile
memory, so the executor does not simply re-send it; it **reconciles** it against the boiler's
readback:

- only a **READ-ACK** of ID 56 counts as a readback, never the WRITE-ACK echo of the firmware's
  own write;
- when the readback differs from the target, the target is written again at most **once a minute**
  and at most **three times** per unconfirmed target, bounding flash wear on a boiler that stores it;
- a new target, or a fresh accepted command, resets the tries and is written at once;
- with no valid readback, the target is written once per accepted command, and a persisted target
  is re-sent once at boot;
- ID 56 always goes out after ID 1, and reconciliation is off while the setpoint is unset (0).

The DHW bounds come from the boiler: once it answers ID 48, its upper and lower byte replace the
registry's bounds of `dhw_setpoint` (see [Commands](/protocol/commands#bounds)). The `dhw_enable`
switch (the ID 0 DHW bit) is persisted and is not touched by the season, the watchdog or the
failsafe.

## The other writes

- **The introduction**: ID 2 (`0x0000`: no master flags, MemberID 0), ID 124 (OpenTherm version
  2.2) and ID 126 (`0x0101`) are written **once** at bus start. ID 2 has no entity; IDs 124 and 126
  are write-only rows. See [The bus](/protocol/bus#the-introduction).
- **Plain frame writes**: every other writable row (the room setpoint and room temperature, the
  maximum modulation, the maximum CH setpoint, the version rows) is written only from the web, one
  frame per request, after the command layer has judged it. **Home Assistant may write no frame**:
  it drives the executor, never the bus, and the write-only frame rows get no Home Assistant entity
  (see [Commands](/protocol/commands#why-home-assistant-may-send-no-frame)).

The executor's CH bounds are its own configuration, not ID 57: `max_ch_setpoint` is a plain frame
row, bounded by the boiler's ID 49 answer where there is one.

## Synthetic entities

The **{{ data.synthetic.length }}** entities with no Data-ID are the executor's inputs and outputs
and the room view. They are never on the wire and never in the poll ring; the registry gives them
no Data-ID field at all (the generator emits −1), so no row can be given one by a slip.

<table>
<thead><tr><th>Key</th><th>Name</th><th>Kind</th><th>Writable</th><th>Options / unit</th></tr></thead>
<tbody>
<tr v-for="e in data.synthetic" :key="e.key"><td><a :href="entitiesHref"><code>{{ e.key }}</code></a></td><td>{{ e.name }}</td><td>{{ e.kind }}</td><td>{{ yes(e.writable) }}</td><td>{{ detail(e) }}</td></tr>
</tbody>
</table>

- **The switches** (`ch_enable`, `dhw_enable`, `heating_season`) are the executor's inputs; with
  its state they decide the ID 0 high byte. Who may write each, in which mode, is on
  [Commands](/protocol/commands#ownership).
- **The enumerations** report what the executor decided: the owner (`control_mode`), the row of
  its ladder (`control_state`, with the reason and cause as attributes) and which room source is
  shown (`room_source`). The value over REST and MQTT is the option, never its index.
- **The sensors** report what reached the boiler and how the device got there: the setpoint and
  CH bit actually sent, the failsafe count and last duration, and the room temperature selected
  for display ([Room sources](/device/room-sources)).

There is no `climate` entity — the firmware is an executor, and the control law lives in Home
Assistant — and no `water_heater`. There is deliberately no per-second "command age": the control
state already tells `ha`, `ha_waiting` and `failsafe` apart.
