# Networking

The device joins the owner's Wi-Fi as a station, and when it has nothing to join (or the network
it knows keeps failing) it raises its own open access point with a captive portal so the setup page
can be reached. It also keeps a wall clock over SNTP and carries the MQTT link to Home Assistant.
None of it is allowed to stop the conversation with the boiler, and nothing here reboots because a
peer is absent. This page covers who owns the radio, the provisioning rules, the captive portal,
time, the MQTT link's place, and what happens when the network goes away.

| Component | Kind | Role |
| --- | --- | --- |
| `ot_provision` | pure | the provisioning state machine: radio mode, setup window, trial and rollback, failure classification. Host suite `test/test_provision` |
| `ot_net` | impure | owns the radio, the stored configuration document, the MAC-derived identity and the captive DNS socket; executes `ot_provision`'s answers |
| `ot_captive` | pure + socket | the connectivity-probe answers and the DNS responder of the access point. Host suite `test/test_captive` |
| `ot_time` | impure | SNTP and the POSIX time zone |
| `ot_mqtt_link` | impure | the esp-mqtt client and its one task; see [MQTT](/interfaces/mqtt) |

The owner's procedure for joining a network is in [First setup](/guide/first-setup).

## One owner of the radio

`ot_net_start(board, cb, ctx)` loads the configuration document, builds the network interfaces,
initialises Wi-Fi and starts the provisioning task. Every `esp_wifi` call in the firmware runs
inside `ot_net`, under one radio lock; two tasks driving one radio is how a mode change lands
between a `set_config` and a `connect`. The Wi-Fi event handler runs on the system event task and
**only enqueues** a fact; the provisioning task (250 ms tick) feeds each fact into `ot_provision`
under the machine lock, asks it for a mode and an action, releases the lock, and then acts.

Two locks, always taken radio first, then machine: a request handler asking "is the device
claimed?" must not wait out a seconds-long scan, and the task must not hold the machine while it
talks to the radio.

`ot_net` decides nothing. Every mode, deadline and rollback is `ot_provision`'s answer, which is
why the multi-minute provisioning cases are one-line host tests rather than waits in front of a
device. `ot_provision` takes time as an argument on every call and never reads a clock.

### Identity

The device id is the full station MAC as 12 lower-case hex characters (`ot_net_device_id()`). It
seeds the MQTT topic prefix (`opentherm/<device_id>`), the MQTT client id and the Home Assistant
discovery identity. **Identity is the MAC, never the name**: renaming a device must not re-key
anything, or Home Assistant ends up holding two devices, one of them dead and unremovable.

The access point is named `opentherm-<xxxx>`, the last two bytes of the **station** MAC, so its
four digits match the default device name and the topic prefix. (Reading the soft-AP MAC instead
once produced an off-by-one `c38d`/`c38c` mismatch on hardware.)

## Radio modes

`ot_prov_mode()` maps the provisioning state to one of four modes:

| State (`/api/status` `state`) | Mode (`mode`) | Meaning |
| --- | --- | --- |
| `unconfigured` | `access-point` | no usable Wi-Fi pair is stored |
| `connecting`, `trial`, `retrying` | `ap-station` while the setup window runs, else `station` | joining, testing a newly saved pair, or backing off |
| `connected` | `station` | the station has an address |
| `fallback-ap` | `ap-station` | the known network keeps failing; the access point is back while the station keeps trying underneath |
| `window-closed` | `off` | the setup window ran out with nothing stored; the radio stays off until the next boot |

The access point is **open** (no WPA2) and admits at most four clients, on ESP-IDF's default
soft-AP address; `ot_net` and `ot_captive` hold no hard-coded address. Open is deliberate: a WPA2
key nobody has been told is a brick, and publishing a generated one through a sensor puts it
exactly where it cannot be read, since reading a sensor needs the network the key is for. What
bounds the exposure is the access policy and the setup window's clock, not the radio.

The three-valued state the rest of the firmware sees, `ot_net_get_state()`, is derived from those
facts: `OT_NET_CONNECTED` when the station holds an address, otherwise `OT_NET_ACCESS_POINT` when
the access point is on the air, otherwise `OT_NET_DOWN`.

## Provisioning

