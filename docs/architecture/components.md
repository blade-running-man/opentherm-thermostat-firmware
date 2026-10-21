# Components

The firmware is 40 ESP-IDF components under `components/`, plus the entry point in `src/main.cpp`.
One component is one responsibility, and a component is a directory. This page shows how they
layer, the dependency graph exactly as the `CMakeLists.txt` files declare it, and an index of
every component with its role, its host suite and a link to its own `README.md` — the
per-component contract (Purpose · Responsibility · Public API · Implementation · Tests · Notes).

## The component model

The structural pattern throughout is a **pure, host-testable core** paired with a **thin, impure
shell**:

- **Pure** components include no ESP-IDF or FreeRTOS header: no `esp_timer`, no NVS, no GPIO, no
  lock. Time enters as a `now_ms` argument and effects arrive as function pointers. They are
  ESP-IDF components and PlatformIO libraries at once, so the host suites compile exactly the
  sources the device runs.
- **Impure** components own the tasks, the locks, the wire, NVS and the radio. They are kept thin
  on purpose: they carry out verdicts a pure sibling computed and a host suite pinned.

The rule that keeps the split honest is mechanical. PlatformIO compiles **every** source of a
library whose header a suite includes, so one `#include "esp_timer.h"` in a pure component stops
every suite that includes its header from linking. That is why the pure components most exposed
to it — `ot_control`, `ot_control_io`, `ot_sensor`, `ot_room`, `ot_led`, `ot_onewire_decode`,
`ot_mqtt` and `ot_ha` — say in their `CMakeLists.txt` which requirement (the clock, NVS, the bus, a
driver, the MQTT client) must never be added.

Where a pure decision and its device code share one directory (`ot_auth`, `ot_captive`,
`ot_config`, `ot_log`), the device files are wrapped in `#ifdef ESP_PLATFORM`, so the host build
compiles them to nothing; `ot_lock` compiles to a no-op lock the same way. `ot_thermostat` goes the
other way: its `library.json` narrows the host build to the one pure file,
`ot_thermostat_room_cfg.c`. These are the components marked **split** below.

## Layers

Grouped by role, from the wire up. The grouping is for reading, not a strict stack: a few edges
run upward across it (`ot_bus` requires `ot_state`; `ot_control_io` and `ot_thermostat` require
`ot_config`; `ot_thermostat` privately requires `ot_net` and `ot_time`). The graph itself, below,
is what is acyclic.

| Layer | Components |
| --- | --- |
| **Hardware** | `board`, `ot_onewire`, `ot_onewire_decode` |
| **OpenTherm wire** | `ot_frame`, `ot_encode`, `ot_decode`, `ot_master`, `ot_bus_sched`, `ot_bus_track`, `ot_bus`, `ot_observe` |
| **State and registry** | `ot_registry`, `ot_lock`, `ot_state`, `ot_sensor`, `ot_room` |
| **Executor** | `ot_control`, `ot_api`, `ot_command`, `ot_control_io`, `ot_thermostat` |
| **Configuration** | `ot_secrets`, `ot_config`, `ot_json`, `ot_provision`, `ot_wire` |
| **Network and Home Assistant** | `ot_captive`, `ot_net`, `ot_time`, `ot_mqtt`, `ot_ha`, `ot_mqtt_link` |
| **HTTP and web** | `ot_policy`, `ot_auth`, `ot_ticket`, `ot_log`, `web_assets`, `ot_http` |
| **Status LED** | `ot_led`, `ot_led_task` |

`ot_api` sits with the executor rather than with HTTP because the graph says so: it requires
`ot_control` (for the state and reason names it prints), and `ot_control_io`, `ot_thermostat` and
`ot_mqtt` require it in turn (for `ot_api_control_t` and the one value emitter).

## Dependency map

First-party `REQUIRES` and `PRIV_REQUIRES` only (ESP-IDF components such as `esp_timer`, `nvs_flash`
or `mqtt` are left out). Read `A → B` as "A requires B". The graph is acyclic; the leaves — `board`,
`ot_frame`, `ot_decode`, `ot_encode`, `ot_bus_sched`, `ot_registry`, `ot_lock`, `ot_sensor`,
`ot_json`, `ot_secrets`, `ot_provision`, `ot_policy`, `ot_ticket`, `ot_captive`, `ot_time`,
`ot_log`, `ot_onewire_decode`, `web_assets` — require no other component of this firmware.

