# Testing

Three test layers, none of which needs hardware, plus the source guards that pin the glue no
suite can compile. This page lists every suite, says what the host build is and is not able to
catch, and names the gates a change passes before it is merged.

| Layer | Command | What it covers |
| --- | --- | --- |
| Host suites (C/C++, Unity) | `pio test -e native` | the protocol, the state model, the executor core, the command layer, the room sources, JSON, configuration, MQTT/HA decisions, provisioning, the LED ladder — one suite per directory under `test/` |
| Python | `python3 -m pytest tools/tests -q` | the registry generator, the discovery documents, the i18n name overlay, and the source guards |
| Web | `cd web && npm test` **and** `npm run build` | the SPA's models, parsers, i18n catalogs and drawing maths |

A green run of all three is not readiness. Anything the owner must check on real hardware is a
scenario under [Hardware verification](/hardware-verification/).

## Test first

The protocol, the state, the executor core, the room-source filter, JSON and the command layer
depend neither on hardware nor on a framework, so they are tested on the host — and the test is
written **before** the implementation. Expected values come from the specification, the boiler
reference and captured frames, never from the code under test: a test written from the code it
tests proves only that the code equals itself.

The pattern throughout the tree is a pure half beside a thin impure half: `ot_control` and
`ot_control_io` beside the `ot_thermostat` task, `ot_bus_sched` and `ot_bus_track` beside the
`ot_bus` task, `ot_mqtt` and `ot_ha` beside `ot_mqtt_link`, `ot_led` beside `ot_led_task`. What is
left in the impure half is what cannot be moved, and the calls that tie the halves together are
pinned by the [source guards](#source-guards).

## Host suites

```sh
pio test -e native                    # every suite
pio test -e native -f test_ot_decode  # one suite
```

`pio test -e native` is the barrier for any protocol, state, executor, command or configuration
change.

### One directory, one suite

PlatformIO links every `.cpp` file of a test directory into **one binary**, so two files with a
`main()` in one directory collide. A suite is therefore a directory, and the rule for a suite
that grows past the 600-line ceiling is to cut it into a new directory, not a new file beside the
old one. Several suites below were made that way: `test_config_apply_broker` and `test_config_merge`
(out of `test_config_apply`), `test_ot_control_io_state` and `test_ot_control_io_send` (out of
`test_ot_control_io`), `test_ot_command_check` (beside `test_ot_command`).

### `[env:native]`

- runs every directory matching `test_*` (`test_filter`) with the Unity framework;
- excludes `src/` (`build_src_filter = -<*>`): the firmware entry point does not build on the
  host;
- sets `lib_extra_dirs = components`. The components are ESP-IDF components and PlatformIO
  libraries at once, so the host suites compile **the very same sources** the device runs;
- adds the `include/` directories of the components the suites reach with explicit `-I` flags;
- builds with `-std=c++17 -Wall -Wextra -pthread -Wno-missing-field-initializers` and promotes
  exactly one warning to an error, `-Werror=switch`.

#### Why `-Werror=switch`

ESP-IDF compiles components for the board without `-Wall` (its `build.cmake` has it commented
out), so the host build is the only place where an enumerator appended without its `case` fails.
`ot_config_err_name()` and `ot_config_strerror()` rely on it: a new configuration error code that
nobody named would otherwise reach the owner as "unknown". **DO NOT** drop the flag to silence a
failure; name the new code.

The flag guards only what the host builds. `ot_http` is not host-built, so its switches end in an
explicit 500 for a code they do not know (`components/ot_http/ot_http_control.c` says so beside
the switch) rather than relying on the compiler.

#### What a suite pulls in

PlatformIO's library dependency finder reaches a library through the suite's **own** `#include`,
not through another library's header, so every pure component a suite links is named by an
include in that suite (`test_ot_mqtt` and `test_ot_mqtt_out` say so in their headers). The
reverse also holds: one include too many drags a FreeRTOS-bound component into the host build.
`test_ot_thermostat_room` includes only `ot_thermostat_room_cfg.h`, never
`ot_thermostat_internal.h`, which would pull in `ot_bus.h` and through it `ot_master`, `ot_net`
and `ot_api`.

The board environments set `test_ignore = *`, so no host suite is ever built for a device.

### Fakes are never shared

A fake belongs to the one suite that needs it and lives inside that suite's directory. **DO NOT**
move a fake into a shared directory: the moment two suites share it, "it grew a capability for
suite B" becomes a way to quietly weaken suite A. The recording fakes of `test_ot_mqtt` (the
executor and the bus, so every call and argument is visible) are local to it, and
`test_ot_mqtt_control` drives the real `ot_control` instead.