A Wi-Fi pair arrives through `POST /api/provision`, the one write an unclaimed device accepts.
`/api/config` refuses `wifi_ssid` and `wifi_psk` and points at that route. `ot_net_provision_body()`
decodes the body itself, under the lock, because the "keep the stored key" form of `wifi_psk` needs
the stored key, and a request handler that could read the key could also log it. The pair is stored
as **one record under one key**, and only then is the machine told; the call **returns as soon as
the pair is stored**, because the station tunes to the router's channel and drags the access point
with it, so the phone holding the setup page drops. The answer has to go out first.

### The rules the machine enforces

| Rule | Default |
| --- | --- |
| First setup window of the device's life, anchored on the access point coming **on the air** | 15 min |
| Ceiling on how far client activity can extend a window (`ot_net_note_client()` on every request) | 60 min from the access point coming up |
| Every later window (after one closed, or for a never-addressed station at the next boot) | 5 min |
| A newly saved pair must produce an address within (trial), else the known-good pair is restored | 90 s |
| An associated station must get an address within, else the fourth kind of failure | 30 s |
| Retry backoff, doubling, with **no attempt limit** | 1 s to 30 s |
| Sustained failure before the access point returns by itself | 30 min |

- **An open access point means unclaimed.** `ot_net_is_provisioned()` answers from
  `ot_prov_is_provisioned()`, and is false for as long as an open access point is on the air,
  whatever NVS holds: in that moment anyone in radio range is the client, so storing credentials
  must not flip it. The access policy **asks** it on every request; a pushed copy would be one
  tick out of date, and that tick is exactly when the station gets its address and the access
  point leaves the air.
- **"Stored" is not "connected".** A saved pair is on probation until it produces an address; only
  then is it committed as known-good (once per pair, not per reconnection). A mistyped password
  therefore cannot masquerade as a working connection, and the rollback restores the pair that
  last worked.
- **Four answers to the owner.** A disconnect reason is classified into `network-not-found`,
  `wrong-password`, `refused` or `no-address`, with one spelling shared by REST and MQTT. A wrong
  key has no reason code of its own (the access point stops replying after the handshake MIC
  check), so timeouts read as `wrong-password`. Any unlisted reason falls into `refused`, which
  counts toward the self-return clock, so a device failing for an unenumerated reason still
  raises its access point again.
- **There is never a state with no network, no access point and no way back.** A closed window
  has the next boot; a typo has the rollback; a failing network has the self-returning access
  point; a station that associates but never gets an address persists `address_never_arrived`, and
  the next boot reopens the access point for 5 minutes (the self-return clock deliberately excludes
  that failure).

Two facts must survive a reboot, `win_closed` (a window has run out) and `no_addr` (the station
never got an address); `ot_net` writes them on transition only, so a flapping router does not burn
a flash cycle per tick. See [Configuration storage](/device/config-storage).