```text
ot_master       → board, ot_frame, ot_decode, ot_encode
ot_observe      → ot_frame
ot_bus_track    → ot_bus_sched
ot_bus          → ot_bus_sched, ot_bus_track, ot_master, ot_frame, ot_observe, ot_state
ot_state        → ot_registry, ot_frame, ot_lock
ot_room         → ot_sensor
ot_onewire      → ot_onewire_decode
ot_control      → ot_bus_sched                      (the OT_STATUS_* bit names only)
ot_api          → ot_state, ot_registry, ot_lock, ot_control
ot_command      → ot_registry, ot_state, ot_frame, ot_control
ot_config       → ot_secrets
ot_auth         → ot_config
ot_wire         → ot_json, ot_config, ot_provision, ot_secrets
ot_control_io   → ot_control, ot_config, ot_api, ot_frame
ot_led          → ot_control
ot_ha           → ot_state, ot_registry; priv ot_json
ot_mqtt         → ot_command, ot_control, ot_registry; priv ot_api
ot_net          → board, ot_config, ot_provision, ot_captive, ot_wire
ot_thermostat   → ot_control, ot_config, ot_api, ot_frame;
                  priv ot_control_io, ot_bus, ot_bus_sched, ot_net, ot_state, ot_time, ot_room
ot_mqtt_link    → ot_wire; priv ot_mqtt, ot_ha, ot_net, ot_thermostat, ot_bus, ot_state,
                  ot_registry, ot_config
ot_led_task     → ot_led, ot_net, ot_mqtt_link, ot_control, ot_state, ot_thermostat, board
ot_http         → ot_wire, ot_bus, ot_observe, ot_policy, ot_log, ot_auth, ot_captive, ot_config,
                  ot_net, web_assets, ot_api, ot_command, ot_ticket, ot_state, ot_registry,
                  ot_thermostat, ot_control, ot_control_io, ot_mqtt_link
src (main)      → board, web_assets, ot_net, ot_http, ot_log, ot_master, ot_bus, ot_frame,
                  ot_mqtt_link, ot_onewire, ot_state
```

`ot_thermostat` is the top of the executor: the task that pulls the executor core, the bus, the
configuration store, the room registry and the state model together once a second. `ot_http` is
the widest fan-in, because it is the one web surface over everything.

### Decisions that shaped the graph

- **`ot_mqtt_link` is its own component, not part of `ot_net`.** The broker lifecycle has to call
  `ot_thermostat` (to apply commands and read the executor snapshot), and `ot_thermostat` already
  requires `ot_net`. Inside `ot_net`, that is a cycle that will not link; as a separate component
  it only reads `ot_net`'s public header (`ot_net_broker()`).
- **`ot_bus` depends on `ot_state` in one direction only** — for `ot_state_is_unsupported()`, to
  drop a Data-ID the boiler does not know from the poll ring. `ot_state` must never depend on the
  bus; the reply reaches it through the callback `src/main.cpp` installs.
- **`ot_wire` does not require `ot_command`.** While it did, a suite of text parsing linked the
  whole boiler model and failed on an unresolved `ot_lock`; the parsed-value types therefore live
  in `ot_wire.h`, and `ot_http` glues the two.
- **`ot_auth`, `ot_wire` and `ot_captive` never see `esp_http_server`.** The moment any of them
  could send a response, the access policy in `ot_policy` would have a second door.
- **`ot_led` owns its own `ot_led_net_state_t`** instead of requiring the impure `ot_net` for
  `ot_net_state_t`; `ot_led_task` converts one to the other with an explicit `switch`, so an
  unknown network state fails toward showing a problem.
- **`ot_led_task` is a fan-in leaf.** It reads four components from three layers, but nothing
  depends on it, so it adds only edges that terminate. It stays safe by being reader-only: every
  call it makes is a non-blocking snapshot getter, never one that dials, waits or touches the bus
  lock.
- **`ot_control` copies a four-line saturating add from `ot_sensor` rather than requiring it.**
  Requiring it would put the sensor's state type and outlier filter into every suite that includes
  `ot_control.h`, `ot_command`'s among them.

## Component index

**Pure** means no ESP-IDF header; such a component builds on the host. The suite column names the
host suite directory under `test/`, or the source guard in `tools/tests/` that pins glue no suite
compiles.

### OpenTherm wire

