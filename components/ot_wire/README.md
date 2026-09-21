# ot_wire — the config/provisioning/command wire glue: bytes ⇄ decision, host-testable

`ot_wire` turns request bodies into decisions and internal records into JSON, for the
configuration surface that the HTTP server serves. It is a **pure** component (no socket, no
radio, no ESP-IDF runtime), so every handler that decides *what a body means* can be
exercised on the host instead of only by flashing a board. The HTTP layer next door is left
with "read the body, call this, send what it says".

## Responsibility

**Owns:**

- Parsing and rendering the configuration-surface documents:
  - `GET /api/config` (render the projection) / `POST /api/config` (decode a settings patch)
  - `POST /api/provision` (decode the picked network) / `GET /api/provision` (status document)
  - `GET /api/wifi/scan` (render the networks heard)
- Parsing the command-layer request bodies: an entity write `{"value": …}` and an operation
  body (a flat object of numeric parameters).
- The single spelling of each provisioning state, mode, and MQTT CONNACK refusal code, so
  the REST projection and the MQTT projection cannot invent two different words for the
  same thing.
- Two safety rules, enforced on every function: **no secret is ever rendered** (only the
  redacted `ot_config_public_t` is accepted by the config renderer) and **no submitted
  value is ever quoted in an error** (a message names a FIELD, via a pointer to a literal,
  never its contents — because everything logged is served by `GET /api/log`, which is
  world-readable on the open setup access point).

**Does NOT do:**

- Networking or HTTP. No HTTP server, HTTP handlers, or networking component in `REQUIRES` —
  the moment it could send a response it could answer a request, opening a second door past
  the access policy.
- Value validation. Types and lengths only; what a value may *be* is `ot_config_apply()`'s
  answer, asked once so the two cannot disagree.
