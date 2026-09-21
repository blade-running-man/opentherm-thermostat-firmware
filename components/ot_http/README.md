# ot_http — the HTTP/WebSocket server surface

The single web-facing surface of the firmware: the REST API, the `/ws` live push and the SPA
served from flash. Started once at boot as an `esp_http_server` instance; every route runs on the
server's single task.

## Purpose

Give every client — the browser SPA and any `curl` alike — one HTTP surface onto the firmware:
read the device's state and configuration, write a single OpenTherm entity or the executor's
commanded values, run a named operation, and follow live changes over a WebSocket. The component
is thin glue: it reads and bounds the request, applies one access gate, calls the pure layer that
actually decides, and turns that layer's verdict into an HTTP status code.

## Responsibility

**Owns:**

- The `esp_http_server` lifecycle (`ot_http_start` / `ot_http_stop`), the route table, and the
  `max_open_sockets` / `stack_size` / LRU configuration.
- The one access gate every request passes (`ot_http_allowed`): the Host-header allowlist, the
  Content-Type gate, the credential check and the access-policy verdict.
- Reading and bounding the request body, the uniform refusal shape, and path normalisation.
- Translating each lower layer's verdict (the entity command layer, the executor/thermostat task,
  the config store) into an HTTP status code — it decides **no** rule of its own.
- The `/ws` ticket handshake and the once-a-second state push (snapshot, delta, keepalive).
- Serving the gzipped SPA assets and answering the captive-portal probes.

**Does NOT do:**

- Decide what a value may be, who owns a mode, or whether the boiler supports a Data-ID — every
  such refusal is decided in the pure command layer, the executor/thermostat task or the config
  store, and this component only names the answer. A check duplicated on the surface would diverge
  silently from the check in the depths.
- Hold any state model or render JSON by hand — that is done by the API projection and the raw
  observation layers; this component only prints what they hand back.
- Expose a private handle. **The web interface has no privileged endpoint:** every route below is
  reachable by `curl` exactly as by the browser — no header, cookie or route the API does not give
  everyone. If a screen needs a path of its own, the API is wrong. `/ws` hands out exactly the
  values `GET /api/state` does; the ticket is an ordinary route under the ordinary policy.

## Public API

Installed header `include/ot_http.h`:

| Symbol | Contract |
| --- | --- |
| `ot_http_config_t { uint16_t port; }` | Start config; `OT_HTTP_DEFAULT_CONFIG()` = port 80. |
| `ot_http_start(const ot_http_config_t *)` | Registers all routes, starts httpd, starts the `/ws` push tick. `ESP_ERR_INVALID_STATE` if already running, `ESP_ERR_INVALID_ARG` on NULL config. |
| `ot_http_stop(void)` | Stops the push tick (before the server), then httpd. `ESP_OK` when not running. |

There is deliberately **no** "notify the server that state changed" call and **no**
"set provisioned" call in the header. Whether the device counts as provisioned is asked of the
network component on every request, never cached: a cached copy would be a tick out of date, and
that tick is exactly the window in which the open access point is still on the air while the
station already has an address — the moment during which a cached "provisioned" flag could hand a
write to a passer-by still on the access point.

### HTTP routes

Registered in `ot_http_start`. Order matters: the API routes and `/ws` sit **above** the `/*`
write gate and the `/*` GET catch-all, or the wildcards would shadow them.

| Method + path | Handler / file | One-line contract |
| --- | --- | --- |
| `GET /api/log` | `ot_http_log_get` (config) | The log ring as JSON; `500` if it does not fit. |
| `GET /api/status` | `ot_http_status_get` (config) | Network + MQTT status document. |
| `GET /api/config` | `ot_http_config_get` (config) | The public config projection (secrets already sentinelled) plus `known_good`. |
| `POST /api/config` | `ot_http_config_post` (config) | Apply a config patch; `{"saved":true}`. `400`/`422` on parse, `409` read-only store, `422` on refusal. |
| `GET /api/wifi/scan` | `ot_http_wifi_scan_get` (config) | Wi-Fi scan result; `503` if unavailable. |
| `POST /api/provision` | `ot_http_provision_post` (config) | Store credentials; answers **before** connecting so the reply survives the access-point hand-off. |
| `GET /api/ot/raw` | `ot_http_ot_raw_get` (boiler) | Raw bus observation over all 128 Data-IDs, diagnostics only, no names/units. |
| `GET /api/control` | `ot_http_control_get` (control) | The executor's live document. |
| `POST /api/ops/*` | `ot_http_op_post` (ops) | Run one named operation (`scan`, `linetest`, `boost`, `boost_off`). `404`/`400`/`422`/`409`/`503`; success `202`. |
| `GET /api/state` | `ot_http_state_get` (registry) | Every entity's value/availability/age. |
| `GET /api/entities`, `GET /api/entities/*` | `ot_http_entities_get` (registry) | Registry metadata, or one entity + value; unknown key `404`. One handler, two table entries. |
| `POST /api/entities/*` | `ot_http_entity_post` (registry) | Write one entity `{"value":…}`. `404/405/409/422/503/500`; success `202` (`{"queued":…}` frame or `{"applied":{"command":N}}` executor). |
| `POST /api/ws-ticket` | `ot_http_ws_ticket_post` (ws) | Issue a one-time ticket `{"ticket":…,"ttl_ms":30000}`; `Cache-Control: no-store`. |
| `GET /ws` | `ot_http_ws_handler` + `ot_http_ws_pre_handshake` (ws) | WebSocket; the ticket is checked in the pre-handshake callback (a closed socket on failure: `503` when no slot is free, `401` when the ticket is invalid). |
| `POST/PUT/DELETE/PATCH /*` | `write_gate` (ot_http.c) | Catch-all so the policy applies to every write; unhandled writes answer `501`. |
| `GET /*` | `ot_http_captive_probe_get` (static) | Captive-portal probe, else an asset, else the SPA shell. Registered last. |

