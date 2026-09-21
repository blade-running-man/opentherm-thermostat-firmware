# ot_bus — the task that owns the OpenTherm bus

## Purpose

`ot_bus` is the single FreeRTOS task that drives the OpenTherm wire. It is the **only** caller of
`ot_master_exchange()`: it steps the scheduler, sends one frame per conversation, records every
reply, and never stops talking to the boiler. It is the impure task shell wrapped around two pure
siblings — the scheduler `ot_bus_sched` (what to send next) and the write bookkeeping `ot_bus_track`
(who wrote what and whether it left the wire).

## Responsibility

What it owns:

- **The bus task.** One `for (;;)` loop, priority 10, 4 KiB stack, started by `ot_bus_start()`.
  It picks the next step from `ot_bus_sched`, runs `ot_master_exchange()`, and feeds the reply back
  into the tracker, the observed table, the stats and the response callback.
- **The write queue of one** (`ot_bus_write`, `ot_bus_write_if_idle`) — a new write evicts an
  unexecuted one, because a stale setpoint is worse than a lost one.
- **The ID 0 status high byte** (`ot_bus_set_status`) — the *only* way the firmware asks the boiler
  for heat.
- **The master introduction** — three writes (ID 2, ID 124, ID 126) sent exactly ONCE at start, one
  per meaningful slot, never repeated.
- **Diagnostics** — the stats snapshot (`ot_bus_stats`), the ID-1 write bookkeeping
  (`ot_bus_write_state`), the observed reply table (`ot_bus_observed`), the Data-ID sweep
  (`ot_bus_scan`), and the multimeter line test (`ot_bus_line_test`).

What it does NOT do:

- It does not decide *what* to send — that is the pure `ot_bus_sched` scheduler.
- It does not do the write bookkeeping itself — that is the pure `ot_bus_track`.
- It does not know the boiler's semantics, the control ladder, or the room. It carries out others'
  decisions on the wire.
- It depends on `ot_state` in ONE direction only (`ot_state_is_unsupported()`, to drop a flagged ID
  from the poll ring). `ot_state` must never depend on the bus — that would be a link-breaking cycle.

### The load-bearing invariant: the loop never stops

**`ot_bus` must never stop the conversation with the boiler under any failure.** A slave that has
not seen a correct frame for roughly 5 seconds treats this as a short-circuited thermostat and
**goes into a demand for full heat**. Going silent is therefore not a safe state — it is the hottest
one possible.

The header (`include/ot_bus.h`) states this as a rule: the task loop has no exit, and **`DO NOT` add
a `break`, a `return` or a stop condition to it.** Neither a frame error, nor a missing reply, nor a
WiFi failure, nor a stale sensor may break the loop. Every consequence of this shows up in the code:

- The introduction step counts as *done when the write is queued*, not when it is answered, so a
  silent boiler never holds a slot forever.
- The line test leaves its mode unconditionally on elapsed time — a stuck flag would fall silent
  forever, which is exactly the silence the boiler reads as a heat demand.
- `ot_bus_set_status()` cannot fail and cannot stop the conversation: there is no state in which
  falling silent is safer than sending the byte we already have.

## Public API

All functions are declared in `include/ot_bus.h`. Unless noted, callers are "any task".

