# ot_net — the half of provisioning that owns the radio, the flash and the clock

`ot_net` brings the device onto a network (Wi-Fi, station or its own access point) and keeps
it reachable when that fails. It is the impure counterpart of `ot_provision`: the pure state
machine next door decides *what the device should be doing*, and `ot_net` feeds it facts,
reads back a mode and an action, and carries them out against `esp_wifi`, NVS and the clock.

## Responsibility

**Owns**

- The radio. There is **one owner** — the provisioning task started by `ot_net_start()` — and
  every `esp_wifi` call in the component runs under a single lock that task takes. `ot_net.h`
  forbids any `esp_wifi` call from outside this component: two tasks driving one radio is how a
  mode change lands between a `set_config` and a `connect`.
- The stored configuration document (`ot_config_t`, loaded once at start), the Wi-Fi
  credentials in NVS, the known-good/rollback bookkeeping, and the two durable provisioning
  flags.
- Device identity derived from the MAC (device id, MAC string), the access-point SSID, and the
  currently-held IP address.
- The captive-portal DNS responder (`ot_captive`), which is raised and dropped in lockstep with
  the access point.

**Does NOT do**

- **Decides nothing.** Every mode/action/deadline is `ot_provision`'s answer; this component
  only executes it and reports what actually happened. That split is why the whole provisioning
  behaviour is host-tested in `test/test_provision` as lines of C rather than waits in front of
  a device.
- **No HTTP.** It has no business answering HTTP; it only owns the captive DNS *socket*, not the
  table of probe paths (that belongs to `ot_http`).
- **Never reboots because a peer is absent, and never `ESP_ERROR_CHECK`s anything downstream of
  stored or received data.** A radio or a configuration that will not come up must leave a
  device that still drives the boiler and still says why over USB; a panic there is an
  unreachable reboot loop.
- **Does not own the MQTT broker's lifecycle.** A broker lifecycle inside `ot_net` would have to
  call `ot_thermostat`, which in turn requires `ot_net` — a dependency cycle that will not link.
  The MQTT glue therefore lives in its own component, `ot_mqtt_link`, which only *reads*
  `ot_net.h` (`ot_net_broker()`). The rule stated in the header applies here: a new responsibility
  becomes a new sibling component, not a paragraph grown into one of these files.

## Public API

Declared in `include/ot_net.h`. All accessors take the machine lock for a snapshot or a single
committed change and then let go, so the fields of any one call agree with one another.

### Lifecycle & state

| Function | Contract |
| --- | --- |
| `ot_net_start(board, cb, ctx)` | Loads the document, builds the locks/queue/netifs, inits Wi-Fi, registers the event handler and starts the provisioning task. Refuses a non-Wi-Fi board with `ESP_ERR_NOT_SUPPORTED`. May return *without* creating its lock (bad board, OOM); every accessor is written to survive that. |
| `ot_net_get_state()` | Current `ot_net_state_t`: `OT_NET_DOWN` / `OT_NET_ACCESS_POINT` / `OT_NET_CONNECTED`. |
| `ot_net_cb_t` (typedef) | State-change callback, invoked from the provisioning task. Keep it short. |
| `ot_net_has_credentials()` | Whether a usable stored pair exists. **Not** "the device is claimed". |
| `ot_net_is_provisioned()` | The single most important question this component answers. **False for as long as an open access point is on the air**, whatever NVS holds — in that moment anyone in radio range is "the client", so storing credentials must not flip it. **Asked, never pushed**: a pushed copy would be a tick out of date, and that tick is exactly the window this exists to close — the access point goes off the air the instant the station gets an address, and a policy holding last tick's answer would refuse a write that is now legitimate, or allow one that is not. |

### Provisioning

| Function | Contract |
| --- | --- |
| `ot_net_provision(wifi)` | Stores the pair as one blob, then queues a trial. **Returns as soon as the pair is stored** (an invariant, not an optimisation): the station tunes to the router's channel and drags the access point with it, so the phone holding the setup page drops — the answer must go out first, or everything that worked looks like a failure. Does not validate the pair; the caller has already run `ot_config_wifi_usable()` to build the record. |
| `ot_net_provision_body(body, result)` | The same thing straight from a POST body, decoded **here** under the lock via `ot_wire_parse_provision()`. Decoding is done here because the "keep the stored key" third state of `wifi_psk` requires reading the stored key, and a request handler that could read the key could also log it (the log ring is served to anyone reachable via `GET /api/log`). The body comes in and the answer goes out while the key stays on this side of the wall. `result` may be NULL; when set, it carries why a body was refused in a form a handler can turn into a status code and field name. |
| `ot_net_note_client()` | Queues "somebody is on the access point" to extend the setup window — the window closing while the owner is in the next room reading the password off their router is a common, harmless failure, not an attack. Cheap and safe from a request handler. |