### Internal seam (`ot_http_internal.h`, not installed)

No component outside may register a route. Key shared pieces: `ot_http_scratch` (one 32 KB response
buffer, not re-entrant), `ot_http_now_ms`, `ot_http_send_json`, `ot_http_read_body`
(`OT_HTTP_BODY_MAX` = 2048), `ot_http_send_error` (uniform `{"error":"<literal>"}`),
`ot_http_allowed`, `ot_http_request_path`, `ot_http_control_refusal` (+ `ot_http_refusal_t`), the
`/ws` push helpers (`ot_ws_push_room`/`add`/`forget_all`/`queue`) and lifecycle
(`ot_http_ws_start`/`stop`), and `OT_HTTP_MAX_CLIENTS` = 7.

## Implementation

Eleven `.c` files split **by addressee, not by size** (a 350-line-per-file ceiling forced two of
the splits):

- **`ot_http.c`** — the route table and server lifecycle. Derives `max_uri_handlers` from the route
  array (so adding a route can't overflow the handler table and trigger an abort at registration),
  `max_open_sockets = 7`, `lru_purge_enable = true`, `stack_size = 8192` (measured: `POST
  /api/config` is the deepest call), `uri_match_fn = httpd_uri_match_wildcard`. Registration
  failures are **logged, never fatal** — a device serving ten of eleven routes is reachable; one
  that aborts in start-up reboots forever. Also holds `ot_http_request_path` (strips
  query/fragment/absolute-form authority/trailing slash — the server routes on the path but hands
  the handler the raw target) and `write_gate` (`501`).
- **`ot_http_gate.c`** — `ot_http_allowed`, the one door. In order: note the client (extends the
  access-point setup window); the Host-header allowlist (the DNS-rebinding defence, `403`, a
  truncated Host is fail-**closed**); the Content-Type gate for bodied writes (the CSRF defence,
  `415`, forcing a CORS preflight the device denies); the credential check (the stored hash never
  reaches this layer — the candidate is handed to the network component, which verifies in constant
  time); then the access-policy verdict → `ALLOW`/`401`/`403`. Only a genuine password grant maps
  to `authenticated`; "no password is set" is never treated as authenticated. A wrong password
  costs a fixed delay, not a lockout.
- **`ot_http_body.c`** — `ot_http_read_body` (loops, because a single receive can return short) and
  `ot_http_send_error`. `message` must always be a literal: the refusal body enters the log ring,
  which `GET /api/log` serves to anyone.
- **`ot_http_config.c`** — the six configure-the-device routes (they work on an unprovisioned
  device behind an open access point). `this_device()` is the one identity builder (borrowed
  buffers belong to the caller). `POST /api/config` does **not** call into the MQTT link — the
  httpd task must never enter esp-mqtt; the link task follows the broker configuration itself.
- **`ot_http_static.c`** — gzipped assets (`Content-Encoding: gzip`, compressed at build time), the
  SPA-shell fallback for client-side routes, `404` for unknown `/api/` paths, and the
  captive-portal probes ahead of the shell.
- **`ot_http_boiler.c`** — `GET /api/ot/raw` only (a read, not an operation).
- **`ot_http_registry.c`** — the state/entity reads and the entity write. The write only
  translates: parse the entity write → run it through the command check with a web origin → a
  `switch` naming each command-result code, then either queue a bus frame (`{"queued":…}`) or apply
  it to the executor (`{"applied":…}`).
- **`ot_http_ops.c`** — the operation table behind `POST /api/ops/<name>`. Each op carries a
  `halts_bus` flag; an op that stops the master (`linetest`) requires a password **always**, because
  a silent master is a hazard: the slave reads master silence longer than five seconds as a shorted
  thermostat and goes into a demand for heat, so halting the bus is never a safe default. Ops only
  parse and map; every rule lives in the pure layer. Do not add an op with no caller.
- **`ot_http_ws.c`** — the routes, the ticket table and the lifecycle. A ticket is 128 random bits
  hex-encoded from the hardware RNG. It is verified in the pre-handshake callback because, on an
  Upgrade, the server answers the handshake itself and bypasses the handler. **Room is checked
  before the ticket is redeemed**, so a rejected handshake doesn't burn a one-time ticket. The tick
  timer marks `s_in_tick` before reading `s_server` (both `volatile`); stop nulls the pointer
  first, then waits out any live tick.
- **`ot_http_ws_push.c`** — the socket list and what goes out.
- **`ot_http_control.c`** — `GET /api/control` and `ot_http_control_refusal`, the single
  translation of the thermostat's error enum, shared by the entity write and `boost` so the two
  can't answer one verdict differently.

### `/ws` handshake and push

1. Client `POST /api/ws-ticket` (ordinary policy) → a one-time ticket, TTL 30 s, `Cache-Control:
   no-store`.
2. Client opens `GET /ws?ticket=…`. The pre-handshake callback checks for a free slot (`503` and a
   closed socket if none), redeems the ticket (`401` and a closed socket if invalid), and adds the
   fd to the push list. The handler itself does **not** call `ot_http_allowed` — the browser sends
   no `Authorization` on a WebSocket handshake, so the ticket is the gate. Inbound data frames are
   read and discarded (the channel is one-way, but an unread frame would misalign the TCP stream).
3. A 1 Hz timer (`ws_tick`, task dispatch) calls `ot_ws_push_queue`, which — only if there are
   sockets — queues `ws_work` onto the **server task**. Rendering happens there so it shares
   `ot_http_scratch` with the route handlers safely (one task, no re-entrancy). Do not move
   rendering into the timer callback.
4. `broadcast` sends: a `state` snapshot to each socket that hasn't had one, one `delta` frame per
   tick (mask built from the web consumer's dirty marks), and a `ping` after 10 s of silence. The
   client accepts exactly three inbound shapes — `{"type":"state","values":…}`,
   `{"type":"delta","values":…}` and `{"type":"ping"}` — and silently drops anything else, so
   sending the wrong shape is sending nothing. `values` is a flat `key → value` map, the same
   values `GET /api/state` hands out, printed by the same projection.

### Data structures & invariants

- **One shared 32 KB `ot_http_scratch`** — legitimate only because the server serves every socket
  from one task; never hand a pointer into it to anything outliving the response.
- **`OT_HTTP_MAX_CLIENTS` = 7** — real browser sessions; the three httpd service sockets sit on top,
  and the LWIP budget (10) stands exactly on that. The `/ws` socket list must be no shorter than the
  server's queue, or the server could accept a handshake the push list has no room for.
- **LRU eviction is on**, and every successful `/ws` send advances the session's LRU counter —
  otherwise a one-way session's counter freezes and it becomes the first eviction victim.
- **Non-blocking `/ws` send** (`MSG_DONTWAIT`): a short write is an **error** (it would desync the
  frame stream forever); the socket is dropped and the browser reconnects to a fresh snapshot. This
  prevents one asleep tab from stalling the whole server while its send blocks.
- **`prune`** asks the server which fds are still WebSockets rather than remembering — closes aren't
  reported to the handler.
- Success is **`202`, never `200`**, for writes and ops: the bus queue holds one write and the next
  evicts it, and an executor command lands on the next step — so at reply time nobody yet knows the
  boiler took it.

### DO NOT / gotchas

- Both the command-result `switch` (in the registry write) and the thermostat-error `switch` (in
  the control translation) end in an **explicit trailing `500`**. This component is not compiled in
  the host test build, so its `switch` statements get no exhaustiveness warning; the explicit
  trailing `500` is what keeps a newly-appended enum code from falling through to a frame nobody
  filled in, or to an accidental `202`.
- Never format client-supplied text into a refusal `message` — it reaches `GET /api/log`.
- Don't duplicate a body-size limit or a config-to-tenths mapping in a route file; two writings of
  one limit diverge silently.

## Tests

**No dedicated host suite** — this component is impure glue that the host test build does not
compile. Its behaviour is covered indirectly: the pure layers it calls (the frame/command layer,
the executor core, the state and API projections, the access policy, the ticket table, the auth
parser, the captive-portal logic, the raw observation) each carry their own host suites, and the
`/ws` frame contract is pinned on the client side against the SPA's WebSocket parser.

## Notes

- No CORS header is ever sent: the UI is served from the device, and a `*` would let any page in
  the browser read the device's state.
- The `/api/ops/*` single entry is the only door to operations: a new operation needs a table row,
  not a new route.
