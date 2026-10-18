# ot_config — the persistent configuration store, and the rules that keep a stored value from bricking the device

`ot_config` owns what survives a power cut: the Wi-Fi pair, the MQTT broker, the device name, the
two passwords, the executor's settings and the MQTT room-source slot. It is built as **two halves**
whose split is the design, not tidiness:

- a **pure half** (no ESP-IDF, no NVS, no clock) that decides whether a value is acceptable, what a
  document means when it mentions only half of itself, and which keys a reset destroys — everything a
  host test can reach; and
- a **device half** (`#ifdef ESP_PLATFORM`) that opens namespaces, moves bytes and asks the pure
  half's questions — deliberately dull, never host-built.

Every rule here exists because a bad value stored once outlives every reboot, and a wall thermostat
that panics on boot over its own configuration has no way back over the air.

## Responsibility

What it owns:

- The in-memory shape of the whole stored document (`ot_config_t`) and the sanctioned public
  projection of it (`ot_config_public_t`).
- **Validation on the way in** — every `ot_config_check_*()` refusal is decided here, once, so the
  HTTP/settings surface never invents its own idea of what is legal.
- **Repair on the way out** (`ot_config_sanitize`) — a second pass over whatever the flash actually
  handed back, replacing anything unusable with its default.
- Atomicity where it was ever load-bearing: the Wi-Fi SSID and PSK are **one record under one key**;
  the broker host and its password, and the UI password record, are treated the same way at the
  policy level.
- The NVS layout: three namespaces, the schema number and its migration, the retired-key list, the
  two-tier reset, and the "ways back" (known-good credential rollback, provisioning flags).
- The UI password **record** format and its device KDF (PBKDF2-HMAC-SHA256).

What it does **not** do:

- It keeps **no live document of its own**. `ot_config_room_mqtt()` is declared here but implemented
  in `ot_net_config.c`, where the mutex-guarded live copy lives, so nothing here can disagree with
  `ot_net_broker()`.
- It never logs a rejected value, never returns a secret, and never `ESP_ERROR_CHECK`s anything that
  came out of flash.
- It does not own boiler-side bounds (DHW setpoint bounds become `ot_command`'s once the boiler has
  answered its own DHW-bounds read) and it never writes an OpenTherm frame.

## Public API

### `include/ot_config.h` (the pure contract)

| Symbol | Contract |
| --- | --- |
| `ot_config_field(field)` | The `{name, key, ns, secret}` row for a field id; **NULL** for an id outside the table or an unfilled row (a 404 beats a crash on the HTTP task). |
| `ot_config_ns_name(ns)` | Namespace name string, or NULL out of range. |
| `ot_config_reset_erases(tier, ns)` | Whether a reset tier destroys a namespace. Hard erases the whole enum; soft erases only the Wi-Fi and OWNER namespaces. |
| `ot_config_retired_key(index)` | The i-th retired NVS key, NULL past the end. Append-only table. |
| `ot_config_project(cfg, out)` | Fills `ot_config_public_t`: terminated strings a renderer may `strlen()`, secrets replaced by the sentinel or `""`, never the value. Clamps/`terminated()`-guards a document that may not have been sanitized. |
| `ot_config_password_set(cfg)` | Derived from the UI record being non-empty — never a separate stored flag. Exposed so the access policy shares one definition of "set". |
| `ot_config_check_ssid/psk/host/port/user/broker_password/prefix/name/ui_password/tz/ntp` | Per-field validators returning `ot_config_err_t`. `port` takes `uint32_t` so 70000 is refused, not wrapped. SSID/PSK take an explicit length (the record is not NUL-terminated). |
| `ot_config_check_range(field, value)` | One number against its own bounds (and half-degree grid where it applies): `OK` or `ERR_RANGE`. `uint32_t` for the same no-wrap reason. |
| `ot_config_check_flow(min, max, failsafe)` | The two cross-field flow rules: `min < max` (`ERR_FLOW`), failsafe inside `[min,max]` (`ERR_FAILSAFE`). |
| `ot_config_check_mode(mode, host)` | Home-Assistant mode with an empty broker host → `ERR_MODE_NEEDS_BROKER`. |
| `ot_config_wifi_usable(wifi)` | The **one** answer to "is a network worth trying stored" — SSID and PSK both pass their checkers. Used by both the AP-decision and the sanitizer so they cannot disagree. |
| `ot_config_strerror(err)` | A full sentence for the owner; never echoes the rejected value. |
| `ot_config_err_name(err)` | A short field token (`"mqtt-host"`, `"flow"`, …) the settings page matches on; `"rejected"` for an unknown code, never NULL. |
| `ot_config_defaults(cfg, device_id)` | What a fresh device holds. `device_id` is a parameter (12 hex chars) so the component is testable with a foreign identity; NULL/malformed yields a prefix/name with no id. |
| `ot_config_apply(cfg, patch, hash)` | **All-or-nothing** apply of a decoded POST body. Everything validated before anything written. Absent field = leave alone; absent document = `ERR_NO_DOCUMENT`. `hash` may be NULL only when no UI password is set. |
| `ot_config_apply_ex(cfg, patch, hash, moved)` | As above, plus a repair bitmask of fields it **moved** to keep the document legal (today only `local_ch_setpoint_dc`, pulled into the merged flow band). |
| `ot_config_sanitize(cfg, device_id)` | Re-runs the validators over a just-read document, replacing the unusable with defaults; returns a repair bitmask. Nothing logs (a rejected value may be a secret). |
| `ot_config_hash_password / check_ui_password_against` | Render / constant-time-verify the UI record; KDF injected so the format is host-testable. |
| `ot_config_room_mqtt(out)` | A **copy** of the four room-slot fields for a once-a-second poller. Declared here, implemented in `ot_net_config.c` (see Responsibility). |
| `ot_config_schema_check(stored)` | `CURRENT` / `MIGRATE` (stored `<` current, including 0/fresh) / `FUTURE` (rollback → read-only). |

