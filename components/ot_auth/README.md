# ot_auth — turning an HTTP `Authorization` header into a yes or a no

## Purpose

`ot_auth` parses a raw HTTP Basic `Authorization` header, decides whether the presented
password matches the stored record, and returns an outcome that is safe to log. It is the one
component whose entire input is chosen by an attacker: the setup access point is open, so on it
"the client" is anyone in radio range, and afterwards the device sits on a household LAN that is
not trusted. Everything here is written for that reader: nothing is copied before its length is
known, no answer depends on how nearly right a guess was, and no path can turn a device with no
password into one that accepts a password.

## Responsibility

What it owns:

- Parsing the raw value of the `Authorization` field (no field name, no colon) into a scheme
  decision and a password.
- Deciding the auth outcome for a candidate against a stored record, via an injected verifier.
- Wiping the plaintext password out of the caller's struct on every path.
- Providing the device's real verifier (`ot_auth_device_verifier`), which binds the stored-record
  parser to the mbedtls PBKDF2-HMAC-SHA256 key-derivation function.

What it deliberately does NOT do — each absence is the design:

- **No stored form of the password.** The stored record is a text field managed by the
  configuration layer, shaped `1$<iterations>$<salt hex>$<digest hex>` (PBKDF2-HMAC-SHA256, one
  fresh salt per stored password, carried inside the record). The configuration layer erases any
  stored password hash its own parser cannot read, so an unreadable record leaves the device open
  rather than sealed with the owner locked out; a password written in any other shape would
  therefore vanish at the next boot. DO NOT add a hash to this component.
- **No hashing.** The verifier arrives as a function pointer. mbedtls is on the device and not on
  the host; keeping the crypto out lets the parser be host-tested against the input it exists for.
- **No lockout.** No counting of failed attempts and refusing afterwards — that is a denial of
  service against the owner, who is the person most likely to mistype and whose only other way in
  (reset button, USB port, behind the front panel) must stay open.
- **No logging.** The whole log ring is world-readable on the open access point, so nothing here
  logs and `ot_auth_check()` never returns the parsed credential — only the shape of the failure,
  which is safe to publish.
- **No HTTP.** It never sees `esp_http_server`; it is handed a header value and returns an answer.
  The moment it could send a response it could send a 401 of its own, giving the access policy a
  second door.
- **No sleeping.** The failure delay is returned as a number; the caller applies it. A pure
  function that sleeps cannot be tested and would put the wait on whichever task called it.

## Public API

All declared in `include/ot_auth.h`.

### Constants

| Name | Meaning |
| --- | --- |
| `OT_AUTH_PASS_MAX` (128) | Longest password the parser carries. Tied to the configuration layer's stored-password maximum by a `_Static_assert` in `ot_auth.c` so the buffer can never be smaller than what will be stored. |
| `OT_AUTH_FAIL_DELAY_MS` (1000) | Milliseconds the caller must wait before answering a failed attempt. One second, chosen against the owner (a mistyped password should not look like a crash) rather than the attacker. |

### Types

- `ot_auth_parse_t` — what the header *was*, distinct from whether the credential was right:
  `OT_AUTH_OK`, `OT_AUTH_ABSENT` (no/empty/whitespace header — not a wrong guess),
  `OT_AUTH_NOT_BASIC` (another scheme, or a word merely starting with "Basic"),
  `OT_AUTH_MALFORMED` (not base64, or not `user:password`), `OT_AUTH_TOO_LONG` (password longer
  than could ever be stored, or a header too big to carry one — refused, never truncated).
- `ot_auth_credentials_t` — one field, `password[OT_AUTH_PASS_MAX + 1]`. No user name: this
  device has no accounts, and whatever is typed into the browser name box is discarded (its length
  too — see Implementation).
- `ot_auth_verifier_t` — `bool (*)(const char *stored, const char *candidate)`. Returns true when
  the candidate matches the stored record; the device's implementation binds the configuration
  layer's record check to its KDF.
