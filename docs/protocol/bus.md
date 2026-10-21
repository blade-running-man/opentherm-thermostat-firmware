# The bus

The firmware is the **sole OpenTherm master**. One FreeRTOS task, `ot_bus`, owns the line and holds
one conversation after another for as long as the device is powered. This page covers who talks
when, what is asked, how writes are queued and tracked, how the firmware learns what the boiler
supports, and the two diagnostic modes. The bit-level exchange is on
[Physical layer](/protocol/physical-layer); the decisions the executor sends through the bus are
on [Executor](/architecture/executor).

The work is split along one seam: `ot_master` knows bits and timings and not what to ask;
`ot_bus` knows what to ask and not bits. The "what and when" is itself a pure function,
`ot_bus_sched`, and the write bookkeeping beside it is another, `ot_bus_track`, so the polling
order and pace are host-tested rather than checked with a stopwatch at the boiler.

| Component | Role | Tests |
| --- | --- | --- |
| `ot_bus` | the task; the only caller of `ot_master_exchange()`; the lock around the scheduler | source guards in `tools/tests/test_source_guards.py` |
| `ot_bus_sched` | what to send at time *t*: WAIT or TALK, which Data-ID, read or write | `test/test_ot_bus_sched` |
| `ot_bus_track` | the write generation and the record of answered ID 1 writes | `test/test_ot_bus_track` |
| `ot_observe` | the raw table of the last reply for every Data-ID 0..127 | `test/test_ot_observe` |

## The silence rule

::: warning The bus never stops
An OpenTherm slave that has not heard a correct frame from the master for roughly five seconds
(the specification allows 5..15 s) treats the thermostat as short-circuited and goes into a
**demand for heat**. Falling silent is not a safe state; it is the hottest one.
:::

So the `ot_bus` task loop has no exit: no `break`, no `return`, no stop condition, and the header
forbids adding one. A frame error, a missing reply, a Wi-Fi or broker failure, a stale sensor or a
dead Home Assistant changes what is sent, never whether something is sent. The supporting rules:

- `ot_bus_start()` has no counterpart: there is no stop function.
- `ot_bus_set_status()` cannot fail and cannot stop the conversation.
- An empty poll ring still sends ID 0 (below), never a WAIT.
- The line test leaves its mode on elapsed time, never on a peer's answer.
- The bus is started **before** the network in `src/main.cpp`, and the scheduler's very first step
  talks at once: a slow router must not delay the first conversation.
- Nothing reboots because a peer is absent ([Failure model](/architecture/failure-model)).