### `include/ot_config_nvs.h` (the device contract)

| Symbol | Contract |
| --- | --- |
| `ot_config_nvs_load(cfg, device_id, repairs)` | Reads the whole document, migrates an older schema forward, sanitizes, and on a writable store performs the boot write. **Never fatal**: `cfg` is filled with defaults on any failure; `repairs` names both sanitized fields and values the flash held but this build could not take. |
| `ot_config_nvs_save(cfg)` | Writes the whole document back. Refuses a read-only (rolled-back) document (`ERR_INVALID_STATE`). Not atomic across namespaces (NVS offers no such thing); the Wi-Fi pair is the one atomic unit. |
| `ot_config_nvs_save_wifi(wifi)` | The provisioning write of the pair as one blob under one key; separate so provisioning never rewrites broker settings. Not gated by read-only — it is a way back. |
| `ot_config_nvs_commit_known_good() / restore_known_good(out)` | Promote the current pair to the known-good slot / roll the known-good pair back. `ESP_ERR_NVS_NOT_FOUND` when there is nothing to promote/restore. Not read-only-gated. |
| `ot_config_nvs_has_credentials() / has_known_good()` | Whether a **usable** pair is stored (`ot_config_wifi_usable`, not "the key exists"). Feeds the AP decision. |
| `ot_config_nvs_load_prov_flags(win, no_addr)` / `save_prov_flags(...)` | The two facts a reboot must not erase. Loader never fails; false is the safe default for both. Saved on a transition, both in one commit. |
| `ot_config_nvs_reset(tier)` | Erases every namespace the tier names with `nvs_erase_all()` (never a key list); every namespace attempted even after a failure; returns the first error. |
| `ot_config_device_kdf() / device_hash_ctx(ctx, salt)` | The device's PBKDF2 function and a hash context with a fresh salt. |

## Implementation

Cut into eleven files: the pure half (six, all host-built) and the device half (five, `#ifdef
ESP_PLATFORM`). The split is enforced by CMake compiling every `.c` in the directory for the host
build — a device-half file without the guard breaks every host suite that includes `ot_config.h`.

Pure half:

- **`ot_config.c`** (253) — the field table `FIELDS[]` (name / NVS key / namespace / secret flag),
  the `RETIRED_KEYS[]` list, the namespace names, the reset-tier logic, `ot_config_project()` and
  `ot_config_schema_check()`.
- **`ot_config_check.c`** (310) — every `ot_config_check_*()`, `ot_config_wifi_usable()`, and the
  `RANGES[]` bound table (with a `_Static_assert` that both flow limits sit on the half-degree grid).
- **`ot_config_apply.c`** (336) — `ot_config_apply_ex()`/`ot_config_apply()`: the largest function,
  alone in its file because every new field grows it (a check above the no-fail line, a commit below).
- **`ot_config_defaults.c`** (293) — `ot_config_defaults()` and `ot_config_sanitize()`, one file
  because sanitize repairs to exactly what defaults would have written; shares `default_prefix()`,
  `default_name()`, `DEFAULT_*`.
- **`ot_config_record.c`** (159) — the UI password record `1$<iters>$<salt hex>$<digest hex>`: hand
  parser (no `sscanf %s` over flash data), `ot_config_hash_password`, `ot_config_check_ui_password_against`.
- **`ot_config_strerror.c`** (118) — `ot_config_strerror()` and `ot_config_err_name()`, each a single
  `switch` with **no default** so `-Werror=switch` on the host catches a code added without a message.
