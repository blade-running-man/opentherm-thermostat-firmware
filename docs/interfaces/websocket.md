# WebSocket

`/ws` is the push channel: the device sends the entity state to the browser as it changes, so a page
does not have to poll `GET /api/state`. It carries exactly what that route carries, printed by the
same function, and not one field more. The routes, the ticket and the lifecycle are in
`components/ot_http/ot_http_ws.c`, the socket list and the push in `ot_http_ws_push.c`, the ticket
table in `components/ot_ticket`, and the frames are rendered by `ot_api_render_frame()`
(`components/ot_api`). The reference client is `web/src/api/ws.ts`.

## The channel is push-only

Frames go from the device to the client and nothing goes the other way. Every data frame a client
sends is read and dropped — it has to be read, or the TCP stream is left misaligned — and a client
frame larger than 128 bytes closes the socket. Commands travel over REST
(`POST /api/entities/{key}`, `POST /api/ops/{name}`), which has both an access policy and response
codes; a command accepted on this socket would be an endpoint the REST API does not have, which is
the privileged path the project forbids (see [Web UI](/interfaces/web-ui)).

## The ticket handshake

A browser does not send an `Authorization` header on a WebSocket handshake — neither
`new WebSocket()` nor the `ws://user:pass@host` form does so reliably. Without a separate mechanism
the socket would be either open to everyone who can reach the device or unusable from a browser. So
the socket is opened with a **one-time ticket** fetched from an ordinary REST route under the
ordinary policy:

