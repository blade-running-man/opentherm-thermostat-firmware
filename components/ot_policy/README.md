# ot_policy — who may do what, and when, as a pure function of path, method, and "is a password set"

## Purpose

`ot_policy` answers a single question for the HTTP surface: **is this request allowed in
this state?** The answer is computed with no HTTP types, no network stack and no I/O — a set
of pure functions over a method, a path, a small context struct and two security-header
values. Being pure is the whole reason the component is separate from `ot_http`: `ot_http`
needs `esp_http_server.h`, which does not exist on the host build, whereas these rules can
be compiled and tested on the host.

The shape of the answer in one sentence: reading is open until the owner sets a password,
writing is refused until they do, and radio proximity to an **unclaimed** device authorises
nothing but handing it a network.

## Responsibility

Owns:

- The route-level access decision (`ot_http_check`): ALLOW / 401 / 403 as a function of
  method, path and the `{provisioned, password_set, authenticated}` context.
- The second-half decision for a bus-halting operation (`ot_http_check_op`).
- The two browser-attack gates on a body-bearing write: the `Content-Type` check
  (`ot_http_content_type_ok`, which blocks cross-site request forgery) and the `Host`-header
  allowlist (`ot_http_host_ok`, which blocks DNS rebinding).

Does NOT do:

- Parse HTTP, read headers, touch sockets, or answer with a status code. The HTTP glue
  reads the request, calls these functions, and maps the decision onto 200/401/403/415. That
  glue answers 415 on a false `content_type` check and refuses an over-long/truncated `Host`
  before this component is even consulted.
- Know which operations halt the bus. `halts_bus` is supplied by the operation table and
  passed in; the *rule* lives here, the *flag* lives next to the operation.
- Normalise paths. Matching is exact or a strict slash-prefix; nothing is canonicalised.

**An operation that halts the conversation with the boiler always needs a password.** This
is the load-bearing exception to the rule that controlling the heating is exempt while no
password is set. A boiler slave that hears silence from the master for longer than five
seconds reads it as a short-circuited thermostat and goes to a demand for heat — and nothing
overrides that, because the control loop's frames are exactly what a halted bus is not
sending. A caller looping such an operation therefore holds the boiler hot indefinitely, so
it requires a credential even where an ordinary setpoint write does not. With no password set
the result is 403 (not 401): there is nothing to authenticate against.

## Public API

All in `include/ot_http_policy.h`, `extern "C"`.

### Types

| Type | Meaning |
| --- | --- |
| `ot_http_method_t` | `OT_HTTP_GET`, `OT_HTTP_POST`, `OT_HTTP_OTHER`. Anything not GET/POST (PUT, DELETE, PATCH, unparsed) is `OT_HTTP_OTHER` and refused, not mapped onto the nearest verb. |
| `ot_http_ctx_t` | `bool provisioned` (has Wi-Fi creds and is on the owner's network; false = serving its own AP), `bool password_set`, `bool authenticated`. |
| `ot_http_decision_t` | `OT_HTTP_ALLOW`; `OT_HTTP_UNAUTHORIZED` (401: a password exists and was not presented); `OT_HTTP_FORBIDDEN` (403: no credential could make this acceptable in this state). |

### Functions

| Function | Contract |
| --- | --- |
| `ot_http_decision_t ot_http_check(ot_http_method_t method, const char *path, ot_http_ctx_t ctx)` | The route-level decision. NULL path → FORBIDDEN. See the ladder below. |
| `ot_http_decision_t ot_http_check_op(bool halts_bus, bool password_set)` | The **second** half of the decision on `POST /api/ops/<name>`, about the operation not the route. `halts_bus` false → ALLOW (the route rule already answered). Halts the bus → requires a password: ALLOW if `password_set`, else FORBIDDEN. |
| `bool ot_http_content_type_ok(const char *content_type)` | True iff the value names JSON (`application/json`, case-insensitive, optional `; charset=...` tail, tolerant of surrounding whitespace). NULL/empty → false. Not a bare prefix: `application/json-patch+json` is refused. |
| `bool ot_http_host_ok(const char *host, const char *sta_ip, bool ap_mode)` | True iff `host` names an address this device is legitimately reachable as; false only for a clearly-foreign, non-empty host (the rebinding signature). Allowlist + two fail-open cases below. |

### `ot_http_check` decision ladder

1. `path == NULL` → FORBIDDEN.
2. **GET:** reading is open — `!password_set` → ALLOW; otherwise ALLOW iff `authenticated`,
   else 401.
3. Not POST (i.e. `OT_HTTP_OTHER`) → FORBIDDEN.
4. **Not provisioned** (own access point): the only accepted write is `/api/provision`;
   anything else → FORBIDDEN. Radio proximity is not authorisation. Then, because "nobody
   owns this yet" and "the router died" look identical from outside but only the second holds
   the owner's data: if `password_set`, provisioning requires auth (ALLOW iff `authenticated`,
   else 401); if not, ALLOW.
5. **Provisioned, no password set:** three writes get through, everything else → FORBIDDEN
   (fails closed, e.g. OTA):
   - `/api/config` — setting the first password (every write needs a password and the
     password is a write);
   - a command — `path_is_a_command`: under `/api/entities/` or `/api/ops/` (controlling the
     heating, which the owner chose to keep usable on a device that has no password);
   - `/api/ws-ticket` — the ticket that opens `/ws` (it only spends a table slot; `/ws`
     carries nothing a plain GET does not already hand over).
