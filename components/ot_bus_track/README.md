# ot_bus_track — the bus's write bookkeeping beside `ot_bus_sched`

## Purpose

A pure helper that wraps `ot_bus_sched`'s single-slot write queue (a "queue of one": at most one
write waits at a time, and a newer write replaces the waiting one). It lets the bus track which
ID 1 write actually went out and was answered, and it closes the one race the single-slot queue
leaves open when a task queues a write while an exchange is already on the wire.

The firmware holds the commanded boiler state (ID 1 is TSet, the control setpoint) and needs to
know that a value it decided on has genuinely reached the boiler. That guarantee is exactly what
the ID 1 record here provides.

## Responsibility

Owns two pieces of bookkeeping that live *beside* `ot_bus_sched`'s scheduler, under the bus's own
spinlock:

- **the write generation** (`gen`) — bumped once for every write queued by anybody; the witness
  that lets `done()` re-arm a write that would otherwise be dropped unsent;
- **the answered-ID-1 record** (`id1_seq`, `id1_raw`) — a count of ID 1 writes the boiler
  answered plus the raw value of the last of them, which callers read to confirm a commanded
  setpoint has gone out on the bus.

It does **not**:

- own its structs — both `ot_bus_track_t` and `ot_bus_sched_t` belong to the caller;
- take any lock — every call must be made under the one lock the caller already holds over both;
- touch `ot_bus_sched` internals except as noted (it calls `ot_bus_sched_write()` /
  `ot_bus_sched_done()` and reads/sets a handful of scheduler fields);
- fail — no call returns an error.

It is a component of its own because `ot_bus.c` pulls in FreeRTOS: a host suite that included an
`ot_bus` header would have to compile FreeRTOS with it. Keeping this bookkeeping pure and
separate lets it be tested on the host. `ot_bus_sched` is likewise a separate pure component and
is left untouched. **DO NOT** fold this into `ot_bus`.

## Public API

All functions require the caller's single lock over both structs; none takes a lock, none fails.
A zeroed `ot_bus_track_t` is the valid starting state.

| Function | Contract |
| --- | --- |
| `ot_bus_track_write(t, s, data_id, value)` | Queue a write for the next meaningful slot. Bumps `t->gen`, then calls `ot_bus_sched_write()`. Evicts any unexecuted queued write (the single-slot queue). |
| `ot_bus_track_write_if_idle(t, s, data_id, value) → bool` | Queue only if no write is pending; returns whether it queued. For the executor's own writes, which must never evict a write queued by another origin (e.g. a manual hand write): a separate check-then-queue would leave a window in which another task's write lands and is evicted. `false` means "not now" — the caller asks again on its next step. |
| `ot_bus_track_done(t, s, step, gen_at_step, answered, start_ms, end_ms)` | The exchange of `step` is over. Calls `ot_bus_sched_done()` (the only caller of it). Re-arms a write that was queued during the exchange if the generation moved. Counts the ID 1 write into `id1_seq`/`id1_raw` when it was a write, was answered, and `step->data_id == 1`. |

### Type

`ot_bus_track_t` (defined in the public header, carried by value under the caller's lock):

- `uint32_t gen` — +1 for every write queued, by anybody. The bus reads it in the critical
  section that builds a step and hands the same value back to `ot_bus_track_done()` as
  `gen_at_step`; a change means a write arrived while that step's exchange was on the wire.
- `uint32_t id1_seq` — +1 for every ID 1 write the boiler answered, whoever queued it; 0 before
  the first. A count, not a "last write sent" field.
- `uint16_t id1_raw` — the raw value of the last answered ID 1 write.

## Implementation

Single source file `ot_bus_track.c`; the contracts are documented in the header and pinned by the
`test_ot_bus_track` host suite through the real scheduler.

- **`ot_bus_track_write`** — bumps `t->gen`, then `ot_bus_sched_write()`. The generation has
  exactly one writer, this function.
- **`ot_bus_track_write_if_idle`** — reads `s->write_pending`; if idle, delegates to
  `ot_bus_track_write` (so the generation still moves) and returns `true`, else returns `false`.
  An idle slot means no write is on the wire, so `done()` could not drop this one anyway — the
  bump costs nothing and keeps the rule "every queued write moves the generation" free of an
  exception.
- **`ot_bus_track_done`** — calls `ot_bus_sched_done()`, then makes two decisions:
  - **The re-arm (the race it closes).** `ot_bus_sched_done()` clears the pending write for
    whatever is queued when it runs, and the exchange runs outside the lock, so a write queued
    during the exchange — e.g. a hand write that already received its WRITE-ACK — would be
    dropped unsent. When `t->gen != gen_at_step`, the write queued since is re-armed by setting
    `s->write_pending = true`. This works *only because* `ot_bus_sched_done()` clears
    `write_pending` but never `write_id` / `write_value`, which still hold the write queued
    since. **DO NOT** re-arm unconditionally or on `step->is_write` alone (the frame just sent
    would go out twice); the generation is the only witness. **DO NOT** make `done()` clear
    `write_id`/`write_value` too, or the re-arm would resend whatever the clear left.
  - **The ID 1 record.** When `step->is_write && answered && step->data_id == 1`, bump
    `t->id1_seq` and store `t->id1_raw = step->value`. Uses `step->value` (what actually went
    out), **not** `s->write_value` (the queue may already hold the next write).

### Invariants and rationale

- **Every queued write moves the generation.** No exceptions — the idle path bumps it too.
- **`answered` counts any reply.** WRITE-ACK, DATA-INVALID and UNKNOWN-DATAID all mean the
  boiler heard the frame. An ID 1 write counts only when answered: the invariant is that the
  value "has gone out on the bus", not that the boiler accepted it. A frame nobody answered may
  never have been heard. A DATA-INVALID counts deliberately — the held value is always inside the
  boiler's flow bounds, so a range disagreement that held the CH (central-heating) bit down would
  be a silent no-heat, failsafe included.

## Tests

Host suite `test_ot_bus_track` — pins the contracts through the real `ot_bus_sched` scheduler.
Because the component keeps no lock and no static state, it compiles and runs on the host without
FreeRTOS.

## Notes

- `CMakeLists.txt` registers the single source, exposes `include/`, and declares
  `REQUIRES ot_bus_sched` as public, since the header's signatures carry `ot_bus_sched_t` /
  `ot_bus_step_t`.
- All state lives in the caller-owned `ot_bus_track_t`; the component holds none of its own.
