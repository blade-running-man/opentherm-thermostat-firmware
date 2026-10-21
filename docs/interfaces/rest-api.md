# REST API

The device serves a small JSON API over plain HTTP on port 80. It is the same API the built-in
web UI uses — there is no private endpoint (see [Web UI](/interfaces/web-ui)) — and its writes into
the controls are judged by the same one function MQTT uses (see [Commands](/protocol/commands)).
Configuration, provisioning, the raw bus view and the operations are REST only.

Every route is registered in one table in `ot_http_start()` (`components/ot_http/ot_http.c`). The
handlers live beside it, one file per addressee: `ot_http_config.c` (log, status, config, scan,
provisioning), `ot_http_registry.c` (state, entities, entity writes), `ot_http_control.c` (the
executor document), `ot_http_ops.c` (operations), `ot_http_boiler.c` (the raw bus view),
`ot_http_ws.c` (the ticket and `/ws`) and `ot_http_static.c` (the SPA and the captive-portal
probes). Every handler calls the one access gate, `ot_http_allowed()` (`ot_http_gate.c`), before it
does anything else. Two paths do not: the captive-portal probes are answered with fixed bytes before
the gate, and the `/ws` handshake is answered by `esp_http_server` itself and checked by a ticket
instead (see [WebSocket](/interfaces/websocket)).

::: tip Conventions on this page
`DEVICE` in the examples is the device's IP address. There is no mDNS name: nothing registers one,
and the Host check below would refuse it anyway. Every JSON response is
`Content-Type: application/json`. No `Access-Control-Allow-Origin` header is ever sent: the UI is
served from the device itself, so there is no cross-origin case, and a page on another origin cannot
read the device's state.
:::

## Route table

