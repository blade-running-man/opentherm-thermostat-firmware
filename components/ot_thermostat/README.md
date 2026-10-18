# ot_thermostat — the task layer that carries out the executor, and the ONE writer of the ID 0 high byte

## Purpose

This component is the FreeRTOS task, the spinlock, the bus access, the NVS store and the
`RTC_NOINIT` memory that carry out the executor's decisions. The executor's logic (`ot_control`)
and its input/output translations (`ot_control_io`) are pure and host-tested; this component is
their impure home, kept thin on purpose because they are thick. It is the sole writer of the
master status high byte (the CH- and DHW-enable bits in Data-ID 0) and of the held Data-ID 1
(`TSet`, the CH flow setpoint).

## Responsibility

**Owns:**

- The one executor instance (`ot_control_t`) together with the snapshot and the output of its
  last step, all under one spinlock.
- A once-a-second tick (`TICK_MS` = 1000 ms). Each tick, in order: snapshot the configuration
  from `ot_net` and map it into the executor's configuration; apply the configured time zone;
  drain the room-source registry into the executor's input; read the bus's write report and the
  last Data-ID 56 READ-ACK; step the executor under the spinlock; then, outside the spinlock,
  hand the status byte to the bus, queue the held ID 1 and ID 56 into an idle bus slot, mirror
  the watchdog state into `RTC_NOINIT`, persist heat hours when asked, log what changed and
  publish the synthetic entities into `ot_state`.
- The room-source registry: a locked mailbox in front of the single-owner `ot_room` registry,
  drained once a tick on this task alone. Slot 0 is always the on-shield DS18B20 (ambient role);
  slot 1 is the MQTT room source when that source is enabled in configuration.
- Memory that survives a reset: the watchdog overdue count and the heat-hours part-hour in
  `RTC_NOINIT`, and the whole heat hours in NVS (namespace `ctl`, key `heat_h`).
- Following the configured time zone without a reboot, by re-reading it each tick and applying it
  on change.

**Does NOT do:**

- **Decide anything.** What a reply means, what the executor ladder does, what to persist and what
  to restore all live in `ot_control` / `ot_control_io`, which are pure and host-tested. This layer
  only carries verdicts out.
- **Evict a hand write.** The executor's own writes go through `ot_bus_write_if_idle()` only, so a
  hand write already queued on the bus is never displaced. It never calls `ot_bus_write()`.
- **Encode with `ot_command`.** Its own writes are encoded by `ot_control_io_dc_f88()`. `ot_command`
  stays the gate for hand writes made through the HTTP layer, because `ot_command_encode()` refuses
  an ID the state model has marked unsupported and never clears that mark — which would silence the
  executor's own held setpoint.
- **Reboot because a peer or a task is absent.** If the task fails to start, the boiler keeps being
  polled and every command answers `OT_THERMOSTAT_NO_TASK`.

## Public API (`include/ot_thermostat.h`)