- State-model projection. That is `ot_api` (generated from the entity list); this is a
  *different* source of truth (`ot_config`'s `name` column), with a different lifetime.
- Encoding a value into an OpenTherm frame. That is `ot_command`; here bytes become a
  value, there a value becomes a frame — hence `ot_command` is deliberately absent from
  `REQUIRES` (see Implementation).

## Public API (`include/ot_wire.h`)

### Result / error reporting

| Symbol | Contract |
| --- | --- |
| `ot_wire_status_t` | Enum of outcomes: `OK`, `BAD_BODY` (not a flat JSON object; distinct from empty), `BAD_FIELD` (present but wrong type/too long), `REFUSED` (a validator refused; see `err`), `READ_ONLY` (flash holds a schema this build does not understand — nothing is written), `WRONG_ROUTE` (a field belonging to another route, e.g. the Wi-Fi pair at `/api/config`). |
| `ot_wire_result_t` | `{status, err, field}`. `field` is a pointer to a **literal**, never a submitted value. `err` is meaningful only when `status == OT_WIRE_REFUSED`. |
| `ot_wire_strerror(result)` | One owner-facing sentence with no value of any kind in it; safe to log and to send. |

### Provisioning (`POST`/`GET /api/provision`)

| Symbol | Contract |
| --- | --- |
| `ot_wire_parse_provision(body, stored, out)` | Decodes the picked network into an `ot_wifi_t`. `wifi_psk` has three states via `ot_secret_decide()`: absent = KEEP, `""` = CLEAR (open network), else STORE. `stored` (may be NULL) supplies the KEEP value. The `__UNCHANGED__` sentinel submitted as a value with nothing stored is **REFUSED** (it passes `ot_config_check_psk` whole and would strand the device). A kept password paired with a *different* SSID is refused with `OT_CONFIG_ERR_WIFI_PAIR`, so the two routes that write Wi-Fi credentials agree. `out` is written only on `OK`. |
| `ot_wire_provision_t` | Everything the status document is built from; filled by the networking layer under its lock. **No PSK ever, and no SSID but the configured one.** |
| `ot_wire_state_name(state)` / `ot_wire_mode_name(mode)` | The single spelling of a provisioning state / mode. Never NULL; unknown → `"unknown"`. |
| `ot_wire_connack_name(code)` | The single spelling of an MQTT 3.1.1 CONNACK code (0 = "no refusal", 1–5 named; only 4/5 concern credentials). Never NULL; unknown → `"unknown refusal code"`. Takes `uint32_t`, not the esp-mqtt enum, so it builds on the host. |
| `ot_wire_mqtt_t` | Broker connection as `GET /api/status`/`/api/provision` reports it. **Counters only** — no host/user/password. Filled directly by `ot_mqtt_link_status()`; lives here, not in the MQTT link component, so this component stays host-buildable. |
| `ot_wire_device_t` | `{device_id, mac, sw_version}` — borrowed strings from `esp_*` calls `ot_wire` must not make, for the same host-buildability reason. |
| `ot_wire_render_provision(status, mqtt, device, out, cap)` | Renders the status document. Returns bytes written, or **0 = a defect to turn into a 500**, never a short document. `mqtt` and `device` may each be NULL, which **omits** their members (opposite of rendering zeros/empty strings). |

### Configuration (`POST`/`GET /api/config`)

| Symbol | Contract |
| --- | --- |
| `ot_wire_patch_storage_t` | Backing store for the patch's string pointers (NULL in a patch = absent). ~1 KB — must **not** live on a handler stack; holds secrets in the clear until the next parse, so must never reach `ESP_LOG*`. |
| `ot_wire_parse_config(body, out, storage)` | Decodes a settings submission into an `ot_config_patch_t`. Types/lengths only. **Unknown keys ignored** (newer page ↔ older firmware). The two Wi-Fi keys are refused by name (`WRONG_ROUTE`). The five executor-owned keys (`local_ch_enable`, `local_ch_setpoint_dc`, `dhw_enable`, `dhw_setpoint_dc`, `heating_season`) are refused by presence with `OT_CONFIG_ERR_READ_ONLY_FIELD`. Absent ≠ empty for every field. Built into a local patch and copied out only on success (all-or-nothing). |
| `ot_wire_render_config(pub, known_good, out, cap)` | Renders the projection. Accepts only the redacted `ot_config_public_t`, never `ot_config_t`. `known_good` comes from the provisioning machine (`ot_prov_has_known_good`), not the stored SSID. Returns bytes, or **0 = defect**. |

### Wi-Fi scan (`GET /api/wifi/scan`)

| Symbol | Contract |
| --- | --- |
| `ot_wire_network_t` | One heard network: `{ssid, rssi, secure}`. `""` ssid = hidden network (a true thing to report). |
| `ot_wire_render_scan(list, count, out, cap)` | Renders a **top-level JSON array** (possibly empty), one row per BSSID. Duplicates are **not** filtered — presentation's job. |

### Command-layer bodies

| Symbol | Contract |
| --- | --- |
| `ot_wire_value_t` | `{is_bool, boolean, number}`. A boolean is kept separate from a number: `true` and `1.0` are different answers. |
| `ot_wire_params_t` / `OT_WIRE_PARAMS_MAX` (4) | Flat, numeric operation parameters (name in the path). **Deliberately no string parameters** — a type with no caller reads as a supported contract. |
| `ot_wire_param(p, name, out)` | Linear search over the four slots; `false` and `*out` untouched if the parameter is absent. |
| `ot_wire_parse_entity_write(body, out)` | Decodes `{"value": <number|boolean>}`. Boolean tried first and separately. `BAD_BODY` = not a flat object; `BAD_FIELD` = key missing or neither number nor bool. `*out` untouched on failure. Uses `ot_json_f32` so `18.5` is accepted. |
| `ot_wire_parse_operation(body, out)` | Decodes a flat object of numeric params (`{}` for none). Enumerates keys via `ot_json_key_at()`. **Surplus parameters are rejected, not discarded** (`BAD_FIELD`), so an operation is never launched with a subset of what was passed. |

## Implementation

The component is four sources behind one public header, cut along responsibility because a
single file grew past the project's 350-line-per-file ceiling. One responsibility per file:

- **`ot_wire.c`** — the shared sentences and spellings: `ot_wire_strerror()`,
  `ot_wire_state_name()`, `ot_wire_mode_name()`, `ot_wire_connack_name()`, and the scan
  renderer `ot_wire_render_scan()`.
- **`ot_wire_config.c`** — `GET`/`POST /api/config`. Holds the static helpers `read_field()`
  (string fields; MISSING leaves the pointer NULL) and `read_number()` (executor numbers,
  carried as `uint32_t` so out-of-range values reach `ot_config_check_range()` whole), the
  `wrong_route()` constructor, the executor-owned refusal list, the room-MQTT source
  decode (`room_mqtt_enable`/`role`/`stale_s`/`ha_forwarded`), and the full
  `ot_wire_render_config()` projection.
- **`ot_wire_provision.c`** — `POST`/`GET /api/provision`. Holds `ot_wire_parse_provision()`
  (the three-state PSK logic, the KEEP/CLEAR/STORE/REJECT switch, the same-SSID pairing
  check, and validation on the way *in*), `put_duration()` (renders `OT_PROV_NEVER` as
  `null`, not `0`), `put_mqtt()` (the one nested object, **rendered last** because
  `ot_json_check()` stops at the first nested object so flat fields stay reachable), and
  `ot_wire_render_provision()`.
- **`ot_wire_command.c`** — the command bodies: `ot_wire_param()`,
  `ot_wire_parse_entity_write()`, `ot_wire_parse_operation()`.

### Shared internals — `ot_wire_writer.h`

Private to the directory, beside the sources and **NOT in `include/`** (only `include/` is
exposed to other components; moving it would make these helpers public API). All
`static inline`:

- `writer_t` + `put`/`put_u32`/`put_i32`/`put_bool`/`put_string`/`put_key`/`finish` — a
  **bounded append**: on overflow it sets `overflowed`, and `finish()` returns **0**, so a
  truncated body can never be served behind a 200. `put_string()` routes **every** string
  through `ot_json_escape` — SSIDs and device names are attacker/owner-chosen and reach a
  browser as JSON.
- Result constructors `ok()`, `bad_body()`, `bad_field(field)`, `refused(err, field)` —
  kept by name so call sites and comments read unchanged after the cut.

### Invariants / DO NOT

- **No secret is rendered; no submitted value is quoted in an error.** These two rules bind
  every function in every file (top of `ot_wire.c`, the header, and `ot_wire_writer.h`).
- **DO NOT** move `ot_wire_writer.h` into `include/`.
- **DO NOT** add `ot_command` (or any socket-capable component) to `REQUIRES`: while
  `ot_command` was present, the text-parsing `test_wire_command` suite dragged in
  `ot_state`/`ot_registry`/the boiler model and failed on an unresolved `ot_lock`.
  PlatformIO compiles all of a library's sources, so splitting the types across files of the
  *same* component would not have helped — the parsed value/params types therefore live in
  `ot_wire.h` (request bodies are what parsing produces).
- **DO NOT** give `put_mqtt` / the `device` block an "omitted means zero/empty" form: NULL
  (nothing known) and all-zero (looked and found nothing yet) are opposite answers.
- **DO NOT** return the executor-owned keys to `POST /api/config`, even though `GET` still
  renders all five (LOCAL mode must show what it holds); `heating_season` in particular
  could otherwise heat behind Home Assistant's back.

### CMake (`CMakeLists.txt`)

`idf_component_register(SRCS ot_wire.c ot_wire_config.c ot_wire_provision.c
ot_wire_command.c INCLUDE_DIRS include REQUIRES ot_json ot_config ot_provision
ot_secrets)`. **`REQUIRES`, not `PRIV_REQUIRES`**, because the header exposes `ot_wifi_t`,
`ot_config_patch_t`, `ot_config_err_t`, the `ot_prov_*` enums and `OT_SECRET_SENTINEL` in
its own interface.

## Tests

Host suites (one directory each):

- `test/test_wire` — the shared spellings, scan rendering, and config parse/render.
- `test/test_wire_provision` — `ot_wire_parse_provision()` and the provision status document.
- `test/test_wire_status` — the status/MQTT/device rendering.
- `test/test_wire_command` — the command bodies (a text-parsing suite that linking pressure
  kept free of `ot_command`).

## Notes

- **Two lists, on purpose.** `ot_api` projects the generated state model; `ot_wire` is the
  configuration surface (`ot_config`'s `name` column, different source of truth and
  lifetime). They deliberately share only the escaper in `ot_json`.
- **One spelling, everywhere.** State/mode/CONNACK names are decided here so REST and MQTT
  cannot drift. `ot_prov_failure_name()` (owned by `ot_provision`) is used for `failure`,
  for the same reason.
- **The `__UNCHANGED__` sentinel disappearing act.** The sentinel is 13 printable bytes
  that pass `ot_config_check_psk` whole; written through, it leaves credentials that cannot
  associate, no access point comes back up, and the device vanishes. Refusing it is why
  `ot_wire_parse_provision` exists rather than the handler doing it inline.
- **`last_connack` + `last_connack_reason`.** Rendered together (number to look up, sentence
  to read) because an owner who mistyped a broker password would otherwise be told "broker
  unreachable" every ten seconds, with the real refusal reachable only over a serial cable.
- **Room-MQTT settings** are plain `NS_APP` fields (not executor-owned), so they travel
  through `/api/config` like any other setting; without the decode in `ot_wire_config.c`
  they were plumbed to `ot_config_apply()` but unreachable from the wire.
