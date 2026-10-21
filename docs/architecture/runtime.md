# Runtime

What happens from power-on, which FreeRTOS tasks then run and who owns which piece of state, and
how the four flows that matter travel through them end to end. Everything here is read from
`src/main.cpp` and the components it starts.

## Boot sequence

`app_main()` starts the subsystems in a fixed order, and the order carries load. Every start after
NVS is "log the error and carry on": a component that cannot come up leaves a device that still
drives the boiler, or at least still says over USB and the web what broke.

| # | Call | Why here | On failure |
| --- | --- | --- | --- |
| 1 | `ot_log_install()` | First, so the boot lines land in the ring `GET /api/log` serves, not only on a USB console nobody connected. | — |
| 2 | Log project name and version | — | — |
| 3 | `nvs_init()` — `nvs_flash_init()`; on `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` the partition is erased and initialised again | Every runtime setting lives in NVS; a truncated or re-versioned store costs the settings, never the operation. | any other error aborts the boot (`ESP_ERROR_CHECK`, the only one in `app_main`) |
| 4 | `board_get()` and a log line with the board, the OpenTherm pins and the button | — | — |
| 5 | Log the compiled-in web UI size | — | — |
| 6 | `ot_master_init(b)` | The bus comes **before** the network: a router that answers slowly must not delay the first conversation, because master silence is a heat demand. | logged; the bus is not started, everything else still is |
| 6a | `ot_state_reset()` | Before the bus starts: the boiler's first reply may arrive before `ot_bus_start()` returns. | — |
| 6b | `ot_bus_start(poll, n, on_ot_response, …)` — the poll ring copied from the registry | — | logged |
| 7 | `ot_net_start(b, on_net_state, …)` | Owns the configuration document the executor reads. | logged; the boiler keeps being polled |
| 8 | `ot_thermostat_start()` | After the bus (it owns the destination of the ID 0 high byte) and the network (it reads the configuration), before HTTP (which calls into it). Restores the reset-surviving counts and initialises the executor **before** creating its task. | logged; nothing asks for heat, every command answers "no task" |
| 9 | `ot_mqtt_link_start()` | After the executor whose commands it carries; before HTTP, whose `GET /api/status` reads its counters. It dials nothing itself — its task starts the client on its first pass if a broker host is stored. | logged; no MQTT |
| 10 | `ot_http_start()` on port 80 | — | logged |
| 11 | The `ds18b20` task, if `board->onewire_gpio >= 0` | Last and lowest priority: the 1-Wire read blocks about 800 ms and must never delay the bus, the network or HTTP. | logged; no room readings |
| 12 | `ot_led_task_start(b)`, if `board->rgb.gpio >= 0` | Last, reader-only. | logged; no status LED |

After step 12 `app_main()` returns. SNTP is started later, from the network callback, the first
time the station gets an address (`ot_time_start()` is idempotent and returns at once; nothing
waits behind it). A fresh device asks for no heat: `heating_season` and `local_ch_enable` default
to `false`.

## Tasks

| Task | Created by | Prio | Stack (bytes) | Role |
| --- | --- | --- | --- | --- |
| `ot_bus` | `ot_bus_start()` | 10 | 4096 | The only caller of `ot_master_exchange()`: a conversation about every 950 ms, never later than 1150 ms; the master introduction once; the line test. The loop has no exit. |
| `ot_net` | `ot_net` | 5 | 4096 | The provisioning task: owns the radio, feeds Wi-Fi events into the `ot_provision` machine, ticks it every 250 ms, performs its actions, writes NVS. The Wi-Fi event handler itself only enqueues. |
| httpd | `esp_http_server` | 5 (IDF default) | 8192 | Every HTTP request, one at a time, with up to 7 client sessions. Also performs the `/ws` sends, which a 1 s `esp_timer` queues onto it with `httpd_queue_work()`. Runs `ot_thermostat_control_apply()` for REST commands. |
| `ot_thermostat` | `ot_thermostat_start()` | 4 | 4096 | The executor's once-a-second tick ([Executor](/architecture/executor)). The stack high-water mark is reported in `GET /api/control`. |
| `ot_mqtt` | `ot_mqtt_link_start()` | 3 | 6144 | Owns the esp-mqtt client: follows the broker settings, drains the 8-deep inbox, runs `ot_thermostat_control_apply()` for MQTT commands, publishes availability, discovery and state. Wakes on notification, else every 1 s (every 100 ms while publishes are owed). |
| `mqtt_task` | esp-mqtt | 3 | 6144 | The client's own network loop; its event handler copies an inbound message into the inbox and parses nothing. |
| `ot_dns` | `ot_captive` | 3 | 3072 | The captive DNS responder on UDP 53, only while the access point is up. |
| `ds18b20` | `src/main.cpp` | 1 | 4096 | Reads the shield's DS18B20 over RMT every 3 s and hands the raw value to the room mailbox. |
| `led` | `ot_led_task` | 1 | 4096 | Samples the world once a second, animates every 50 ms, drives the WS2812 pixel. |