There is no fake ESP-IDF in the tree. `ot_master` — the timer-and-GPIO conversation on the
wire — has no host suite; its pure neighbours do (`test_ot_frame`, `test_ot_encode`,
`test_ot_decode`), and what it does on the wire is a hardware scenario.

### The suites

#### The OpenTherm wire

| Suite | Purpose |
| --- | --- |
| `test_ot_frame` | the 32-bit frame and its parity, assembly and the value codecs |
| `test_ot_encode` | the Manchester encoding of a frame: start and stop bits, every half-bit pair a transition, MSB first |
| `test_ot_decode` | the receive Manchester state machine against generated sample streams, at the edges of the timing tolerance |
| `test_ot_bus_sched` | the conversation schedule: the first step talks at once, ID 0 on every other slot, the poll ring, the minimum gap |
| `test_ot_bus_track` | the bus's write bookkeeping driven through the real scheduler: the write generation and the ID 1 write count |
| `test_ot_observe` | the raw observation table over all 128 IDs, including UNKNOWN-DATAID answers |

#### State, registry and commands

| Suite | Purpose |
| --- | --- |
| `test_registry` | lookup over the generated registry |
| `test_state` | the state model: READ-ACK lands decoded, DATA-INVALID is not a value, UNKNOWN-DATAID is not DATA-INVALID |
| `test_api` | the JSON projection: snprintf semantics, `null` for a value that never arrived, availability |
| `test_ot_command` | a key and a number turned into a frame: writability, the boiler's own bounds over the table's, support |
| `test_ot_command_check` | `ot_command_check()`: every refusal of every writer, with an origin, against the real `ot_control` |
| `test_wire_command` | the body of an entity write: a number is not a boolean |

#### The executor

| Suite | Purpose |
| --- | --- |
| `test_ot_control` | the ladder of states: every boundary between adjacent rows, every entry into HA ownership |
| `test_ot_control_boost` | the boost as a ladder row: validation, expiry, the eight-hour cap |
| `test_ot_control_bus` | what goes on the bus: the held ID 1 and its invariant, the re-send cadence, the DHW bit, the ID 56 reconciliation |
| `test_ot_control_check` | ownership and bounds of every command, the answer of `apply()`, the wire spellings |
| `test_ot_control_failsafe` | the watchdog and the bounded failsafe: the accumulator and its restore, the summer bounds, hysteresis, the counters |
| `test_ot_control_io` | the inbound translations: the configuration snapshot, the persist patch, the bus report, the ID 56 readback |
| `test_ot_control_io_send` | the executor's own writes: ID 1 before ID 56, and the ID 56 a busy bus refused, owed until a slot is idle |
| `test_ot_control_io_state` | the outbound translations: what survives a reset, the synthetic entities, the `GET /api/control` document |

#### Room sources

| Suite | Purpose |
| --- | --- |
| `test_ot_sensor` | one source: the outlier filter and `FRESH` / `STALE` / `NEVER` |
| `test_ot_room` | the slot registry: an ambient-only source never steers; the steer and display picks |
| `test_ot_onewire` | the 1-Wire CRC-8 and the DS18B20 scratchpad check |
| `test_ot_thermostat_room` | the one pure piece of the thermostat's room glue: configuration to `ot_room` slots |

#### MQTT and Home Assistant

| Suite | Purpose |
| --- | --- |
| `test_ot_mqtt` | what an arriving message means and what an accepted command makes happen, against recording fakes |
| `test_ot_mqtt_control` | MQTT through the real `ot_command_check()` and `ot_control`: a retained command does not feed the watchdog |
| `test_ot_mqtt_out` | what goes out: the topic tree, the state payloads, the owner topic, `control_state`'s attributes |
| `test_ot_mqtt_seam` | four probes no other suite combines, among them a restored reboot with a retained command, and every writable frame row refused to HA |
| `test_ot_ha` | `ot_ha_render()` and `ot_ha_topic()`: the six tokens filled, and the three discovery rules only the runtime can break |
| `test_ot_ha_plan` | when a discovery document is owed: unsupported IDs skipped, bounds re-published on change, everything on HA's `online` |