The one silence that cannot be avoided is a **reboot**, routine during an update: the device is
gone for more than five seconds, the boiler briefly demands heat, and that is the boiler's
expected behaviour, not a firmware defect. The other deliberate silence is the
[line test](#the-line-test), which therefore needs a password.

## One conversation at a time

A conversation is one frame each way, and `ot_master_exchange()` blocks for it: normally at most
about 0.93 s (the 34 ms frame and the 900 ms reception budget), 1.5 s if the timer never fires.
OpenTherm sets three timing rules:

- the slave answers 20..800 ms after the end of the master's frame;
- the master waits **at least 100 ms** between conversations;
- the master speaks **at least once every 1 s + 15 %**, that is 1150 ms.

A fixed "every 950 ms" period would violate them: 34 ms of frame + 800 ms of waiting + 34 ms of
reply + the 100 ms pause is 968 ms. So the next start is the later of two deadlines, not a period:

```text
next_start = max(end_of_previous_reply + 100 ms, start_of_previous + 950 ms)
```

| Constant | Value | Meaning |
| --- | --- | --- |
| `OT_BUS_PERIOD_MS` | 950 | the desired pace |
| `OT_BUS_MIN_GAP_MS` | 100 | the minimum pause between conversations |
| `OT_BUS_DEADLINE_MS` | 1150 | a TALK later than this after the previous start is flagged `overdue` |
| `OT_BUS_MAX_POLL` | 32 | the capacity of the poll ring |

An overdue step is logged (`deadline missed; conversation is late`) and counted, never silently
caught up: something held the task, and the next occurrence must be connectable to whatever the
system was doing. Time differences are computed by unsigned subtraction, so the ~49.7-day
wrap of the millisecond counter cannot silence the bus. The task runs at priority 10 with a
4 KiB stack: lateness here violates the protocol, it is not a sluggish interface.

## What each step sends

The scheduler alternates a **mandatory slot** and a **meaningful slot**. The order inside a step
is part of the contract:

1. **ID 0 on every other step**, always: READ-DATA with the master's status byte in the high byte.
   It carries the CH-enable bit and returns the boiler's status flags, and skipping it loses
   control for a cycle.
2. Otherwise a **pending write**, if any.
3. Otherwise the next ID of a running **sweep** (below).
4. Otherwise the next ID of the **poll ring**.
5. Otherwise, with an empty ring, **ID 0 again**, with the same status byte. The ring may empty
   completely once everything it held proved unsupported; the meaningful slot is then filled with
   the status rather than with silence, and the high byte is set on this branch too, because a
   status of zero there would withdraw the heat demand every other conversation.

ID 0 is a READ-DATA even though it carries the master's flags: that is the ID 0 protocol (the
low byte is reserved for the slave's answer). **Do not** turn it into a WRITE-DATA; the boiler
would refuse the frame and the status would stop being exchanged.

### The status byte

`ot_bus_set_status(high)` stores the master's flags for the next ID 0. It is a byte, not a request
the scheduler consumes: the status is a current state, and a byte cleared after one send would
drop CH enable a second after asking for heat. It takes effect on the next ID 0 frame the
scheduler builds after the call. A byte set just after an ID 0 step was built misses that frame,
which leaves with the old byte, and waits out the meaningful slot after it: the worst case is the
second conversation after the one on the wire, about 1.9 s at the 950 ms pace. It is **zero after
start**, which is a safety decision: a device that has
just booted, or whose executor has not decided yet, does not ask for heat.

| Bit | Name | Driven by this firmware |
| --- | --- | --- |
| `0x01` | `OT_STATUS_CH_ENABLE` | yes: the executor's CH decision |
| `0x02` | `OT_STATUS_DHW_ENABLE` | yes: the `dhw_enable` switch |
| `0x04` | `OT_STATUS_COOLING` | no (named so the position is never reused) |
| `0x08` | `OT_STATUS_OTC_ACTIVE` | no |
| `0x10` | `OT_STATUS_CH2_ENABLE` | no |

This is **the only way the firmware asks the boiler for heat**, and `ot_thermostat` is its only
writer: the executor composes the byte once a second and the task hands it over. What the bits
mean for the boiler is on [Boiler](/boiler/#id-0-the-status-exchange).

### The poll ring

The ring is computed from the registry, not listed beside it: every Data-ID that has a readable
entity, except ID 0 (`POLL_IDS` in `tools/opentherm_ids.py`, emitted as `OT_POLL_IDS` in
`registry_generated.h`, copied into the scheduler by `src/main.cpp`). ID 0 is excluded because it
already goes out on every other step; in the ring it would go out more often than the rule. With
ID 0 taking half of the conversations, a full pass over the ring takes about two seconds per
entry. Which IDs are read is listed on [Boiler](/boiler/).

## Writes

### A queue of one

The scheduler holds **one** pending write; the next write evicts an unexecuted one. A flow
setpoint sent a minute late is worse than a lost one, because the boiler would act on a stale
decision. **Do not** grow it into a ring "so that nothing is lost". A write is cleared from the
queue after its conversation whatever the outcome: the bus does not repeat a write on its own,
because only the caller knows whether the value is still current.

| Call | Who uses it | Behaviour |
| --- | --- | --- |
| `ot_bus_write(id, value)` | hand writes from REST (after `ot_command_check()`) | queues, evicting a pending write |
| `ot_bus_write_if_idle(id, value)` | the executor's own ID 1 and ID 56 | queues only if nothing is pending, and says whether it did |

The executor never uses `ot_bus_write()`: a routine re-send must not evict a hand write. Checking
the queue and then calling `ot_bus_write()` would leave a window in which another task's write
lands and is evicted, so `write_if_idle` is the same check and the same queueing under one lock.
A `false` means "not now"; the executor asks again on its next step. A source guard pins that the
thermostat never calls `ot_bus_write()`.

### The write generation

`ot_bus_track` closes the one race the queue of one leaves open. The exchange runs outside the
lock, and the scheduler's `done()` clears the pending flag for whatever is queued when it runs. A
write queued while an exchange is already on the wire — a hand write already answered `202` —
would be dropped unsent. So every queued write bumps a counter, `gen`; the bus captures `gen` in
the critical section that builds a step and hands it back when the exchange is over, and if it
moved, the write queued since is re-armed for the next meaningful slot. The re-arm is conditional
on purpose: with nothing queued since, the frame just sent would go out twice.

### The answered-ID-1 record

The executor's rule that **the CH bit never rises before the held ID 1 has gone out** needs to
know that a write went out, not that it was queued. `ot_bus_track` counts every ID 1 write the
boiler **answered**, whoever queued it, and keeps its value: `id1_seq` and `id1_raw`, read with
`ot_bus_write_state()`.

- Any parsed reply counts: WRITE-ACK, DATA-INVALID and UNKNOWN-DATAID all mean the boiler heard
  the frame. A frame nobody answered does not count; it may not have been heard.
- A **DATA-INVALID** counts deliberately. The held value is always inside the configured flow
  bounds, and holding CH down on a range disagreement would turn it into a silent no-heat,
  failsafe included. The executor logs the first DATA-INVALID answer to ID 1 once, with the
  DATA-VALUE that reply carried. The specification does not fix that value — a boiler may echo
  the refused setpoint, send `0x0000` or anything else — so it is a diagnostic for the log, never
  data.
- It is a **count**, not a flag: the executor reads it once a second, and two writes can leave
  between two reads — a *foreign* ID 1 among them, which must un-confirm the held value.
- The recorded value is the step's, not the queue's: the queue may already hold the next write.

How the executor uses the record, and why it encodes ID 1 itself (from tenths of a degree
straight into f8.8 with `ot_control_io_dc_f88()`) instead of going through the command layer, is
on [Executor](/architecture/executor).

### The introduction

At start the master introduces itself **once**, with three writes that take the meaningful slot of
the first conversations, one at a time (the queue holds one):

| Data-ID | Value | Meaning |
| --- | --- | --- |
| 2 | `0x0000` | master configuration and MemberID: no master flags, and MemberID 0 — the firmware is not a member of the OpenTherm association and does not borrow a number, nor echo the boiler's |
| 124 | f8.8 of `2.2` (`0x0233`) | the OpenTherm version the master follows |
| 126 | `0x0101` | the master product version: type 1, version 1, the firmware's own numbering |

A step counts as done when its write is **queued**, not answered, so a silent boiler cannot hold
the introduction (and the ring) forever. Another task's write is never overwritten by it; it
waits for the next turn. It is not repeated: the boiler keeps it for the session, and a boiler
that lost power lost the session too. ID 0 keeps going out on every other step meanwhile.

## Replies

After every exchange the bus updates its statistics; after every parsed reply it records the
reply in the raw table and calls the response callback. The callback, `on_ot_response()` in
`src/main.cpp`, feeds `ot_state_apply_dataid()` (the entity model) and `ot_thermostat_heard()`
(the executor's ID 56 readback and ID 1 record). It must not block: while it runs, the
conversation is not happening.

### Availability and unsupported IDs

`ot_state` turns each parsed reply into a per-entity availability, the same for every entity
carried by that Data-ID. Only parsed replies reach it: the response callback runs only when the
exchange succeeded, so a timeout, a frame error or a parity error changes nothing in the model.

| Reply | Availability | Value |
| --- | --- | --- |
| READ-ACK, WRITE-ACK | `ok` | decoded through the entity's codec |
| DATA-INVALID | `invalid` | `null`: "the ID exists, there is no data now" |
| UNKNOWN-DATAID, first | `unknown` | `null` |
| UNKNOWN-DATAID, second in a row | `unsupported` | `null` |
| not asked yet | `unknown` | `null` (`age_ms` is `null`) |
| no answer after an earlier one | unchanged: the last value and availability are kept | `age_ms` keeps growing |

Nothing expires a value: whether a reading is current is told by its `age_ms`, not by its
availability. Two UNKNOWN-DATAID answers in a row, not one: parity does not catch every
corruption, and a single damaged frame must not remove an ID. Any other reply resets the streak. Once marked, the ID stays
unsupported until a reboot; the mark is not stored.

After each reply the bus asks `ot_state_is_unsupported()` and, if so, removes the ID from the
ring (`ot_bus_sched_disable()`) and logs `ID … is not supported by the boiler; removed from the poll
ring` once. Asking it further would spend a ring slot once a minute on a known-empty answer. The
removal compacts the ring in place and corrects the position: by then `done()` has already moved
past the slot just asked, and without the correction the successor of the removed ID would lose a
whole turn of the ring, about a minute. Measured on a live boiler without the correction: eight
IDs pruned, and exactly their eight successors missing from the log for a turn. **Do not** replace
the correction with a reset of the position to the start: that would cost the IDs not yet asked a
turn instead. There is deliberately no inverse; the reset is a reboot.

DATA-INVALID is never mistaken for data: on a boiler with no outdoor sensor, ID 27 must read
`invalid` / `null`, not `ok` / `0`. An `ok` reply of a bounds ID (48, 49) also replaces the
registry's write bounds of its entity (see [Commands](/protocol/commands#bounds)).

### Link state

Five failed conversations in a row (five seconds, the slave's own threshold) count as losing the
boiler. The state is three-valued: *unknown* at start, *answering*, *silent*. **Do not** reduce
it to a two-valued "answering" flag: that flag is false at start, so a boiler that has **never**
answered, the common case at first connection, would never make the transition to silent and
would produce no line at all, and "the bus works, the boiler is silent" would look like "the bus
did not start". The bus logs `boiler is answering` and `boiler is not answering (…) after N tries; polling continues` once
per transition.

`ot_bus_stats()` returns `cycles`, `ok`, `failed`, `overdue`, `consecutive_fail`,
`boiler_answering` and the sweep progress, copied under the same lock the bus task publishes them
under, so a reader never sees a torn snapshot.

## The raw table

`ot_observe` keeps, for every Data-ID 0..127, whether it was seen, the type and raw value of the
latest reply, when it came and how many came. Every parsed reply is recorded, UNKNOWN-DATAID
included: "I do not have that" is half the diagnostic value. It is RAM only and is never a source
of entities — the single entity list is `tools/opentherm_ids.py`; from this table a human learns
what is worth adding to it.

`GET /api/ot/raw` renders the table with a header filled from `ot_bus_stats()` and
`ot_master_stats()`: uptime, cycles, ok, failed, overdue, answering, the input duty, the
timeout / frame-error / parity-error split, `rx_edges`, `pins_shorted` and the sweep progress.
The failure split is kept apart on purpose: a timeout means no edge at all (a dead receive path
or a silent boiler), a frame error means edges that did not form a frame (polarity, timing,
interference). The web UI's Boiler page is built on it ([Web UI](/interfaces/web-ui)).

## The sweep

`POST /api/ops/scan` with `{"from": …, "to": …}` (default 0..127; a range outside it, or one with
`from` above `to`, is a `422`)
asks every Data-ID in the range once. It is **read only**: there is no code path in the sweep that
produces a write, because a write to a wrong number changes a boiler setting with no way to learn
the previous one. The results land in the same raw table; an ID absent after a completed sweep did
not answer, one present as UNKNOWN-DATAID answered "I do not have that".

The sweep's replies also go through the response callback, so they reach `ot_state` like any
other. That matters for the **write-only** IDs (14, 16, 24, 124, 126 and ID 1), which are never
polled and are read only by a sweep. A boiler may answer a *read* of a write-only ID with
UNKNOWN-DATAID even though it accepts writes to it; a second sweep with no answered write in
between then marks the ID unsupported until a reboot, and from then on a hand write to it is
refused `409` by the command layer (`OT_CMD_UNSUPPORTED_BY_BOILER`). ID 1 is not affected: the
command layer does not ask its flag, and the executor encodes its own writes
([Commands](/protocol/commands#why-ch-setpoint-skips-the-support-check)).

The sweep takes the meaningful slot instead of the ring, never the ID 0 slot and never a pending
write, so it does not stop the master and needs no more than the ordinary access rights. With ID 0
on every other step a full pass costs 256 conversations, about four minutes. There is no second
pass over non-answering IDs: running the sweep again is more honest than hiding a lost frame.

## The line test

`POST /api/ops/linetest` with `{"duration_ms": …, "half_period_ms": …}` (defaults **20 000** and
**2 000**) stops conversing and slowly toggles the output, so the adapter's output stage can be
checked with a multimeter: with the boiler disconnected, the resistance across the OpenTherm
terminals; with it connected, how the voltage collapses from 15..24 V to 7 V and below. A frame of
34 ms once a second is invisible to a multimeter.

| Limit | Value | Refusal |
| --- | --- | --- |
| duration | 1..30 000 ms (`OT_BUS_TEST_MAX_MS`) | `422` |
| half period | 100..5 000 ms | `422` |
| a test already running | — | `409` |
| a password is set and the request does not present it (provisioned device) | — | `401` |
| no password set on the device | — | `403` |
| the setup access point is on the air (every `POST` but `/api/provision`, password or not) | — | `403` |

Both values are whole milliseconds: a fraction inside the range is truncated. A duration below 1,
`0.5` included, is a `422` like any other value out of range, because it would truncate to 0;
a `409` means only that a test is already running.

::: warning The boiler will most likely start heating
During the test no correct frames go out, and after five seconds of silence the boiler treats the
thermostat as shorted and demands heat. That is expected and safe, but surprising if not warned
about. It is the one operation that halts the bus, so `ot_policy` requires a password for it
always, even on a device whose other routes are open (see [Security](/architecture/security)).
:::

The test ends on elapsed time alone: a stuck flag would leave the bus silent forever. When it ends
the output is driven idle and polling resumes (`line test finished, polling resumed`).

## Tests

The pure scheduler, tracker and raw table have host suites (`test_ot_bus_sched`,
`test_ot_bus_track`, `test_ot_observe`); the scheduler suite pins the slot order, the two
deadlines, the empty ring and the ring compaction. `ot_bus.c` itself has no host suite: its glue
is pinned by source guards that fail when a call a fix depends on is replaced by its obvious
neighbour (the tracker on every write, the step generation handed back). On the boiler:
[Conversation with the boiler](/hardware-verification/02-boiler-conversation). Component contracts:
[`ot_bus`](../../components/ot_bus/README.md),
[`ot_bus_sched`](../../components/ot_bus_sched/README.md),
[`ot_bus_track`](../../components/ot_bus_track/README.md),
[`ot_observe`](../../components/ot_observe/README.md).