- **`ot_config_internal.h`** — shared static-inline helpers (`is_hex`, `terminated`, `copy_str`,
  `into_band`) plus `password_record_t` and the exported `ot_config_parse_record`. Beside the sources,
  **not** in `include/`, so no other component can reach these names.

Device half:

- **`ot_config_nvs_load.c`** (214) — `ot_config_nvs_load()` and `boot_write()` (the migration and
  retired-key erase performed on every writable boot).
- **`ot_config_nvs_save.c`** (145) — `ot_config_nvs_save()` and `ot_config_nvs_save_wifi()`.
- **`ot_config_nvs_io.c`** (196) — the field-by-field NVS helpers (`ot_config_io_*`): load/store one
  value by field id, warn by key name only, erase a key / the retired list. `erase_retired()` lives
  here because both the loader and the saver call it.
- **`ot_config_nvs_recover.c`** (192) — the known-good rollback, `has_credentials`/`has_known_good`,
  the provisioning flags, and the two-tier reset (`erase_namespace`).
- **`ot_config_nvs_kdf.c`** (51) — the one file that includes mbedtls and `esp_random`: PBKDF2-HMAC-SHA256
  and the salted hash context.
- **`ot_config_nvs_internal.h`** — the shared `ot_config_io_*` declarations and the `TAG`. Guarded by
  `#ifdef ESP_PLATFORM` so a host file including it by mistake gets nothing, not a missing `nvs.h`.

The load/save split exists because the loader and saver together would otherwise put one device-half
file over the 350-line ceiling: the loader owns the boot write and migration, the saver owns the
plain write-back, and the field-by-field helpers they share sit in `ot_config_nvs_io.c`.

### Data structures

- **`ot_config_t`** — the whole document in memory: the Wi-Fi record, MQTT block, name/tz/ntp, the
  `dhw_enable` bit, the executor's settings (control mode, heating season, watchdog, failsafe triple,
  flow band, LOCAL command, DHW setpoint), the four room-slot fields, the `ui_pw_hash` record and a
  `read_only` flag. **DO NOT** hand it to a renderer or log — it holds the broker password in
  plaintext (MQTT needs the cleartext to authenticate) and the UI record beside it; `ot_config_project()`
  is the only sanctioned exit.
- **`ot_wifi_t`** — SSID and PSK with explicit lengths, **neither NUL-terminated** and neither with
  room to be; read exactly `*_len` bytes. One struct, one NVS key.
- **`ot_config_public_t`** — the projection: terminated `ssid`, secrets as sentinel/`""`, a derived
  `ui_password_set`. A renderer may `strlen()` every pointer.
- **`ot_config_patch_t`** — a decoded POST body. A **NULL string means the key was absent** (leave the
  stored value); an empty string clears it — the whole point of the `ot_secrets` sentinel scheme.
  Numbers carry a `has_*` flag and are `uint32_t` so an over-large value reaches the range checker
  whole instead of wrapping.
- **`ot_config_repairs_t`** — a `uint32_t` bitmask keyed by `ot_config_field_t` ids
  (`OT_CONFIG_REPAIRED(field)`), reused by apply's `moved`, sanitize's return, and the loader's `lost`.

### NVS persistence

- **Three namespaces**, and which one a field lives in is what decides whether a soft reset destroys
  it: `cfg_wifi` (the pair + known-good + provisioning flags), `cfg_owner` (the UI password record),
  `cfg_app` (everything else — broker, name, executor settings, room slot, schema number).
- **Schema** (`OT_CONFIG_SCHEMA_VERSION = 2`) lives in `cfg_app`, the namespace a soft reset keeps.
  Bumped only when the *meaning or layout* of a stored value changes — schema 2 moved the DHW bit from
  `cl_dhw_en` to `dhw_en`. Adding a key with a fresh-read default is **not** a bump.
- **Retired keys** (`cl_dhw_en`, `cl_auto`, `cl_man_ch`) are erased on **every writable boot and every
  save**, whatever the schema number — because NVS erases only by namespace, a retired key otherwise
  lingers for an OTA rollback to read back as live, and a later field reusing the name inherits a
  stale value. This is why the erase is *not* gated on the schema.
- **Migration 1→2** is the same read pass with two extra steps in a power-cut-safe order: carry the
  DHW bit under its new key first, erase the retired keys, write the schema number last.
- Booleans go through NVS as **u8**, never a raw `_Bool` (flash can hand back `0xff`, which a `_Bool`
  may not legally hold); the Wi-Fi record is a **whole blob** where the size *is* the version. Numbers
  are stored as u16 (there are only u16 helpers), which is why the in-memory fields are u16 even where
  only a small enum is legal — a u8 would let a damaged `0x0101` read back as a valid-looking `1`.

### Error enum (`ot_config_err_t`)