Covered in [Frame](/protocol/frame), [Physical layer](/protocol/physical-layer) and
[Bus](/protocol/bus).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_frame`](../../components/ot_frame/README.md) | The 32-bit frame layout (even parity) and the DATA-VALUE codecs: f8.8, signed and unsigned bytes, flags. | yes | `test_ot_frame` |
| [`ot_encode`](../../components/ot_encode/README.md) | Renders a frame into the 68 Manchester half-bits to transmit. | yes | `test_ot_encode` |
| [`ot_decode`](../../components/ot_decode/README.md) | The receive Manchester state machine over a 100 µs sampler. | yes | `test_ot_decode` |
| [`ot_master`](../../components/ot_master/README.md) | One conversation with correct timing: the two GPIOs, their inversion, the one hardware timer, the TX/RX state machine. One caller only. | no | — (hardware) |
| [`ot_bus_sched`](../../components/ot_bus_sched/README.md) | What to ask and when: the 950 ms pace and the 1150 ms deadline, ID 0 on every other slot, the one-slot write queue, the Data-ID sweep. | yes | `test_ot_bus_sched` |
| [`ot_bus_track`](../../components/ot_bus_track/README.md) | The write bookkeeping beside the scheduler: the write generation and the count and value of answered ID 1 writes. | yes | `test_ot_bus_track` |
| [`ot_bus`](../../components/ot_bus/README.md) | The FreeRTOS task that owns the wire and never stops; the status byte, the write queue, the introduction, the line test. | no | `test_source_guards.py` |
| [`ot_observe`](../../components/ot_observe/README.md) | The raw diagnostic table over all 128 Data-IDs and its JSON (`GET /api/ot/raw`). Never a source of entities. | yes | `test_ot_observe` |

### State and registry

Covered in [Entity registry](/development/entity-registry) and [Room sources](/device/room-sources).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_registry`](../../components/ot_registry/README.md) | Read-only lookup over the generated `registry_generated.h`: by key, by index, by Data-ID; the poll ring. | yes | `test_registry` |
| [`ot_lock`](../../components/ot_lock/README.md) | The one recursive mutex over the state model; a no-op on the host. | split | — |
| [`ot_state`](../../components/ot_state/README.md) | The decoded, keyed model: value, availability, timestamps, per-consumer change marks, the unsupported verdict, the write seam for synthetic entities. | yes (under `ot_lock`) | `test_state` |
| [`ot_sensor`](../../components/ot_sensor/README.md) | One room-temperature source: outlier filter and `FRESH`/`STALE`/`NEVER`, the overdue time an accumulator. | yes | `test_ot_sensor` |
| [`ot_room`](../../components/ot_room/README.md) | The registry of four room-source slots with two picks: steer (a fresh `room`-role slot only) and display. | yes | `test_ot_room` |

### Executor

Covered in [Executor](/architecture/executor) and [Commands](/protocol/commands).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_control`](../../components/ot_control/README.md) | The executor core: the six-state ladder, the watchdog, the bounded failsafe, the boost, the held ID 1, the ID 56 reconciliation. | yes | `test_ot_control`, `_check`, `_failsafe`, `_boost`, `_bus` |
| [`ot_control_io`](../../components/ot_control_io/README.md) | The translations around the core: config snapshot, persist patch, bus confirmation, ID 56 readback and owed write, `RTC_NOINIT` record and restore, synthetic entities, `GET /api/control`. | yes | `test_ot_control_io`, `_send`, `_state` |
| [`ot_command`](../../components/ot_command/README.md) | Key + number + origin → a frame or an executor command; every refusal of every writer is decided here. | yes | `test_ot_command`, `test_ot_command_check` |
| [`ot_api`](../../components/ot_api/README.md) | The one JSON projection of registry, state and the control document; the single value emitter REST, `/ws` and MQTT share. | yes | `test_api` |
| [`ot_thermostat`](../../components/ot_thermostat/README.md) | The task that carries the executor out, once a second: the spinlock, the bus, NVS, `RTC_NOINIT`; the one writer of the ID 0 high byte. | split | `test_source_guards.py`; `test_ot_thermostat_room` |

### Configuration

Covered in [Configuration storage](/device/config-storage) and [Security](/architecture/security).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_secrets`](../../components/ot_secrets/README.md) | Write-only secrets: the `__UNCHANGED__` sentinel, keep/clear/store/reject, constant-time comparison. | yes | `test_secrets` |
| [`ot_config`](../../components/ot_config/README.md) | The configuration document: field table, validators, all-or-nothing patch, sanitising what comes back from flash, the PBKDF2 password record, NVS. | split | `test_config_*` (8 suites) |
| [`ot_json`](../../components/ot_json/README.md) | A narrow, hostile-input-safe reader for flat JSON bodies and the single string escaper. | yes | `test_json` |
| [`ot_provision`](../../components/ot_provision/README.md) | The Wi-Fi provisioning state machine: setup window, trial and rollback, failure classes, the fallback access point. | yes | `test_provision` |
| [`ot_wire`](../../components/ot_wire/README.md) | Bytes ⇄ decision for `/api/config`, `/api/provision`, `/api/wifi/scan`, `/api/status` and the command bodies. | yes | `test_wire`, `_command`, `_provision`, `_status` |