| Symbol | Contract |
| --- | --- |
| `ot_bus_start(poll, count, cb, ctx)` | Creates and starts the bus task (priority 10, 4 KiB stack). `poll` is the polling ring besides the mandatory ID 0; may be NULL. Returns `ESP_ERR_NO_MEM` if `xTaskCreate` fails, else `ESP_OK`. **No stop function exists — deliberately.** Sends the master introduction once. |
| `ot_bus_write(data_id, value)` | Queues a write into the one-slot queue; the next call evicts an unexecuted write. A write queued while another is on the wire is not lost — it takes the next meaningful slot. Safe from any task. |
| `ot_bus_write_if_idle(data_id, value)` | Queues a write ONLY if no write is queued or on the wire; returns whether it did. For the control loop's own writes (held ID 1, ID 56 reconciliation) that must never evict a hand write. Same check-and-queue under one lock. `false` means "not now". |
| `ot_bus_write_state(out)` | Copies `{id1_seq, id1_raw}` under the queue lock. `id1_seq` steps +1 per ID-1 write *answered* (WRITE-ACK / DATA-INVALID / UNKNOWN-DATAID all count; an unanswered frame does not). A count, not a flag, so a late reader cannot miss a foreign ID-1. Never blocks. |
| `ot_bus_set_status(high)` | Sets the master flags for the ID 0 request high byte (an OR of `OT_STATUS_*`, names from `ot_bus_sched.h`). Reaches the boiler within one conversation. **The only way the firmware asks for heat.** Cannot fail, cannot stop the bus. Does not block. |
| `ot_bus_stats(out)` | Copies `ot_bus_stats_t` under the spinlock (torn-read-free), including live scan progress. |
| `ot_bus_line_test(duration_ms, half_period_ms)` | Stops conversing and slowly toggles the output for a multimeter check. Returns `false` if a test already runs OR params are out of range (`OT_BUS_TEST_MAX_MS`, `OT_BUS_TEST_HALF_MIN_MS`, `OT_BUS_TEST_HALF_MAX_MS`). **Warning: the boiler will most likely light during the test**, because the sustained master silence reads as a heat demand. |
| `ot_bus_line_test_active(void)` | Whether a line test is currently running. |
| `ot_bus_scan(from, to)` | Sweeps `[from..to]`, asking each ID once. **READ ONLY** — no code path here writes, and none may. Results accumulate in the same observed table. |
| `ot_bus_scan_stop(void)` | Stops an in-progress sweep. |
| `ot_bus_observed(void)` | Returns a pointer to the internal, program-lifetime `ot_observe_t` table. **Read without a lock** — a diagnostic table where a torn row is acceptable and a lock on the conversation path is not. |

Types: `ot_bus_response_cb` (called from the bus task after every successful reply — **DO NOT
BLOCK**); `ot_bus_stats_t` (cycles / ok / failed / overdue / consecutive_fail / boiler_answering /
scan_done / scan_total); `ot_bus_write_state_t` (`id1_seq`, `id1_raw`). Constants:
`OT_BUS_TEST_MAX_MS` (30000), `OT_BUS_TEST_HALF_MIN_MS` (100), `OT_BUS_TEST_HALF_MAX_MS` (5000) —
named in the header so a second copy cannot drift.

## Implementation

Single source file `ot_bus.c`. `CMakeLists.txt` registers it with
`REQUIRES ot_bus_sched ot_bus_track ot_master ot_frame ot_observe ot_state esp_timer`.

### Module state

- `s_sched` (`ot_bus_sched_t`) — the pure scheduler: write queue, ID-0 status byte, scan, poll ring.
- `s_track` (`ot_bus_track_t`) — the pure write bookkeeping, beside `s_sched`; every write goes
  through it.
- `s_observed` (`ot_observe_t`) — the heard-replies table.
- `s_stats` (`ot_bus_stats_t`), `s_link` (`link_state_t`), `s_cb` / `s_ctx`, and the introduction
  progress (`s_identity_step`, `s_identity_124`).
- Line-test state: `s_test_on`, `s_test_until_ms`, `s_test_half_ms` (all `volatile`).

### Locking

- **`s_mux`** — a `portMUX_TYPE` spinlock (`taskENTER_CRITICAL` / `taskEXIT_CRITICAL`), NOT a mutex.
  It guards `s_sched`, `s_track` and `s_stats` as a whole — the write queue, the ID-0 status byte,
  the scan, the poll ring, the write generation and the diagnostic counters. A spinlock is chosen
  because every critical section is a call to a short pure function, and blocking the scheduler over
  it is cheaper than a mutex object.
- **DO NOT** take `ot_state`'s mutex inside `s_mux`: interrupts are off. The
  `ot_state_is_unsupported()` question is asked OUTSIDE the section; only `ot_bus_sched_disable()`
  runs inside it.
- Every `ESP_LOG` and the `s_link` transition stay OUTSIDE `s_mux` — a log with interrupts off is
  forbidden, and `s_link` is the bus task's own.

### The task loop (`bus_task`)

1. **Line test.** If active, toggle the output every `s_test_half_ms` and exit the mode
   unconditionally when `s_test_until_ms` passes.