- `ot_auth_result_t` — `OT_AUTH_GRANTED`, `OT_AUTH_DENIED`, `OT_AUTH_NO_PASSWORD` (nothing stored;
  NOT granted — a caller bridging to an `authenticated` field must map only `GRANTED` to true).
- `ot_auth_outcome_t` — `{ result, delay_ms, header }`. `delay_ms` is `OT_AUTH_FAIL_DELAY_MS`
  after an attempt that was made and failed, 0 otherwise. `header` is the parse result, and is
  `OT_AUTH_ABSENT` whenever `result` is `NO_PASSWORD` (the header is not decoded at all).

### Functions

| Function | Contract |
| --- | --- |
| `ot_auth_parse_t ot_auth_parse(const char *header, ot_auth_credentials_t *out)` | Parses a raw header value. `out` is zeroed before anything is decided; a refused header is wiped, never leaving a previous or partial password behind. Pure, no allocation. `NULL out` → `MALFORMED`; `NULL header` → `ABSENT`. |
| `void ot_auth_forget(ot_auth_credentials_t *creds)` | Overwrites a credential the caller is done with, through a `volatile` pointer so the compiler cannot delete the wipe as a dead store. `NULL`-safe. |
| `ot_auth_outcome_t ot_auth_check(const char *header, const char *stored, ot_auth_verifier_t verify)` | The full decision. Checks `stored` first: empty/`NULL` → `NO_PASSWORD` with no decode. Otherwise parses, treats `ABSENT` as no-guess (no delay), and grants only when parse is `OK`, the candidate is non-empty, `verify` is non-`NULL`, and `verify()` returns true. Wipes its local credential before returning. Pure; nothing sleeps. |
| `ot_auth_verifier_t ot_auth_device_verifier(void)` | The device's verifier — the configuration layer's record parser with the mbedtls PBKDF2-HMAC-SHA256 KDF bound. Device only; does not exist off the device (host callers get a link error, which is the informative failure). |

## Implementation

Two sources; the split is the design.

### `ot_auth.c` — the pure half (no ESP-IDF, no clock, no crypto, no allocation, no logging)

- **`ENCODED_MAX` (512)** — the longest Authorization token the parser will look at, in base64
  characters (384 decoded bytes). A cap on *work*, not on storage: the header is walked once and
  the too-long case is refused before the decode. DO NOT turn it back into an arithmetic formula
  over a buffer holding both `user` and `password` — that made the *sum* the bounded thing, so a
  long user name in front of a *correct* 128-char password came back `TOO_LONG`, and it left a
  rounding gap that let a token write two bytes past the buffer end.
- **`wipe()`** — zeroes through a `volatile` pointer so the wipe survives dead-store elimination.
- **`b64_value()`** — returns `-1` for anything not in the alphabet, *including* `'='`. Padding is
  handled structurally, not decoded to a value (otherwise a decoder accepts `"YQ==YQ=="`).
- **`sink_t` / `sink_byte()`** — the decoder emits one byte at a time into a sink; there is no
  buffer holding the whole `user:password` blob. The user name is counted and dropped as it
  arrives (before the first colon), so its length is never charged against the password's room. A
  NUL anywhere sets a flag (a NUL would truncate the C-string password early — refused, not
  truncated; checked in the name too so the answer does not depend on where the byte fell). The
  `pass_len >= OT_AUTH_PASS_MAX` guard is the only bound between attacker-chosen input and the
  caller's memory, and the only place a decoded byte reaches memory. The first colon separates: a
  colon may appear in a password, so everything after the first colon is password. Flags are
  resolved once, in `ot_auth_parse()`, so the order of the three refusals is visible in one place.
- **`b64_decode()`** — requires a whole number of four-char groups, validates the alphabet before
  the trailing padding, and is deliberately *not* constant time (everything it touches was chosen
  by the sender). It does not reject non-zero leftover bits of a padded group (permitted, but it
  would change no answer). DO NOT harden it into a constant-time secret comparison's shape — that
  pattern is slow because it touches the secret, and this decoder touches only attacker input.