```js
const { ticket } = await (await fetch('/api/ws-ticket', { method: 'POST' })).json();
const ws = new WebSocket(`ws://${location.host}/ws?ticket=${ticket}`);
```

1. **`POST /api/ws-ticket`** (no body) is judged by the access policy like any write: allowed on a
   provisioned device with no password, needs the password once one is set, and refused (`403`) on
   the setup access point. It answers `{"ticket":"<32 hex>","ttl_ms":30000}` with
   `Cache-Control: no-store` — a one-time secret has no place in a cache.
2. **`GET /ws?ticket=…`** with the upgrade. `esp_http_server` answers a handshake itself and never
   calls the route's handler for it, so the check lives in the pre-handshake callback
   (`ot_http_ws_pre_handshake`), the one place where the protocol is still HTTP and a status code
   still means something.

The callback checks, in this order:

| Check | Refusal (the socket is then closed) |
| --- | --- |
| A free slot in the push list | `503 {"error":"no free socket"}` — **the ticket is kept** |
| A `ticket` query parameter that redeems | `401 {"error":"ticket required"}` |

Room is checked **before** the ticket is spent: the reverse order burns a one-time ticket on a
connection that is torn down at once, and the honest client needs a new one for its next attempt.
The ticket is read from the raw query string, not from the routed path, because the path parser
strips the query — and the query is the only place a browser can put it.

### The ticket table

| Property | Value | Why |
| --- | --- | --- |
| Value | 128 bits from `esp_fill_random()`, 32 hex characters | Cannot be guessed in 30 s; the radio is up because the request arrived over it |
| Lifetime | 30 s (`OT_TICKET_TTL_MS`) from issue | Long enough for the round trip, short enough to be useless later |
| Use | exactly once | A reconnect fetches a new ticket; a reused one is `401` |
| Slots | 14 (`OT_TICKET_SLOTS`) | Twice the 7 client sessions: 7 can be in use, 7 more cover abandoned ones (a tab closed between the ticket and the handshake) |
| Full table | the oldest is evicted | The oldest live ticket is the one most likely abandoned; refusing would lock the owner out for 30 s |
| Storage | RAM only | A saved ticket would outlive a change of the UI password |

The size of the table is the answer to overflow, not the eviction policy: someone asking for
tickets faster than they spend them will evict someone else's under any policy. What protects the
endpoint is the access policy in front of it. `ot_ticket` is pure — the randomness and the clock are
arguments — so it cannot invent a ticket and is host-tested (`test/test_ticket`).

## Frames

Every frame is one JSON text frame. The shapes are set by the client, `InboundFrame` in
`web/src/api/ws.ts`, which silently drops anything else — so a frame of another shape is not
"something else", it is nothing.

```json
{"type":"state","values":{"ch_setpoint":45.00,"flame":true,"control_state":"ha","dhw_setpoint":null}}
{"type":"delta","values":{"ch_setpoint":45.50}}
{"type":"ping"}
```

| `type` | Meaning | Client action |
| --- | --- | --- |
| `state` | Every entity — the same values as `GET /api/state` | **Replace** the local map |
| `delta` | Only the entities that changed since the last tick | **Merge** into the local map |
| `ping` | Nothing went out for 10 s | Reset the silence timer |

`values` is a flat map from entity key to a number, `true`/`false`, an enum's option string, or
`null`. It is printed by `emit_bare_value()`, the one place a value's spelling is decided, shared
with REST and the MQTT state payloads. A key with no data is printed as `null`, never skipped: a
`state` frame replaces the client's whole map, and a skipped key would vanish instead of reading
"no data". There is no availability or age in a frame; a client that needs them reads
`GET /api/state`.

## When something is pushed

A one-second `esp_timer` (`WS_TICK_US`) queues the push work onto the HTTP server task with
`httpd_queue_work()`. The timer task itself reads no state, renders nothing and touches no socket —
the shared 32 KB response buffer is legitimate only because one task uses it, and rendering from the
timer would corrupt a handler's response into broken JSON at some client. With no socket open the
tick queues nothing, so a device nobody watches does no work for it.

Each tick, on the server task:

1. **A snapshot** (`state`) to every socket that has not had one yet. A page that has just opened
   must see everything at once; some entities (the boiler's identity, its fault flags) do not change
   for hours.
2. **One delta** with every entity whose web mark (`OT_CONSUMER_WEB` in `ot_state`) was set since
   the last tick — one frame per tick, not one per entity, because after start-up dozens are dirty
   and every send is a chance to hold the server task on a slow client.
3. **A `ping`** to every socket that has received nothing for 10 s (`WS_PING_MS`).

The change marks are one set shared by all sockets and are cleared before the delta goes out. That
loses nothing: a socket the delta did not reach is closed at once, and its reconnection starts with
a snapshot. The client either received the delta or will receive the full state — there is no third
outcome in which it shows a stale value indefinitely.

### Why the ping is 10 s

The client promises to treat 35 s of silence as a dead socket (`SILENCE_TIMEOUT_MS` in `ws.ts`) and
reconnects; a TCP connection abandoned by a NAT box or a sleeping access point otherwise stays
"open" for the browser for ever. Ten seconds leaves room for a missed tick and a server task busy
for a few seconds. Do not raise it to the limit: the promise is a ceiling, not a target.

## Sockets and their limits

- **Seven at most.** The push list has `OT_HTTP_MAX_CLIENTS` (7) slots, the same as the server's
  client sessions; a shorter list would let the server accept a handshake the push then refuses,
  after the ticket was spent. The list is pruned against the server on every tick and handshake, so
  a closed socket does not hold a slot.
- **Non-blocking sends.** Every frame is sent with `MSG_DONTWAIT` through a per-session send
  override. The default send blocks the server task for up to 5 s on a client that stopped reading —
  a phone asleep with the tab open — which would freeze every HTTP client. A short write is treated
  as a failure, because `esp_http_server` does not resend the rest and the WebSocket stream would be
  desynchronised for ever. The socket is closed; the browser reconnects and gets a snapshot. A frame
  is a couple of kilobytes against a 5760-byte TCP send buffer (`CONFIG_LWIP_TCP_SND_BUF_DEFAULT`),
  so a full buffer means a client that has not read for seconds.
- **Surviving LRU eviction.** The server evicts the least recently used session when an eighth
  connection arrives, and it advances a session's counter only on an incoming request — which a
  one-way socket never makes. Every successful send therefore advances the counter itself
  (`httpd_sess_update_lru_counter()`), or the long-lived socket would always be the first victim of a
  second tab or a phone's captive-portal probe.
- **A snapshot that cannot fit** the response buffer closes the socket rather than keeping open a
  connection over which nothing will arrive.

## The reference client

`web/src/api/ws.ts` (`DeviceSocket`) opens one socket for the life of the page — `connectDevice()`
is called once from `web/src/main.tsx`, not per screen, because the device has seven sockets in
total.

- It fetches a **new ticket on every connect**, reconnects included.
- It **reconnects with exponential back-off** from 1 s to 30 s, with jitter so several tabs do not
  reconnect in lockstep; a refused ticket counts as a failed connection.
- After **35 s without a frame** it closes the socket itself and lets the reconnect logic take over.
- A `state` frame replaces `deviceState` (`web/src/api/device.ts`), a `delta` merges into it; the
  connection badge reads `connecting`, `open` or `closed`.

Source: [`components/ot_http/README.md`](../../components/ot_http/README.md),
[`components/ot_ticket/README.md`](../../components/ot_ticket/README.md).
