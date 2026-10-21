<script setup>
import { data } from '../reference/entities.data'
const dash = '—'
const opts = (e) => (e.options ? e.options.join(' · ') : dash)
</script>

# Executor

The executor decides, once a second, what the boiler is told: the CH and DHW enable bits in the
ID 0 high byte, the flow setpoint in ID 1, and when the DHW setpoint in ID 56 is written. It also
decides who may command it, and what happens when the controller falls silent. This page covers
the core (`ot_control`), the translations around it (`ot_control_io`) and the task that carries it
out (`ot_thermostat`).

## Three components, one decision

| Component | Does | Tested |
| --- | --- | --- |
| `ot_control` | The ladder of states, the ownership edges, the watchdog, the bounded failsafe, the boost, the held ID 1, the ID 56 reconciliation, the ownership and bounds of every command. Pure: time is an argument, no ESP-IDF header, no allocation, no global. | `test_ot_control`, `_check`, `_failsafe`, `_boost`, `_bus` |
| `ot_control_io` | The arithmetic between the core and its neighbours: the configuration snapshot, the persist patch, the bus confirmation, the ID 56 readback and owed write, the reset-reason restore and the `RTC_NOINIT` record, the synthetic entities, the `GET /api/control` document. Pure. | `test_ot_control_io`, `_send`, `_state` |
| `ot_thermostat` | The task, the spinlock, the bus calls, NVS and `RTC_NOINIT`. Decides nothing; it is the sole writer of the ID 0 high byte and of the held ID 1. | source guards |

All temperatures are `int16_t` tenths of a degree (`_dc`). The configuration store has no float
type, and a float that crosses the store twice stops comparing equal, so degrees never become
floats inside the executor.

### One tick

`ot_thermostat` runs `tick()` at once on start and then every second, in this order:

1. Snapshot the configuration from `ot_net` and map it (`ot_control_io_cfg()`); follow a changed
   time zone.
2. Re-read the room-source configuration, drain the room mailbox into `ot_room`, step it, and fill
   the steer selection into the executor's input.
3. Read the bus's ID 1 report (`ot_bus_write_state()`) and turn it into a confirmation.
4. Under the spinlock: read the ID 56 readback the bus task recorded, run `ot_control_step()`,
   keep its output.
5. Outside the spinlock: persist the whole heat hours if asked, **then** mirror the watchdog count
   and the part-hour into `RTC_NOINIT`; log changes; hand a changed status byte to
   `ot_bus_set_status()`; offer ID 1, then any owed ID 56, through `ot_bus_write_if_idle()`;
   publish the synthetic entities into `ot_state`.

Configuration is polled, not pushed: there is no change hook on the store, so a saved setting
reaches the executor within one tick. Inside `ot_control_step()` the clocks advance for the
interval that just elapsed **before** the ownership edges are observed — swapped, the first step in
HA mode would count the last LOCAL second against Home Assistant's watchdog.

## The held setpoint and the CH invariant

The held flow setpoint exists from boot: `ot_control_init()` sets it to `failsafe_setpoint`,
quantised to 0.5 °C and then clamped into `[flow_min, flow_max]` (in that order — where a bound is
not a multiple of 0.5 the bound wins). There is no "no setpoint" state. When ownership passes to
Home Assistant, its setpoint also starts at `failsafe_setpoint`, so a first command of
`ch_enable = 1` alone heats at the one value the owner configured for that case.

::: warning The invariant
**The CH bit never rises unless the bus has carried the held ID 1 since it last changed.** A boiler
asked for heat with no chosen `TSet` heats to a value nobody chose. Pinned by
`test_ch_never_rises_before_the_held_setpoint_is_confirmed` in `test_ot_control_bus`.
:::

How the step enforces it:

- A change of the held value un-confirms it.
- The bus reports a **count** of answered ID 1 writes and the last value (`id1_seq`, `id1_raw`,
  kept by `ot_bus_track`). A moved count re-confirms the held value only if the value on the wire
  equals it, so a foreign ID 1 seen on the wire un-confirms it too. The count, not a flag, is why a
  late reader cannot miss one.
