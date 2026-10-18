# OpenTherm thermostat firmware — briefing for the AI

## What this is

Own ESP-IDF firmware for a room thermostat of a gas boiler with an OpenTherm interface.
C / ESP-IDF 5.5.5 (carried by the pinned platform) / PlatformIO, one universal binary,
everything configured at runtime.

**The firmware is the executor, not the brain.** Home Assistant is the controller; the firmware
is the **sole OpenTherm master** and the holder of the commanded state. It guards against nonsense
values and against the controller disappearing (a watchdog and a bounded failsafe), and it forms
its own view of the room through a registry of room-temperature sources (MQTT and a shield DS18B20
today; WiFi push/pull, BLE and general 1-Wire are planned). There is no PI loop and no schedule in
the firmware — the control law and any schedule live in Home Assistant.

## Read first

- `docs/firmware-design.md` — **the single, code-accurate design and reference**: the executor
  core, the bus, the entities, MQTT/HA, the room sources, the failure model, configuration, the
  tests, and a boiler-reference appendix. **The authority for what the firmware does.**
- `docs/implementation-reference.md` — **the code-level implementation reference**: the layered
  architecture and the acyclic dependency map, the safety invariants and where each is enforced,
  the end-to-end flows, configuration/persistence, the security model, entity generation, the test
  gates, and a per-component index. Read this for "how is it built"; it links out to each README.
- **`components/<name>/README.md` — one per component**, the per-component implementation doc
  (Purpose · Responsibility · Public API · Implementation · Tests · Notes). Start here when you
  enter a single component. **They are docs, not the code — re-read the source before trusting a
  detail.**
- `docs/hardware-verification/` — the test scenarios the owner walks on real hardware, grouped
  by what the device does (`scenarios/`, starting with `00-setup-and-safety.md`), not by any
  build phase. A successful build is not readiness; anything the owner must verify on the boiler
  lives here.

## Commands

```sh
pio run -e lolin_c3_mini            # main board; the SPA build is part of it
pio run -e supermini_c6             # second target, ESP32-C6 SuperMini
pio test -e native                  # host suites -- the barrier for any protocol change
pio test -e native -f test_ot_decode # a single suite
python3 -m pytest tools/tests -q    # tests of the registry generator
pio run -e lolin_c3_mini -t upload  # flashing -- the owner's business, ASK FIRST
cd web && npm run dev               # SPA dev server
cd web && npm run build             # the real web gate -- NOT tsc --noEmit
cd web && npm test                  # every web suite (web/scripts/test.mjs) -- the other half
```

`pio` may be missing from `PATH`; it lives at `~/.platformio/penv/bin/pio`.

**The build status must not be judged by the tail of the output.** `pio run | tail` returns
the exit code of `tail`, not of the build, and a failed build looks successful. Write to a
file and check `$?`.

## How work is done here

**Delegate, do not accumulate.** The main thread is for decisions and the plan. Everything
that requires reading many files goes to a subagent, which returns a summary.

**Split the work before starting.** The plan turns into N independent tasks, each one
naming its own files and its own test suite. Non-overlapping tasks are sent
**in a single message**. Overlapping ones go sequentially, and that is stated in the plan,
not discovered in a conflict.

Three constraints, all learned from experience, and they are lifted differently:

- **A shared build directory.** Liftable: `PLATFORMIO_BUILD_DIR=/tmp/pio-<task>` moves the
  build away wholesale, the main `.pio` is left alone. Each parallel agent gets its own.
- **A shared `web/dist`.** Every firmware build runs `npm run build` into the repository's
  `web/dist`, and `PLATFORMIO_BUILD_DIR` does not move it. A firmware task running beside a
  web task goes into its own git worktree.
- **A shared header.** Not liftable by anything: an edit by one breaks the build for another.
  Non-overlapping files remain a condition of parallelism, not a wish.

**Several sessions work on this repository at once.** Check `git status` before starting and
before committing; a dirty tree you did not make is another session's work in progress — do
not touch it, and commit only your own paths.

**Verification is output, not an assertion.** "The tests pass" without the pasted
output is a step that was not done.

**Counter-review belongs in the MIDDLE of the work, not at its end.** A body of green tests can
still hide a channel that does not work end to end, because each half was tested against its own
idea of the contract. Any piece of work with more than one moving surface gets a counter-review as
soon as the surfaces first meet — and a design gets one before it is built on top of.

## File ceiling

**350 lines per hand-written source file**, 600 per Unity suite (a suite is one binary per
directory, so it is cut along directories). Generated files do not count.