### Network and Home Assistant

Covered in [Networking](/device/networking), [MQTT](/interfaces/mqtt) and
[Home Assistant discovery](/interfaces/home-assistant-discovery).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_captive`](../../components/ot_captive/README.md) | The setup access point's captive portal: the OS probe table, a pure DNS reply builder, the UDP 53 task. | split | `test_captive` |
| [`ot_net`](../../components/ot_net/README.md) | Owns the radio, the stored configuration document and its persistence; runs the provisioning task; the one un-redacted broker projection. | no | — (driven by `ot_provision`) |
| [`ot_time`](../../components/ot_time/README.md) | SNTP and the POSIX time zone; valid only once a sync happened. Nothing on the bus path waits for it. | no | — |
| [`ot_mqtt`](../../components/ot_mqtt/README.md) | What an MQTT message means: the topic tree, the command's one entry point (origin `HA`), the retained-command refusal, the state payloads. | yes | `test_ot_mqtt`, `_out`, `_control`, `_seam` |
| [`ot_ha`](../../components/ot_ha/README.md) | The Home Assistant discovery documents and when each is owed (WAIT / WANT / DROP). | yes | `test_ot_ha`, `test_ot_ha_plan` |
| [`ot_mqtt_link`](../../components/ot_mqtt_link/README.md) | The esp-mqtt glue: one task owns the client, follows the broker settings by fingerprint, drains the inbox, publishes. | no | `test_source_guards_mqtt.py` |

### HTTP and web

Covered in [REST API](/interfaces/rest-api), [WebSocket](/interfaces/websocket) and
[Web UI](/interfaces/web-ui).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`ot_policy`](../../components/ot_policy/README.md) | Who may do what: the access decision, the bus-halting-operation rule, the Host and Content-Type gates. | yes | `test_http_policy` |
| [`ot_auth`](../../components/ot_auth/README.md) | An HTTP Basic `Authorization` header to a yes or no, with an injected verifier and a 1 s failure delay. | split | `test_auth` |
| [`ot_ticket`](../../components/ot_ticket/README.md) | One-shot, 30 s, 128-bit tickets for the `/ws` handshake. | yes | `test_ticket` |
| [`ot_log`](../../components/ot_log/README.md) | The in-RAM ring of recent log lines behind `GET /api/log`; escapes every non-ASCII byte. | split | `test_log` |
| [`web_assets`](../../components/web_assets/README.md) | The SPA, built at CMake configure time and embedded gzipped with `EMBED_FILES`. | — | web suites under `web/` |
| [`ot_http`](../../components/ot_http/README.md) | The one web surface: the access gate in front of every data and write route, `/ws` (admitted by its ticket) and its push, the SPA, the captive probes (constant answers, outside the gate). | no | `test_source_guards.py` (the gate's `Host` read), `test_source_guards_http.py` (the line test's parameters) |

### Hardware and status LED

Covered in [Board](/device/board) and [Indication](/device/indication).

| Component | Role | Pure | Suite |
| --- | --- | --- | --- |
| [`board`](../../components/board/README.md) | The board descriptor (`board_get()`), selected by `IDF_TARGET`: the only place GPIO numbers are named. | — | — (hardware) |
| [`ot_onewire`](../../components/ot_onewire/README.md) | RMT-backed 1-Wire transport for the shield's DS18B20; never fights the OpenTherm timer for the CPU. | no | — |
| [`ot_onewire_decode`](../../components/ot_onewire_decode/README.md) | The DS18B20 CRC-8 and temperature decode. | yes | `test_ot_onewire` |
| [`ot_led`](../../components/ot_led/README.md) | The status LED's health ladder (colour) and animation curves (motion). | yes | `test_ot_led` |
| [`ot_led_task`](../../components/ot_led_task/README.md) | Samples the network, MQTT, the executor and the flame once a second and drives the WS2812 pixel. Reader-only. | no | `test_source_guards_led.py` |

::: info The READMEs are documentation, not the code
Each README is the per-component contract, but line counts, constants and suite names drift.
Before relying on a detail, read the source beside it.
:::