2. **Introduction.** While `s_identity_step < IDENTITY_COUNT` and the queue is empty, queue the next
   identity write. Someone else's write is never overwritten; the reverse (a real write evicting the
   introduction) is allowed — that is the eviction rule the queue-of-one exists for.
3. **Step.** `ot_bus_sched_step()` under `s_mux`, capturing `s_track.gen` in the same section so
   `ot_bus_track_done()` can tell whether a write was queued mid-exchange.
4. **Exchange.** `WAIT` → `vTaskDelay` (at least one tick — a zero delay would spin at above-idle
   priority). Otherwise build the frame, call `ot_master_exchange()`, log if the deadline was
   overdue.
5. **Finish.** ONE critical section runs `ot_bus_track_done()` and publishes every `s_stats` field
   this cycle touches, so no unlocked reader can copy a half-updated snapshot.
6. **On success:** record into `s_observed` (including UNKNOWN-DATAID), call `s_cb`, and if
   `ot_state_is_unsupported()` drop the ID from the poll ring (logged once, keyed off the ring
   shrinking rather than an own list).
7. **On failure:** step `consecutive_fail`; at `LOST_AFTER` (5) clear `boiler_answering` and, once,
   log "boiler is not answering".
8. No exit. Ever.

### Invariants and DO NOTs

- **The loop has no exit** — no `break`/`return`/stop condition, because sustained master silence is
  read by the boiler as a demand for heat.
- **The introduction is sent once**, never moved into the periodic ring "in case the boiler
  rebooted": a boiler that lost power lost the whole session, and restoring it is a separate
  conversation the firmware does not carry.
- **`ot_bus_scan` is read-only** — no write path exists in the sweep, and none may be added.
- **No log in `ot_bus_set_status` / `ot_bus_write_state`** — called every second by the control
  loop; a line per second would flush `GET /api/log`'s ring buffer.
- **`ot_bus_write_state` reports nothing else about the queue** — `DO NOT` add a field without a
  reader.
- **`LOST_AFTER = 5`** ties directly to the roughly five-second silence threshold above which the
  boiler starts demanding heat: declaring the loss earlier is dishonest, later is useless.
- **`link_state_t` is three-valued** (`LINK_UNKNOWN` / `LINK_ANSWERING` / `LINK_SILENT`) so that a
  boiler that has *never* answered is distinguishable from a bus that never started — a bug found by
  running an earlier, two-valued edition.

## Tests

**No dedicated host suite.** This component is FreeRTOS-based and impure by design (it owns the
task, the spinlock and the wire), so it is not host-compiled. Its logic lives in the pure siblings
that *are* host-tested — `ot_bus_sched` (`test_ot_bus_sched`) and `ot_bus_track`
(`test_ot_bus_track`). The glue in `ot_bus.c` that no host suite compiles is pinned by source
guards: `tools/tests/test_source_guards.py` reads `ot_bus.c` and fails when a call a fix depends on
is replaced by its obvious neighbour.

## Notes

- **The silence rule dominates everything.** Master silence longer than about five seconds is read
  by the boiler as a shorted thermostat and becomes a demand for full heat. Every "keep going /
  never stop / count on queue, not on reply" decision in this file traces to it.
- **The ID-0 status high byte was a late, load-bearing finding:** for a long time it was never sent,
  so ID 0 went out as DATA-VALUE 0x0000 and the boiler was never actually asked for heat — and a
  hand writing ID 1 changed a flow setpoint the boiler ignores while the CH-enable bit is down.
  `ot_bus_set_status` is the one channel that fixes this.
- **The stats snapshot shares the spinlock with the cycle-end update.** An earlier version wrote the
  counters unlocked while an HTTP status handler copied them unlocked, so a reader could see, say,
  `boiler_answering` already cleared but `consecutive_fail` not yet stepped. The copy is now taken
  inside the same critical section that publishes the counters.
- **Split rationale (see `CMakeLists.txt`):** the pure scheduler is a separate component precisely so
  it can be host-tested; `ot_bus` is the thin impure shell. `ot_state` is required in one direction
  only to avoid the `ot_bus → ot_state → ot_bus` cycle.
- **Identity ID 124** (OpenTherm version 2.2) is computed at start via `ot_codec_float_to_f88(2.2f)`
  rather than hard-coded as `0x0233`, so the version reads as "2.2" where it is named.