| Method | Path | Handler | What it does |
| --- | --- | --- | --- |
| GET | `/api/log` | `ot_http_log_get` | The device's 80-line log ring |
| GET | `/api/status` | `ot_http_status_get` | Provisioning state, network, identity, the MQTT block |
| GET | `/api/config` | `ot_http_config_get` | Stored settings, secrets redacted |
| POST | `/api/config` | `ot_http_config_post` | Change stored settings |
| GET | `/api/wifi/scan` | `ot_http_wifi_scan_get` | Networks in radio range |
| POST | `/api/provision` | `ot_http_provision_post` | Hand the device Wi-Fi credentials |
| GET | `/api/ot/raw` | `ot_http_ot_raw_get` | The raw bus observation over all 128 Data-IDs |
| GET | `/api/control` | `ot_http_control_get` | The executor's live document |
| POST | `/api/ops/{name}` | `ot_http_op_post` | Run one operation: `scan`, `linetest`, `boost`, `boost_off` |
| GET | `/api/state` | `ot_http_state_get` | Every entity's value, availability and age |
| GET | `/api/entities` | `ot_http_entities_get` | The entity registry with metadata |
| GET | `/api/entities/{key}` | `ot_http_entities_get` | One entity: metadata and value |
| POST | `/api/entities/{key}` | `ot_http_entity_post` | Write one entity |
| POST | `/api/ws-ticket` | `ot_http_ws_ticket_post` | A one-time ticket for `/ws` |
| GET | `/ws` | `ot_http_ws_handler` | The push channel — see [WebSocket](/interfaces/websocket) |
| POST, PUT, DELETE, PATCH | `/*` | `write_gate` | Catch-all for writes, see [below](#the-write-gate-and-the-fallbacks) |
| GET | `/*` | `ot_http_captive_probe_get` | Captive-portal probes, then the SPA assets and shell |

`esp_http_server` matches routes in registration order with wildcard matching
(`httpd_uri_match_wildcard`), which is why every concrete route sits above the `/*` catch-alls.
`/api/entities` needs two entries for one handler: the matcher compares the bare path against the
template without its asterisk (`/api/entities/`), so without the second entry the list would
answer 404. `max_uri_handlers` is derived from the size of the table, so adding a route cannot
overflow the server's handler limit; a route that fails to register is logged and answers 404 — it
never aborts the boot.

The query string, a `#fragment`, an absolute-form `http://host` prefix and a trailing slash (on
anything but `/`) are stripped before the path is matched by the access policy or parsed by a
handler (`ot_http_request_path()`), so `/api/config/` and `/api/config?x=1` are judged as
`/api/config`, and `/api/entities/ch_enable?t=1` addresses the key `ch_enable`.

## Before any handler: the access gate

`ot_http_allowed()` runs four checks in a fixed order; the first refusal answers and ends the
request.

| Order | Check | Refusal |
| --- | --- | --- |
| 1 | **Host allowlist** (`ot_http_host_ok()`, the DNS-rebinding guard), every method | `403 {"error":"forbidden"}` |
| 2 | **Content-Type** (`ot_http_content_type_ok()`, the CSRF guard), every non-GET request with a body | `415 {"error":"unsupported media type"}` |
| 3 | **Credentials** (`ot_auth_check()`), HTTP Basic | a wrong password costs 1 s |
| 4 | **Policy** (`ot_http_check()`): method, path, `provisioned`, `password_set`, `authenticated` | `401` or `403` |

**The Host allowlist.** A request passes when its `Host` (any `:port` stripped) is the device's
current station IP, or `192.168.4.1` while the setup access point is on the air. It also passes,
deliberately, when there is no `Host` header or the device does not yet know its own address — a
lockout of the owner is the worse failure. A `Host` too long for the 64-byte read buffer is refused
rather than treated as absent. A DNS-rebinding page can make its origin resolve to the device, but
the browser still sends the attacker's name in `Host`.

**The Content-Type gate.** A write with a body must say `application/json` (case-insensitive,
optionally `; charset=…`; `application/json-patch+json` does not pass). That forces a CORS preflight
on any cross-origin caller, which the device never answers, and a bare HTML form cannot send JSON at
all. A body-less POST (`/api/ws-ticket`, an operation with no parameters) is not checked.

### Authentication

**Authentication is HTTP Basic**, realm `opentherm`. There are no accounts: the user name is
ignored, but the decoded credential must contain a colon (`anything:password`); everything after
the first colon is the password. It is the owner's UI password, set through `POST /api/config` as
`ui_password`; only a PBKDF2 hash is stored, and the comparison happens inside `ot_net`, so the
stored record never reaches the HTTP layer. Only `OT_AUTH_GRANTED` counts as authenticated.

When a password is set and a wrong or malformed `Authorization` header arrives, the handler sleeps
`OT_AUTH_FAIL_DELAY_MS` (1000 ms) before answering. There is no lockout — that would be a denial of
service against the owner, who is the person most likely to mistype. Because one task serves every
socket, the delay is global. Do not retry a `401` automatically.

### Who may do what

| Request | No UI password set | UI password set |
| --- | --- | --- |
| Any GET | allowed | needs auth, else `401` |
| POST `/api/config`, device provisioned | allowed | needs auth |
| POST `/api/entities/{key}`, `/api/ops/{name}`, device provisioned | allowed | needs auth |
| POST `/api/ops/linetest` (halts the bus), device provisioned | `403` | needs auth |
| POST `/api/ws-ticket`, device provisioned | allowed | needs auth |
| POST `/api/provision`, device **unprovisioned** | allowed | needs auth |
| Any other POST, device provisioned (`/api/provision` included) | `403` | needs auth |
| Any other POST, device unprovisioned | `403` | `403` |
| PUT, DELETE, PATCH | `403` | `403` |

- **Reading is open until a password exists; writing is refused until one exists**, with four
  exemptions: setting the first password (`/api/config`), the controls (`/api/entities/*`), the
  operations (`/api/ops/*`) and the socket ticket (`/api/ws-ticket` — `/ws` carries nothing
  `GET /api/state` does not). The password is optional and off by default, and a device without one
  is a supported configuration, so anybody on the LAN can change the controls of such a device.
- **An operation that halts the bus always needs a password.** `ot_http_check_op()` is asked after
  the operation is looked up, because the flag belongs to the operation, not the path. With no
  password set the answer is `403 {"error":"operation halts the boiler conversation; set a password
  first"}`: the boiler reads master silence longer than 5 s as a shorted thermostat and demands
  heat, so a caller looping the line test would hold the boiler hot indefinitely.
- **Unprovisioned** means "no network joined, or the open setup access point on the air"
  (`ot_net_is_provisioned()`, asked fresh on every request): stored credentials alone do not make the
  device claimed while the access point is up. The one write accepted there is
  `POST /api/provision` — and once a password exists even that needs it, so a neighbour cannot
  re-home an owned device whose router has died. Note that `POST /api/ws-ticket` is refused there
  too, so a page on the setup access point has no live channel.
- **`401` and `403` are different answers.** `401` carries `WWW-Authenticate: Basic
  realm="opentherm"` and `{"error":"unauthorized"}`: a password exists and was not presented.
  `403 {"error":"forbidden"}`: no credential could make the request acceptable in this state.
- The command prefixes (`path_under()` in `ot_policy`) need a trailing slash and something after
  it, so `/api/entities-x` does not inherit the exemption and bare `/api/ops` is not a command.

```sh
curl -u :hunter2hunter2 http://DEVICE/api/state   # read, password set
curl -i -u :wrong http://DEVICE/api/state         # 401 after a one-second delay
```

See [Security](/architecture/security) for the threat model behind these rules.

### Errors and bodies

Every refusal a handler writes is `{"error":"<sentence>"}`; `POST /api/config` and
`POST /api/provision` add `"field"`. The sentence is always a literal from the firmware, never a
value the client submitted, because refusals are logged and `GET /api/log` hands the log to anyone
who can reach the device — on the setup access point, that is everybody. The few answers made with
`httpd_resp_send_err()` (`404 no such entity`, the `500 … does not fit` defects) carry ESP-IDF's
plain-text body instead of JSON.

A request body is read whole into a 2048-byte buffer (`OT_HTTP_BODY_MAX`). A body of 2048 bytes or
more — and an empty body on a route that needs one — is `413 {"error":"body too large"}`; a
connection that ends before `Content-Length` is `400 {"error":"truncated body"}`.

## Reading the entities

### `GET /api/state`

Every entity's value in one document, rendered under one lock so every value belongs to the same
moment:

```json
{"schema":1,"state":{
  "ch_setpoint":{"availability":"ok","value":45.00,"age_ms":812},
  "dhw_setpoint":{"availability":"unsupported","value":null,"age_ms":null},
  "control_state":{"availability":"ok","value":"ha","age_ms":120}}}
```

| Field | Meaning |
| --- | --- |
| `schema` | The registry's shape version (`OT_SCHEMA_VERSION`) |
| `availability` | `ok`; `invalid` (DATA-INVALID); `unsupported` (UNKNOWN-DATAID); `unknown` (never heard) |
| `value` | `null` unless `ok`; a boolean for a binary or switch entity; the option string for an enum; otherwise a number with two decimals |
| `age_ms` | Milliseconds since the value was last updated, `null` if never |

The value spelling is decided in one function, `emit_bare_value()` in
`components/ot_api/ot_api.c`, shared by this route, the `/ws` frames and the MQTT state payloads, so
the three can never disagree. `null` is "no data", never zero: zero would claim "0 °C" where the
boiler said nothing. Every key is listed on [Entities](/reference/entities).

### `GET /api/entities` and `GET /api/entities/{key}`

The list carries the registry metadata and no values:

```json
{"schema":1,"entities":[
  {"key":"ch_setpoint","name":"CH setpoint","data_id":1,"writable":true,"unit":"°C",
   "device_class":"temperature","min":10.00,"max":90.00}, …]}
```

`data_id` is `null` for a synthetic entity (an executor input or output that never travels on the
wire). `unit`, `device_class`, `entity_category` and `options` (an enum's option strings) appear only
when the entity has them. `min`/`max` appear only for a bounded entity, and they are the **live**
bounds: `ot_state` substitutes the boiler's own once it has answered for them, so a client re-reads
them rather than caching them per build. Names are the canonical English ones; the web UI's
translations are a web-only overlay.

One key answers `{"meta":{…},"value":{"availability":…,"value":…,"age_ms":…}}`. An unknown key is
`404` — never an empty document, which a client would read as an entity with no value.

## Writing an entity: `POST /api/entities/{key}`

```sh
curl -X POST http://DEVICE/api/entities/ch_setpoint \
  -H 'Content-Type: application/json' -d '{"value": 45.5}'
```

The body is `{"value": <number or boolean>}`; a boolean is carried as `1`/`0`. The handler holds no
rule of its own: every refusal is `ot_command_check()`'s, with origin `OT_ORIGIN_WEB`, in the one
order stated in `ot_command.h` (see [Commands](/protocol/commands)). The surface only names each
answer:

| Status | When |
| --- | --- |
| `202 {"queued":{"data_id":N,"raw":N}}` | A frame row (an OpenTherm write such as `max_relative_modulation`): the frame was queued for the bus |
| `202 {"applied":{"command":N}}` | An executor command — `ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`, `heating_season` — accepted and stored |
| `400` | The body is not `{"value": …}` holding a number or a boolean |
| `404` | No such key, or the path names none (`/api/entities/`) |
| `405` | The entity is read-only, or its codec is half a word: it can never be written, by anyone |
| `409` | The boiler answered UNKNOWN-DATAID for this ID (never asked for `ch_setpoint`); the control belongs to Home Assistant (`control_mode` 1); the store was written by a newer firmware |
| `422` | The value is out of bounds: the boiler's, the executor's `flow_min_dc..flow_max_dc`, or a switch that is not 0/1 |
| `503` | The thermostat task failed to start at boot; nothing would carry the command out |
| `500` | The store refused a command the executor accepted, or a verdict this build does not know |

Why `202` and not `200`: the bus queue holds **one** write and a newer one evicts an unsent one (a
stale setpoint is worse than a lost one), so at the moment of the reply nobody knows the boiler took
it. An executor command is stored and carried out on the executor's next step; **no value is
echoed**, because the executor quantises a CH setpoint to 0.5 °C before it persists it. The kept
value is read from `GET /api/config`, the effect from `GET /api/control`. `command` is the executor
command number: 1 CH enable, 2 CH setpoint, 3 DHW enable, 4 DHW setpoint, 5 heating season.

Ownership is answered twice: early by `ot_command_check()`, and finally by `ot_control_apply()`
against the mode the executor last observed, in case the mode flipped in between. Both read the
same sentence. The modes themselves are on [Executor](/architecture/executor).

## The executor: `GET /api/control`

The executor's live document, rendered by `ot_api_render_control()`
(`components/ot_api/ot_api_control.c`) with its own shape version, separate from the registry's:

```json
{"schema":1,"mode":"local","state":"boost","reason":"none","cause":"none",
 "heating_season":true,"status_high":3,"held_setpoint_dc":500,
 "dhw":{"enable":true,"setpoint_dc":505},
 "boost":{"active":true,"setpoint_dc":500,"remaining_s":3540},
 "failsafe":{"count":2,"last_duration_s":61},"watchdog_overdue_s":7,"stack_hwm":1184}
```

| Field | Meaning |
| --- | --- |
| `mode` | `local` or `ha` — who owns the controls |
| `state` | The ladder row: `season_off`, `boost`, `local`, `ha_waiting`, `failsafe`, `ha` |
| `reason` | What the CH bit is doing: `none`, `watchdog`, `ha_blind`, `fs_disarmed`, `fs_blind`, `fs_room_cold`, `fs_room_warm`, `min_cycle`, `await_setpoint` |
| `cause` | Why the state is `failsafe` (same words); `none` in every other state |
| `status_high` | The ID 0 high byte last handed to the bus — asked, not confirmed by the boiler |
| `held_setpoint_dc` | The CH setpoint held on Data-ID 1, tenths of a degree, always valid |
| `dhw.setpoint_dc` | `null` while unset |
| `boost` | `setpoint_dc` and `remaining_s` are `null` unless a boost runs |
| `failsafe` | Engagements since this boot, and the last one's length |
| `watchdog_overdue_s` | How long the Home Assistant watchdog has gone unfed |
| `stack_hwm` | Bytes of the thermostat task's stack never used; `null` until measured |

Temperatures are integer tenths (`_dc`), so no float is ever formatted. An absent value is `null`,
never `0`: the leftovers of the last boost printed as current would describe a boost nobody started.

## Operations: `POST /api/ops/{name}`

One route for every operation; the name is looked up in the table in `ot_http_ops.c`, so a new
operation needs a table row and no new route. The body is a flat object of numeric parameters, or
no body at all for the defaults. The parser (`ot_wire_parse_operation()`) knows no parameter names:
it refuses a fifth parameter (`OT_WIRE_PARAMS_MAX` is 4) — so an operation never runs with four of
the five values passed — a name longer than 23 characters, and a value that is not a number. Each
operation then reads only the names it uses and ignores the rest: `{"setpoint":50,"minutes":60,
"duration":5}` is a `202` boost, and a misspelt name is simply absent — `scan` and `linetest` run on
their defaults, `boost` answers `422` for the missing parameter.

| Name | Parameters | Halts the bus | What it does |
| --- | --- | --- | --- |
| `scan` | `from` (0), `to` (127) | no | Sweep the Data-ID range; the mandatory ID 0 keeps going out every other step. Progress in `GET /api/ot/raw` (`scan_done`/`scan_total`) |
| `linetest` | `duration_ms` (20000, at most 30000), `half_period_ms` (2000, 100..5000) | **yes** | Toggle the line for a multimeter instead of talking to the boiler |
| `boost` | `setpoint` (°C) and `minutes` (1..480), both required | no | A timed heat boost, row 2 of the executor's ladder |
| `boost_off` | — | no | End a boost; idempotent |

```sh
curl -X POST http://DEVICE/api/ops/boost -H 'Content-Type: application/json' \
  -d '{"setpoint": 50, "minutes": 60}'
```

| Status | When |
| --- | --- |
| `202 {"running":true}` | Queued — the sweep takes minutes, the line test seconds; nobody has a result yet |
| `400` | The body is not a flat JSON object, or has more than four parameters, an over-long name or a non-numeric value |
| `403` | `linetest` on a device with no password (see above) |
| `404 {"error":"no such operation"}` | The name is not in the table (the submitted name is never echoed) |
| `409` | A line test already running; a boost in HA mode; a boost with `heating_season` off |
| `422` | A sweep range outside 0..127; a line-test duration or half-period outside its limits; a boost without both parameters, with minutes that are not a whole number 1..480, or a setpoint outside `flow_min_dc..flow_max_dc` |
| `503` | A boost on a device whose thermostat task did not start |

The boost's answers come from the same `ot_http_control_refusal()` the entity route uses, so one
verdict cannot read differently on the two routes. The line-test limits are taken from `ot_bus.h` and
checked first, so a `409` means exactly one thing: a test is already running.

## The raw bus: `GET /api/ot/raw`

Diagnostics, not the state model: what the boiler last answered for each Data-ID, with no names and
no units — those come only from the generated registry.

```json
{"uptime_ms":812345,"cycles":8123,"ok":8100,"failed":23,"overdue":0,"answering":true,
 "in_duty":0,"timeout":20,"frame_error":3,"parity_error":0,"rx_edges":260012,
 "pins_shorted":false,"scan_done":0,"scan_total":0,
 "ids":[{"id":0,"type":"read-ack","raw":770,"age_ms":420,"count":4061}, …]}
```

`ids` is in ascending Data-ID order and lists only the IDs heard; `type` is the reply's message type
(`read-ack`, `write-ack`, `data-invalid`, `unknown-dataid`, …). The counters are described on
[Bus](/protocol/bus) and [Physical layer](/protocol/physical-layer).

## Device and network

### `GET /api/status`

The provisioning machine's state, the network, the device identity and the broker:

```json
{"state":"connected","mode":"station","provisioned":true,"has_credentials":true,
 "wifi_known_good":true,"connected":true,"wifi_ssid":"home","ip":"192.168.1.20",
 "ap_ssid":"","ap_on_air":false,"setup_window_open":false,
 "setup_window_remaining_ms":null,"fallback_ap_in_ms":null,"failure":"none","failure_reason":0,
 "device_id":"aabbccddeeff","mac":"aa:bb:cc:dd:ee:ff","sw_version":"1.0.0",
 "mqtt":{"configured":true,"connected":true,"published":4211,"commands":37,"rejected":0,
         "reconnects":1,"last_connack":0,"last_connack_reason":"no refusal"}}
```

| Field | Values |
| --- | --- |
| `state` | `unconfigured`, `connecting`, `connected`, `trial`, `retrying`, `fallback-ap`, `window-closed` |
| `mode` | `off`, `access-point`, `station`, `ap-station` |
| `failure` | `none`, `network-not-found`, `wrong-password`, `refused`, `no-address`; `failure_reason` is the Wi-Fi driver's code |
| `setup_window_remaining_ms`, `fallback_ap_in_ms` | `null` means "no end", never a huge number |
| `mqtt` | Filled whole by `ot_mqtt_link_status()`: counters only, never host, user or password |

The spellings are decided once, in `ot_wire` and `ot_provision`, so REST and MQTT cannot drift.
`last_connack` is the broker's MQTT 3.1.1 refusal code and `last_connack_reason` its name
(`bad username or password`, `not authorized`, …), so a mistyped broker password reads as a refusal,
not as "broker unreachable". The states are explained on [Networking](/device/networking), the
counters on [MQTT](/interfaces/mqtt#status-counters).

### `POST /api/provision`

Hands the device a network: `{"wifi_ssid":"home","wifi_psk":"…"}`. `wifi_psk` follows the secret
rules below (absent keeps the stored key, `""` is an open network); keeping the stored key while
naming a different SSID is refused, because the two are one record.

The answer comes **first**, before the device connects — the access point is about to follow the
station to the router's channel, and the phone that sent this will lose it. It is the status
document above with the identity and **without** the `mqtt` block: nothing has been in a position
to reach a broker yet, and zeros would read as a broker that went silent.

| Status | When |
| --- | --- |
| `200` | Accepted; the connection attempt follows |
| `400` | Not a JSON object |
| `422` | `wifi_ssid` missing or a field mistyped (`field` is the key), or a name or key the device cannot use (`ot_config_strerror()`'s sentence) |
| `503 {"error":"busy"}` | The provisioning machine cannot take it now |

### `GET /api/wifi/scan`

A top-level array, one row per BSSID, duplicates not filtered:
`[{"ssid":"home","rssi":-52,"secure":true}, …]`. `""` is a hidden network. Every SSID goes through
the JSON escaper — a network name is chosen by whoever set up the router next door. The scan blocks
the one server task for about two seconds; `503 {"error":"scan unavailable"}` when the radio cannot
scan.

### `GET /api/log`

The last 80 log lines (`OT_LOG_LINES`, each up to 160 characters) as a JSON array of strings, oldest
first. Every non-ASCII byte is escaped as `\u00xx`, which is one reason every firmware log string is
English. `500` if the ring does not fit the response buffer.

## Configuration: `GET` and `POST /api/config`

`GET` renders `ot_config_public_t`, the projection in which every stored secret has already become
the sentinel — the renderer is never handed the record that holds them.

| Group | Keys |
| --- | --- |
| Network (read-only here) | `wifi_ssid`, `wifi_psk`, `wifi_known_good` |
| Broker | `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_password`, `topic_prefix`, `ha_discovery` |
| Device | `device_name`, `tz`, `ntp_server`, `ui_password`; read-only `ui_password_set`, `read_only` |
| Executor settings | `control_mode` (0 local, 1 HA), `watchdog_s`, `failsafe_setpoint_dc`, `failsafe_room_target_dc`, `failsafe_heat_days`, `failsafe_min_cycle_s`, `flow_min_dc`, `flow_max_dc` |
| Room source (MQTT) | `room_mqtt_enable`, `room_mqtt_role`, `room_mqtt_stale_s`, `room_mqtt_ha_forwarded` |
| Executor state (read-only here) | `heating_season`, `local_ch_enable`, `local_ch_setpoint_dc`, `dhw_enable`, `dhw_setpoint_dc` |

`POST` takes any subset of the writable keys. An absent key keeps what is stored; an unknown key is
ignored, so a newer page can talk to an older firmware. The patch is all-or-nothing. What each value
may be is the subject of [Configuration](/guide/configuration); the validation is
`ot_config_apply()`'s, asked once.

```sh
curl -X POST http://DEVICE/api/config -H 'Content-Type: application/json' \
  -d '{"mqtt_host":"192.168.1.10","mqtt_password":"s3cret","control_mode":1}'
# {"saved":true}
```

| Status | When |
| --- | --- |
| `200 {"saved":true}` | Saved |
| `400` | Not a flat JSON object |
| `409` | The stored document was written by a newer firmware (`read_only`); nothing is written (`field` `read-only`) |
| `422` | A field of the wrong type or too long (`field` is the key); `wifi_ssid`/`wifi_psk` sent here — "set through `POST /api/provision`"; one of the five executor-state keys sent here — they change through `POST /api/entities/{key}`; a value refused by validation (`field` is the refusal's name: `flow`, `failsafe`, `mode-needs-broker`, `range`, `broker-pair`, `topic-prefix`, `ui-password`, …) |

A saved broker setting takes effect within a second without a call from here: the MQTT task follows
the stored settings and restarts its own client (see
[MQTT](/interfaces/mqtt#following-the-broker-settings)). The handler never enters esp-mqtt, because
a blocking client stop would freeze the one task that serves every HTTP socket.

### Write-only secrets and the read sentinel

Three fields are secrets: `wifi_psk`, `mqtt_password` and `ui_password`. **No API projection ever
returns a stored secret.**

| Stored | Reads back as |
| --- | --- |
| something | `"__UNCHANGED__"` (`OT_SECRET_SENTINEL`, `ot_secrets.h`) |
| nothing | `""` |

On the way back in (`ot_secret_decide()`):

| Submitted | Effect |
| --- | --- |
| key absent | keep what is stored |
| `"__UNCHANGED__"` while something is stored | keep — the page echoed the sentinel back |
| `"__UNCHANGED__"` while nothing is stored | **refused** — the sentinel is a published string and must never become a password |
| `""` | clear the stored value |
| anything else | store it |

This is what lets a form post a secret field back unchanged without wiping a credential. It does
not make the whole `GET` document a valid `POST`: `wifi_ssid`, `wifi_psk` and the five
executor-state keys are refused by name (`422`), so the settings page posts a hand-picked subset; `ui_password_set` says whether a password exists without saying what it is. The broker
address and its password are one record: changing `mqtt_host` while keeping a stored password is
refused (`broker-pair`), so a password is never sent to a host it was not entered for.

## The write gate and the fallbacks

**`/*` for POST, PUT, DELETE and PATCH** (`write_gate`) exists so the access policy is applied to
every write, not only to the ones that happen to have a handler. A write the policy lets through but
nothing implements is `501 {"error":"not implemented yet"}`. There is no firmware-upload route: OTA
over HTTP is not implemented (see [OTA and system](/device/ota-and-system)).

**`/*` for GET** (`ot_http_captive_probe_get`) answers, in order:

1. **The captive-portal probes** — `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/ncsi.txt`,
   `/connecttest.txt`, `/success.txt`, `/canonical.html` — with the exact bytes each operating
   system expects, before the access gate. Answering them with the SPA shell makes a phone decide the
   network has no internet and drop it mid-setup.
2. **The SPA assets** — `/`, `/app.js`, `/style.css`, gzipped at build time and served with
   `Content-Encoding: gzip` (see [Web UI](/interfaces/web-ui#embedding)).
3. **An unknown `/api/…` path** — `404 {"error":"no such endpoint"}`, never the shell: "no such
   endpoint" must not turn into "the endpoint returned HTML".
4. **Anything else** — the SPA shell, so a reload on `/settings` works.

## The server

| Setting | Value | Why |
| --- | --- | --- |
| Port | 80, plain HTTP | |
| Client sessions | 7 (`OT_HTTP_MAX_CLIENTS`) | The three service sockets come on top; 7 + 3 is the LWIP budget of 10 |
| LRU purge | on | Without it an eighth connection is refused until a keep-alive expires |
| Task stack | 8192 bytes | `POST /api/config` was measured within a few hundred bytes of the default 4096 |
| Response buffer | one shared 32 KB static buffer | One task serves every socket, so two handlers never render at once |

Because one task serves every request, anything slow — a Wi-Fi scan, a wrong password's delay —
holds every other client up for its duration. The bus runs in its own higher-priority task and is
never affected.

Source: [`components/ot_http/README.md`](../../components/ot_http/README.md),
[`components/ot_policy/README.md`](../../components/ot_policy/README.md),
[`components/ot_api/README.md`](../../components/ot_api/README.md),
[`components/ot_wire/README.md`](../../components/ot_wire/README.md).
