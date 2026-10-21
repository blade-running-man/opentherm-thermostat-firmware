# Configuration storage

Everything configurable lives in NVS and is entered at runtime; nothing is set at compile time and
no secret is in the binary. This page covers what is stored, where, with which defaults and bounds,
how a stored value is kept from bricking the device, how secrets stay write-only, and the two
things outside the configuration document that also survive a reset: the executor's `RTC_NOINIT`
record and the heat-hours count. How the owner edits these values is on
[Configuration](/guide/configuration); the HTTP surface is on [REST API](/interfaces/rest-api).

| Component | Role |
| --- | --- |
| `ot_config` | the stored document. A **pure half** (field table, validators, defaults, all-or-nothing apply, sanitize, the password record, schema decisions, reset tiers; host suites `test/test_config_*`) and a **device half** (`#ifdef ESP_PLATFORM`, the NVS I/O; never host-built) |
| `ot_secrets` | pure: the write-only sentinel, the keep/clear/store/reject decision, the constant-time compare. Host suite `test/test_secrets` |
| `ot_net` | owns the **live** document: loaded once at start, mutex-guarded, handed out only projected |
| `ot_thermostat_persist.c` | the `RTC_NOINIT` record and the heat hours |

## Why the document is built this way

A bad value stored once outlives every reboot, and a wall thermostat that panics on its own
configuration has no way back over the air. Hence:

- **No `ESP_ERROR_CHECK` on anything that came from flash.** A bad stored value that aborts boot is
  an unrecoverable reboot loop.
- **Validation on the way in.** Every refusal is decided once, in `ot_config_check_*()`, so the
  HTTP and settings surface never invents its own idea of what is legal.
- **Repair on the way out.** `ot_config_sanitize()` re-runs the validators over whatever the flash
  handed back and replaces anything unusable with its default; it returns a bitmask of the repaired
  fields (named in the log by key, never by value).
- **One live document.** `ot_net` holds it; the HTTP layer never sees `ot_config_t` and never opens
  NVS, so it cannot grow a second idea of a valid document. `ot_config_room_mqtt()` is declared in
  `ot_config` but implemented in `ot_net_config.c` for the same reason.

`app_main`'s own `nvs_init()` erases and re-initialises the whole partition when NVS reports
`ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`: every setting lives there, and a
refusal to boot would leave dead a device that merely needs its settings reset. The price is the
settings, never the operation.

## NVS layout

