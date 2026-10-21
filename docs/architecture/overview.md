<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# Architecture overview

The firmware is an ESP-IDF application (C, with a C++ entry point, built with PlatformIO) that
makes an ESP32 the only OpenTherm master on a gas boiler's bus. It is an **executor**, not a
controller: Home Assistant decides when to heat and how hot, and the firmware carries the decision
out, guards it, and keeps the boiler safe when Home Assistant, the broker or the network
disappears. This page is the big picture; the rest of this section goes one level deeper, and the
Protocol, Interfaces and Device sections carry the details.

## The executor model

The control law does not live on the device. The work is split three ways:

| Who | Owns |
| --- | --- |
| **Home Assistant** | The control law, schedules, scenarios, the owner's own sensors — every "when to heat and how hot" decision. |
| **The firmware** | The bus (sole master, never silent), the commanded state, value bounds, the watchdog, the bounded failsafe, and its own view of the room (the [room-source registry](/device/room-sources)). |
| **The web interface** | Diagnostics and minimal control: the control card and the boost in LOCAL mode. It has **no privileged API handle** — everything it does is an ordinary REST call that `curl` can make too. |

The firmware is the sole OpenTherm **master**. It is not an OpenTherm Gateway (OTGW): it does not
sit between an existing thermostat and the boiler, it relays nobody's frames, and the adapter
shield has no slave side. It is also the **holder of the commanded state**: Home Assistant's last
accepted command, the LOCAL switch and setpoint, the season and the DHW settings all live in the
firmware, and what the boiler is told once a second is computed from them by one pure function
([Executor](/architecture/executor)).

