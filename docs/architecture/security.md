# Security

Who the firmware trusts, what protects each kind of operation, and what is deliberately left open.
The rules live in four small pure components — `ot_policy`, `ot_auth`, `ot_secrets` and
`ot_ticket` — plus the password record in `ot_config`, and `ot_http` applies them through one
gate in front of every handler that serves device data or accepts a write.

## Trust model

| Where the client is | How the firmware treats it |
| --- | --- |
| On the device's own setup access point | The access point is **open**, so "the client" is anyone in radio range. While an open access point is on the air the device counts as **unclaimed**, whatever NVS holds (`ot_net_is_provisioned()` is false). |
| On the household LAN | Not trusted. Reading is open until the owner sets a UI password; writing is limited until then (see the policy below). |
| On the MQTT broker | Trusted as far as the broker's own authentication and ACLs go. The device does not authenticate MQTT messages itself, but every command still passes `ot_command_check()` with origin `HA`: Home Assistant may drive the executor, never write an OpenTherm frame, and nothing at all in LOCAL mode. MQTT runs without TLS (the transport is compiled out). |
| At the device with a USB cable | Fully trusted: flashing and the console are the owner's way in, and must stay open. |

The setup access point is the riskiest surface, so everything that parses what arrives there is a
pure function with a host suite: the JSON reader (`ot_json`), the configuration and provisioning
documents (`ot_wire`), the DNS reply builder (`ot_captive`), the `Authorization` parser
(`ot_auth`) and the policy itself (`ot_policy`). The JSON reader accepts flat objects only and
refuses — never resolves — a duplicate key, so `{"ui_password":"x","ui_password":"__UNCHANGED__"}`
cannot mean two things.

## The one gate