#### Web surface, configuration and security

| Suite | Purpose |
| --- | --- |
| `test_http_policy` | who may do what, as a pure function of path, method and "is a password set" |
| `test_auth` | parsing an `Authorization` header from attacker-controlled input |
| `test_secrets` | write-only secrets: the sentinel out, keep / clear / store / reject back |
| `test_json` | the reader that decodes request bodies |
| `test_ticket` | the one-shot `/ws` tickets: single use, expiry at the edge |
| `test_log` | the in-memory ring log: bounded, non-blocking, valid JSON for any text |
| `test_wire` | `POST` / `GET /api/config`: what may be sent, what is returned, no secret ever rendered |
| `test_wire_provision` | `POST /api/provision`, the body typed in on the open access point |
| `test_wire_status` | the Wi-Fi scan and the provisioning state documents |
| `test_config_apply` | applying a settings document: one bad field changes nothing, the Wi-Fi pair, power cuts |
| `test_config_apply_broker` | the broker host and its password as a pair: a new host never carries the old password |
| `test_config_merge` | the cross-field rules, judged on the merged document |
| `test_config_check` | single-field validation: SSID and key lengths, embedded NULs, open networks |
| `test_config_names` | the stored NVS names, pinned, and their length limits |
| `test_config_project` | the defaults and the projection a client is handed |
| `test_config_record` | the UI password record: salted, its iteration cost, a record that does not parse lets nobody in |
| `test_config_sanitize` | a record read back from flash: garbage comes back usable, a wrecked network record raises the access point |
| `test_provision` | the provisioning state machine, driven through failure scenarios with time as a parameter |
| `test_captive` | the captive-portal probe answers, byte for byte, and the captive DNS decisions |

#### Indication

| Suite | Purpose |
| --- | --- |
| `test_ot_led` | the status LED's health ladder and animation curves |

## Python tests

```sh
python3 -m pytest tools/tests -q
```

`tools/tests/conftest.py` loads `tools/` modules by path on a bare interpreter — no PlatformIO, no
firmware build.

| File | Purpose |
| --- | --- |
| `test_registry.py` | written against the table, not the output: no duplicate keys, IDs in range, the poll ring (no ID 0, no write-only ID, at most `OT_BUS_MAX_POLL` = 32), no string that needs JSON escaping, every entity in both outputs exactly once, the committed files equal the render |
| `test_virtuals.py` | the synthetic entities: no Data-ID, never in the poll ring, the contracted executor rows exactly, option order equals the executor's enum order |
| `test_entity_names_i18n.py` | the DE/NL/UK overlay covers exactly the registry, and the generator refuses a mismatch |
| `test_discovery.py` | the rendered discovery documents against the rules that kill an HA entity silently |
| `test_ha_schema.py` | every discovery document handed to Home Assistant's own `DISCOVERY_SCHEMA` |
| `test_source_guards.py` | source guards over `ot_bus.c`, `ot_thermostat`'s task and room glue, the `ds18b20` task in `src/main.cpp`, and the `Host` read in `ot_http_gate.c` |
| `test_source_guards_mqtt.py` | source guards over the `ot_mqtt_link` / `src/main.cpp` glue |
| `test_source_guards_led.py` | source guards over the `ot_led_task` / `src/main.cpp` glue |
| `test_source_guards_http.py` | source guards over `ot_http_ops.c`: the line test's parameter checks |

### Source guards

FreeRTOS glue has no host suite. What ties it to its pure, tested half is a **call**, and a call
replaced by its obvious neighbour compiles, links and passes every suite while undoing the fix it
carried. The guards read the sources with comments stripped (a comment that names a call is not
one) and pin the calls; each also asserts that the call it expects **is** there, so a rename
cannot make a guard pass by finding nothing. Examples:

- the thermostat queues with `ot_bus_write_if_idle()`, never `ot_bus_write()`, which would evict
  a pending hand write;