Below the tasks, `ot_master` runs a `gptimer` interrupt every 100 µs: it clocks the 500 µs
transmit half-bits and samples the receive line for `ot_decode`. Its callback is in IRAM; nothing
in it allocates, locks or logs. See [Physical layer](/protocol/physical-layer).

The priorities are a safety decision, not a preference: nothing that talks to a peer may be able
to starve the bus task, so `ot_bus` is above everything the firmware creates, the executor is
below the radio, and MQTT is below the executor.

### Who owns what

| State | Owner | Guarded by |
| --- | --- | --- |
| The scheduler, the write queue, the status byte, bus statistics | `ot_bus` | the bus spinlock (`s_mux` in `ot_bus.c`) |
| The decoded entity values | `ot_state` | `ot_lock()` — one recursive mutex, held by a renderer for a whole consistent document |
| The executor instance, its last snapshot and output, the ID 56 readback record | `ot_thermostat` | its own spinlock, held only across pure calls (no log, no NVS, no mutex inside) |
| An executor command and its persist, as one step | `ot_thermostat` | a separate mutex (`s_apply`), taken before the spinlock or `ot_net`'s mutex, never by the task |
| The room-source registry | the `ot_thermostat` task alone | a mailbox under its own spinlock in front of it |
| The configuration document | `ot_net` | its mutex; every reader takes a snapshot |
| The esp-mqtt client | the `ot_mqtt` task alone | — |
| The `/ws` client list and the ticket table | the httpd task alone | — |

The bus task does take `ot_lock()` indirectly, through `ot_state_apply_dataid()` in the reply
callback, so it can wait for a renderer. That is measured, not overlooked: rendering the whole
state takes single milliseconds against an 1150 ms deadline, and a torn snapshot would cost more.
If the margin is ever eaten, the renderer is what must change — copy under the lock, serialise
outside it — not the lock in the callback.

## End-to-end flows

### Home Assistant command → boiler

1. A message lands on `<prefix>/<key>/set`. The esp-mqtt event handler copies topic, payload and
   the retain flag into the inbox and returns; a burst beyond eight messages is dropped.
2. The `ot_mqtt` task drains the inbox and calls the pure `ot_mqtt_decide()`. A **retained**
   command is refused here (replayed by the broker, it would look like a fresh decision), and so is
   a payload that is not a number.
3. `ot_mqtt_handle()` runs it exactly as the REST route would, with the origin fixed to `HA` and
   the effects injected. It calls `ot_command_check(key, value, OT_ORIGIN_HA, …)` against the
   executor's own configuration snapshot — writability, boiler support, representation, then
   ownership, then bounds. A Home Assistant write of a plain OpenTherm frame row is refused
   (`OT_CMD_NOT_FOR_HA`) in both modes: Home Assistant drives the executor, never the bus.
4. For an executor command (`ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`,
   `heating_season`) the final answer is `ot_thermostat_control_apply()`: under the apply mutex
   and the spinlock, `ot_control_apply()` re-checks ownership against the mode the executor last
   observed and lands the command; an accepted HA `ch_enable` or `ch_setpoint` feeds the watchdog.
   What must be stored is written outside every lock through `ot_net_config_apply()`.
5. On the next tick (within a second), `ot_control_step()` computes the CH bit and the held ID 1.
   `ot_thermostat` hands a changed status byte to `ot_bus_set_status()` and offers ID 1, then any
   owed ID 56, through `ot_bus_write_if_idle()` — which never evicts a hand write already queued.
6. `ot_bus` puts them on the wire in its next slots. The CH bit does not rise until the bus has
   reported the held ID 1 as answered.

A REST write (`POST /api/entities/<key>`) takes the same path with origin `WEB`, answered `202`
once accepted; a plain frame row from the web goes to `ot_bus_write()` instead, which replaces an
unexecuted queued write. Details: [Commands](/protocol/commands), [REST API](/interfaces/rest-api).

### Frame → state → JSON

1. The `ot_master` interrupt samples the line every 100 µs into `ot_decode`, which rebuilds the
   34 bits; `ot_frame_decode()` checks parity and yields type, Data-ID and value.
2. `ot_bus` records **every** parsed reply, UNKNOWN-DATAID included, in `ot_observe` (the raw
   128-ID table behind `GET /api/ot/raw`), then calls `on_ot_response()` in `src/main.cpp`.
3. The callback calls `ot_state_apply_dataid()`, which decodes the value through the entity codec
   for every registry entity reading that Data-ID, updates availability, and sets per-consumer
   change marks only on a real change. It also calls `ot_thermostat_heard()`, which keeps the
   ID 56 READ-ACK and the first DATA-INVALID answer to ID 1 under the executor's spinlock.