| Symbol | Contract |
| --- | --- |
| `ot_thermostat_start()` | Starts the task. Idempotent (a second call returns `ESP_OK` and does nothing). Restores reset-surviving state and initialises the executor and the room registry **before** creating the task, so no request meets an uninitialised one. Call it after `ot_bus_start()` and `ot_net_start()` and before `ot_http_start()`. Fails only if the task cannot be created (`ESP_ERR_NO_MEM`); the caller must not treat that as fatal, since the boiler keeps being polled either way. |
| `ot_thermostat_heard(data_id, type, raw)` | Records one bus reply through `ot_control_io_heard()`: the Data-ID 56 readback (a READ-ACK only) and the first DATA-INVALID answer to ID 1. Runs on the **bus task**, on the conversation's path — it takes the spinlock for one pure call and does nothing else (no log, no mutex, no NVS). Safe to call before `_start()`. Do NOT add work here. |
| `ot_thermostat_room_submit(slot, celsius)` | A room-temperature reading from any task, stashed in the locked mailbox under the room spinlock. It does not touch `ot_room` (single-owner, drained on the thermostat task alone). An out-of-range `slot` is silently ignored. Safe to call before `_start()`. |
| `ot_thermostat_control_cfg(out)` | The configuration snapshot the last step used, so a command can be pre-checked before it is applied. Up to one tick old, by design. Before `_start()` it is a zeroed snapshot (LOCAL mode, season off, zero flow bounds — every setpoint refused), which is the truth about a device with no executor. Task-safe. |
| `ot_thermostat_control_apply(origin, cmd, value, store_err)` | A command for the executor from `origin`. Re-checks ownership inside the spinlock against the mode the executor last observed, lands the command (quantising a CH setpoint to 0.5 °C), then persists it via `ot_net_config_apply()` outside every lock on the caller's task. A mutex holds the apply and its persist together so two commands cannot land in RAM in one order and reach the store in the other. Returns `ot_thermostat_err_t`; sets `*store_err` (may be NULL) only on `OT_THERMOSTAT_NOT_SAVED`. For the httpd task and the MQTT publisher task — never the esp-mqtt task itself. |
| `ot_thermostat_boost_start(setpoint_dc, minutes)` | Starts or replaces a boost (LOCAL mode only). All of `ot_control_boost_start()`'s checks (owned-by-HA, season-off, bad-minutes, out-of-range) plus `OT_THERMOSTAT_NO_TASK`. The boost lives in RAM only — a reboot ends it deliberately. |
| `ot_thermostat_boost_cancel()` | Ends the boost. Task-safe, harmless when none runs, and returns at once before `_start()`. |
| `ot_thermostat_control_get(out)` | Fills the `GET /api/control` document: the last step's snapshot and output, the boost as it is now, and the task's stack high-water mark. One consistent, task-safe copy. Before `_start()` it is zeroed with `stack_known` false. |
| `ot_thermostat_err_t` | The task-layer verdict. Values `OK`..`BAD_MINUTES` are **equal in value** to the matching `ot_control` codes; the two this layer adds sit clear at `0x40`: `NO_TASK` (the task never started at boot) and `NOT_SAVED` (`ot_control` accepted the command but the store refused the patch — the reason is in `*store_err`). A `static_assert` keeps `ot_control`'s range below `0x40` so the two never collide. |

## Implementation

The component is cut from one file at the 350-line source ceiling, along seams where nothing is
shared. The private `ot_thermostat_internal.h` declares the cross-file seams; each source file's
statics are touched by one task at a time.

- **`ot_thermostat.c`** — the task, the tick loop, the executor state and every public entry point.
  Holds the one spinlock (`s_mux`) that guards the executor (`s_ctl`), its last snapshot and output
  (`s_cfg`/`s_out`) and the reply record `ot_thermostat_heard()` keeps (`s_heard`). Task-only
  statics (the config snapshot, the seen-ID1 counter, the owed ID 56, the last status byte) need no
  lock. A separate FreeRTOS mutex (`s_apply`, static, taken outside `s_mux` and never by the task)
  serialises `control_apply()` with its persist so two commands cannot reach the store out of order.
  Task priority is 4 — below `ot_bus` (10) and `ot_net` (5) so nothing can starve the bus. Stack is
  4096 bytes; the high-water mark is reported in `GET /api/control`.
- **`ot_thermostat_report.c`** — what the task says and shows: applying the time zone on the first
  tick and again on change (the config is polled, not pushed); the once-per-boot warnings that the
  boiler answers UNKNOWN-DATAID or DATA-INVALID to ID 1; logging step changes on change only (the ID
  1 line before the status line, so the log shows the setpoint reaching the wire before the CH bit
  rises); and publishing the synthetic entities into `ot_state`.
- **`ot_thermostat_persist.c`** — the executor's memory across a reset. On boot it reads the reset
  reason, the `RTC_NOINIT` blob and the heat hours from NVS and hands them to `ot_control_io_restore()`,
  which decides what to believe. Every step it mirrors the overdue count and part-hour into
  `RTC_NOINIT`, and when the executor asks it writes whole heat hours to NVS (`ctl`/`heat_h`).
