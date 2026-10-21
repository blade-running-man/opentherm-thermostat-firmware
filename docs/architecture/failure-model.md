# Failure model

The device sits between a boiler, a Wi-Fi network, an MQTT broker, Home Assistant and one or two
temperature sensors, and any of them can disappear for minutes or weeks. This page says where the
firmware's two governing rules are enforced, how its clocks and its memory across a reset are made
safe, and what the device does — and what the boiler gets — for each failure.

::: tip The two rules
**The bus never stops.** An OpenTherm slave that hears no correct master frame for about five
seconds reads it as a shorted thermostat and demands heat, so silence is the hottest state, not a
safe one. **Nothing reboots because a peer is absent.** A missing router, broker, Home Assistant or
sensor is reported — in the log, in `GET /api/status`, on the LED, on MQTT — and retried, never
answered with a restart.
:::

## Where the rules are enforced

### The bus never stops

- **The bus task has no exit.** The loop in `components/ot_bus/ot_bus.c` has no `break`, no
  `return` and no stop condition, there is no `ot_bus_stop()`, and the header forbids adding one.
  A frame error, a missing reply, a dead boiler — each is counted and logged, and the next
  conversation follows.
- **An empty poll ring still talks.** The scheduler sends ID 0 on every other slot whatever the
  ring holds, so there is no "nothing to ask, wait" path ([Bus](/protocol/bus)).
- **A missed deadline is said out loud.** A conversation later than 1150 ms is logged
  (`deadline missed`) and counted, never silently caught up.