- The bit is gated only on a **rising** edge: wanted, currently down, unconfirmed → held down with
  reason `await_setpoint`. A bit already up is not dropped for a new setpoint; that would cost a
  burner cycle per slider move.
- ID 1 is asked for on every step until confirmed, then again every 10 s
  (`OT_CONTROL_RESEND_MS`): OpenTherm gives `TSet` no lifetime, so the master keeps refreshing it.
  It is re-asserted in every state, including those with the CH bit down — harmless, because the
  CH-enable bit overrides the setpoint.
- `ot_thermostat` offers ID 1 **first**, before any owed ID 56, and logs the "ID 1 on the wire" line
  before the status-byte line of the same tick, so the log shows the order.

The executor encodes its own ID 1 and ID 56 with `ot_control_io_dc_f88()`, **not**
`ot_command_encode()`. That gate refuses a Data-ID the state model has marked unsupported, and the
mark is never cleared: two UNKNOWN-DATAID answers to ID 1 in a row (line noise suffices) would make every
re-send refused, the setpoint never confirmed, and the CH bit — failsafe included — never rise
again until a reboot. For the same reason any answered ID 1 write counts as carried, a
DATA-INVALID or UNKNOWN-DATAID answer included; the task warns once in the log instead (a
DATA-INVALID usually means the boiler's range and `flow_min..flow_max` disagree).

## The state ladder

`control_state` is an ordered ladder evaluated **first match wins**. The enum order is the
priority, and it is also the option order of the generated `control_state` entity — Home Assistant
stores the option string, so a reorder would rewrite the owner's recorder history.

| # | State | Condition | CH bit | ID 1 |
| --- | --- | --- | --- | --- |
| 1 | `season_off` | `heating_season` is false | 0 | held (harmless) |
| 2 | `boost` | the executor has seen LOCAL mode, and a boost is running | 1 | the boost setpoint |
| 3 | `local` | the executor has seen LOCAL mode | `local_ch_enable` | `local_ch_setpoint` |
| 4 | `ha_waiting` | HA mode, no accepted HA `ch_enable` or `ch_setpoint` since ownership began, watchdog not expired | 0 | held |
| 5 | `failsafe` | HA mode, and latched, or the watchdog expired, or Home Assistant is blind | [the failsafe's](#the-bounded-failsafe) | `failsafe_setpoint` |
| 6 | `ha` | HA mode, heard, fresh, unlatched | Home Assistant's | Home Assistant's |

Season-off outranks everything, Home Assistant and failsafe included: it is the person's "off".
The rows overlap (a boost with the season off; a blind Home Assistant with a fresh watchdog), which
is why it is written as a straight ladder of `if … return` and why each boundary between adjacent
rows has a test in `test_ot_control` (`test_season_off_beats_a_running_boost`,
`test_a_boost_beats_local`, `test_local_mode_never_reaches_the_ha_rows`,
`test_ha_waiting_becomes_failsafe_when_the_watchdog_expires`,
`test_the_boundary_between_ha_and_failsafe_is_the_watchdog`).

The `reason` attribute says what the CH bit is doing (`await_setpoint`, `fs_blind`,
`fs_disarmed`, `fs_room_cold`, `fs_room_warm`, `min_cycle`, `none`); the `cause` attribute says why
the state is `failsafe` (`watchdog`, `ha_blind`, or `none`). Both are published as the attributes
of `control_state` ([MQTT](/interfaces/mqtt)).

## Transitions and the watchdog

**Entries into Home Assistant ownership** forget every HA command held in RAM and pass through
`ha_waiting`: booting in HA mode, a `LOCAL → HA` switch, and the season going on while in HA mode.
Nothing Home Assistant sent before the entry comes back after it. The edges are observed in
`step()` alone, sampled once a step: a flip that starts and ends between two steps is never seen,
and an HA command at most one step old survives it. Any mode edge also ends a boost.

**`ha_waiting` ends** on the first accepted HA `ch_enable` or `ch_setpoint`, or becomes
`failsafe` when the watchdog expires.

**Leaving `failsafe`** takes an accepted HA `ch_enable` while Home Assistant is not blind. That
command is itself an entry into ownership — whatever Home Assistant said during the failsafe is
forgotten — and it is heard in the same call, so the next step goes **straight to `ha`**, not back
through `ha_waiting`. A `ch_setpoint` feeds the watchdog but does not unlatch: it is not a decision
about heat.

**The watchdog is fed only by accepted HA `ch_enable` and `ch_setpoint` commands.** Not by DHW, not
by the season, not by a retained message (refused before it reaches the executor), not by Home
Assistant's birth message, and not by a room reading. The deadline is `watchdog_s` (default
900 s). The overdue time is a saturating accumulator advanced each step, counted only while the
executor has seen HA mode and held at zero in LOCAL — Home Assistant cannot feed a watchdog it does
not own.

::: details Why no heartbeat topic
A heartbeat proves that Home Assistant is running, not that its controller is producing outputs. An
automation that stopped computing setpoints while Home Assistant stayed up would keep a heartbeat
watchdog fed forever. Feeding only on real CH commands makes "the controller stopped deciding" and
"the controller died" the same event, which is the one the failsafe exists for.
:::

### Why `check` and `apply` are split

The configuration snapshot is built outside the executor's spinlock (building it takes the store's
mutex), so the snapshot a command is judged against can predate a mode flip the step has already
seen. `ot_control_apply()` therefore judges **ownership** against the mode the executor last
observed and takes only **value bounds** from the snapshot, and it never treats the snapshot's mode
as an edge — that would be taken back on the next step as an entry and silently forget Home
Assistant's accepted command. A mode flip reaches Home Assistant's commands at most one step after
it reaches the store. `apply()` stamps no moment: the watchdog is an accumulator, and a stored
moment is how the 49.7-day wrap comes back.