- **`ot_thermostat_room.c`** — the room-source registry: its own spinlock (`s_room_mux`, not the
  executor's), the mailbox, the pure `ot_room` instance, and a config fingerprint so enabling or
  disabling the MQTT slot, or changing its role, stale window or HA-forwarded flag, takes effect
  without a reboot. Each tick it drains the mailbox into `ot_room`, steps it, fills the steer
  selection into the executor's input, and publishes `room_temperature_effective` and `room_source`
  from the display selection.
- **`ot_thermostat_room_cfg.c`** and its own header `ot_thermostat_room_cfg.h` — the **pure**
  configuration-to-`ot_room` mapping, kept in its own translation unit with a header that pulls in
  only pure dependencies (`ot_config.h`, `ot_room.h`, not `ot_bus.h`) so it builds and tests on the
  host alone. Slot 0 is always the shield (ambient role, not HA-forwarded, 60000 ms stale window);
  slot 1 is the MQTT source when enabled (role and HA-forwarded flag verbatim, stale window
  converted seconds → ms).

### Locking and invariants (the `DO NOT`s)

- One spinlock (`s_mux`) held only across the pure calls. Do NOT call `ESP_LOG`,
  `ot_net_config_apply()`, `ot_state_*` or NVS inside it — taking a mutex with interrupts off is not
  allowed, and a log line there is half of a lock-order inversion.
- The NVS whole-hour save runs **before** the `RTC_NOINIT` part-hour mirror. A reset in the window
  between the two stores must err safe: banking one extra hour disarms the summer failsafe sooner
  (less unwanted heat), whereas losing the hour from both would prolong an out-of-season burn.
- The CH bit never rises before the held ID 1 has gone out. ID 1 is queued **first**, then the owed
  ID 56. The owed-ID-56 record is file-scope and never reset, so a refused ask is not forgotten a
  tick later.
- Do NOT read the ID 56 readback from `ot_state_get("dhw_setpoint")` — `ot_state` keeps a WRITE-ACK
  like a READ-ACK, which would defeat the readback cap. Read it from the record `ot_thermostat_heard()`
  keeps.
- The `RTC_NOINIT` blob is believed only after a reset that `ot_control_io_restore()` lists and only
  with its magic and check word intact. Do NOT move it to `.bss` — zeroed at boot, it would restart
  the watchdog on every reboot and hold a reboot loop in a heat pulse per boot.

## Tests

This component is **not host-tested** — it is FreeRTOS, NVS and the bus. Its correctness rests on
`ot_control` / `ot_control_io` (host-tested) and on hardware verification. The glue that has no host
suite is pinned by source guards: `tools/tests/test_source_guards.py` reads this component's sources
and fails when a load-bearing call is swapped for its obvious neighbour (for example
`ot_bus_write_if_idle` → `ot_bus_write`, or a freshly-reset owed-ID-56 record).

The one pure exception is **`ot_thermostat_room_cfg.c`**, host-built and host-tested on its own in
`test/test_ot_thermostat_room/`. `library.json`'s `srcFilter` narrows the `env:native` build of this
directory to that single file, so a host test can reach the header without dragging in the
FreeRTOS/bus/NVS sources.

## Notes

- **Configuration is polled, not pushed** — there is no change hook on the config store; the time
  zone and the room-MQTT config are re-read each tick. The zone is applied even when empty on the
  first tick, so an explicit `UTC0` holds at boot rather than defaulting to newlib's UTC.
- **No `%f` anywhere in this component** — one float through `ESP_LOG` runs newlib's full float
  formatter twice on this stack; tenths are logged via the sign-and-integer `DC_FMT` / `DC_ARG`
  macros.
- **An ambient source never steers.** With only the DS18B20 slot configured, the steer selection
  stays not-fresh, so the failsafe still heats blind rather than following an ambient reading.
- **A boost lives in RAM only** — resurrecting one after an OTA would be a boiler heating nobody
  remembers.
- **`ot_thermostat_room_submit` silently drops out-of-range slots** — a bad caller must not corrupt
  the mailbox or crash the device over a room reading.
- **Every call refuses cleanly before `_start()`** — a zeroed snapshot or document is the truth about
  a device with no executor.