- **The one deliberate silence ends by itself.** The line test (`POST /api/ops/linetest`) replaces
  conversations for at most 30 s and leaves its mode on elapsed time alone, never on a peer's
  answer — a stuck flag would leave the bus silent for good. Because a caller looping it would hold
  the boiler hot, it always needs a password ([Security](/architecture/security#the-bus-halting-operation)).
  The Data-ID scan does not halt the bus: ID 0 keeps going out between its probes.
- **Nothing can starve it.** `ot_bus` runs at priority 10, above every task the firmware creates;
  the executor (4), the radio (5) and MQTT (3) sit below it. The reply callback never blocks beyond
  the state model's lock, and logs nothing.
- **Nothing on the bus path waits for a peer.** SNTP is started from the network callback and
  returns at once; no task the bus waits behind calls `sync_wait`.

### Nothing reboots

- **There is no `esp_restart()` in the firmware.** No timer, counter or watchdog of its own restarts
  the chip; a configuration change, a new broker and new Wi-Fi credentials are all applied live.
- **Boot never stops on a subsystem.** Every start in `app_main()` logs and continues. The only
  `ESP_ERROR_CHECK` is in `nvs_init()`, after the recoverable cases (no free pages, a new NVS
  version) have been erased and retried. `ot_http_start()` derives its handler count from the route
  table and logs a registration failure instead of aborting.
- **Peers are retried for ever.** esp-mqtt keeps its auto-reconnect (every 10 s) and nothing counts
  "broker unreachable for N minutes". The Wi-Fi retry in `ot_provision` only ever produces a delay,
  doubling to a ceiling; it has no terminal state.
- **There is no image rollback to trip.** There is no OTA, and the bootloader's app rollback is not
  enabled, so an absent peer cannot make a bootloader revert a good image.

Three ESP-IDF mechanisms remain that can restart the chip, none of them about peers: a panic
(`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`, the IDF default), the interrupt watchdog (300 ms, which
ends in a panic) and the brownout detector. The task watchdog is enabled with a 5 s timeout but does
**not** panic (`CONFIG_ESP_TASK_WDT_PANIC` is unset); it only logs. These defaults are not
overridden in `sdkconfig.defaults`.

## Timers survive the wrap

A 32-bit millisecond clock wraps every 49.7 days. The unsigned difference `now - then` of two
`uint32_t` moments survives one wrap, but not more: a peer last heard 50 days ago reads as heard
minutes ago — in January, unwatched. The firmware uses two disciplines, chosen by how long the
interval can get.

**Saturating accumulators** — for every duration a safety decision rests on, because a dead peer
can stay dead for any length of time. Each step adds the interval since the previous step (one
unsigned difference of about a second) with a saturating add, so the count sticks at its maximum
instead of wrapping:

- in `ot_control`: the watchdog overdue time, the setpoint-confirmation age, the CH-bit age, the
  DHW-write age, the failsafe duration and the powered part-hour (`otc_add_sat()` in
  `ot_control_failsafe.c`);
- in `ot_sensor`: each room source's overdue time (so a sensor dead for 50 days stays dead).

**Wrap-safe unsigned differences** — everywhere else, for intervals far shorter than 49.7 days: the
bus pace and deadline (`ot_bus_sched`), the provisioning windows and back-off (`ot_provision`), the
30 s ticket lifetime (`ot_ticket`). These compare `(uint32_t)(now - then)` against a limit, never
`now` against a stored deadline. The LED's Wi-Fi grace period (`ot_led`) uses the same difference
with no bound on the outage it measures: a Wi-Fi outage longer than ~49.7 days wraps it, and the
grace colour can briefly show again — cosmetic, since the LED decides nothing. The same
difference is reported as an age: `age_ms` in `GET /api/state` and `/ws` (`ot_api`) and in
`GET /api/ot/raw` (`ot_observe`). Those ages are correct only below 2³² ms; a value last updated
more than 49.7 days ago reports its age modulo the wrap. `ot_mqtt_link` sidesteps the question with
a 64-bit clock for its log throttling.

The only stored moment in the executor is the boost deadline, whose ≤ 8 h span sits far inside the
±24.8 days a signed difference covers; the line test's end is compared the same way.

## A restore errs safe

Two counts must survive a reset, because a fast reboot loop — a panic on some payload, a brown-out
loop of a radio transmitting on a weak supply — would otherwise defeat the failsafe:

- **The watchdog overdue time.** Without it, a loop faster than `watchdog_s` would restart the
  watchdog on every boot and hold `ha_waiting`, CH off, for ever.
- **The heat-hours part-hour.** Without it, a loop faster than an hour would never complete a
  powered hour, and the failsafe's summer bound would never disarm.

### The `RTC_NOINIT` record

Both counts live in `RTC_NOINIT` RAM (neither zeroed nor loaded at boot: a software reset, a panic
and every watchdog leave it alone, a power-on fills it with noise), packed by `ot_control_io` as
`{magic, overdue_ms, hh_ms, check}` with magic `0x4F544332` (`"OTC2"`) and check word
`~(magic ^ overdue_ms ^ hh_ms)`. `ot_control_io_restore()` believes the record — both counts or
neither — only when:

1. the reset reason is a reboot-loop reason: software, panic, interrupt watchdog, task watchdog,
   other watchdog, or brown-out; **and**
2. the magic and the check word are intact.

A power-on starts both counts from zero (a cold start is not a loop); anything unlisted does too,
which costs at most one watchdog period. Brown-out is listed on purpose — it is the commonest
reboot loop of an ESP32-C3 — and the check word, not the reason, rejects RAM a voltage dip
corrupted. The reset reason and what was restored are logged at boot; that line is the one clue a
reboot loop leaves.

### NVS before the mirror

The whole heat hours are in NVS (`ctl`/`heat_h`). Each tick, a completed hour is written to NVS
**before** the part-hour is mirrored to `RTC_NOINIT`. The two stores cannot be atomic, so the order
picks which way a reset between them errs: NVS first banks one extra hour, and the summer failsafe
disarms **sooner** — less unwanted heat. The other order would lose the hour from both and prolong
an out-of-season burn.

A missing or unreadable `heat_h` restores the count at its maximum: the failsafe is disarmed until
Home Assistant has asked for heat. Every other failure of that read lands on the same safe end.

## Peer by peer

### The controller and its transport

| What failed | What the boiler gets | What is visible |
| --- | --- | --- |
| Home Assistant dead, broker up (HA mode) | after `watchdog_s`: the [bounded failsafe](/architecture/executor#the-bounded-failsafe) | `control_state = failsafe`, cause `watchdog`; `failsafe_count` retained; the log |
| Broker dead (HA mode) | the same, on the same deadline | `GET /api/status`'s MQTT block, the LED (`mqtt_down`), the log |
| Wi-Fi dead (HA mode) | the same | the LED (`wifi_down`); after 30 min of failure the device's own access point comes back alongside the retrying station ([Networking](/device/networking)) |
| Any of the above in LOCAL mode | nothing changes: no watchdog runs in LOCAL | as above, minus the failsafe |
| Home Assistant alive, its forwarded room sensor dead | `failsafe` via `ha_blind` — opt-in, only with `room_mqtt_ha_forwarded = true` | `control_state = failsafe`, cause `ha_blind` |
| Home Assistant alive, its automation wrong | whatever it asks, within `[flow_min, flow_max]`; an out-of-range command is refused and feeds no watchdog | refusals counted and logged; otherwise outside the firmware's knowledge |
| Home Assistant sends a retained command | refused before it reaches the executor | the MQTT refusal count, a log line |

Within HA mode with the season on, a failsafe is left only by an accepted Home Assistant
`ch_enable` while Home Assistant is not blind. Turning the season off or switching to LOCAL leaves
it too, and the way back (season on, or `LOCAL → HA`) is an entry into ownership that clears the
latch and starts again in `ha_waiting` ([Executor](/architecture/executor#transitions-and-the-watchdog)).

### Sensors

| What failed | What the boiler gets | What is visible |
| --- | --- | --- |
| A room source stops reporting | after its stale window, the next fresh `room`-role source steers; none fresh → the failsafe heats blind | `room_source`, `room_temperature_effective` stop updating |
| The shield DS18B20 missing or noisy | nothing: it is `ambient` and never steers | a log line on the first failure and every 100th after, telling "no presence pulse" from "CRC failed" |
| An outlier reading | filtered by `ot_sensor`, with an escape hatch for a real step change | — ([Room sources](/device/room-sources)) |

### The boiler

| What failed | What the boiler gets | What is visible |
| --- | --- | --- |
| The boiler stops answering | polling continues at the same pace | after 5 failed conversations: `boiler is not answering … polling continues` in the log, `answering: false` in `GET /api/ot/raw`; entities keep their last value and age |
| A Data-ID unknown to the boiler | dropped from the poll ring after two UNKNOWN-DATAID answers in a row (any other answer resets the count); writes to it refused `409` — except `ch_setpoint` (ID 1), which goes to the executor without the support check | the entity reads `unsupported` and is not announced to Home Assistant |
| The boiler answers DATA-INVALID or UNKNOWN-DATAID to ID 1 | the setpoint still counts as sent, so the CH bit is not held hostage | one warning in the log |
| The boiler reports a fault | nothing the firmware decides: the fault flags are entities for Home Assistant | the ID 5 entities ([Boiler](/boiler/)) |

### The device itself

| What failed | Effect | What is visible |
| --- | --- | --- |
| `ot_master_init()` failed | no bus — the one silence the firmware cannot prevent; everything else still starts so the owner can see why | the boot log |
| The thermostat task did not start | the bus keeps polling with the CH bit down; every command answers `503` (no task) | the boot log |
| The HTTP server did not start | bus, executor and MQTT carry on | the boot log over USB |
| An accepted command the store refuses | refused (`NOT_SAVED`), never a silent `202` | the HTTP answer, the log |
| Heat hours not written | the next hour asks again; a lost hour only shortens the arm | a warning |
| SNTP dead | nothing: no decision depends on the wall clock | — |
| A reboot | the master is silent from reset until the first conversation, which is why the bus starts before the network; then, in HA mode, `ha_waiting` until Home Assistant speaks. The watchdog count is carried over only after a software, panic, watchdog or brown-out reset with an intact record (from zero after a power-on); a carried count already at `watchdog_s` starts straight in `failsafe` | the reset reason in the log |
| A reboot loop | the `RTC_NOINIT` counts carry on, so the failsafe still fires on schedule and the summer bound still disarms | the reset reason in the log |

::: info Verified on the boiler
The scenarios that walk these failures on real hardware — Home Assistant stopped, the summer case,
a reboot loop, broker and Wi-Fi each killed for an hour, repeated resets with the burner running —
are in [Watchdog and failsafe](/hardware-verification/07-watchdog-and-failsafe) and
[Room sources](/hardware-verification/08-room-sources).
:::