4. A Data-ID the state model now marks unsupported is dropped from the poll ring.
5. The consumers render through `ot_api`'s one value emitter: `GET /api/state` on request; the
   `/ws` push once a second (a full `state` frame on connect, then one `delta` frame per tick with
   whatever changed, a `ping` after 10 s of silence); and the MQTT publishing pass, entity by
   entity, retained.

### Room temperature → steer

1. The `ds18b20` task reads the shield sensor over RMT (`ot_onewire`, CRC and decode in
   `ot_onewire_decode`) and calls `ot_thermostat_room_submit(0, celsius)`. A reading on
   `<prefix>/room/state` is classified by `ot_mqtt_decide()` as a room reading (a retained one is
   refused) and submitted to slot 1 — as a **measurement, never a command**: it does not pass
   `ot_command`, cannot feed the watchdog, and cannot keep a dead controller looking alive.
2. Both land in a mailbox under its own spinlock; any task may submit.
3. On each tick, on the thermostat task alone, the mailbox is drained into `ot_room`; each slot's
   `ot_sensor` filters outliers and tracks `FRESH`/`STALE`/`NEVER`.
4. `ot_room_select_steer()` picks the first slot that is **both `room`-role and fresh**. Slot 0, the
   shield, is always `ambient` and so never steers; slot 1, the MQTT source, exists only when
   enabled and takes its role from configuration. A `room`-role, HA-forwarded slot that is `STALE`
   raises the "HA is blind" flag.
5. The steer pick fills the executor's input (`room_fresh`, `room_dc`, `ha_forwarded_stale`); only
   the failsafe reads it. A separate display pick (room-role preferred, ambient as fallback) feeds
   `room_temperature_effective` and `room_source`. Details: [Room sources](/device/room-sources).

### Failsafe when Home Assistant goes silent

1. While the executor has seen HA mode, the watchdog accumulator grows by each step's interval
   (saturating). Only an accepted HA `ch_enable` or `ch_setpoint` resets it — not DHW, not the
   season, not a retained message, not Home Assistant's birth message.
2. When it reaches `watchdog_s` (default 900 s), the ladder resolves to `failsafe`.
3. The failsafe decides the CH bit in order: disarmed if Home Assistant has not asked for heat
   within `failsafe_heat_days` of powered hours; otherwise **blind** at `failsafe_setpoint` if no
   steer source is fresh; otherwise ±0.3 K hysteresis around `failsafe_room_target` with
   `failsafe_min_cycle_s` as the minimum on and off time.
4. Throughout, the bus keeps talking, the CH bit still waits for the held ID 1, and a reboot
   mid-failsafe restores the watchdog and heat-hour counts from `RTC_NOINIT` and NVS, so a reboot
   loop cannot reset its way out.
5. Within HA mode with the season on, the failsafe is left only by an accepted HA `ch_enable`
   with Home Assistant no longer blind. The season going off or a switch to LOCAL also leaves it;
   coming back from either is an entry into ownership that clears the latch and starts in
   `ha_waiting`.

Details: [Executor](/architecture/executor#the-bounded-failsafe),
[Failure model](/architecture/failure-model).

## Timing constants

| Constant | Value | Where |
| --- | --- | --- |
| Bus pace / minimum gap / deadline | 950 ms / 100 ms / 1150 ms | `ot_bus_sched.h` |
| Receive sample / transmit half-bit | 100 µs / 500 µs | `ot_master.c` |
| Executor tick | 1 s | `ot_thermostat.c` |
| Held ID 1 re-send once confirmed | 10 s | `OT_CONTROL_RESEND_MS` |
| ID 56 retry / tries per target | 60 s / 3 | `ot_control.c` |
| Watchdog | 900 s default, 60–7200 s | `ot_config` |
| Boost | at most 480 min | `OT_CONTROL_BOOST_MAX_MINUTES` |
| Boiler declared not answering | 5 failed conversations in a row | `ot_bus.c` |
| Line test | at most 30 s | `OT_BUS_TEST_MAX_MS` |
| MQTT pass / busy pass / keepalive / reconnect | 1 s / 100 ms / 60 s / 10 s | `ot_mqtt_link` |
| `/ws` push tick / ping after silence | 1 s / 10 s | `ot_http_ws.c`, `ot_http_ws_push.c` |
| `/ws` ticket lifetime | 30 s | `OT_TICKET_TTL_MS` |
| Provisioning tick | 250 ms | `ot_net_prov.c` |
| DS18B20 read | every 3 s; the shield slot is stale after 60 s | `src/main.cpp`, `ot_thermostat_room_cfg.c` |
| LED animation / sample | 50 ms / 1 s | `ot_led_task.c` |
