# ot_bus_sched — the pure "what to ask and when" decision of the OpenTherm bus

A hardware-free scheduler that decides, as a pure function of state and time, which Data-ID
the master should exchange next and how long to wait before doing so. It owns the polling
pace, the mandatory ID 0 alternation, the single-slot write queue, and the diagnostic sweep.
It has no timers, tasks, or GPIO, so its timing rules are verified on the host rather than
with a stopwatch at the boiler.

## Purpose

The OpenTherm master must obey three timing rules at once:

- the slave's reply arrives 20..800 ms after the end of the master's frame;
- between conversations the master waits AT LEAST 100 ms;
- the master must speak AT LEAST once every 1 s + 15 %, i.e. within 1150 ms.

A naive "one conversation every 950 ms" violates these: 34 ms of frame + 800 ms of waiting +
34 ms of reply = 868 ms, plus a 100 ms pause = 968 ms > 950. So the moment of the next
conversation is the maximum of two deadlines (the desired period and the minimum gap), never
a fixed period. This component computes that decision and nothing else; the surrounding bus
task performs the actual exchange on the wire and reports the result back.

## Responsibility

Owns:

- The decision of the next conversation: `WAIT` (with a delay) or `TALK` (with a frame),
  computed as the maximum of two deadlines — the period and the minimum gap — never a plain
  period.
- The slot order: the mandatory ID 0 status on every second (odd) step, then the meaningful
  slot (a pending write, the sweep, or the polling ring).