Cut along an already existing seam: one responsibility per file, the public header
unchanged. A component is a directory. **Recount with `wc -l` before acting on this rule** —
line counts drift; a stale number sends the next session to split a file that is already split.
**Cut first, then add:** a file that is about to be entered and is over the ceiling is cut before
the new work goes in, not after.

Two standing exemptions:

- **The three contract public headers** (`ot_config.h`, `ot_wire.h`, `ot_provision.h`) are over
  the line and deliberately not cut: what is there is the contract and its explanations, and
  splitting a public header changes exactly what this ceiling orders not to change.
- **`web/src/pages/control/pixelBoiler.ts` (owner's decision).** It is one indivisible
  responsibility — the single canvas renderer for the whole heating-system scene (boiler, burner,
  internal heat-exchanger U-bends, column radiator, sink/vanity, pipes) — where every `drawX`
  method shares the same coordinate constants, colour palette and the private `px()` helper.
  Cutting it would fragment that one scene contract and duplicate the shared state for no gain.
  The pure maths is already split out (host-tested `pixelFire.ts` / `pixelFlow.ts`); the DOM draw
  code stays whole. Let it grow past 350; do NOT split it.

Nothing automated enforces the ceiling — it is a convention.

## Rules that carry load

- **No Arduino.** Neither the core, nor the libraries, nor transitively. `platformio.ini`
  has no right to acquire `framework = arduino`. Check with an anchor at the start of the
  line: the word `pioarduino` in the platform URL is mandatory and irrelevant here.
- **`ot_bus` does not stop the conversation with the boiler under any failure.** Silence from
  the master longer than 5 s is interpreted by the OpenTherm slave as a shorted thermostat, and it
  **goes into a demand for heat**. Falling silent is not a safe state but the hottest one. Neither
  a WiFi failure, nor a broker failure, nor a stale sensor, nor a dead Home Assistant has the right
  to stop the bus.
- **Nothing reboots because a peer is absent.** No "the broker has been unreachable for
  N minutes — restart".
- **The CH bit never rises before the held ID 1 has gone out.** A boiler asked for heat with no
  TSet heats to a value nobody chose.
- **One list of entities, ever.** REST, the MQTT topics, discovery for Home
  Assistant and the frontend types are generated from `tools/opentherm_ids.py`.
  `registry_generated.h` and `entities.ts` are outputs: fix the generator, not the file.
- **The web interface has no privileged handle.** If a screen needs a path of its own,
  then the API is wrong.
- **Not a single GPIO number outside `components/board/`.** Two boards are enough for
  this to stop being a formality: on the C3 lines 18 and 19 are taken by USB, on the C6 they
  are free, and USB moved to 12 and 13. Copying numbers from one descriptor into
  another means killing the console. `grep` gives a list of candidates, and each one is checked
  by eye: the argument must come from `board_get()`.
- **Language: English everywhere except ONE place.** Comments, headers, `docs/`, this file,
  commit messages, every `ESP_LOG*` string **and every string in `web/`** are English.
  Entity display names in `tools/opentherm_ids.py` are English too — the canonical name that
  reaches Home Assistant via discovery. The DE/NL/UK web translations of those names live in
  `tools/entity_names_i18n.py`, keyed by the same entity keys, and are generated into the SPA
  as a web-only overlay; they never touch discovery, so Home Assistant stays English.
  **Russian stays in exactly one place:** the default device name in `ot_config_defaults.c`
  (the owner's own HA device name). Log strings are English for a second, measured reason:
  `ot_log_render()` escapes every non-ASCII byte as `\u00xx`, so a non-ASCII log line comes back
  through `GET /api/log` unreadable and ungreppable.
- **Secrets are write-only.** A stored password is returned by no API projection;
  a read hands back a sentinel value.
- **The OpenTherm input inversion is derived from someone else's code, not from the
  specification.** If not a single frame decodes, the first thing to do is flip
  `ot_in_inverted`, not to look for a bug in `ot_decode`.
- **The platform pin carries load.** `platformio.ini` pins
  `pioarduino/platform-espressif32#55.03.311`, because a floating URL once moved the
  toolchain out from under a verified design. Move it deliberately and say so.

## Tests

The protocol, the state, the executor core, the room-source filter, JSON and the command
layer depend neither on hardware nor on a framework — they are tested on the host, and the
test is written **before** the implementation.

One directory — one suite: PlatformIO links all the `.cpp` files of a directory into one
binary, so two `main()`s in one directory would collide.

The components are ESP-IDF components and PlatformIO libraries at the same time, so
host tests compile exactly the same sources the device executes. The fake ESP-IDF for
`ot_master` lives in `test/test_ot_master/idf/`. **DO NOT** move the fake into a shared
directory: the moment two suites share it, "it grew a capability for suite B" becomes a way to
quietly weaken suite A.

`[env:native]` builds with `-Werror=switch`. ESP-IDF compiles components for the board without
`-Wall`, so the host build is where an enumerator appended without its `case` fails —
`ot_config_err_name()` and `ot_config_strerror()` rely on it. It guards only what the host builds:
`ot_http` is not host-built, so its switches end in an explicit 500 for a code they do not know.
**DO NOT** drop the flag to silence a failure; name the new code.

Glue with no suite is pinned by source guards: `tools/tests/test_source_guards.py` reads
`ot_bus.c` and `ot_thermostat`'s sources and fails when a call a fix depends on is replaced by its
obvious neighbour; `test_source_guards_mqtt.py` does the same over the `ot_mqtt_link`/`main.cpp`
glue no host suite compiles.

The HA-schema check (`tools/tests/test_ha_schema.py`) hands every rendered discovery document to
Home Assistant's own `DISCOVERY_SCHEMA`, so it needs a Python with `homeassistant` installed
(`HA_PY`; on the owner's machine the HA virtualenv beside this repo). Home Assistant's own pytest
plugin breaks plain tests, so always run pytest with `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`.

Everything the owner is obliged to check on real hardware goes into a scenario under
`docs/hardware-verification/scenarios/`. A successful build is not readiness.

## Documentation

Every non-trivial function carries a comment saying **why**, with a **self-contained rationale
inline** (not a pointer to an external document), and a `DO NOT` where the obvious improvement
breaks something. Comments that retell the code are noise and must be deleted.

Every public header describes the contract: ownership, the task context, what happens
on failure, which `esp_err_t` the caller is obliged to handle.

A decision a reader half a year from now could reasonably dispute is explained where it lives —
in the comment beside the code, or in the component's `README.md` — self-contained, so that no
deleted design document is needed to understand why the obvious alternative was not taken.

## Layout

**Every `components/<name>/` directory carries a `README.md`** documenting that component's
contract and implementation (Purpose · Responsibility · Public API · Implementation · Tests ·
Notes). The table below is the one-line role; the README is the full per-component doc, and
`docs/implementation-reference.md` ties them together.

| Path | Role |
| --- | --- |
| `src/main.cpp` | entry point |
| `components/board/` | board descriptors; the only place where pins are named. Selection by `IDF_TARGET`, one board per chip. The OpenTherm adapter it drives is documented in `docs/diyless-opentherm-master-shield.md`; the two boards themselves in `docs/esp32-c3-mini.md` and `docs/esp32-c6-supermini.md` |
| `components/ot_frame/` | the OpenTherm frame and the value codecs — pure, host tests |
| `components/ot_decode/` | the receive Manchester state machine — pure, host tests |
| `components/ot_registry/` | lookup over the generated registry; `include/registry_generated.h` is the generator's output, must not be edited |
| `components/ot_state/` | the state model over the registry: value, availability, timestamps. Not `ot_observe` — that one is raw and over all 128 IDs |
| `components/ot_api/` | the projection of the registry and the state into JSON — the only form of output to the outside |
| `components/ot_command/` | a key and a number turned into an OpenTherm frame: writability, the boiler's support, the bounds. Every refusal is decided here, not on the HTTP surface. `ot_command_check()` carries an origin (for the `409`); Home Assistant may write no OpenTherm frame (`OT_CMD_NOT_FOR_HA`), refused in the one entry point after the request itself is judged |
| `components/ot_ticket/` | one-shot tickets for the `/ws` handshake — pure, host tests |
| `components/ot_policy/` | who is allowed what, as a pure function of path, method and "is a password set". Includes the rule that an operation halting the bus always needs a password |
| `components/ot_sensor/` | one room-temperature source: outlier filter, `FRESH`/`STALE`/`NEVER`. Pure. Overdue time is an accumulator, never `now - last_ok`. The foundation of the room-source registry |
| `components/ot_control/` | the executor core: the ladder of states, the watchdog, the bounded failsafe, the boost, the held ID 1. Pure; host suites `test_ot_control*` |
| `components/ot_control_io/` | the executor's translations between `ot_control` and its neighbours: the configuration snapshot, the persist patch, the bus report, the reset-reason restore and the `RTC_NOINIT` record, the synthetic entities, the `GET /api/control` document; the ID 56 readback (a READ-ACK only), the owed ID 56 (`ot_control_io_send()`, ID 1 first) and the heard record (`ot_control_io_heard()`, the DATA-INVALID-on-ID-1 rule). Pure; host suites `test_ot_control_io*` |
| `components/ot_bus_track/` | the bus's write bookkeeping beside `ot_bus_sched`, under the bus's lock: the write generation (a hand write queued while an exchange is on the wire is re-armed, not lost) and the count and value of the ID 1 writes answered (`id1_seq`, `id1_raw`), which the invariant reads. Pure; `test_ot_bus_track` |
| `components/ot_ha/` | the Home Assistant discovery documents and when each is owed (`ot_ha_want`, `ot_ha_plan`): the six-token templates `render_discovery.py` shapes, filled at runtime; WAIT/DROP by support and by discovery on/off. Pure; host suite `test_ot_ha` |
| `components/ot_mqtt/` | what an MQTT message means: the topics, the command's one entry point (`ot_mqtt_handle()`, pure, origin `OT_ORIGIN_HA`, effects injected), the retained-command refusal (`ot_mqtt_decide()`), the state payloads (`ot_api`'s spelling), the publisher's bookkeeping. Does not depend on `ot_ha`. Pure; host suites `test_ot_mqtt`, `test_ot_mqtt_out`, `test_ot_mqtt_control`, `test_ot_mqtt_seam` |
| `components/ot_mqtt_link/` | the impure glue: esp-mqtt, its one owner task, the wiring. Follows `ot_net_broker()` once a second by a 32-bit FNV-1a fingerprint, restarts the client on change, fills `/api/status`'s mqtt block via `ot_mqtt_link_status()`. NOT in `ot_net` — a cycle would not link. No host suite; pinned by `tools/tests/test_source_guards_mqtt.py` |
| `components/ot_room/` | the registry of room-temperature sources: `OT_ROOM_MAX_SLOTS == 4` slots, each its own `ot_sensor`, each `room` or `ambient`. Two independent picks: a **steer** selection (safety-critical — only a `room`-role, FRESH slot) and a **display** selection (room-role preferred, ambient fallback). Pure (`REQUIRES ot_sensor` only), host suite `test/test_ot_room`. Wired in `ot_thermostat` (mailbox glue in `ot_thermostat_room.c`, slots in `ot_thermostat_room_cfg.c`): **slot 0 = the shield DS18B20 (always ambient), slot 1 = the MQTT source (role by config)**. Only MQTT + the shield DS18B20 exist as adapters today; WiFi push/pull, BLE and general 1-Wire are planned |
| `components/ot_time/` | SNTP and the time zone. Validity is "a sync happened", never "`time()` returned something" |
| `components/ot_thermostat/` | the task layer that carries out `ot_control`, and the ONE writer of the ID 0 high byte: the spinlock, the bus (`ot_bus_write_if_idle()` only), NVS, `RTC_NOINIT`. Impure by design, thin on purpose: the decisions it carries out are `ot_control`'s and `ot_control_io`'s, and are host-tested. `ot_thermostat_report.c` is what the task logs and publishes, `ot_thermostat_persist.c` the heat hours and the reset-reason restore |
| `components/ot_led/` | the status LED's health ladder and animation curves: colour from the WiFi→MQTT→HA chain and the failsafe, motion from boiler activity. Pure; host suite `test_ot_led` |
| `components/ot_led_task/` | the status LED's impure glue: samples `ot_net`, `ot_mqtt_link`, `ot_thermostat` and `ot_state` (flame) once a second and drives the WS2812 via `led_strip`/RMT. Reader-only — never writes an OpenTherm frame or takes the bus lock. No host suite; pinned by `tools/tests/test_source_guards_led.py` |
| `components/ot_*/` | one component per responsibility; a component is a directory |
| `tools/` | `opentherm_ids.py` — the single list of entities; `generate_registry.py` — the generator; `registry_model.py`, `render_c.py`, `render_ts.py` and `render_discovery.py` — the generator's model and its C / TS / discovery renderers; `regenerate.py` — the pre-build hook; `tests/` — pytest, including `test_source_guards.py`, `test_source_guards_mqtt.py` and `test_source_guards_led.py` (the source guards), `test_discovery.py` (the HA-kill rules on the rendered documents) and `test_ha_schema.py` (every document handed to Home Assistant's own `DISCOVERY_SCHEMA` — needs `HA_PY`) |
| `components/web_assets/` | the SPA, embedded via `EMBED_FILES`, built at the CMake configure stage |
| `web/` | the SPA on Preact + Vite + TypeScript |
| `test/` | host suites, one directory each |
| `partitions.csv` | 2 × 1984 KiB OTA, no file system — `ota_0` must stay at `0x10000` |
| `../heater-firmware/` | the ComfoAir Q firmware the infrastructure was carried over from; do not touch |