## The bounded failsafe

When Home Assistant is gone (watchdog expired) or blind, the executor enters `failsafe` and may
raise the CH bit itself. That deliberately reverses a naive "a failsafe changes the setpoint but
never raises CH": the season is the person's off, and Home Assistant's CH = 0 was a duty-cycle
decision of a controller that has since died. Two bounds keep it from heating a house in summer:

1. **`heating_season` must be true** — row 1 of the ladder guarantees it.
2. **Home Assistant must have asked for heat within `failsafe_heat_days`.** The executor counts
   **powered hours** since the last accepted HA `ch_enable = 1` (which resets the count to zero);
   past `failsafe_heat_days × 24` hours the failsafe disarms (reason `fs_disarmed`, CH down). Wall
   time is not used — SNTP may be as dead as Home Assistant — and powered-off time does not count.
   The whole hours are in NVS (namespace `ctl`, key `heat_h`), written only while below the limit;
   the part-hour survives a soft reset in `RTC_NOINIT`.

::: info A device Home Assistant never asked for heat is disarmed
With no `heat_h` stored, the restore sets the count to its maximum: the failsafe stays disarmed
until Home Assistant has sent `ch_enable = 1` at least once. A fresh device whose controller dies
before ever asking must not heat blind on the strength of a missing key.
:::

Within the bounds, the CH verdict is:

| Condition | CH | Reason |
| --- | --- | --- |
| No `room`-role source is fresh | on at `failsafe_setpoint`, at once — no minimum cycle | `fs_blind` |
| Room ≤ target − 0.3 K | on | `fs_room_cold` |
| Room ≥ target + 0.3 K | off | `fs_room_warm` |
| Inside the dead band | the last verdict holds | as before |
| The verdict differs from the bit sent less than `failsafe_min_cycle_s` ago | the bit sent | `min_cycle` |

The target is `failsafe_room_target`; `failsafe_min_cycle_s` is both the minimum on and the minimum
off time, measured on the bit actually sent, whoever last moved it. The hysteresis keeps its own
verdict apart from the bit sent, because the CH invariant can hold the bit down for a step while
the hysteresis wants it up. A freeze costs the house more than a week of gas, which is why the blind
branch skips the minimum cycle.

The hysteresis branch is dormant by default: only the MQTT room source can be `room`-role (the
shield DS18B20 is always `ambient`), and it is off until the owner enables it. On a fresh
configuration, therefore, an armed failsafe always heats blind. See
[Room sources](/device/room-sources).