- The master status high byte (the ID 0 request's high byte carrying `ch_enable` etc.).
- A one-deep write queue (eviction, not a ring).
- A diagnostic sweep (read-only) over an inclusive `[from..to]` range, with progress.
- Removal of unsupported Data-IDs from the polling ring, with in-place compaction that
  preserves ring order and corrects the cursor.

Does NOT do:

- Touch the wire, timers, tasks, FreeRTOS, or GPIO — it is pure. The caller (the bus task)
  performs the exchange and feeds the result back via `ot_bus_sched_done()`.
- Retry failed writes, or judge exchange success/failure — `done()` advances state
  regardless of outcome.
- Ever stop the conversation: an empty ring is not a failure; the meaningful slot then
  carries ID 0 too, because the boiler interprets master silence longer than five seconds as
  a short-circuited thermostat and drives itself into a heat demand.
- Produce any write from the sweep — there is no code path for it (a wrong write number is an
  irreversible boiler setting change with no way to learn the previous value).
- Persist anything across reboot (the sweep result is diagnostics, not configuration; there
  is deliberately no inverse verb — the reset is a reboot).

## Public API

All functions operate on a caller-owned `ot_bus_sched_t`. The component holds no global
state and does no locking; the caller (bus task) provides mutual exclusion. `step()` and
`scan_progress()` are read-only (`const`); the rest mutate the struct.

| Function | Contract |
| --- | --- |
| `ot_bus_sched_init(s, poll, count)` | Zero-init `s`, copy up to `OT_BUS_MAX_POLL` (32) poll IDs. `status_high` starts at 0 (a SAFETY choice: a fresh device must not ask for heat). `poll==NULL`/`count==0` gives an empty ring. |
| `ot_bus_sched_set_status(s, high)` | Set the master flags (OR of `OT_STATUS_*`) for the next ID 0 request. A current-state assignment, not a queue; takes effect on the next ID 0 slot, within ~2 s. |
| `ot_bus_sched_write(s, data_id, value)` | Queue ONE write; a new one evicts the unexecuted previous. |
| `ot_bus_sched_disable(s, data_id)` | Remove a Data-ID from the ring (called after the boiler answers unknown-dataid twice in a row). Idempotent; unknown IDs ignored. No error return, no stop — an empty ring is legal. In-place compaction + cursor correction. |
| `ot_bus_sched_step(s, now_ms) → ot_bus_step_t` | Decide the action at `now_ms`. Pure/read-only; does not advance state. Returns `WAIT` (`delay_ms`) or `TALK` (`data_id`, `is_write`, `value`, `overdue`). |
| `ot_bus_sched_scan(s, from, to)` | Start a read-only sweep of inclusive `[from..to]` (swapped if reversed). A call during a sweep restarts it. |
| `ot_bus_sched_scan_stop(s)` | Stop the sweep. |
| `ot_bus_sched_scan_progress(s, *done, *total)` | Report sweep coverage; both 0 when stopped. Read-only. |
| `ot_bus_sched_done(s, step, start_ms, end_ms)` | Record a finished conversation (success or not — the bus only keeps the pace) and advance state: flip the status turn, advance the sweep/ring, or clear the write. |

### Types and constants (`include/ot_bus_sched.h`)

- `ot_bus_verb_t` — `OT_BUS_WAIT` / `OT_BUS_TALK`.
- `ot_bus_step_t` — `{verb, delay_ms, data_id, is_write, value, overdue}`. `overdue` flags a
  TALK scheduled later than `OT_BUS_DEADLINE_MS` (a defect to observe).
- `ot_bus_sched_t` — the full scheduler state: sweep cursor (`scan_*`), poll ring
  (`poll`, `poll_count`, `next`), pending write, `last_start_ms`/`last_end_ms`/`started`
  timing, `status_turn_done` alternation flag, and `status_high`.
- Timing: `OT_BUS_PERIOD_MS` 950 (desired pace), `OT_BUS_MIN_GAP_MS` 100 (minimum gap between
  conversations), `OT_BUS_DEADLINE_MS` 1150 (the 1 s + 15 % ceiling), `OT_BUS_MAX_POLL` 32.
- Status bits (ID 0 high byte): `OT_STATUS_CH_ENABLE` 0x01, `OT_STATUS_DHW_ENABLE` 0x02,
  `OT_STATUS_COOLING` 0x04, `OT_STATUS_OTC_ACTIVE` 0x08, `OT_STATUS_CH2_ENABLE` 0x10.
  The firmware drives bits 0 and 1; the rest are named so no one reuses those positions.

## Implementation

Two source files (`include/ot_bus_sched.h`, `ot_bus_sched.c`, ~350 lines total) plus
`CMakeLists.txt`.

**Timing (`since`, `step`):** `since(now, then)` returns `now - then` in unsigned arithmetic
so the difference survives the uint32 millisecond overflow at ~49 days. A direct
`now >= deadline` comparison would break silently once per 49 days, and the resulting silence
would be read by the boiler as a heat demand — so subtraction, not comparison. `step()`
returns TALK immediately when nothing has run yet (`!started`); otherwise it WAITs until
`max(period, gap)` has elapsed since the last conversation and only then TALKs, setting
`overdue` when the elapsed period exceeded 1150 ms.

**Slot order (`step`):** on each TALK, if `!status_turn_done` it sends ID 0 as a READ-DATA
request (so `is_write` stays false — the master flags ride the high byte, the low byte is
reserved for the slave's answer; DO NOT convert to WRITE-DATA, the boiler would refuse the
frame). Otherwise it picks, in priority: a pending write, then the active sweep ID, then the
ring slot `poll[next % poll_count]`, and if the ring is empty it falls back to ID 0 again
(with `status_high` in the high byte — both assignments needed so an emptied ring doesn't ask
for heat every other conversation).

**State advance (`done`):** flips `status_turn_done`; on the meaningful turn it advances the
sweep (`scan_next++`, clearing `scan_active` once `scan_next` reaches `scan_to`), or clears
the pending write (regardless of outcome — the bus must not repeat a stale setpoint), or
advances the ring cursor modulo `poll_count`.

**`disable` compaction:** compacts the array in place preserving order, counting entries
dropped to the LEFT of `next`, then does `s->next -= dropped` and clamps to 0 if past the end.
The cursor correction is mandatory because of the caller's order of calls: the bus task runs
`step() → exchange → done() → is-unsupported check → disable()`. By then `done()` has already
advanced `next` past the asked slot, and compaction shifts the departed ID's successor into
that slot; without the subtraction that successor loses a full ring turn (~1 minute). This was
observed on live hardware: eight IDs dropped from the ring and exactly their successors went
missing from the log a turn later. DO NOT simplify to `s->next = 0` — that would cost the
not-yet-asked ring elements another turn, and the departure of the last element (cursor
already wrapped to zero, nothing dropped to its left) needs no correction at all.

Invariants / DO NOT constraints (from the source comments):

- The next-conversation moment is the max of two deadlines, never a fixed period (the naive
  950 ms period violates the minimum-gap rule).
- ID 0 goes out on every second step; never skip it (losing control for a cycle).
- The status is an assignment, not a consumed request — clearing it would drop `ch_enable`
  and cancel the heat demand a second after asking.
- The write queue holds exactly one; do not grow it into a ring (a late setpoint is worse
  than a lost one).
- The sweep only reads; there is deliberately no write path.
- An empty ring is legal; never return an error or a WAIT/skip from `disable`/`step`.
- `status_high` starts at 0 by SAFETY, not as a mere default.

## Tests

Host suite `test_ot_bus_sched` (source `test_ot_bus_sched.cpp` under the project's `test/`).
The scheduler is pure precisely so the polling pace and order are checked on the host, not by
stopwatch. Keep the scheduler untouched where possible: the suite sits near the per-directory
line ceiling, so any change that forces new tests forces a suite split.

## Notes

- Separate component on purpose: in a host build PlatformIO compiles ALL of a library's
  sources, so keeping the scheduler inside the bus component (whose `ot_bus.c` pulls in
  FreeRTOS and `esp_err.h`) broke `test_ot_bus_sched` with `esp_err.h not found`. DO NOT move
  it back into the bus component and DO NOT fix via `srcFilter` (that would make only half of a
  component host-buildable and break the guarantee that host tests compile exactly the sources
  the device executes).
- Registration: `idf_component_register(SRCS "ot_bus_sched.c" INCLUDE_DIRS "include")`, no
  dependencies.