What the firmware deliberately does **not** do: no PI/PID loop, no anti-windup, no weekly schedule,
no heating modes, no anti-cycling logic of its own, no room-temperature control loop outside the
failsafe. See [What is delegated](#what-is-delegated-to-home-assistant) below.

### Two modes

At any moment the configuration field `control_mode` is one of two values:

- **LOCAL** — the default on a fresh flash. The device is driven from its own web interface over
  REST: a stored CH enable, a stored flow setpoint, and an optional boost. Home Assistant's
  commands are refused in this mode.
- **HA** — Home Assistant is the controller. The firmware carries out its commands (within bounds
  and ownership), runs the watchdog, and enters the failsafe if Home Assistant falls silent. The
  configuration store refuses HA mode while no MQTT broker host is configured.

A fresh device asks for nothing: `heating_season` and `local_ch_enable` both default to `false`.

## One entity list, four projections

Everything that reaches the outside is generated from one Python table, `tools/opentherm_ids.py`,
by `tools/generate_registry.py`. This build lists **{{ data.total }}** entities
({{ data.writable }} writable): **{{ data.total - data.synthetic.length }}** carried by
**{{ data.byDataId.length }}** OpenTherm Data-IDs, and **{{ data.synthetic.length }}** synthetic —
the executor's inputs and outputs, which never appear on the wire.

| Output | Generated file | Used by |
| --- | --- | --- |
| The C registry | `components/ot_registry/include/registry_generated.h` | `ot_state`, `ot_api` (REST and `/ws`), `ot_command`, the MQTT topics, the poll ring in `src/main.cpp` |
| Home Assistant discovery templates | rendered by `tools/render_discovery.py`, filled at runtime by `ot_ha` | MQTT discovery |
| The frontend types | `web/src/api/entities.ts` | the SPA, and the [Entities](/reference/entities) page of this site |
| The web-only translated names | from `tools/entity_names_i18n.py` | the SPA's DE/NL/UK overlay (Home Assistant stays English) |

The generated files are outputs: a change is made in the table or the generator, never in the
file. There is no second hand-written list anywhere — not even the poll ring, which `app_main()`
reads from the registry. Details: [Entity registry](/development/entity-registry).

## Transports

| Transport | Direction | Where it lives | Covered in |
| --- | --- | --- | --- |
| HTTP REST (`/api/*`) | read and write | `components/ot_http/` | [REST API](/interfaces/rest-api) |
| WebSocket (`/ws`) | push only, device to browser | `components/ot_http/ot_http_ws.c`, `ot_http_ws_push.c` | [WebSocket](/interfaces/websocket) |
| MQTT with Home Assistant discovery | read and write | `components/ot_mqtt/`, `components/ot_mqtt_link/`, `components/ot_ha/` | [MQTT](/interfaces/mqtt), [Home Assistant discovery](/interfaces/home-assistant-discovery) |
| The web UI | a client of REST and `/ws` | `web/`, embedded by `components/web_assets/` | [Web UI](/interfaces/web-ui) |

None of the transports keeps its own copy of an entity value, and none decodes an OpenTherm
frame: they read `ot_state`, and REST, `/ws` and the MQTT state payloads print every value through
the one emitter in `ot_api`, so the three can never disagree. None of them decides whether a write
is allowed either: REST calls `ot_command_check()` with origin `WEB`, MQTT with origin `HA`, and
that one function answers every refusal
([Executor](/architecture/executor#the-single-refusal-point)). The WebSocket carries exactly what
`GET /api/state` carries and not one field more.

## Data flow

```text
                     gas boiler (OpenTherm slave)
                        ^  a conversation ~ every 950 ms  |  replies
                        |                                 v
  ot_master   [gptimer ISR, 100 us]  ot_encode -> line -> ot_decode -> ot_frame
                        ^                                 |
  ot_bus      [ot_bus task, prio 10]   ot_bus_sched (what to ask)   ot_bus_track (writes answered)
              status byte + one-slot write queue          |  on_ot_response() in src/main.cpp
                        ^                                 v
                        |                   ot_state (decoded, keyed)    ot_thermostat_heard()
                        |                                 |              (ID 56 readback)
  executor    [ot_thermostat task, 1 s]                   |
              ot_control_step() <- ot_room (steer) <- room mailbox <- DS18B20 task / MQTT room topic
              status_high, held ID 1, owed ID 56          |
                        ^                                 v
  commands    ot_command_check(origin)              ot_api (one value emitter)
                ^                  ^                      |                 |
   POST /api/entities/<key>   MQTT <prefix>/<key>/set   GET /api/state, /ws   MQTT <prefix>/<key>/state
        (httpd, WEB)          (ot_mqtt task, HA)        (httpd)              (ot_mqtt task)
```

The [Runtime](/architecture/runtime) page walks the four end-to-end flows: a Home Assistant
command reaching the boiler, a reply reaching JSON, a room reading reaching the failsafe, and the
failsafe itself.

## The safety spine

Three invariants stand above every other rule, and a fourth governs every peer failure:

1. **The bus never goes silent.** An OpenTherm slave that hears no correct master frame for about
   five seconds reads it as a shorted thermostat and **demands heat**. Silence is not a safe
   state; it is the hottest one. No peripheral failure — Wi-Fi, broker, a stale sensor, a dead
   Home Assistant — may stop the bus task, which has no exit. See [Bus](/protocol/bus).
2. **The CH bit never rises before the held ID 1 has gone out.** A boiler asked for heat with no
   chosen flow setpoint (`TSet`) heats to whatever it last had. The executor holds a valid
   setpoint from boot and gates the CH bit on the bus having carried it
   ([Executor](/architecture/executor#the-held-setpoint-and-the-ch-invariant)).
3. **Timers survive the 49.7-day wrap, and a restore errs safe.** Every duration a safety decision
   rests on — the watchdog, the failsafe, the setpoint confirmation, a room source's staleness — is
   a saturating accumulator, so a peer dead for longer than the wrap stays dead; the shorter
   intervals elsewhere are wrap-safe unsigned differences. The `RTC_NOINIT` record that carries the watchdog count and the
   part-hour across a reset is checksummed and believed only after a reboot-loop reset; the whole
   heat hours come from NVS on every boot, and a missing value restores to the disarmed end.
4. **Nothing reboots because a peer is absent.** There is no "the broker has been unreachable for
   N minutes — restart"; there is, in fact, no `esp_restart()` call in the firmware at all.

Where each one is enforced, and what the device does for every failure, is on
[Failure model](/architecture/failure-model).

## Pure decisions, thin device halves

Almost every component that decides something is pure C with no ESP-IDF header: time enters as a
`now_ms` argument and effects are injected as function pointers, so it builds in the host test
environment, and the host build compiles **exactly the sources the device runs**. The device half
beside it owns the task, the lock, the wire, NVS or the radio and only carries the verdict out.

| Decides (pure, host-tested) | Carries out (device) |
| --- | --- |
| `ot_control`, `ot_control_io`, `ot_command`, `ot_room`, `ot_sensor` | `ot_thermostat` |
| `ot_bus_sched`, `ot_bus_track`, `ot_frame`, `ot_encode`, `ot_decode` | `ot_bus`, `ot_master` |
| `ot_mqtt`, `ot_ha` | `ot_mqtt_link` |
| `ot_policy`, `ot_auth`, `ot_secrets`, `ot_ticket`, `ot_wire`, `ot_json`, `ot_api`, `ot_captive` (the probe table) | `ot_http` |
| `ot_provision` | `ot_net` |
| `ot_captive` (the DNS reply) | `ot_captive`'s own UDP 53 task, which `ot_net` starts and stops with the access point |
| `ot_led` | `ot_led_task` |
| `ot_onewire_decode` | `ot_onewire` |

Glue with no host suite is pinned by source guards in `tools/tests/` instead. See
[Components](/architecture/components) and [Testing](/development/testing).

## The rules the design is held to

| Rule | How it is held |
| --- | --- |
| The host suites, both board builds, `npm run build` and the generator's pytest are green | the gates in [Testing](/development/testing) — output pasted, not asserted |
| A build for an unknown target fails with a clear message | `components/board/CMakeLists.txt` stops with `FATAL_ERROR`, not a link failure |
| No Arduino, directly or transitively | `platformio.ini` sets `framework = espidf` in every board environment |
| No GPIO number outside `components/board/` | every pin is read from `board_get()` ([Board](/device/board)) |
| No second entity list | the registry header and `entities.ts` are generated |
| No API projection returns a stored password | [Security](/architecture/security#write-only-secrets) |
| The web interface has no privileged handle | every SPA call is a public route `curl` can make too, under the same access policy; `/ws` opens with a ticket from the ordinary `POST /api/ws-ticket` ([Security](/architecture/security#the-two-routes-outside-the-gate)) |
| No code path reboots because a peer is unreachable | [Failure model](/architecture/failure-model) |
| `ot_bus` has no path on which the conversation stops for good | the task loop has no exit ([Bus](/protocol/bus)) |
| Every boundary between adjacent ladder rows has a host test, and the CH invariant is a named test | `test/test_ot_control*`; the invariant is `test_ch_never_rises_before_the_held_setpoint_is_confirmed` in `test_ot_control_bus` |
| Home Assistant discovers every entity without hand-written YAML; in LOCAL mode its control entities are unavailable | [Home Assistant discovery](/interfaces/home-assistant-discovery) |
| The behaviour holds on a real boiler | the owner's [hardware verification](/hardware-verification/) scenarios |

## What is delegated to Home Assistant

Deliberately not in the firmware, listed so the omission is not mistaken for an oversight:

- **The control law.** No PI/PID loop; Home Assistant computes the flow setpoint.
- **Schedules and modes.** No weekly schedule, no "eco/comfort/away" modes.
- **Anti-cycling.** The boiler's own anti-cycling (a panel parameter) and Home Assistant's control
  handle it; the only minimum cycle in the firmware is the failsafe's.
- **Room temperature as a control input.** The room-source registry exists for the failsafe's eyes
  and for diagnostics; outside the failsafe the firmware steers by nothing.

Out of scope: a weather-compensation curve, a second heating circuit (CH2), the solar and
ventilation Data-ID blocks, TLS for MQTT (the TLS transport is compiled out in
`sdkconfig.defaults`), and OTGW mode.

The room-source registry has two adapters: the shield's DS18B20 and one MQTT topic
([Room sources](/device/room-sources)). There is no firmware upload over HTTP; a new image is
flashed over USB ([OTA and system](/device/ota-and-system)).

## No Arduino

The firmware is ESP-IDF only: `platformio.ini` sets `framework = espidf` and pins
`pioarduino/platform-espressif32#55.03.311`, which carries ESP-IDF 5.5.5. No environment, core or
library brings Arduino in. The reasons are practical: the design uses IDF facilities directly (a
`gptimer` ISR for the Manchester line, RMT for 1-Wire, `esp_http_server` with a WebSocket
pre-handshake callback, esp-mqtt, NVS namespaces, `RTC_NOINIT` memory), and host testing needs the
protocol and decision layers free of any framework.

The platform pin is deliberate: a floating platform URL once moved the toolchain under a verified
design, so it is moved only on purpose, and said so.