`GET /api/status` renders the machine's view: `state`, `mode`, `provisioned`, `has_credentials`,
`wifi_known_good`, `connected`, `wifi_ssid` (the configured one, never a key), `ip`, `ap_ssid`,
`ap_on_air`, `setup_window_open`, `setup_window_remaining_ms`, `fallback_ap_in_ms` (`null` for "no
end"), `failure`, `failure_reason`, the device identity and the MQTT block. See
[REST API](/interfaces/rest-api).

### Scanning

`ot_net_scan()` is one synchronous sweep of what the **device** can see (2.4 GHz only), at most 32
records, one per BSSID (a mesh answers several times under one name). It takes the radio lock, and
on a device serving the setup page from its own access point the radio leaves that channel while
it sweeps, so the phone may lose the page mid-request. Nothing retries or reboots for that; manual
entry stays available.

## Captive portal

While the access point is on the air, two halves of one decision keep a phone's operating system
from opening its own sign-in sheet while letting a real browser reach the setup page:

- **A probe table.** The connectivity checks of Android (`/generate_204`, `/gen_204`), Apple
  (`/hotspot-detect.html`), Windows (`/ncsi.txt`, `/connecttest.txt`) and Firefox (`/success.txt`,
  `/canonical.html`) get byte-exact copies of the answers their originals serve, so each OS
  concludes the network works. Probes are answered with success, **never redirected**: a redirect
  is what makes the OS open its captive sheet. Paths match whole, never by prefix. The answers are
  constants, so they are served ahead of the access policy without leaking anything.
- **A DNS responder** on UDP 53, bound to the access point's own address (not `INADDR_ANY`), that
  answers every A lookup with that address. An answer for every name is correct only on a network
  with nothing else in it, so the responder **lives and dies with the access point**
  (`otnet_captive_dns_follow_ap()`).

A portal that will not start is logged and not retried into a loop: the owner can still reach the
device by typing its address. Android's HTTPS half of the probe is not addressed.

## No mDNS

The firmware registers no mDNS service and no `.local` host name; the device is reached by its IP
address (shown in `GET /api/status` `ip` and passed to Home Assistant as the device's
`configuration_url`). The HTTP layer's Host-header allowlist accordingly contains no `.local` name;
see [Security](/architecture/security).

## Time

`ot_time` wraps SNTP and the time zone.

- **Validity is "a sync happened", never "`time()` returned something".** `ot_time_now()` answers
  false until SNTP has synchronised at least once. A device that never reached an NTP server
  returns 1970 with complete confidence, and a threshold on the epoch would call that invalid by
  luck rather than by knowledge, and would call a genuinely wrong clock valid.
- **Nothing reads the wall clock.** `ot_time_now()` has no caller, and log timestamps are
  milliseconds since boot (`CONFIG_LOG_TIMESTAMP_SOURCE_RTOS`). Every clock the firmware decides
  by is monotonic milliseconds, deliberately: a clock that can jump backwards is no base for a poll
  ring or a watchdog.
- **Start.** `app_main`'s network callback calls `ot_time_start(ntp_server)` on every transition
  to `OT_NET_CONNECTED`; there is nothing to synchronise against before the station has an address.
  The call is idempotent and **returns immediately**; nothing waits for a sync, because nothing the
  bus waits behind may block. A changed NTP server therefore takes effect at the next boot. An
  SNTP start failure is logged and nothing else happens.
- **Zone.** `ot_thermostat` follows the stored `tz` once a second and calls
  `ot_time_set_zone()` whenever it differs from the one last applied, so a saved zone applies
  within a second, without a reconnect or a power cycle. An empty zone is applied as an explicit
  `UTC0`. The defaults are `MSK-3` and `pool.ntp.org`.
- The weekday is converted once, in `ot_time_now()`, to 0 = Monday (`struct tm` counts from
  Sunday).

## The MQTT link's place

The broker connection is **not** part of `ot_net`. A broker lifecycle inside `ot_net` would have to
call `ot_thermostat`, which already requires `ot_net`: a dependency cycle that does not link. The
glue therefore lives in its own component, `ot_mqtt_link`, which only *reads* `ot_net`: once a
second it fingerprints `ot_net_broker()` (the one un-redacted projection, password in the clear for
MQTT to authenticate) with a 32-bit FNV-1a hash and restarts the client when the settings change.
`app_main` starts it after the executor and before HTTP, and a broker that is absent or refuses
stops nothing. Topics, discovery and the room-reading topic are on [MQTT](/interfaces/mqtt) and
[Home Assistant discovery](/interfaces/home-assistant-discovery).

## When the network goes away

Losing Wi-Fi is an ordinary, expected event, and the response is to keep doing the device's job:

| Layer | What happens |
| --- | --- |
| OpenTherm bus | **nothing**: it was started before the network, and no network event reaches it |
| Provisioning | retries with backoff (1 s to 30 s, no attempt limit); after 30 min of sustained failure raises the fallback access point while the station keeps trying |
| MQTT | esp-mqtt reconnects on its own every 10 s; Home Assistant marks the device unavailable through the last-will |
| Executor | the Home Assistant watchdog keeps counting; when it runs out the executor enters the bounded failsafe (see [Failure model](/architecture/failure-model)) |
| LED | blue "connecting" for the first 10 s, then red; amber if only the broker is lost; while the fallback access point is on the air, blue setup blink (see [Indication](/device/indication)) |

Nothing reboots: no "the network has been unreachable for N minutes, restart". A failed
`ot_net_start()` at boot is logged and stepped over, so a device whose radio will not come up still
drives the boiler and says why over USB. `ot_net` never `ESP_ERROR_CHECK`s anything downstream of
stored or received data, because a configuration survives a reboot and a panic over one is a loop
with no way out over the air.