**"Blind"** means a `room`-role source marked as forwarded by Home Assistant
(`room_mqtt_ha_forwarded = true`) has gone `STALE`: Home Assistant is alive enough to command but its
eyes are gone, so its commands are no longer trusted.

**The cause is recomputed every step**, not latched with the state: `watchdog` while the watchdog is
expired (even if also blind), else `ha_blind` while blind, else `none`. `none` is a valid cause
while still in `failsafe`: the state latches until an accepted HA `ch_enable`, so a failsafe held
only by the latch — Home Assistant feeds the watchdog with setpoints but has not re-sent
`ch_enable` — reads `failsafe` with cause `none`, not a stale `watchdog`.

**Visibility.** The failsafe fires exactly when Home Assistant and perhaps the broker are gone, so
publishing at that moment reaches nobody. The executor keeps `failsafe_count` (entries since boot)
and `last_failsafe_duration_s` (RAM only), published as retained states that the broker hands Home
Assistant when it comes back, and logs every transition.

## The boost

The boost is row 2 of the ladder: "heat at this flow setpoint for this long, then give the bit
back" — `POST /api/ops/boost`, LOCAL mode only. The HTTP operation first parses its body —
both `setpoint` and `minutes` required, `minutes` a whole number, `setpoint` finite, each a `422` —
and only then asks the executor, which refuses in this order: owned by HA (`409`),
season off (`409`, with a reason rather than a running boost the boiler ignores), minutes not
1–480 (`422`), setpoint outside `[flow_min, flow_max]` (`422`). A start replaces a running boost.
It lives in RAM only and ends on a reboot, on any mode edge, on `boost_off`, or at its deadline —
the one stored moment in the executor, compared wrap-safely since its ≤ 8 h span sits far inside
the ±24.8 days a signed difference covers.

## Hot water and ID 56

`dhw_enable` (default on) is persisted and written by whoever owns the controls; it is **not** fed
to the watchdog and **not** touched by the season, `ha_waiting` or the failsafe. On a combi boiler
with no tank it most likely means hot water at the tap.

The DHW setpoint (ID 56) is the one value that may live in non-volatile memory on the boiler side,
so it is reconciled against what the boiler reads back — a **READ-ACK** of ID 56 only, never the
WRITE-ACK echo of our own write (`ot_state` keeps both alike, which is why the executor keeps its
own readback record):

- Written when the readback differs from the target, at most once every 60 s and at most 3 times
  per unconfirmed target — a boiler that stores whole degrees would otherwise be rewritten 1440
  times a day.
- With a valid readback, a new target resets the tries and is written at once if the boiler
  disagrees.
- With no valid readback, the target is written once per accepted `dhw_setpoint` command and once
  at boot, then left alone. The target changes only through that entity
  (`POST /api/entities/dhw_setpoint` or its MQTT command) — `POST /api/config` refuses
  `dhw_setpoint_dc` by name (`read-only-field`) — so on such a boiler every new target is written
  once, when its command is accepted.
- Never while unset (stored as 0). The comparison is exact, or a half-degree change would never be
  written.

ID 56 is offered after ID 1. One the bus refused (a hand write held the slot) is recorded as owed
and offered at the next idle slot with the current target.

## The single refusal point