- `ot_bus.c` reaches the write queue only through `ot_bus_track_*()`;
- the thermostat tick saves the whole heat hour to NVS before mirroring the part-hour to
  `RTC_NOINIT`, and sets the room input from `ot_room`'s steer pick; the `ds18b20` task in
  `src/main.cpp` hands its reading to `ot_thermostat_room_submit()`;
- the HTTP gate treats a `Host` header too long for its buffer (`ESP_ERR_HTTPD_RESULT_TRUNC`) as
  foreign and refuses it, rather than as absent, which would fail open;
- the line-test handler checks its duration and half-period in the form that survives the cast
  into the bus, so a value the bus would refuse answers `422`, not a misleading `409`;
- no command reaches `ot_command_check()` or the executor past `ot_mqtt_handle()`, and the retain
  flag is handed to `ot_mqtt_decide()`;
- the MQTT glue never reboots (`esp_restart`), takes the state model's lock or disables reconnect,
  and logs no secret;
- the LED glue converts the network state with an explicit `switch`, checks `OT_AVAIL_OK` before
  reading `flame`, and never touches a bus write or the bus lock.

### The Home Assistant schema check

`test_ha_schema.py` needs a Python with `homeassistant` installed; on a plain interpreter it is
skipped (`pytest.importorskip`), which is the ordinary `tools/tests` gate. Run it with that
interpreter, named `HA_PY` (on the owner's machine, the Home Assistant virtualenv beside this
repository):

```sh
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 "$HA_PY" -m pytest tools/tests -q
```

Home Assistant's own pytest plugin breaks plain tests, so **always** run pytest with
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1` under an interpreter that has HA. An entity HA refuses produces
no error anywhere — it simply never appears — so `test_discovery.py` (each rule stated by hand) and
this check (HA asked directly) are the only witnesses short of the owner's Home Assistant. See
[Home Assistant discovery](/interfaces/home-assistant-discovery).

## Web tests

```sh
cd web
npm test                # every *.test.ts under web/src
npm run build           # tsc -b and the Vite build: type-checks the suites, runs none
npm run test:mock       # the dev mock's own suites, under web/mock/tests
npm run typecheck:mock  # tsc --noEmit over web/mock/
```

The suites are plain `.ts` files run by Node's own type stripping, which is why the web side needs
Node 22.6 or newer: on an older Node every suite dies with a syntax error and the runner reports
`no tally`. `web/scripts/test.mjs` finds every `*.test.ts` under `web/src` itself — a new suite
runs without being listed anywhere — runs each in its own Node process, and checks the
`<suite>: N/M passed` tally each one prints, not only the exit status, so a suite that crashes or
swallows its own failure still counts as failed. The suites sit beside what they test:
`src/i18n/tests/` (catalogs, the generated entity names, firmware text, formatting, tokens) and
`tests/` under `src/pages/boiler`, `control`, `log` and `settings`.

The web gate is **both** `npm test` and `npm run build`; `tsc --noEmit` is not the gate. The test
run is deliberately not folded into the build: every firmware build runs `npm run build`, and a red
web suite must not look like a broken firmware build. The mock suites are separate again: the mock
is a developer convenience, and a red mock suite must not read as a broken SPA.

## The gates before a merge

| Gate | Command | When |
| --- | --- | --- |
| Host suites | `pio test -e native` | every change; the barrier for protocol, state, executor, command and config |
| Both boards build | `pio run -e lolin_c3_mini` and `pio run -e supermini_c6`, judged by exit code | every firmware change |
| Generator and guards | `python3 -m pytest tools/tests -q` | every change to `tools/`, and to glue a guard reads |
| HA schema | `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 "$HA_PY" -m pytest tools/tests -q` | every change to the registry or discovery |
| Web | `cd web && npm test` and `cd web && npm run build` | every change under `web/`, and every registry change (`entities.ts` is an input) |
| Docs | `cd docs && npm run docs:build` | every change under `docs/` |

**Verification is output, not an assertion.** A claim that the tests pass comes with the output
that shows it.

## Hardware verification

A successful build is not readiness. Everything the owner is obliged to check on a real boiler —
the watchdog and failsafe after Home Assistant stops, the summer bound, broker and Wi-Fi outages
with zero reboots, resets with the boiler running — is a scenario under
[Hardware verification](/hardware-verification/), grouped by what the device does, starting with
setup and safety.