6. **Provisioned, password set:** ALLOW iff `authenticated`, else 401.

## Implementation

Files: `ot_http_policy.c` (all logic), `include/ot_http_policy.h` (contract + rationale),
`CMakeLists.txt` (registers `ot_http_policy.c`, exposes `include`).

Key helpers and invariants:

- **`path_is(path, expected)`** — exact match via `strcmp`, never a prefix, so
  `/api/configuration` cannot inherit `/api/config`'s bootstrap exemption. A named function
  rather than a `strncmp` at each call site, because prefix rules are how exemptions leak.
- **`path_under(path, prefix)`** — a path strictly under a prefix, with three properties: the
  prefix must end in `/` (so `/api/entities-secret` cannot inherit `/api/entities`), something
  must follow it (the bare stem and a trailing slash are not covered), and nothing is
  normalised — `/api/entities/../ota` matches and is deliberately safe, because
  `esp_http_server` dispatched it to the entity handler on the same string, which only reaches
  an entity lookup that will fail.
- **`path_is_a_command`** — `/api/entities/` or `/api/ops/`. The path cannot tell a
  bus-halting op from an ordinary one; that distinction is `ot_http_check_op`, which needs the
  operation table.
- **`ot_http_content_type_ok`** — skips leading space/tab, `strncasecmp` against
  `application/json`, then requires the next char to end the media type (`\0`, `;`, space,
  tab). **DO NOT** turn this into a bare prefix match — `application/json-patch+json` must not
  slip through on the first sixteen letters.
- **`ot_http_host_ok`** — strips an optional `:port` (last `:` followed only by digits, which
  leaves a bracketed IPv6 literal like `[::1]` untouched → refused, since the device is reached
  by its IPv4 address). Allowlist: the device's own `sta_ip`; and `192.168.4.1` (the ESP-IDF
  SoftAP default) **only when `ap_mode`**. Two deliberate FAIL-OPEN cases, toward keeping the
  owner reachable: `host` NULL/empty (terse clients; an attacker would set a real Host), and
  `sta_ip` NULL/empty (device does not yet know its own address / pure AP). A host longer than
  the 64-byte buffer → false (it is not a real address and not NULL/empty).
- **`ot_http_check_op`** — must not answer twice for the non-halting case (`!halts_bus` →
  ALLOW), because two answers are how two answers start to disagree. For a halting op:
  FORBIDDEN (not 401) when no password, since there is no credential to loop toward; once a
  password exists, `ot_http_check` has already turned an unauthenticated POST away with a 401,
  so reaching here with `password_set` means the caller presented it.

The 401-vs-403 split is deliberate throughout: telling a client "authenticate" when no
authentication exists yet sends it round a loop it cannot leave.

## Tests

Host suite `test/test_http_policy/test_http_policy.cpp` (Unity). It exercises all four public
functions purely, with no network stack: `ot_http_check` across the whole ladder (every
method, both provisioned states, both password states, authenticated or not, and the exact
exempt paths), `ot_http_check_op` (halting and non-halting × password set or not),
`ot_http_content_type_ok` (JSON accepted with/without charset and whitespace, look-alike media
types refused, NULL/empty refused) and `ot_http_host_ok` (own IP, SoftAP default only in AP
mode, foreign host refused, port stripping, IPv6 literal, over-long host, both fail-open
cases).

## Notes

- **Unclaimed vs. unprovisioned.** A device whose router died looks identical from outside (an
  access point, no connection) to a brand-new one, but it holds the owner's data. Re-homing it
  to another network is the owner's decision alone — hence provisioning requires auth once a
  password exists, so a neighbour cannot wait for a power cut and walk off with the device.
- **The heating-control exemption on a password-less device.** A password is optional and off
  by default, and a device without one is a supported configuration. Because of that, if only
  `/api/config` were exempt while no password is set, every heating command would answer 403
  out of the box until the owner set a password nothing prompted them to set — a feature that
  does not work rather than a device that is secure. So a command (`/api/entities/`,
  `/api/ops/`) and the `/ws` ticket are exempt too. Its cost, stated plainly: anybody on the
  household LAN can change the heating — the same thing anybody standing at the unit can
  already do. OTA is deliberately not exempt: an OTA endpoint answering before anyone has
  claimed the device is unauthenticated code execution on the LAN, and this firmware does not
  verify image signatures.
- **The bus-halting exception.** A boiler slave that hears no master for more than five
  seconds interprets it as a short-circuited thermostat and goes to a demand for heat, and the
  control loop cannot override it because the loop's own frames are what a halted bus is not
  sending. That is why a bus-halting operation always needs a password, even where an ordinary
  setpoint write does not.
- **The two browser-attack gates work together.** The `Content-Type` gate stops cross-origin
  writes by forcing a CORS preflight the device never answers (the device sends no
  `Access-Control-Allow-Origin`). The `Host` allowlist stops DNS rebinding, which defeats the
  first gate by re-resolving the attacker's domain to the LAN IP (making the write same-origin
  again) but cannot forge the `Host` header the browser still sends with the name it navigated
  to. There is deliberately no mDNS/`.local` name in the allowlist — no code registers one, so
  adding one would only admit a name no legitimate client reliably sends.