- **`ot_auth_parse()`** — zeroes `out` first; matches the `Basic` scheme as a whole word,
  case-insensitively, with a terminator check so `"Basicx"` is rejected; extracts exactly one
  token (a trailing non-whitespace token is `MALFORMED`); refuses over `ENCODED_MAX`; then decodes
  and resolves flags in fixed order (`MALFORMED` for non-base64 / NUL / no-colon, then `TOO_LONG`,
  else `OK`). One exit wipes the password on any non-`OK` result.
- **`ot_auth_check()`** — the `stored` empty check is *first* so no bug below can turn "no
  password" into "accepts one". `ABSENT` returns with no delay (the first request of every page
  load sends no header). Grant requires parse `OK`, non-empty candidate, non-`NULL` verifier, and
  a true verify — fails closed on a missing verifier. Wipes its local credential before returning.

### `ot_auth_device.c` — the crypto-bound half (five lines)

Wrapped entirely in `#ifdef ESP_PLATFORM`, so no host build compiles a line of it (the host test
build compiles every `.c` in this directory, and this file cannot build without mbedtls). Faking
mbedtls would mean testing the fake. `device_verify()` calls the configuration layer's
`ot_config_check_ui_password_against(stored, candidate, ot_config_device_kdf())`, and
`ot_auth_device_verifier()` returns it. It **binds, does not decide** — if the record format
changes, it changes in the configuration layer, once, and nothing here notices.

### `CMakeLists.txt`

`SRCS ot_auth.c ot_auth_device.c`, `INCLUDE_DIRS include`, `REQUIRES ot_config` (not
`PRIV_REQUIRES` — `ot_auth.c` includes `ot_config.h` for the `_Static_assert`, so the include
directory must travel). Deliberately NOT listed: the HTTP-server component (this component must
never see `esp_http_server`) and `mbedtls` (reached only through `ot_config_device_kdf()`, on the
far side of the configuration layer's interface — claiming it would make "ot_auth hashes
something" look true).

## Tests

Host suite `test/test_auth` covers the pure half by sending the input it exists for: an
eight-kilobyte header, a NUL inside a password, a colon in three places. Two named cases pin the
length behaviour: `test_a_user_name_never_costs_the_password_its_length` and
`test_nothing_a_header_can_carry_writes_past_the_password_buffer` (a canary behind the caller's
struct, which works only because the write lands in the caller's memory). The suite also carries
two `static_assert`s: the shape of `ot_auth_verifier_t` and the full signature of the
configuration layer's `ot_config_check_ui_password_against()`, both in headers the host can
compile. `ot_auth_device.c` has no host coverage by design (device-build-only); a dropped or
added parameter there would be caught only by a full device build.

## Notes

- **Buffer-length invariant.** `OT_AUTH_PASS_MAX >= OT_CONFIG_UI_PASS_MAX` is a compile-time
  `_Static_assert` in `ot_auth.c`. If the receiving buffer were smaller than what the
  configuration layer stores, an owner could set the longest accepted password and then never log
  in — with no way in over the air. A compile error is the right way to find that out.
- **`NO_PASSWORD` is not `GRANTED`.** The HTTP layer decides what an unclaimed device allows from
  its own `password_set` flag; mapping `NO_PASSWORD` to `authenticated = true` would hand a
  stranger on the open access point the device.
- **The one-task delay.** `esp_http_server` serves all sockets from a single task
  (`max_open_sockets = 7`), so a caller sleeping on a failed attempt stops that task: an attacker
  gets one guess per second *in total*, not per socket. The same fact is the price — every other
  request waits that second too, which is why the delay is one second and not five.
- **Standards followed in the code:** the scheme is matched as a whole word and the first colon
  splits user from password (a password may contain colons); leftover padding bits of a base64
  group are not required to be zero; obsolete header folding never reaches a handler.