Values are compared in stored tests and mapped to HTTP codes, so the enum is **append-only** — new
codes (`ERR_TZ`, `ERR_NTP`, `ERR_FLOW`, `ERR_FAILSAFE`, `ERR_MODE_NEEDS_BROKER`, `ERR_RANGE`,
`ERR_READ_ONLY_FIELD`, `ERR_BROKER_PAIR`) are appended, never inserted. Both `ot_config_strerror()`
(owner sentence) and `ot_config_err_name()` (page token) are exhaustive switches with no default so
`-Werror=switch` plus a test catch a code with no message/name. `ERR_READ_ONLY_FIELD` is returned by
`ot_wire`, never by `ot_config_apply()` (the executor persists those fields through apply, so apply
must accept them).

### Invariants and "DO NOT" constraints

- **No `ESP_ERROR_CHECK` on anything from flash** — a bad stored value that aborts boot is an
  unrecoverable reboot loop on a wall device.
- **The Wi-Fi pair is one record under one key** — a power cut must never land between SSID and PSK.
  DO NOT split it "for symmetry with the API".
- **One answer to "is a network stored"** — `ot_config_wifi_usable()` is used by both the AP decision
  and the sanitizer. Two predicates once produced "present to the state machine, cleared in the
  document" = no network, no AP, no way back. DO NOT reduce it to an SSID length test.
- **No byte is reserved for a terminator** in the Wi-Fi record; `terminated()` guards every stored
  string before a checker `strlen()`s it (verified against a real ASan out-of-bounds read).
- **Secrets are write-only and never logged** — `ot_config_project()` returns the sentinel; store
  warnings name the key, never the value; cleared passwords are `memset`, not just terminated (the
  tail goes to flash too).
- **A read-only (rolled-back) store refuses settings writes** but never the ways back
  (`save_wifi`, known-good, prov flags) — otherwise a rolled-back device can be left with no network
  and no AP.
- **The UI "password set" flag is derived, never a second key** — a half-written second key would lock
  the owner out of a password that does not exist. An **unreadable** UI record is *cleared*, leaving the
  device open on the LAN (visible, fixable) rather than sealed. DO NOT "harden" this into keeping it.
- **KDF iteration cap** (`OT_CONFIG_KDF_ITERATIONS_MAX`, 100× the compiled 10000): the writer and the
  parser share one constant so a stored record can never be one the reader refuses; it bounds a corrupt
  digit string to seconds, not seventeen minutes of PBKDF2 per login (there is no lockout, by design).
- The salt need only be **unique, not unpredictable** — `esp_fill_random` before Wi-Fi starts is
  acceptable **here and nowhere else**. DO NOT copy that reasoning to a key or a token.

## Tests

The pure half is covered on the host by the `test_config_*` suites (referenced by name in the source):
`test_config_names` pins every NVS name / namespace literal and forbids a live field from reusing a
retired key; `test_config_sanitize` runs `ot_config_wifi_usable`, `has_credentials` and the sanitizer
over one table so the AP decision and the document can never disagree; `test_config_merge` covers the
merged-document cross-field rules; plus named assertions
`test_every_field_has_a_bit_of_its_own_in_the_repair_set` (≤ 32 fields) and
`test_every_refusal_has_a_name_of_its_own` (every `err` has a token). The device half
(`ot_config_nvs_*.c`) has **no dedicated host suite** — it is compiled out on the host by design, since
a host test of it could only exercise a fake NVS.

## Notes

- **Two storage policies, one disclosure policy**: the broker password is stored recoverably (MQTT
  needs the plaintext to authenticate); the UI password is a one-way record. Neither is ever returned.
- **Identity is the MAC, never the name**: the topic prefix and default name key off the device
  id so a rename never re-keys anything (which would strand a dead device in Home Assistant).
- **Two cross-field rules run on the merged document** (`ERR_FLOW`, `ERR_FAILSAFE`): each field can be
  legal alone while the pair is a band with no legal setpoint. On the way in they refuse; on the way
  out the sanitizer resets the whole triple to defaults (a kept half is a band nobody chose).
- **`local_ch_setpoint_dc` is moved, not refused**, when a saved/repaired flow band no longer contains
  it — it is read-only on the wire, so a refusal the owner cannot resolve would let `ot_control` clamp
  it silently while `GET /api/config` shows the stale value.
- The default device name is Cyrillic (the one sanctioned exception to the English-everywhere rule),
  which is why `OT_CONFIG_NAME_MAX` counts **bytes** (48), not characters.
- When a broker host changes while the stored broker password is kept (the page renders it as the
  sentinel), the change is refused (`ERR_BROKER_PAIR`) until the password is re-entered — otherwise
  that password would be sent in cleartext to the new, possibly attacker-chosen host.