### What the owner is told

| Function / type | Contract |
| --- | --- |
| `ot_net_scan(out, cap, count)` | One synchronous, seconds-long sweep of what the *device* can see (2.4 GHz only on the C6, so not the same list the phone sees). Takes the radio lock, so the provisioning tick waits behind it and a phone served from the device's own access point may lose the setup page mid-sweep; nothing retries or reboots for that, and manual entry stays available. Duplicate SSIDs are **not** filtered — one record per BSSID, so a mesh answers several times under one name. Bounded by `OT_NET_SCAN_MAX` (32) because the buffer is static; the row is `ot_wire_network_t` (aliased `ot_net_scan_entry_t`). |
| `ot_net_status(out)` | A snapshot, under the lock, of everything the provisioning status document is built from (`ot_wire_provision_t`, aliased `ot_net_status_t`). Carries the configured SSID (not a secret — it is the one fact that makes "it will not connect" diagnosable), **never a PSK**, and no SSID of any other network. |

### Configuration document

The document is loaded here, lives here, and leaves only projected — the HTTP layer never sees
`ot_config_t` and never opens NVS, so it cannot grow a second idea of a valid document or of
what "password set" means.

| Function | Contract |
| --- | --- |
| `ot_net_config_snapshot(out, known_good)` | Projects the document (`ot_config_public_t`) with every secret already replaced by its sentinel, under the same lock as `known_good`. `known_good` is the machine's answer to "has a stored pair ever actually produced an address" — the only condition under which a failed provisioning rolls back — and is not part of the stored document. Both are taken under one lock so the two describe one moment. `known_good` may be NULL. |
| `ot_net_config_apply(patch)` | Validates the patch, applies it all-or-nothing, and persists it. A rejected patch changes nothing in memory or flash, so the owner can retry against the document they were looking at. Before `ot_net_start()` has created its lock → `OT_CONFIG_ERR_NO_DOCUMENT`, nothing taken. |
| `ot_net_check_password(candidate)` | Constant-time compare against the stored web-UI password hash. The hash never leaves the component. |
| `ot_net_broker(out)` → `ot_net_broker_t` | **The one un-redacted projection**: MQTT settings **with the password in the clear**, because MQTT authenticates with the plaintext and a hashed broker password is a broker that never connects. **This struct must never reach `ESP_LOG*`.** Filled under the lock; the caller owns it and should keep it on a stack that dies quickly. |
| `ot_net_device_id(out)` / `ot_net_mac_string(out)` | Full MAC as 12 lower-case hex chars / colon-separated address. Identity is the MAC, never the name. |
| `ot_net_ip_string(out)` | Current address, or `""`. For Home Assistant's `configuration_url`, whose validator refuses a value without a scheme. |
| `ot_net_password_set()` | Derived from the stored password record being non-empty; the access policy asks this rather than keeping its own flag, so the two cannot disagree about whether the device has an owner. |

Note: `ot_config_room_mqtt()` (declared in `ot_config.h`, not `ot_net.h`) is also **implemented
here** in `ot_net_config.c`. It is the one function of its shape implemented outside its own
component, because `ot_config` keeps no live document to copy from while `ot_net` does. Same
shape as `ot_net_broker()`: a copy under the machine lock.

## Implementation

The component is four sources plus a private header, cut where the responsibilities already met.
The public `include/ot_net.h` is the interface. `CMakeLists.txt` uses `REQUIRES` (not
`PRIV_REQUIRES`) for `board`, `ot_config` and `ot_provision` because `ot_net.h` exposes their
types (`board_t`, `ot_wifi_t`, `ot_prov_state_t`) in its own interface, so anything including it
needs those directories too. `esp_timer` is named explicitly because `esp_timer_get_time()` is
the clock every `ot_provision` deadline is measured against, and it is not one of the IDF's
default-pulled requirements.

