# ot_secrets — write-only handling of secret fields on the way out of and back into the API

Pure helpers that keep stored credentials from ever leaving the device while still letting the
settings page load the whole configuration and submit the whole configuration back. A stored
password is returned by no API projection: a read hands back a sentinel, and a submitted sentinel
means "leave what is stored untouched".

## Responsibility

Owns:
- The rule for **which keys are secret** (`ot_secret_key`): a case-insensitive substring match, not
  a whitelist.
- The **redaction** a `GET` projection applies to a stored value (`ot_secret_redact`).
- The **decision** a `PUT`/apply path makes about a submitted value — keep, clear, store or reject
  (`ot_secret_decide`).
- **Constant-time comparison** of a stored secret against a candidate (`ot_secret_equals`), plus a
  test-only readback of how many bytes the last comparison read (`ot_secret_compared_bytes`).
- The published sentinel string constant `OT_SECRET_SENTINEL` (`"__UNCHANGED__"`).

Does NOT do:
- No storage, NVS, JSON parsing or HTTP. It is pure logic over C strings; the caller supplies
  `has_stored`, the submitted value and the stored value, and acts on the returned enum.
- No key list of its own — the stored configuration document is user-editable and its key set has
  drifted across firmware versions, so nothing here enumerates config keys.

## Public API

All functions are `extern "C"` and pure (except that `ot_secret_equals` writes a single file-static
byte counter read back by `ot_secret_compared_bytes`).

| Symbol | Contract |
| --- | --- |
| `OT_SECRET_SENTINEL` | Macro `"__UNCHANGED__"`. What a set secret reads back as; must match `CONFIG_UNCHANGED` in `web/src/api/client.ts`. A published string. |
| `bool ot_secret_key(const char *key)` | True when `key` names something that must never be sent to a client. Case-insensitive **substring** match against `password`, `passwd`, `psk`, `secret`, `token`, `key`. `NULL` → false. |
| `const char *ot_secret_redact(const char *stored)` | What `GET` returns in place of a stored value: the sentinel when something is set, `""` when `stored` is `NULL` or empty. Never the value itself. Returns a static/literal pointer — do not free. |
| `ot_secret_action_t ot_secret_decide(bool has_stored, const char *submitted)` | What to do with a submitted value. `submitted == NULL` means the key was absent. Returns `KEEP` / `CLEAR` / `STORE` / `REJECT`. |
| `bool ot_secret_equals(const char *stored, const char *candidate)` | Constant-time equality. An empty/`NULL` stored secret (and a `NULL` candidate) matches nothing. |
| `size_t ot_secret_compared_bytes(void)` | Bytes the last `ot_secret_equals` read. Exists only so a test can pin the constant-time property. Not for production use. |

### `ot_secret_action_t`

| Value | Meaning |
| --- | --- |
| `OT_SECRET_KEEP` | The field was not touched, or there is nothing to change. |
| `OT_SECRET_CLEAR` | The user emptied it deliberately. |
| `OT_SECRET_STORE` | Store the submitted value. |
| `OT_SECRET_REJECT` | The submission is not acceptable; change nothing and say so. |

## Implementation

Files:
- `include/ot_secrets.h` — the contract; the header comments carry the full rationale.
- `ot_secrets.c` — the implementation (~88 lines).
- `CMakeLists.txt` — `idf_component_register(SRCS "ot_secrets.c" INCLUDE_DIRS "include")`. No
  dependencies; standalone.

Key logic:

- **`contains_ci`** — a hand-rolled case-insensitive substring scan (bitwise `| 0x20` lowercasing).
  `ot_secret_key` runs it over the fixed word list `{"password", "passwd", "psk", "secret",
  "token", "key"}`. `psk` is included because Wi-Fi calls the pre-shared key that and nothing else
  in this configuration contains those three letters. The asymmetry is deliberate: over-redaction
  shows a field as unchanged and the user retypes it; under-redaction publishes a credential.

- **`ot_secret_decide`** — the round-trip logic:
  - `submitted == NULL` (key absent) → `KEEP`.
  - `submitted == OT_SECRET_SENTINEL` and `has_stored` → `KEEP` (the page rendered the sentinel and
    handed it straight back untouched).
  - `submitted == OT_SECRET_SENTINEL` and **not** `has_stored` → `REJECT`. Nothing is stored, so
    this cannot be an untouched field; storing it would make a *published* string the password of
    every never-configured device.
  - `submitted == ""` → `CLEAR` if `has_stored`, else `KEEP` (deliberate wipe vs. nothing to do).
  - otherwise → `STORE`.

- **`ot_secret_equals`** — constant-time. Reads every byte of the **longer** of the two strings
  (`span = max(len)`), so timing does not reveal where the first difference is; the length
  difference (`stored_len ^ cand_len`) is folded into the `diff` accumulator rather than short-
  circuited. Increments the file-static `s_compared` per byte. Guards: `stored` `NULL`/empty or
  `candidate` `NULL` → `false` — an unprotected device must not accept an empty guess as correct.

Invariants / DO NOT:
- `OT_SECRET_SENTINEL` is a **published** string. DO NOT ever store it as a real secret value —
  `ot_secret_decide` refuses exactly that case.
- Keep `OT_SECRET_SENTINEL` in sync with `CONFIG_UNCHANGED` in `web/src/api/client.ts`; the client
  half (`web/src/pages/settings/sections/SecretField.tsx`) relies on the same string.
- DO NOT introduce early returns into `ot_secret_equals` — the constant-time property is the point.
- `ot_secret_compared_bytes` is test-only; do not build production logic on it.

## Tests

Host suite `test/test_secrets/test_secrets.cpp` covers this component (pure logic, no hardware or
framework).

## Notes

- The rules are the server half of a write-only scheme; the client half is
  `web/src/pages/settings/sections/SecretField.tsx`.
- Enforces the project-wide rule "Secrets are write-only" (see `CLAUDE.md`): a stored password is
  returned by no API projection; a read hands back a sentinel.
- The substring-match design intentionally redacts future keys (e.g. a new "MQTT Password" or
  "API Token") without anyone extending a whitelist.
- `ot_secret_equals`'s constant-time guarantee exists because a device on a LAN can be brute-forced
  quickly; a byte-at-a-time early return would leak how much of a guess was right.