Every handler that serves device data or accepts a write starts with `ot_http_allowed()`
(`components/ot_http/ot_http_gate.c`): every `/api/*` route, `POST /api/ws-ticket`, the SPA's
assets, and the catch-all for every other write. Two routes answer without it, each on purpose
(see [The two routes outside the gate](#the-two-routes-outside-the-gate)). The gate, in order:

1. **Host allowlist (DNS rebinding).** A request whose `Host` is not the device's own station
   address — or `192.168.4.1` while the access point is up — is refused `403`, reads included. It
   fails open only where it cannot tell: no `Host` header at all, or a device that does not know
   its own address yet. A `Host` too long for the read buffer is foreign and refused, not treated
   as absent.
2. **Content-Type (CSRF).** A write that carries a body must declare `application/json` (a
   `charset` parameter is fine, `application/json-patch+json` is not), else `415`. That forces a
   CORS preflight on any cross-origin caller, which the device denies: it never sends an
   `Access-Control-Allow-Origin` header, so a cross-origin `fetch` or a plain HTML form cannot
   drive a write.
3. **Credentials.** The `Authorization` header is parsed by `ot_auth` and the candidate is checked
   by `ot_net` against the stored record; the record never reaches the HTTP layer. Only
   `GRANTED` counts as authenticated — "no password is set" is never mapped to "authenticated".
4. **A fixed delay** of 1 s after a failed attempt — a delay, not a lockout: a lockout is a denial
   of service against the owner, the person most likely to mistype.
5. **The policy verdict** from `ot_http_check()`: allow, `401` (with `WWW-Authenticate: Basic`),
   or `403`.

Every method other than `GET` and `POST` (`PUT`, `DELETE`, `PATCH` on `/*`) reaches the gate as
"other" and is refused `403` whatever the password. A `POST` to a path with no handler still goes
through the gate, and only one the policy allows reaches the `501` behind it, so "writing is
refused until a password exists" is a rule, not an accident of which routes exist.

### The two routes outside the gate

| Route | What admits it instead | Why that is safe |
| --- | --- | --- |
| `GET /ws` (the WebSocket handshake) | The ticket alone, checked in `ot_http_ws_pre_handshake()` (`ot_http_ws.c`) before the `101`. Neither the `Host` allowlist nor the password runs on the handshake: a browser sends no `Authorization` on `new WebSocket()`, and the server answers an upgrade itself, so the route handler never runs. | The ticket comes only from `POST /api/ws-ticket`, which passes the whole gate — `Host` allowlist and policy included — so a rebinding page or a caller without the password cannot get one, and a cross-origin page cannot read the one it caused to be issued. It is 128 bits, single-use and 30 s long, and the socket is push-only, carrying nothing `GET /api/state` does not. See [`/ws` tickets](#ws-tickets). |
| `GET` of a phone's connectivity-probe path (`ot_http_captive_probe_get()` in `ot_http_static.c`) | Nothing: a path listed in `ot_captive`'s probe table (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt` and the rest) is answered with no `Host` check and no password, whatever the provisioning state. | The answer is constant bytes from the table — a `204` with no body or a fixed success page — that read no device state and change nothing; the one side effect is `ot_net_note_client()`, which can extend the setup window only up to its ceiling. Any `GET` path not in the table falls through to the asset handler, which starts with the gate. |

## Who may do what

`ot_http_check(method, path, {provisioned, password_set, authenticated})` is a pure function with
no HTTP types (`components/ot_policy/ot_http_policy.c`). Paths match exactly, or strictly under a
prefix ending in `/` with something after it; nothing is normalised.

| Request | Unclaimed, no password | Unclaimed, password set | Claimed, no password | Claimed, password set |
| --- | --- | --- | --- | --- |
| `GET` anything | open | password (`401`) | open | password (`401`) |
| `POST /api/provision` | open | password (`401`) | `403` | password (`401`) |
| `POST /api/config` | `403` | `403` | open (to set the first password) | password (`401`) |
| `POST /api/entities/<key>`, `POST /api/ops/<name>` | `403` | `403` | open (heating control) | password (`401`) |
| `POST /api/ws-ticket` | `403` | `403` | open | password (`401`) |
| any other `POST` | `403` | `403` | `403` | password (`401`), then `501` |
| `PUT`, `DELETE`, `PATCH` | `403` | `403` | `403` | `403` |

"Unclaimed" is the access point on the air, whatever NVS holds — which also covers an owned device
whose router has died. A `GET` ignores that distinction; a write does not.

Three rules carry the table:

- **Radio proximity to an unclaimed device authorises nothing but handing it a network.** And once
  a password exists the device has an owner: re-homing it to another network is then theirs to
  authorise, or a neighbour could wait for a power cut, catch the access point, and take the device
  out of the house.
- **Without a password, reading and controlling the heating are open on the LAN.** The password is
  optional and off by default, and a device without one is a supported configuration; with only
  `/api/config` exempt, every heating command would answer `403` out of the box. The owner chose
  this trade: anybody on the household LAN can then change the heating and the configuration. The `/ws` ticket is open for the same
  reason — `/ws` carries nothing `GET /api/state` does not.
- **`401` versus `403` is deliberate.** Telling a client "authenticate" when no credential could
  ever be accepted sends it round a loop it cannot leave; such requests answer `403`.

### The bus-halting operation

`ot_http_check_op(halts_bus, password_set)` adds one rule after the route rule: an operation that
stops the conversation with the boiler **always** needs a password, even though the other
operations are open without one. Sustained master silence is read by the boiler as a heat demand,
so a caller looping such an operation could hold the boiler hot indefinitely. With no password set
the answer is `403`, not `401`. The flag lives beside each operation in
`components/ot_http/ot_http_ops.c`:

| Operation | Halts the bus |
| --- | --- |
| `linetest` | yes — conversations stop while it runs (at most 30 s) |
| `scan` | no — ID 0 keeps going out between the probes |
| `boost`, `boost_off` | no — they ride the ordinary conversation |

## Write-only secrets

A stored password is returned by **no** API projection. `ot_secrets` implements the round trip:

- A read hands back the sentinel `__UNCHANGED__` for a stored secret and `""` for none —
  `wifi_psk`, `mqtt_password` and `ui_password` in `GET /api/config`. The UI password also has a
  derived, read-only `ui_password_set`.
- On a write (`ot_secret_decide()`): an absent key keeps what is stored; the sentinel keeps it
  when something is stored and is **refused** when nothing is (the sentinel is a published string
  and must never become a password); `""` clears it; anything else is stored.
- `ot_secret_equals()` is constant-time: it reads every byte of the longer string and folds the
  length difference into the result, so timing does not reveal where a guess first went wrong.
- What may never reach a log line is a secret's **value**; key names are logged, because they are
  what the owner needs when a store fails. The log ring is world-readable while no password is set.

The UI password is stored one-way, as `1$<iterations>$<salt hex>$<digest hex>` —
PBKDF2-HMAC-SHA256 with 10 000 iterations and a fresh salt per password, 8–128 characters. The
configuration layer erases a record its own parser cannot read, so a damaged record leaves the
device open rather than sealed with the owner locked out.

The broker password is the one secret stored recoverably, because MQTT authenticates with the
plaintext. It never reaches a projection or a log: `ot_net_broker()` is the one un-redacted
projection, called only by `ot_mqtt_link`, which wipes its copy after each pass.

::: details Why `ot_secret_key()` exists but is not called
`ot_secrets` also offers a case-insensitive substring classifier (`password`, `passwd`, `psk`,
`secret`, `token`, `key`) that recognises a secret by its key name, without a whitelist to extend.
No firmware code calls it: every secret field is projected explicitly in `ot_config`. It is the
rule a new configuration field is checked against.
:::

## `/ws` tickets

A browser cannot reliably send an `Authorization` header on `new WebSocket()`, so the socket is
opened with a ticket instead:

1. The page calls `POST /api/ws-ticket` — an ordinary route under the ordinary policy, answered
   with a 32-hex-character (128-bit) value from `esp_fill_random()`, `Cache-Control: no-store`.
2. It opens `/ws?ticket=<value>`. The pre-handshake callback first checks there is a free socket
   slot (`503` otherwise, the ticket kept), then redeems the ticket — once, within 30 s — or
   refuses `401`.

The table holds 14 tickets (twice the 7 client sessions) in RAM only: a ticket saved across a
reboot would outlive a password change. `ot_ticket` takes the randomness and the clock as
arguments, so it cannot invent a valid ticket itself. The socket is push-only: frames a client
sends are read and discarded, so it can never become a command path the REST API does not have.

## The access point and the captive portal

While unclaimed, the device runs an open access point with a captive portal (`ot_captive`): a DNS
responder that answers every name with the access point's address, and a byte-exact table of the
phone operating systems' connectivity probes, so the setup page opens by itself. Its exposure is
bounded three ways:

- the policy above — on the open access point, the only write is `POST /api/provision`, and none
  at all once a password exists without presenting it;
- the setup window — the access point closes after a timeout that client activity extends up to a
  ceiling, and comes back only on the conditions the provisioning machine names (sustained Wi-Fi
  failure among them);
- the trial — new credentials must produce an address on the owner's network before they replace
  the known-good pair.

Details of the window, the fallback and the rollback: [Networking](/device/networking).

## What is not protected

- **No firmware upload exists.** There is no `/api/ota` route; a write to it reaches the
  catch-all gate (`403`, `401` or `501`). The image does not verify signatures: a new image
  arrives only over the USB cable.
- **No HTTPS, no MQTT TLS.** The UI password travels as HTTP Basic over plain HTTP on the LAN, and
  the broker password over plain MQTT.
- **No accounts.** One password; the user name typed into the browser is discarded.
- **No rate limit beyond the 1 s delay**, by design.