The NVS partition is 16 KiB at `0x9000` (see [Board](/device/board#partition-table)). Which
namespace a field lives in decides what a reset destroys. Names are part of the stored contract:
`test_config_names` pins every literal and forbids a live field from reusing a retired key. Keys are
at most 15 characters.

| Namespace | Key | Type | Contents |
| --- | --- | --- | --- |
| `cfg_wifi` | `sta` | blob | the Wi-Fi SSID and key as **one** record |
| `cfg_wifi` | `sta_good` | blob | the known-good pair (the rollback target) |
| `cfg_wifi` | `win_closed` | u8 | a setup window has run out at some point |
| `cfg_wifi` | `no_addr` | u8 | the station joined and never got an address |
| `cfg_owner` | `ui_pw_hash` | string | the one-way web-UI password record |
| `cfg_app` | `schema` | u32 | the stored layout version (2) |
| `cfg_app` | every other field below | u8 / u16 / string | broker, identity, time, executor settings, room slot |
| `ctl` | `heat_h` | u16 | the executor's heat hours (not part of `ot_config`) |

- **The Wi-Fi pair is one record under one key**, so a power cut can never land between the SSID
  and the key. The record is a blob whose size *is* its version, and neither field is
  NUL-terminated: `ot_net` copies exactly the stored lengths, so a 32-character SSID and a
  64-character key stay legal.
- **Booleans are stored as u8**, never a raw `_Bool`: flash can hand back `0xff`, which a `_Bool`
  may not legally hold. Numbers are u16 (there are only u16 helpers), which is why in-memory fields
  are u16 even where only a small enum is legal: a u8 would let a damaged `0x0101` read back as a
  valid-looking `1`.

### Schema and retired keys

`OT_CONFIG_SCHEMA_VERSION` is 2. It is bumped only when the *meaning* or layout of a stored value
changes (schema 2 moved the hot-water bit from `cl_dhw_en` to `dhw_en`); adding a key whose absence
reads as its default is not a bump. `ot_config_schema_check()` answers `CURRENT`, `MIGRATE` (stored
below current, including 0 for a fresh store) or `FUTURE`.

- **Migration 1 → 2** is the same read pass with two extra steps in a power-cut-safe order: carry
  the hot-water bit under its new key first, erase the retired keys, write the schema number last.
- **Retired keys** (`cl_dhw_en`, `cl_auto`, `cl_man_ch`) are erased on **every writable boot and
  every save**, whatever the schema number. NVS erases only by namespace, so a retired key would
  otherwise linger for a rolled-back firmware to read as live, and a later field reusing the name
  would inherit a stale value. The list is append-only.
- **A newer schema** (a store written by a later firmware) loads **read-only**: `read_only` is set,
  `GET /api/config` reports it, and settings writes are refused with `read-only`. The ways back
  (saving a Wi-Fi pair, the known-good pair, the provisioning flags) are never gated by it, or a
  rolled-back device could be left with no network and no access point.

## The fields

Temperatures are stored in tenths of a degree (`_dc`); there is no floating-point field. The API
name is what `GET` and `POST /api/config` use; the bounds are `ot_config_check_*()`'s.

### Network, broker, identity and time

| Field | NVS key | Default | Accepted |
| --- | --- | --- | --- |
| `wifi_ssid`, `wifi_psk` | `sta` (`cfg_wifi`) | empty (the device runs its own access point) | SSID 1–32; key 8–63 characters, exactly 64 hex digits, or empty for an open network. Written only through `POST /api/provision`; `/api/config` refuses both |
| `mqtt_host` | `mqtt_host` | empty (no broker) | a host name or IP address, no scheme, no port, at most 253 |
| `mqtt_port` | `mqtt_port` | 1883 | 1–65535 |
| `mqtt_user` | `mqtt_user` | empty | at most 128 |
| `mqtt_password` | `mqtt_password` | empty | at most 128; write-only |
| `topic_prefix` | `topic_prefix` | `opentherm/<device_id>` | 1–64, no spaces, `+` or `#`, no empty level |
| `ha_discovery` | `ha_discovery` | true | boolean |
| `device_name` | `device_name` | `Термостат OpenTherm <last 4 hex of the MAC>` | 1–48 **bytes** (a Cyrillic letter takes two), no control characters |
| `tz` | `tz` | `MSK-3` (POSIX TZ; the sign is inverted, so this is UTC+3) | non-empty POSIX TZ without spaces, at most 63 |
| `ntp_server` | `ntp` | `pool.ntp.org` | a non-empty host name or IP address |
| `ui_password` | `ui_pw_hash` (`cfg_owner`) | empty (no password: the device is open on a trusted LAN) | 8–128, no control characters; write-only |

`<device_id>` is the station MAC as 12 hex characters; a malformed id falls back to `opentherm` and
a name without digits, a worse default (two units would share a topic tree) but better than refusing
to boot. The default device name is Russian: it is the owner's Home Assistant device name, the one
place Russian is kept, and renaming it would rename that device.

### Executor and room source

| Field | NVS key | Default | Accepted |
| --- | --- | --- | --- |
| `control_mode` | `ctl_mode` | 0 (LOCAL) | 0 LOCAL, 1 HA; HA refused while `mqtt_host` is empty |
| `heating_season` | `season` | **false**: a fresh flash never asks for heat | boolean; written through the entity path only |
| `watchdog_s` | `wd_s` | 900 | 60–7200 |
| `failsafe_setpoint_dc` | `fs_sp` | 450 (45.0 °C) | 100–900, step 5, and inside `[flow_min_dc, flow_max_dc]` |
| `failsafe_room_target_dc` | `fs_room` | 180 (18.0 °C) | 50–300 |
| `failsafe_heat_days` | `fs_days` | 3 | 1–30 |
| `failsafe_min_cycle_s` | `fs_cycle` | 600 | 60–3600 |
| `flow_min_dc`, `flow_max_dc` | `flow_min`, `flow_max` | 400, 700 (40.0, 70.0 °C) | 100–900, step 5, and `min < max` |
| `local_ch_enable` | `loc_ch` | false | boolean; entity path only |
| `local_ch_setpoint_dc` | `loc_ch_sp` | 450 | 100–900, step 5; entity path only |
| `dhw_enable` | `dhw_en` | **true**: the only non-zero ID 0 bit on a fresh flash | boolean; entity path only |
| `dhw_setpoint_dc` | `dhw_sp` | 0 (unset: no ID 56 goes out until someone writes one) | 0–900; entity path only |
| `room_mqtt_enable` | `room_en` | false | boolean |
| `room_mqtt_role` | `room_role` | 1 (room) | 0–1 |
| `room_mqtt_stale_s` | `room_stale` | 900 | 10–65535 |
| `room_mqtt_ha_forwarded` | `room_ha_fwd` | false | boolean |

What each executor field does is on [Executor](/architecture/executor); the room fields on
[Room sources](/device/room-sources). A key missing from flash (a store written before the field
existed) reads as its default.

- **Five fields belong to the controls.** `heating_season`, `local_ch_enable`,
  `local_ch_setpoint_dc`, `dhw_enable` and `dhw_setpoint_dc` are rendered by `GET /api/config` but
  refused by name in a settings body (`read-only-field`): they are written through
  `POST /api/entities/<key>` or MQTT, so each value has one validator and one ownership check. The
  executor persists them through `ot_config_apply()`, which therefore accepts them.
  `heating_season` in particular is one Home Assistant may only turn off: a settings page loaded in
  season and saved after Home Assistant turned it off would otherwise send `true` back and heat
  behind its back.
- **Two cross-field rules run on the merged document** (the patch's value where it has one, the
  stored one where it does not): `flow_min_dc < flow_max_dc` (`flow`) and the failsafe setpoint
  inside the band (`failsafe`). Each field can be legal alone while the pair is a band with no legal
  setpoint. On the way out, the sanitizer resets the whole triple to defaults; a kept half is a band
  nobody chose.
- **`local_ch_setpoint_dc` is moved, not refused**, when a saved band no longer contains it: it is
  pulled to the nearest 0.5 K edge and the repair is reported. A refusal would be one the owner
  cannot resolve (the field is read-only in the settings), while `ot_control` clamped it silently
  and `GET /api/config` showed the stale value.
- A failsafe setpoint at or above the boiler's own minimum (a panel parameter, not readable over
  OpenTherm) is the owner's duty.
- **Changing `mqtt_host` while keeping the stored broker password is refused** (`broker-pair`) until
  the password is re-entered: otherwise the stored password would go in cleartext to the new,
  possibly attacker-chosen, host. The Wi-Fi pair follows the same rule (`wifi-pair`).

## Applying a change

`ot_config_apply_ex()` is **all-or-nothing**: every field is validated before anything is written,
and a rejected patch changes nothing in memory or flash. An absent field leaves the stored value; an
absent document is `no-document` (a patch with every field absent is a legitimate "nothing
changed" save). Numbers arrive as `uint32_t`, so 70000 reaches the port checker whole and is refused
instead of wrapping to 4464. Unknown keys in a body are ignored, which lets a newer page talk to an
older firmware.

`ot_config_err_t` is append-only (values are compared in stored tests and mapped to HTTP codes).
Each code has an owner-facing sentence (`ot_config_strerror()`, which never echoes the value) and a
short token the settings page matches on (`ot_config_err_name()`; `rejected` for a code it does not
know). Both are exhaustive switches with no default, so `-Werror=switch` on the host catches a code
added without its text.

| Token | Refusal |
| --- | --- |
| `ssid`, `psk`, `wifi-pair` | the Wi-Fi name, key, or one half of the pair without the other |
| `mqtt-host`, `mqtt-port`, `mqtt-user`, `mqtt-password`, `broker-pair` | broker fields, and a host change that keeps the old password |
| `topic-prefix`, `device-name`, `tz`, `ntp-server` | identity and time |
| `ui-password` | the web password |
| `no-hash` | the device could not hash the password just now; nothing changed and retyping will not help |
| `read-only` | the store was written by a newer firmware |
| `no-document` | the request carried no settings |
| `flow`, `failsafe`, `mode-needs-broker`, `range` | the executor's rules; `range`'s sentence lists every bound, because the code travels without its field |
| `read-only-field` | a control-owned field in a settings body (returned by `ot_wire`, never by `ot_config_apply()`) |

## Secrets are write-only

Three values are secrets: the Wi-Fi key, the broker password and the web-UI password. No API
projection returns one. `ot_config_project()` is the only sanctioned exit from `ot_config_t`, and
it renders a set secret as the sentinel `__UNCHANGED__` (`OT_SECRET_SENTINEL`, which must match
`CONFIG_UNCHANGED` in `web/src/api/secrets.ts`) and an unset one as `""`. A form can therefore
post a secret field back unchanged without wiping a credential. That does not make the whole `GET`
document a valid `POST`: `wifi_ssid` and `wifi_psk` (set through `POST /api/provision`) and the five
executor-state keys are refused by name, so the settings page posts a hand-picked subset. A
submitted secret is decided as follows:

| Submitted value | Stored? | Decision (`ot_secret_decide()`) |
| --- | --- | --- |
| key absent | any | keep |
| the sentinel | yes | keep (the page handed back what it was given) |
| the sentinel | no | **reject**: storing it would make a published string the password of every unconfigured device |
| `""` | yes / no | clear / keep |
| anything else | any | store |

- **Two storage policies, one disclosure policy.** The broker password is stored recoverably,
  because MQTT authenticates with the plaintext; `ot_net_broker()` is the one un-redacted
  projection and goes only to `ot_mqtt_link`. The web-UI password is stored as a one-way record,
  `1$<iterations>$<salt hex>$<digest hex>`: PBKDF2-HMAC-SHA256, 10000 iterations, a 16-byte salt,
  verified in constant time. The iteration count a stored record may claim is capped at 100× that,
  so a corrupt digit string costs seconds, not minutes of PBKDF2 per login. The salt need only be
  unique, which is why `esp_fill_random` before Wi-Fi starts is acceptable here and nowhere else.
- **"A password is set" is derived** from the record being non-empty, never a second stored flag: a
  half-written second key would lock the owner out of a password that does not exist. An
  unreadable record is **cleared**, leaving the device open on the LAN (visible, fixable) rather than
  sealed.
- `ot_secret_key()` names a key secret by a case-insensitive substring (`password`, `passwd`, `psk`,
  `secret`, `token`, `key`), so a future field is redacted without anyone extending a list;
  over-redaction costs a retype, under-redaction publishes a credential.
- `ot_secret_equals()` reads every byte of the longer string and folds the length difference into
  the result, so timing does not reveal where a guess first differs.
- Nothing that holds a secret reaches `ESP_LOG*`: the log ring is served to anyone who can reach
  `GET /api/log`. Cleared passwords are zeroed, not just terminated.

## Resets

`ot_config_reset_erases()` defines two tiers: **hard** erases every namespace in the enum (so a
namespace added later is erased without anyone extending a list), **soft** erases `cfg_wifi` and
`cfg_owner` only, keeping the broker, the name and the executor settings.
`ot_config_nvs_reset(tier)` carries it out with `nvs_erase_all()` per namespace. The tiers exist
as this API only: **nothing in the firmware calls `ot_config_nvs_reset()`**, and no button gesture
or endpoint reaches either tier. Neither tier names the `ctl` namespace.

## What survives outside the document

### The `RTC_NOINIT` record

`ot_thermostat_persist.c` keeps the executor's two counts that must survive a soft reset in
`RTC_NOINIT` memory, which a software reset, a panic and every watchdog leave alone and a power-on
fills with noise:

| Field | Why it must survive |
| --- | --- |
| `overdue_ms`, the Home Assistant watchdog's overdue count | a reboot loop faster than `watchdog_s` (a panic on an MQTT payload, say) would otherwise restart the watchdog on every boot and hold `ha_waiting` with heating off for ever, one master-silence heat pulse per boot |
| `hh_ms`, the heat-hours part-hour | a loop faster than an hour would otherwise never complete a powered hour, so the summer bound would never disarm |

The record is `{magic, overdue_ms, hh_ms, check}`, magic `0x4F544332` ("OTC2"),
`check = ~(magic ^ overdue_ms ^ hh_ms)`; both counts are written together every step and believed
together or not at all. `ot_control_io_restore()` (pure, host-tested) believes the blob only after a
reset that keeps RAM (`sw`, `panic`, `int_wdt`, `task_wdt`, `wdt`, `brownout`) **and** with magic
and check intact. Any other reason, power-on included, starts from zero, which costs at most one
watchdog period. Brown-out is listed because a brown-out loop is the commonest reboot loop of an
ESP32-C3; the check word, not the reason, rejects RAM the dip corrupted. `esp_reset_reason_t` is
passed by value, and `_Static_assert`s pin every value to ESP-IDF's enum, so a renumbered release
cannot turn a power-on into a software reset.

::: warning Do not move it to `.bss`
Zeroed at every boot, the record would restart the watchdog on every reboot; that is exactly the
reboot-loop failure it exists to prevent.
:::

### Heat hours

The whole heat hours ("powered hours since Home Assistant last asked for heat", saturating at
65535) live in NVS, namespace `ctl`, key `heat_h`, not in `ot_config`: the executor writes them by
itself, at most once an hour, and a settings document that carried them would be one more writer of
a value the executor owns. Nothing stored reads as **disarmed** (Home Assistant has never asked for
heat). A failed write is logged and not retried; the next hour asks again.

On a step that completes an hour, the NVS write goes **before** the `RTC_NOINIT` mirror. The two
stores cannot be atomic; saving first means a reset between them errs safe (one extra hour banked,
the summer failsafe disarms sooner), whereas mirroring first would lose the hour from both (it
disarms later, prolonging an unwanted summer burn). The boot log line names the reset reason and
what was restored, the one clue a reboot loop leaves.