- **`ot_net.c`** — the shared state (defined once here, declared in the internal header:
  `s_config`, `s_prov`, the two locks, the event queue, the task handle, the netifs, the
  connection-progress booleans, the AP SSID and IP buffers), MAC-derived identity
  (`build_ap_ssid`, `device_id`), `otnet_report_repairs`, and `ot_net_start()` — which builds
  everything in the order written. Defaults are loaded into `s_config` **before** any early
  return, so an accessor reached after a lock-less start still projects a sane document (a zeroed
  one would drop a combi boiler's DHW bit).
- **`ot_net_prov.c`** — the provisioning task. `otnet_on_wifi_event()` runs on the system
  event task and **only enqueues** a fact (`post_event` never blocks; overflows are counted, not
  silently dropped). `otnet_net_task()` waits on the event queue with a 250 ms (`TICK_MS`)
  timeout, `feed()`s each event into the machine under the lock, then `step()`s: tick the
  machine, read mode/action/flags, release the lock, then act (`apply_action`,
  `otnet_apply_mode`, captive DNS, persist flags, publish state). Handles the known-good commit
  and the typo-recovery `RESTORE_CREDENTIALS`. `ot_net_provision()` and `ot_net_note_client()`
  post facts here.
- **`ot_net_radio.c`** — every `esp_wifi` call, under the radio lock. `otnet_apply_mode()`
  applies a mode only on change, holds a wanted `CONNECT` until the station reports ready, opens
  the access point **OPEN** (unauthenticated) on purpose, and copies credentials with a measured
  `memcpy` (not `strncpy` — the fields need no terminator, and reserving a byte would truncate a
  legal 32-char SSID / 64-char PSK). `otnet_captive_dns_follow_ap()` raises/drops the DNS
  responder with the access point. `ot_net_scan()` temporarily switches the station on for the
  sweep, restores the machine's mode after, and always `esp_wifi_clear_ap_list()`s to avoid a
  leak.
- **`ot_net_config.c`** — the read/commit accessors listed above, plus the out-of-component
  `ot_config_room_mqtt()`. Nothing here drives the provisioning machine.
- **`ot_net_internal.h`** — private to the directory (**not** in `include/`, on purpose: moving
  it would make the machine's lock and the stored document public API). Holds the single `TAG`,
  the `ev_kind_t`/`ev_t` event types, `extern`s for the shared state, the lock/clock/`radio_try`
  helpers, the `ot_prov_mode_t → wifi_mode_t` mapping (`wifi_mode_of`), and the cross-source
  declarations — all prefixed `otnet_`, never `ot_net_`, so a grep for the public API does not
  find them.

### Invariants & structures

- **Two locks, ordered radio-then-machine.** A request handler asking `ot_net_is_provisioned()`
  must not wait out a scan, and the task must not hold the machine while talking to the radio.
  Both are **recursive** mutexes because `apply_sta_config()` takes the machine from inside
  `apply_mode()`. **DO NOT** take the radio lock while holding the machine.
- **The event handler only enqueues** — no NVS write, no JSON parse on the system event task,
  whose stack would overflow and whose queue would block the one event the whole design turns on
  (`IP_EVENT_STA_GOT_IP`).
- **"We asked for an access point" and "an access point is on the air" are deliberately different
  events** — the setup window is anchored on the second.
- **Store credentials first, tell the machine after**, so a power cut cannot leave a trial
  running against a pair that is not in flash. Credentials are one blob under one key (NVS gives
  no atomicity between two).
- **Durable facts (known-good, the two provisioning flags) are written on transition only**, so a
  flapping router does not burn a flash cycle per tick.
- **DO NOT** switch the AP SSID MAC back to `ESP_MAC_WIFI_SOFTAP`: it must read `ESP_MAC_WIFI_STA`
  so the SSID string matches the device name and topic prefix (a real on-hardware `c38d`/`c38c`
  mismatch bug).

## Tests

No dedicated host suite lives in this component. The behaviour it executes is tested through the
pure state machine it drives: `test/test_provision` exercises every provisioning case as C. The
captive-DNS socket it drives is `ot_captive`'s (`test/test_captive`).

## Notes

- Ships Wi-Fi only, but the transport is *named*, not assumed (`board->net_transport`): the
  Olimex ESP32-EVB is Ethernet, and `ot_net_start()` refuses a non-Wi-Fi board loudly rather than
  pretending — that early return is where a second transport plugs in.
- Anything holding the broker password (`s_config`, `ot_net_broker_t`) must **never** reach
  `ESP_LOG*`: the log ring is served to anyone who can reach `GET /api/log`, and non-ASCII bytes
  come back escaped and unreadable anyway.
