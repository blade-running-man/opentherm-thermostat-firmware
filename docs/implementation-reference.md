# OpenTherm Thermostat Firmware — Implementation Reference

A single consolidated map of the firmware: what it is, how the components fit
together, the safety rules that dominate the design, the end-to-end flows, and a
short per-component reference. It is built from the per-component `README.md`
files, each of which holds the full contract for its component; this document
summarises and links, it does not repeat them.

---

## 1. Overview

This is the firmware for a **room thermostat of a gas boiler with an OpenTherm
interface**. It is written in C for **ESP-IDF** and built with **PlatformIO**,
targeting two boards — the **LOLIN C3 mini (ESP32-C3)** with the DIYLESS
Thermostat Shield, and the **ESP32-C6 SuperMini**. There is **one universal
binary**: every board and behavioural difference is configured at runtime (or,
for pins, chosen from a board descriptor at build time), never by a special
build.

The firmware follows a **controller / executor split**:

- **Home Assistant is the controller.** It decides *what* the house should be
  heated to and owns the schedule and the room logic.
- **The firmware is the executor.** It is the **sole OpenTherm master** on the
  wire, it **holds the commanded state**, and it is the last line of safety:
  it guards against nonsense values, and it guards against the controller (or
  the network, or a sensor) disappearing. When Home Assistant falls silent, a
  **watchdog** and a **bounded failsafe** keep the house safe without letting
  the boiler run away.

A local web UI (a Preact/Vite SPA served from flash) and a plain REST/WebSocket
API cover the same surface as MQTT, so the device is fully usable in **LOCAL
mode** with no Home Assistant at all.