One function decides every refusal of every writer: `ot_command_check(key, value, origin, cfg,
out)`, which ends in the pure `ot_control_check()`. REST calls it with origin `WEB`, MQTT with
`HA`; anything not `HA` is judged as the web. Its order is one rule: first what is wrong with the
request whoever sends it (unknown key, not writable, unsupported by the boiler, not representable,
outside the boiler's own bounds), then **ownership**, then the executor's own bounds — a caller is
not told the bounds of a command it may not send.

| Code | Meaning | REST |
| --- | --- | --- |
| `UNKNOWN_KEY` | no such entity in the registry | `404` |
| `NOT_WRITABLE`, `HALF_WORD_CODEC` | the entity is read-only, or its codec describes half a word | `405` |
| `OWNED_BY_HA` | a web write while the mode is HA | `409` |
| `OWNED_BY_LOCAL` | a Home Assistant write while the mode is LOCAL | `409` (MQTT: refused, logged) |
| `SEASON_ON_IS_LOCAL` | Home Assistant tried to turn the season on | `409` (MQTT) |
| `NOT_FOR_HA` | a Home Assistant write of a plain OpenTherm frame row, in either mode | MQTT only |
| `OUT_OF_RANGE` | a value outside bounds, NaN, a switch not 0/1, a DHW setpoint ≤ 0 | `422` |
| `UNSUPPORTED_BY_BOILER` | the boiler answered UNKNOWN-DATAID twice in a row for the write ID (any other answer resets the count) | `409` |

So `ch_setpoint = 99` from the web is `409` in HA mode and `422` in LOCAL mode. A `ch_setpoint`
outside `[flow_min, flow_max]` is **refused, not clamped**: a broken automation that keeps being
refused feeds no watchdog and lands in the failsafe, which is the right outcome; a clamp would hide
it behind a plausible flow temperature. Home Assistant never sends one anyway — discovery publishes
the runtime bounds and Home Assistant's number entity refuses out-of-range values.

**The season** is the person's master kill, not a value Home Assistant owns: the web may write it
0 or 1 in either mode; in HA mode Home Assistant may send 0 (its summer logic is the failsafe's
first bound) and never 1; in LOCAL mode Home Assistant is refused like any other command.

`ot_command_check()` is the early answer. `ot_control_apply()` re-checks under the spinlock and is
final, so a command can still be refused after it passed — the HTTP and MQTT layers handle both.
An accepted command the store then refuses to persist is an error (`NOT_SAVED`), never a silent
`202`, because the next step reads the store. Details: [Commands](/protocol/commands).

## Executor settings

All in the configuration document ([Configuration](/guide/configuration)); the executor re-reads
them every tick.

| Field | Default | Range | Meaning |
| --- | --- | --- | --- |
| `control_mode` | `0` (LOCAL) | `0` LOCAL, `1` HA | who owns the controls; HA needs a broker host |
| `heating_season` | off | — | the master kill |
| `watchdog_s` | 900 | 60–7200 | silence before the failsafe |
| `failsafe_setpoint_dc` | 450 | 100–900, 0.5 °C steps | ID 1 in the failsafe, and the initial held value |
| `failsafe_room_target_dc` | 180 | 50–300 | the hysteresis target |
| `failsafe_heat_days` | 3 | 1–30 | the summer bound |
| `failsafe_min_cycle_s` | 600 | 60–3600 | minimum on and off time in the hysteresis |
| `flow_min_dc` / `flow_max_dc` | 400 / 700 | 100–900, 0.5 °C steps | bounds of every CH setpoint |
| `local_ch_enable` / `local_ch_setpoint_dc` | off / 450 | — | the LOCAL controls |
| `dhw_enable` / `dhw_setpoint_dc` | on / unset | — | hot water |

## Synthetic entities

The ID 0 high byte itself is deliberately not an entity — it is the executor's decision, held once.
Its inputs and outputs are, and they carry no Data-ID:

<table>
<thead><tr><th>Key</th><th>Name</th><th>Kind</th><th>Options</th></tr></thead>
<tbody>
<tr v-for="e in data.synthetic" :key="e.key"><td><code>{{ e.key }}</code></td><td>{{ e.name }}</td><td>{{ e.kind }}</td><td>{{ opts(e) }}</td></tr>
</tbody>
</table>

`ch_enable`, `dhw_enable` and `heating_season` are the commands (`ch_enable` shows the owner's
command, not the bit sent); `ch_enable_effective` is the bit actually sent and
`ch_setpoint_effective` the last ID 1 actually on the wire, omitted until one has gone out. The CH
setpoint itself is the real ID 1 entity `ch_setpoint`, and the DHW setpoint the ID 56 entity
`dhw_setpoint`; both route to the executor, not to the bus. There is no `climate` or `water_heater`
entity — the firmware has no room loop of its own to be a thermostat with. The full table is on
[Entities](/reference/entities).