Everything that reaches the outside — REST, the MQTT topics, Home Assistant
discovery, the frontend types — is **generated from one entity list**
(`tools/opentherm_ids.py`); see [§7](#7-entities-are-generated-from-one-source).

---

## 2. Architecture

### The component model

The tree is **one directory per responsibility**, and a component is a
directory. The load-bearing structural pattern throughout is a **pure,
host-testable core** paired with a **thin, impure glue** shell:

- The **pure** components take no `esp_timer`, no FreeRTOS, no NVS, no GPIO —
  time enters only as a `now_ms` argument, effects are injected as function
  pointers. This is what lets the protocol, the executor ladder, the room
  filter, the command legality, the JSON, and the config rules all be tested on
  a workstation, with the host build compiling **exactly the sources the device
  runs**. (PlatformIO compiles every source of a library whose header a suite
  includes; that is *why* an impure `#include` in a pure component would break
  the host build, and why the "no ESP-IDF header here" rule recurs in almost
  every pure component's README.)
- The **impure** components own the tasks, locks, the wire, NVS and the radio.
  They are kept deliberately thin: they carry out verdicts the pure siblings
  computed and host-tested.

### The layers

```
                 ┌───────────────────────────────────────────────────────┐
   HTTP / WEB    │ ot_http  ot_api  ot_json  ot_ticket  ot_policy  ot_auth│
                 │ ot_secrets  ot_captive  ot_provision  ot_wire          │
                 │ web_assets  ot_log  ot_lock                            │
                 └───────────────────────────────────────────────────────┘
                              │                         │
   NETWORK / HA   ┌───────────────────────┐   ┌───────────────────────┐
                  │ ot_net  ot_time        │   │ ot_mqtt  ot_mqtt_link │
                  │                        │   │ ot_ha                 │
                  └───────────────────────┘   └───────────────────────┘
                              │                         │
   EXECUTOR       ┌───────────────────────────────────────────────────┐
                  │  ot_thermostat  (top glue: task, spinlock, NVS)    │
                  │  ot_control  ot_control_io  ot_command             │
                  └───────────────────────────────────────────────────┘
                              │                         │
   STATE/REGISTRY ┌───────────────────────────────────────────────────┐
                  │ ot_registry  ot_state  ot_observe  ot_sensor ot_room│
                  └───────────────────────────────────────────────────┘
                              │
   OT WIRE LAYER  ┌───────────────────────────────────────────────────┐
                  │ ot_bus (task) → ot_bus_sched, ot_bus_track          │
                  │ ot_master → ot_encode, ot_decode → ot_frame         │
                  └───────────────────────────────────────────────────┘
                              │
   HARDWARE       ┌───────────────────────────────────────────────────┐
                  │ board   ot_onewire → ot_onewire_decode              │
                  └───────────────────────────────────────────────────┘
```

**OpenTherm wire layer** — `ot_frame` (the 32 data bits + value codecs),
`ot_encode` (transmit Manchester half-bits), `ot_decode` (receive Manchester
state machine), `ot_master` (one conversation with correct timing over the two
GPIOs and one hardware timer), `ot_bus` (the FreeRTOS task that owns the wire),
`ot_bus_sched` (the pure "what to ask and when" scheduler), `ot_bus_track` (the
pure write bookkeeping).

**State / registry** — `ot_registry` (lookup over the generated entity table),
`ot_state` (the decoded, keyed read-side model with availability), `ot_observe`
(the raw diagnostic view over all 128 Data-IDs), `ot_sensor` (one
room-temperature source with an outlier filter and a FRESH/STALE/NEVER machine),
`ot_room` (a registry of room-temperature slots with the steer/display picks).

**Executor** — `ot_control` (the pure ladder, watchdog, bounded failsafe, boost,
held ID 1), `ot_control_io` (the pure translations around it), `ot_command` (the
single decision point for the legality of any write), `ot_thermostat` (the
impure task that carries all of it out — the top glue).

**Networking / Home Assistant** — `ot_net` (radio, flash, clock — the impure
provisioning executor), `ot_mqtt` (pure: what a message means, the command entry
point, the state payloads), `ot_mqtt_link` (the impure esp-mqtt glue), `ot_ha`
(pure: the discovery documents and when each is owed), `ot_time` (SNTP + time
zone).

**HTTP / web** — `ot_http` (the server surface: REST, `/ws`, the SPA),
`ot_api` (the one JSON projection of registry + state), `ot_json` (the
hostile-input-safe reader and the single escaper), `ot_ticket` (one-shot `/ws`
handshake tickets), `ot_policy` (the pure access decision), `ot_auth` (HTTP
Basic parsing + verdict), `ot_secrets` (write-only secret handling),
`ot_captive` (the setup-AP captive portal), `ot_provision` (the pure Wi-Fi
provisioning state machine), `ot_wire` (bytes ⇄ decision for the config
surface), `web_assets` (the embedded SPA), `ot_log` (the in-RAM log ring),
`ot_lock` (the one recursive lock over the state model).

**Hardware / 1-Wire** — `board` (the only place GPIO numbers are named),
`ot_onewire` (RMT-backed 1-Wire transport for the shield's DS18B20),
`ot_onewire_decode` (the pure CRC + temperature decode).

**Status LED** — `ot_led` (pure: the health ladder and the animation curves that
turn a sampled `ot_led_world_t` into a colour + motion), `ot_led_task` (the
impure glue: one low-priority task that samples the network, MQTT and control
state once a second and drives the WS2812 over `led_strip`/RMT). Reader-only by
design — see the dependency-direction note below.

### Dependency direction

The graph is **acyclic**. Dependencies point downward: the HTTP and HA layers
depend on the executor, which depends on state/registry, which depends on the
wire layer, which depends on hardware. `ot_thermostat` is the **top glue** — the
FreeRTOS task that pulls the executor core, the bus, the config store, the room
registry and the state model together once a second.

Two cycle-avoidance decisions are worth naming because they shaped the tree:

- **`ot_mqtt_link` is its own component, not part of `ot_net`.** A broker
  lifecycle living inside `ot_net` would have to call `ot_thermostat` (to read
  the executor snapshot and apply commands), while `ot_thermostat` already
  requires `ot_net` — a dependency cycle that will not link. The glue is
  therefore a separate component that only *reads* `ot_net`'s public header.
- **`ot_bus` depends on `ot_state` in one direction only**
  (`ot_state_is_unsupported()`), because `ot_state` must never depend on the
  bus — that would be the same kind of link-breaking cycle.
- **`ot_led_task` is a fan-in leaf, not another layer.** It reads `ot_net`,
  `ot_mqtt_link`, `ot_thermostat` and `ot_state` — components from three
  different layers above hardware — but nothing depends back on it, so the
  acyclic graph is unaffected; it only adds edges that terminate. It is kept
  safe by being **reader-only**: every source it samples is a non-blocking
  snapshot getter (`ot_net_get_state`, `ot_mqtt_link_status`,
  `ot_thermostat_control_get`, `ot_state_get`), never a call that dials, waits
  or touches the bus lock — the LED must never be the reason the OpenTherm
  master falls silent ((a) above). The pure `ot_led` owns its own
  `ot_led_net_state_t` (deliberately not `ot_net_state_t`, which is impure and
  not host-buildable); the glue converts one to the other with an explicit
  `switch` in `ot_led_task.c` (never a cast), so an unknown/added net state
  fails toward showing a problem rather than silently reading as "connected".
  No host suite covers this glue; it is pinned by
  `tools/tests/test_source_guards_led.py`.

The pure-core/impure-shell split is itself a dependency rule: the pure siblings
(`ot_bus_sched`, `ot_bus_track`, `ot_control`, `ot_control_io`, `ot_command`,
`ot_mqtt`, `ot_ha`, `ot_provision`, `ot_wire`, `ot_sensor`, `ot_room`, …) never
`REQUIRES` anything that pulls in ESP-IDF, so the suite that includes their
header stays host-buildable.

---

## 3. Safety invariants

Three rules dominate everything else. They are load-bearing, and each is
enforced in specific components.

### (a) The bus never stops talking to the boiler

**An OpenTherm slave that has not heard a correct frame from the master for
roughly five seconds treats it as a short-circuited thermostat and goes into a
demand for full heat.** Falling silent is therefore not a safe state — it is the
**hottest** one possible.

- Enforced in **`ot_bus`**: the task loop has **no exit** — no `break`, no
  `return`, no stop condition — and the header forbids adding one. Neither a
  frame error, a missing reply, a Wi-Fi failure, a broker failure, a stale
  sensor nor a dead Home Assistant may break the loop. `ot_bus_set_status()`
  cannot fail and cannot stop the conversation; the line test and the
  introduction step leave their modes on elapsed time, never on a peer's
  answer.
  See [../components/ot_bus/README.md](../components/ot_bus/README.md).
- Reinforced in **`ot_bus_sched`** (an empty poll ring still sends ID 0, never a
  WAIT/skip), **`ot_time`** (no `sync_wait` on any path the bus waits behind),
  **`ot_policy`** (a bus-halting operation always needs a password — see
  [§6](#6-security-model)), and every networking component ("nothing reboots
  because a peer is absent").

### (b) The CH bit never rises until the held setpoint has gone out

A boiler asked for heat with an **unconfirmed** flow setpoint (OpenTherm Data-ID
1, `TSet`) would heat to a value nobody chose. So the central-heating enable bit
**never rises** until the bus has carried the currently-held ID 1 since it last
changed.

- Enforced in **`ot_control`**: the invariant forces the CH bit down (reason
  `AWAIT_SETPOINT`) whenever heat is wanted, the bit is currently down, and the
  setpoint is unconfirmed. A **change** un-confirms the held value; a
  confirmation from the bus re-confirms it **only if the confirmed value equals
  the held one** — a *foreign* value seen on the wire un-confirms it too. (A bit
  already up is *not* dropped for a new setpoint — that would cost a burner
  cycle per slider move — the rule constrains the bit *rising*, not staying up.)
  See [../components/ot_control/README.md](../components/ot_control/README.md).
- Carried out in **`ot_thermostat`**: ID 1 is queued onto the bus **first**,
  before the owed ID 56, and the ID-1 log line is emitted before the status
  line, so the setpoint provably reaches the wire before the bit rises.
- The confirmation itself comes from **`ot_bus_track`** (`id1_seq`/`id1_raw`, a
  count of ID-1 writes *answered*) via **`ot_control_io_confirm()`**.
- A subtle corollary in **`ot_command` / `ot_control_io`**: the executor encodes
  its own ID-1/ID-56 re-sends with the raw f8.8 codec, **not** through
  `ot_command_encode()` — because that gate refuses an ID the state model marked
  unsupported (two stray UNKNOWN-DATAIDs suffice), and a refused re-send would
  leave ID 1 forever unconfirmed and the CH bit forever down, failsafe included.

### (c) Timers survive the ~49.7-day wrap; restore errs safe

A 32-bit millisecond clock wraps every ~49.7 days, and a naive `now - last_ok`
across that wrap reads a long-dead peer as *fresh* — in January, unwatched.

- The watchdog, the confirmation age, the CH-bit age, the DHW-write age, the
  failsafe duration, the heat-hours arm and the room-source overdue time are all
  **saturating accumulators** advanced each step by a wrap-safe add, never a
  subtraction of two absolute moments. Enforced in **`ot_control`**,
  **`ot_control_io`**, **`ot_sensor`**, and mirrored in the pure timing helpers
  of **`ot_bus_sched`**, **`ot_provision`**, **`ot_ticket`**, **`ot_observe`**
  and **`ot_mqtt`**'s 64-bit log clock. (The only stored *moment* is the boost
  deadline, whose ≤ 8 h span sits far inside a signed difference's ±24.8-day
  range.)
- **`RTC_NOINIT` restore is checksummed.** The two counts that must survive a
  soft reset (watchdog overdue, heat-hours part-hour) are packed in `RTC_NOINIT`
  RAM under a magic word (`"OTC2"`) and a check word; a torn or noise-filled
  blob fails the check and is ignored. The blob is believed **only** after a
  reboot-loop reset reason (SW, panic, the watchdogs, brown-out) — never after a
  power-on. Enforced in **`ot_control_io`** (`ot_control_io_rtc_*`,
  `ot_control_io_restore`), carried out in **`ot_thermostat_persist.c`**.
- **NVS is banked before the RTC mirror, so skew errs safe.** Each step the
  whole-hour heat count is written to NVS **before** the `RTC_NOINIT` part-hour
  mirror. A reset in the window between the two banks one extra hour, which
  disarms the summer failsafe *sooner* (less unwanted heat) — the safe
  direction — rather than prolonging an out-of-season burn.

---

## 4. End-to-end flows

Boot order (from `src/main.cpp`): `ot_log_install` → `ot_bus_start` →
`ot_net_start` → `ot_thermostat_start` → `ot_mqtt_link_start` → `ot_http_start`
→ the DS18B20 task. Each `*_start` is non-fatal: a component that cannot come up
leaves a device that still drives the boiler and still says why over USB.

### (i) Home Assistant command → boiler

1. A retained-safe MQTT message lands on `<prefix>/<key>/set`. `ot_mqtt_link`'s
   esp-mqtt event handler copies the raw bytes into an inbound queue (no
   parsing on esp-mqtt's task).
2. The link task drains the queue and calls the **pure** `ot_mqtt_decide()`,
   which classifies it as `OT_MQTT_COMMAND` (a retained command is refused
   here, a non-number payload is refused here).
3. `ot_mqtt_handle()` runs it exactly as the web POST route would, with the
   origin fixed to `OT_ORIGIN_HA` and the three impure effects injected. It
   calls **`ot_command_check(…, OT_ORIGIN_HA, …)`** — the single legality
   decision (writability, boiler support, bounds, ownership).
4. For a control command (`ch_enable`, `ch_setpoint`, `dhw_enable`,
   `dhw_setpoint`, `heating_season`) the final answer is
   `ot_thermostat_control_apply()`, which re-checks ownership under the
   executor's spinlock and lands the command; an accepted CH command feeds the
   watchdog. (A raw-frame write from HA is refused `OT_CMD_NOT_FOR_HA`.)
5. On the next once-a-second `ot_control_step()`, the executor computes the CH
   bit and the held ID 1. `ot_thermostat` hands the status byte to
   `ot_bus_set_status()` and queues ID 1 via `ot_bus_write_if_idle()`.
6. `ot_bus` sends the frames on the wire through `ot_master_exchange()`; the
   boiler acts on them.

### (ii) Frame receive → state → JSON

1. `ot_master`'s timer ISR samples the line every 100 µs and feeds
   `ot_decode`, which reconstructs the 34 bits; `ot_frame_decode` checks parity
   and yields `{type, data_id, data_value}`.
2. `ot_bus` records the reply into `ot_observe` (the raw 128-ID diagnostic
   table) and calls its response callback.
3. The callback feeds `ot_state_apply_dataid()`, which decodes the value
   through the entity codec for **every** registry entity reading that Data-ID,
   updates availability (OK / INVALID / UNSUPPORTED / UNKNOWN) and sets
   per-consumer dirty marks only on an actual change.
4. `ot_api` renders `ot_state` into JSON on demand — `GET /api/state`, the
   `/ws` push frame, and the MQTT state payloads all go through the **one**
   value emitter, so the three transports can never disagree.

### (iii) Room temperature → steer

1. The DS18B20 task reads the shield sensor via `ot_onewire` (RMT transport,
   ~800 ms blocking, off the bus task) and `ot_onewire_decode` (CRC +
   Celsius), then calls `ot_thermostat_room_submit(0, celsius)`. In parallel,
   an MQTT reading on `<prefix>/room/state` is classified `OT_MQTT_ROOM` and
   submitted to slot 1 by `ot_mqtt_link`'s `fx_room` — as a **measurement,
   never a command**, so it can neither feed the watchdog nor keep the
   controller alive.
2. Both land in a **locked mailbox** in `ot_thermostat_room.c`.
3. Each tick, on the thermostat task alone, the mailbox is drained into the
   single-owner `ot_room` registry (`ot_room_submit` → each slot's `ot_sensor`
   filters outliers and tracks freshness), then `ot_room_tick` advances it.
4. `ot_room_select_steer()` picks the safety-critical reading — the first slot
   that is **both `room`-role and FRESH** (ambient slots are structurally
   excluded from steering) — and `ot_room_select_display()` picks what the UI
   shows (room preferred, ambient fallback).
5. The steer selection fills `ot_control`'s input; the failsafe uses it.

### (iv) Failsafe when HA goes silent

1. While Home Assistant owns the mode, every accepted HA CH command resets the
   watchdog accumulator. If none arrives, the accumulator grows (saturating)
   each step.
2. When it reaches `watchdog_s`, the ladder drops from `HA` to `FAILSAFE`.
3. `otc_failsafe_ch()` decides the CH bit, in order: **(a)** if HA has not asked
   for heat within `failsafe_heat_days` of *powered* hours, the failsafe
   *disarms* and holds CH down (the "it is summer" case); **(b)** otherwise, if
   there is no fresh steer source, it heats **blind** at the failsafe setpoint;
   **(c)** otherwise it applies ±0.3 K hysteresis around
   `failsafe_room_target_dc`, with `failsafe_min_cycle_s` enforced as both
   minimum-on and minimum-off.
4. The bus keeps conversing throughout (invariant (a)); the CH bit still obeys
   the held-setpoint rule (invariant (b)); a reboot mid-failsafe restores the
   watchdog and heat-hours counts from `RTC_NOINIT`/NVS (invariant (c)), so a
   reboot loop cannot reset its way out of the failsafe.

---

## 5. Configuration & persistence

**`ot_config`** owns everything that survives a power cut: the Wi-Fi pair, the
MQTT broker, the device name, the two passwords, the executor's settings and the
MQTT room-source slot. It is **two halves**: a **pure half** (validation,
projection, the reset rules, the merged-document cross-field rules) that host
tests reach, and a **device half** (`#ifdef ESP_PLATFORM`, the NVS I/O) that is
never host-built. Every rule exists because a bad value stored once outlives
every reboot and a wall thermostat that panics on its own config has no way back
over the air — hence **no `ESP_ERROR_CHECK` on anything from flash**, and a
`sanitize` pass that repairs an unusable stored value to its default on the way
out. See [../components/ot_config/README.md](../components/ot_config/README.md).

- **Three NVS namespaces** decide what a reset destroys: `cfg_wifi` (the pair +
  known-good + provisioning flags), `cfg_owner` (the UI password record),
  `cfg_app` (broker, name, executor settings, room slot, schema number). The
  Wi-Fi SSID and PSK are **one record under one key** so a power cut can never
  land between them.
- **Schema** (`OT_CONFIG_SCHEMA_VERSION = 2`) is bumped only when the *meaning*
  of a stored value changes; **retired keys** are erased on every writable boot
  because NVS erases only by namespace.
- The **live document lives in `ot_net`** (loaded once, mutex-guarded); the HTTP
  layer never sees `ot_config_t` and never opens NVS, so it cannot grow a second
  idea of a valid document. `ot_config_room_mqtt()` is declared in `ot_config`
  but implemented in `ot_net_config.c` for the same reason.

**RTC_NOINIT record** — see [§3(c)](#3-safety-invariants). The two executor
counts survive a soft reset there; the record shape and the "believe only after
a reboot-loop reason, and only with magic + check intact" rule live in
`ot_control_io`.

**Write-generation / re-arm** — the bus write queue holds exactly one write, and
a newer write **evicts** the unexecuted one (a stale setpoint is worse than a
lost one). `ot_bus_track`'s `gen` counter closes the one race the single-slot
queue leaves open: a write queued *while an exchange is already on the wire*
would otherwise be dropped unsent; when the generation moved during the
exchange, `ot_bus_track_done()` **re-arms** it. The executor's own writes go
through `ot_bus_write_if_idle()` so they never evict a hand write.

---

## 6. Security model

The threat model is explicit: the setup access point is **open**, so on it "the
client" is anyone in radio range; afterwards the device sits on an untrusted
household LAN. The whole config/command surface is written for that reader.

- **Write-only secrets** — **`ot_secrets`**. A stored password is returned by no
  API projection: a read hands back the sentinel `__UNCHANGED__`, and a
  submitted sentinel means "leave what is stored". Secret keys are matched by
  case-insensitive **substring** (`password`, `psk`, `secret`, `token`, …) so a
  future key is redacted without extending a whitelist. `ot_secret_equals` is
  constant-time. The one exception is the broker password, stored recoverably
  because MQTT authenticates with the plaintext — and it never reaches a
  projection or a log either.
- **HTTP Basic auth** — **`ot_auth`**. Parses the attacker-chosen
  `Authorization` header with nothing copied before its length is known, no
  answer depending on how nearly right a guess was, and the plaintext wiped on
  every path through a `volatile` pointer. The stored form is
  `PBKDF2-HMAC-SHA256` (`1$<iters>$<salt>$<digest>`); the KDF arrives as an
  injected function pointer so the parser is host-tested. There is **no
  lockout** (a DoS against the owner) and a fixed **1 s** failure delay.
  `NO_PASSWORD` is never `GRANTED`.
- **The pure access policy** — **`ot_policy`**. `ot_http_check(method, path,
  {provisioned, password_set, authenticated})` returns ALLOW / 401 / 403 with no
  HTTP types. Reading is open until the owner sets a password; writing is refused
  until they do — except the three bootstrap writes (`/api/config` to set the
  first password, a heating command, and the `/ws` ticket). **Radio proximity to
  an unclaimed device authorises nothing but handing it a network.** The 401-vs-
  403 split is deliberate: telling a client "authenticate" when no authentication
  exists yet sends it round a loop it cannot leave.
- **The bus-halting exception** — `ot_http_check_op(halts_bus, password_set)`:
  an operation that stops the master (e.g. `linetest`) **always** needs a
  password, because sustained master silence is read by the boiler as a heat
  demand (invariant (a)), so a caller looping it holds the boiler hot
  indefinitely. With no password set this is 403, not 401.
- **DNS-rebind + CSRF guards** — enforced in **`ot_http`** using
  `ot_policy`'s pure checks: a **Host-header allowlist** (`ot_http_host_ok`,
  fail-closed on a foreign host, the DNS-rebinding defence) and a **Content-Type
  gate** (`ot_http_content_type_ok`, requiring `application/json` on bodied
  writes, forcing a CORS preflight the device denies — the CSRF defence). No
  CORS header is ever sent.
- **`/ws` tickets** — **`ot_ticket`**. A browser cannot reliably send Basic auth
  on `new WebSocket()`, so the socket presents a one-shot, 30 s, 128-bit ticket
  issued over the password-protected `POST /api/ws-ticket`. The table lives in
  RAM only (a saved ticket would outlive a password change); randomness is
  injected so it cannot fabricate a valid ticket.

---

## 7. Entities are generated from one source

**`tools/opentherm_ids.py` is the single entity list**, and everything that
names an entity is generated from it — never edited by hand:

- `components/ot_registry/include/registry_generated.h` — the C entity table
  (`OT_ENTITIES[]`, the poll ring, kinds, codecs, bounds, units) that
  `ot_registry` looks up.
- `web/src/api/entities.ts` — the frontend types.
- `components/ot_ha/include/ha_generated.h` — the Home Assistant discovery
  documents that `ot_ha` fills at runtime.

The generator is `tools/generate_registry.py`, split into a model
(`registry_model.py`) and C/TS/discovery renderers (`render_c.py`,
`render_ts.py`, `render_discovery.py`), with `regenerate.py` as the pre-build
hook. The English-vs-localised display names are split so Home Assistant stays
English while the SPA carries DE/NL/UK overlays
(`tools/entity_names_i18n.py`).

The rule that follows from this: **fix the generator input, not the outputs.**
Any file with a `DO NOT EDIT` banner is regenerated on the next build. The
`ot_api` projection prints only strings that came from the registry, and
`tools/tests/test_registry.py` proves none of them needs JSON escaping — which
is why `ot_api` carries no escaper at all.

---

## 8. Tests & gates

**The host is the barrier.** The protocol, the state model, the executor core,
the room filter, the command legality, the JSON reader and the config rules are
pure and host-tested, and the test is written *before* the implementation. The
host build uses `-Werror=switch`, so an enum extended without its `case` fails
to compile. One directory is one suite (PlatformIO links all the `.cpp` of a
directory into one binary).

The three green gates are:

- **Native host suites** — `pio test -e native` (the barrier for any protocol,
  state, executor, command or config change).
- **Web** — `cd web && npm test` plus a clean `tsc` + Vite build
  (`cd web && npm run build`).
- **Python generator tests** — `python3 -m pytest tools/tests -q` (the
  discovery-schema validation is skipped without a `homeassistant` virtualenv).

The impure glue that no host suite compiles is pinned by **source-guard tests**,
which read the sources and fail when a load-bearing call is swapped for its
obvious neighbour:

- `tools/tests/test_source_guards.py` — over `ot_bus.c` and `ot_thermostat`
  (e.g. `ot_bus_write_if_idle` must not become `ot_bus_write`).
- `tools/tests/test_source_guards_mqtt.py` — over the `ot_mqtt_link` / `main.cpp`
  glue (e.g. no command-origin literal may appear in `ot_mqtt_link`).
- `tools/tests/test_source_guards_led.py` — over the `ot_led_task` / `main.cpp`
  glue (e.g. the net-state conversion must stay an explicit `switch`, and the
  glue must never reference a bus-write or bus-lock call).

The generator side also carries `test_discovery.py` (the HA-kill rules on
rendered documents) and `test_ha_schema.py` (every document handed to Home
Assistant's own `DISCOVERY_SCHEMA`; needs `HA_PY`). Run pytest with
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`, since Home Assistant's own pytest plugin
breaks plain tests.

Anything that can only be checked on real hardware lives in a separate
hardware checklist; a successful build is not readiness.

> Note: `pio run | tail` returns `tail`'s exit code, not the build's — a failed
> build can look successful. Judge a build by writing output to a file and
> checking `$?`, not by the tail.

---

## 9. Build & flash commands

```sh
pio run -e lolin_c3_mini            # main board (ESP32-C3); the SPA build is part of it
pio run -e supermini_c6             # second target (ESP32-C6 SuperMini)
pio test -e native                  # the host suites — the barrier for any protocol change
pio test -e native -f test_ot_decode   # a single host suite
python3 -m pytest tools/tests -q    # the registry-generator tests
cd web && npm run dev               # SPA dev server
cd web && npm run build             # the real web build gate (NOT tsc --noEmit)
cd web && npm test                  # every web suite
pio run -e lolin_c3_mini -t upload  # flashing — the owner's business, ASK FIRST
```

`pio` may be missing from `PATH`; it lives at `~/.platformio/penv/bin/pio`.
**Flashing is the owner's** — ask first.

---

## 10. Component reference

One short entry per component: purpose, key entry points, its host test suite,
and a link to the full contract.

### OpenTherm wire layer

- **ot_frame** — the OpenTherm frame layout and the DATA-VALUE codecs (f8.8,
  signed/unsigned bytes, flags). Pure, allocation-free, ISR-safe. Entry points:
  `ot_frame_encode` / `ot_frame_decode`, `ot_codec_*`. Parity is **EVEN** (a
  named gotcha). Tested via any suite that includes it.
  [../components/ot_frame/README.md](../components/ot_frame/README.md)

- **ot_encode** — renders a 32-bit payload into 68 logical transmit half-bits,
  Manchester-coded (board inversion applied downstream). Entry point:
  `ot_encode_frame`. Suite: `test_ot_encode`.
  [../components/ot_encode/README.md](../components/ot_encode/README.md)

- **ot_decode** — the receive Manchester state machine over a 100 µs sampler
  (IDLE/BUSY/DONE/ERROR). Entry points: `ot_decode_reset` / `_push` /
  `_payload`. Narrow timing windows are load-bearing. Suite: `test_ot_decode`.
  [../components/ot_decode/README.md](../components/ot_decode/README.md)

- **ot_master** — one conversation on the wire with correct timing: owns the two
  GPIOs, their inversion, the one hardware timer, and the TX/RX state machine.
  Entry points: `ot_master_init`, `ot_master_exchange` (blocks ~1.2 s, **one
  caller only, no lock**), diagnostics `ot_master_input_duty` /
  `ot_master_drive_line`. Suite: `test/test_ot_master/` (with its own fake
  ESP-IDF, deliberately not shared).
  [../components/ot_master/README.md](../components/ot_master/README.md)

- **ot_bus** — the single FreeRTOS task that owns the wire; the only caller of
  `ot_master_exchange`. Entry points: `ot_bus_start` (no stop, deliberately),
  `ot_bus_write` / `_write_if_idle`, `ot_bus_set_status` (the only way to ask
  for heat), `ot_bus_write_state`, `ot_bus_scan` (read-only), `ot_bus_line_test`.
  Impure; pinned by source guards, logic tested in its pure siblings.
  [../components/ot_bus/README.md](../components/ot_bus/README.md)

- **ot_bus_sched** — the pure "what to ask and when": WAIT/TALK as the max of two
  deadlines, ID-0 alternation, the one-slot write queue, the diagnostic sweep,
  unsupported-ID removal. Entry points: `ot_bus_sched_step` / `_done` / `_write`
  / `_set_status` / `_disable`. Suite: `test_ot_bus_sched`.
  [../components/ot_bus_sched/README.md](../components/ot_bus_sched/README.md)

- **ot_bus_track** — the pure write bookkeeping beside the scheduler: the write
  generation (re-arm) and the answered-ID-1 record (`id1_seq`/`id1_raw`). Entry
  points: `ot_bus_track_write` / `_write_if_idle` / `_done`. Suite:
  `test_ot_bus_track`.
  [../components/ot_bus_track/README.md](../components/ot_bus_track/README.md)

### State / registry

- **ot_registry** — read-only lookup over the generated entity table (by index,
  key, Data-ID iterator). Entry points: `ot_registry_by_key`, `_index_of`,
  `_first_for_id` / `_next_for_id`, `_poll_at`. Holds no state.
  `registry_generated.h` is generated. The synthetic-row invariant is pinned by
  a named host test.
  [../components/ot_registry/README.md](../components/ot_registry/README.md)

- **ot_state** — the decoded, keyed read-side model: value, three-valued
  availability, per-consumer dirty marks, read bounds, unsupported verdict, and
  the write seam for synthetic entities. Entry points: `ot_state_apply_dataid`,
  `_get`, `_bounds`, `_set_virtual`, `_take_dirty`, `_is_unsupported`. Runs under
  `ot_lock()`. Host-tested (named suites under `test/`).
  [../components/ot_state/README.md](../components/ot_state/README.md)

- **ot_observe** — the raw diagnostic table over all 128 Data-IDs (last reply,
  type, raw bits, age, count) plus its JSON render. Entry points:
  `ot_observe_record`, `_get`, `_render_json`. Never a source of entities.
  Suite: `test_ot_observe`.
  [../components/ot_observe/README.md](../components/ot_observe/README.md)

- **ot_sensor** — one room-temperature source: range + bounded-jump outlier
  filter with an escape hatch, and a FRESH/STALE/NEVER machine whose overdue time
  is a **wrap-safe accumulator**. Entry points: `ot_sensor_init`, `_update`,
  `_tick`, `_state`, `_value`. Suite: `test_ot_sensor`.
  [../components/ot_sensor/README.md](../components/ot_sensor/README.md)

- **ot_room** — a registry of 4 room-temperature slots (each an `ot_sensor`) with
  two picks: **steer** (safety — first `room`-role FRESH slot, ambient
  structurally excluded) and **display** (room preferred, ambient fallback).
  Entry points: `ot_room_init`, `_submit`, `_tick`, `_select_steer`,
  `_select_display`. Suite: `test_ot_room`.
  [../components/ot_room/README.md](../components/ot_room/README.md)

### Executor

- **ot_control** — the pure executor core: the six-state ladder
  (`SEASON_OFF/BOOST/LOCAL/HA_WAITING/FAILSAFE/HA`), the watchdog, the bounded
  failsafe, the boost, the held ID 1 with its re-send cadence, the DHW/ID-56
  reconciliation. Entry points: `ot_control_init`, `_check`, `_apply`, `_step`,
  the `_boost_*` family, `_ch_command`. Suites: `test_ot_control`,
  `_check`, `_failsafe`, `_boost`, `_bus`.
  [../components/ot_control/README.md](../components/ot_control/README.md)

- **ot_control_io** — the pure translations between `ot_control` and its
  neighbours: config snapshot, persist patch, bus confirmation, the ID-56
  readback/owed/heard rules, the `RTC_NOINIT` pack/check + reset restore, the
  synthetic entities, and the `GET /api/control` document. Suite:
  `test_ot_control_io`.
  [../components/ot_control_io/README.md](../components/ot_control_io/README.md)

- **ot_command** — the single decision point for the legality of any write:
  writability, boiler support, bounds, codec, and ownership (web vs HA). Entry
  points: `ot_command_check` (**every** writer, carrying origin),
  `ot_command_encode` (the frame path — never from a surface), `ot_command_strerror`.
  Suite: `test_ot_command`.
  [../components/ot_command/README.md](../components/ot_command/README.md)

- **ot_thermostat** — the impure top glue: the once-a-second task, the spinlock,
  the bus access, NVS, `RTC_NOINIT`, and the sole writer of the ID-0 status high
  byte and the held ID 1. Entry points: `ot_thermostat_start`, `_heard` (bus
  task), `_room_submit`, `_control_cfg` / `_control_apply` / `_control_get`,
  the `_boost_*` family. Not host-tested (pinned by source guards); the one pure
  exception `ot_thermostat_room_cfg.c` has `test/test_ot_thermostat_room/`.
  [../components/ot_thermostat/README.md](../components/ot_thermostat/README.md)

### Networking / Home Assistant

- **ot_net** — the impure provisioning executor: owns the radio (one task, one
  lock), the stored config document, the identity from the MAC, and the captive
  DNS socket. Entry points: `ot_net_start`, `_get_state`, `_is_provisioned`,
  `_provision` / `_provision_body`, `_config_snapshot` / `_config_apply`,
  `_broker` (the one un-redacted projection), `_scan`, `_status`. Driven by the
  pure `ot_provision`; tested through `test/test_provision`.
  [../components/ot_net/README.md](../components/ot_net/README.md)

- **ot_mqtt** — pure: the topic tree, `ot_mqtt_decide` (what a message means),
  `ot_mqtt_handle` (the one command hand-off, effects injected), the outbound
  payloads, and the publisher bookkeeping. No ESP-IDF header. Suites:
  `test_ot_mqtt`, `_out`, `_control`, `_seam`.
  [../components/ot_mqtt/README.md](../components/ot_mqtt/README.md)

- **ot_mqtt_link** — the impure esp-mqtt glue: one task owns the client, follows
  the broker settings by a 32-bit fingerprint, drains the inbox to the pure
  deciders, and runs the publishing pass. Entry points: `ot_mqtt_link_start`,
  `_status`. No host suite; pinned by `test_source_guards_mqtt.py`.
  [../components/ot_mqtt_link/README.md](../components/ot_mqtt_link/README.md)

- **ot_ha** — pure: which discovery documents Home Assistant is owed
  (WAIT/WANT/DROP) and the bytes of each (six-token fill, overflow-safe, with the
  guards that keep a malformed value from deleting an entity or device). Entry
  points: `ot_ha_want(s)`, `_bounds`, `_topic`, `_render`, the `_plan_*` family.
  Suites: `test_ot_ha`, `test_ot_ha_plan`.
  [../components/ot_ha/README.md](../components/ot_ha/README.md)

- **ot_time** — SNTP + the POSIX time zone; validity is "a sync happened", never
  "`time()` returned something". Entry points: `ot_time_start`, `_set_zone`,
  `_now`. Not host-built (pulls in `esp_netif_sntp.h`).
  [../components/ot_time/README.md](../components/ot_time/README.md)

### HTTP / web

- **ot_http** — the one web surface: REST, `/ws`, the SPA. Thin glue — reads and
  bounds the request, applies the one access gate, calls the pure layer, names
  the status code. Entry points: `ot_http_start` / `_stop`; the route table
  (`/api/state`, `/api/config`, `/api/control`, `/api/entities/*`, `/api/ops/*`,
  `/ws`, …). No web-privileged path — everything `curl` can do the browser can,
  and back. Impure; covered indirectly by the pure layers it calls.
  [../components/ot_http/README.md](../components/ot_http/README.md)

- **ot_api** — the single JSON projection of registry + state + the control
  document; the one `emit_bare_value` shared by REST, `/ws` and MQTT. Entry
  points: `ot_api_render_entities` / `_state` / `_entity` / `_frame` / `_value`,
  `ot_api_render_control`. snprintf semantics; no escaping (registry is clean by
  construction). Suite: `test_api`.
  [../components/ot_api/README.md](../components/ot_api/README.md)

- **ot_json** — a narrow, hostile-input-safe JSON reader over a NUL-terminated
  buffer (nested values rejected, duplicate keys refused, nothing read past the
  terminator) and the single string escaper. Entry points: `ot_json_check`,
  `_string` / `_u32` / `_i32` / `_bool` / `_f32` / `_key_at`, `ot_json_escape`.
  Suite: `test/test_json`.
  [../components/ot_json/README.md](../components/ot_json/README.md)

- **ot_ticket** — one-shot, 30 s, 128-bit `/ws` handshake tickets in a
  caller-owned RAM table (randomness + clock injected). Entry points:
  `ot_ticket_reset`, `_issue`, `_redeem`. Pure, host-tested.
  [../components/ot_ticket/README.md](../components/ot_ticket/README.md)

- **ot_policy** — the pure access decision as a function of method, path and
  `{provisioned, password_set, authenticated}`, plus the bus-halting-op rule and
  the Host / Content-Type gates. Entry points: `ot_http_check`,
  `_check_op`, `_content_type_ok`, `_host_ok`. Suite: `test/test_http_policy`.
  [../components/ot_policy/README.md](../components/ot_policy/README.md)

- **ot_auth** — HTTP Basic header → yes/no, attacker-safe, verifier injected.
  Entry points: `ot_auth_parse`, `_forget`, `_check`, `_device_verifier`
  (device-only). Suite: `test/test_auth`.
  [../components/ot_auth/README.md](../components/ot_auth/README.md)

- **ot_secrets** — write-only secret handling (redact on read, keep/clear/store
  on write) + constant-time compare. Entry points: `ot_secret_key`, `_redact`,
  `_decide`, `_equals`; `OT_SECRET_SENTINEL`. Suite: `test/test_secrets`.
  [../components/ot_secrets/README.md](../components/ot_secrets/README.md)

- **ot_captive** — the setup-AP captive portal: a byte-exact HTTP probe table and
  a pure DNS reply builder that points every A lookup at the AP, plus the UDP:53
  task. Entry points: `ot_captive_answer` / `_table`, `ot_captive_dns_reply`,
  `ot_captive_dns_start` / `_stop`. Suite: `test/test_captive` (the two pure
  files).
  [../components/ot_captive/README.md](../components/ot_captive/README.md)

- **ot_provision** — the pure Wi-Fi provisioning state machine: mode, the setup
  window, trial/rollback, failure classification, the self-healing fallback AP.
  Time enters as a parameter. Entry points: `ot_prov_init`, the `ot_prov_on_*`
  events, `ot_prov_tick`, the `ot_prov_*` questions, `_take_action`. Suite:
  `test/test_provision`.
  [../components/ot_provision/README.md](../components/ot_provision/README.md)

- **ot_wire** — pure bytes ⇄ decision for the config surface: parse/render for
  `/api/config`, `/api/provision`, `/api/wifi/scan`, and the command bodies; the
  single spelling of each provisioning state / mode / CONNACK. Entry points:
  `ot_wire_parse_config` / `_render_config`, `_parse_provision` /
  `_render_provision`, `_parse_entity_write`, `_parse_operation`. Suites:
  `test/test_wire*`.
  [../components/ot_wire/README.md](../components/ot_wire/README.md)

- **web_assets** — the SPA embedded via `EMBED_FILES` (three gzipped files),
  built at CMake configure time so the UI cannot drift from the binary. Entry
  point: `web_assets()`. Web tests live under `web/`.
  [../components/web_assets/README.md](../components/web_assets/README.md)

- **ot_log** — the in-RAM ring of the last 80 log lines rendered for
  `GET /api/log`; escapes every non-ASCII byte (which is *why* log strings are
  English). Entry points: `ot_log_write`, `_render`, `_install`. Exercised
  through the log route.
  [../components/ot_log/README.md](../components/ot_log/README.md)

- **ot_lock** — the one recursive mutex over the state model (a no-op on the
  host, so the state-model sources are identical device/host). Entry points:
  `ot_lock`, `ot_unlock`.
  [../components/ot_lock/README.md](../components/ot_lock/README.md)

### Hardware / 1-Wire

- **board** — the only place GPIO numbers are named; one descriptor per chip,
  selected by `IDF_TARGET`. Entry point: `board_get()` (never NULL). Fields
  include the OpenTherm pins + inversion, the button, the LEDs, the 1-Wire pin
  and the net transport. Verified on hardware, not by a host suite.
  [../components/board/README.md](../components/board/README.md)

- **ot_onewire** — RMT-backed 1-Wire transport for the shield's DS18B20 (RMT so
  it never fights the OpenTherm master for the CPU). Entry points:
  `ot_onewire_open`, `_read_temp` (~800 ms blocking, off the bus task), `_close`.
  Device-only; the decode is the sibling.
  [../components/ot_onewire/README.md](../components/ot_onewire/README.md)

- **ot_onewire_decode** — the pure DS18B20 CRC-8 + temperature decode. Entry
  points: `ot_onewire_crc8`, `_scratchpad_crc_ok`, `_temp_raw`, `_temp_c`.
  Suite: `test_ot_onewire`.
  [../components/ot_onewire_decode/README.md](../components/ot_onewire_decode/README.md)

### Status LED

- **ot_led** — pure: the health ladder (colour, from the WiFi → MQTT → HA
  chain and the failsafe) and the animation curves (motion, from boiler
  activity) that turn a sampled `ot_led_world_t` into an RGB pixel. Declares
  its own `ot_led_net_state_t` rather than depending on the impure
  `ot_net_state_t`. Entry points: `ot_led_render`, `ot_led_note_ota`
  (dormant in v1). Suite: `test_ot_led` (26 tests).
  [../components/ot_led/README.md](../components/ot_led/README.md)

- **ot_led_task** — the impure glue: one `tskIDLE_PRIORITY+1` task samples
  `ot_net_get_state`/`_has_credentials`, `ot_mqtt_link_status`,
  `ot_thermostat_control_get` and `ot_state_get("flame")` once a second, ticks
  the animation every ~50 ms, and drives the one WS2812 pixel on
  `board->rgb.gpio` via `led_strip`/RMT. Reader-only — never writes an
  OpenTherm frame, never takes the bus lock, never blocks. Started from
  `app_main` (`src/main.cpp`) guarded by `board->rgb.gpio >= 0`. No host
  suite; pinned by `tools/tests/test_source_guards_led.py`.
  [../components/ot_led_task/README.md](../components/ot_led_task/README.md)
