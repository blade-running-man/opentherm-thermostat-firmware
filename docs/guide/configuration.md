# Configuration

Everything the owner can change is changed at runtime — in the web interface's **Settings** page or
through the same REST endpoints it uses. Settings are stored in the device's NVS flash, survive
power cuts and reflashing, and take effect without a reboot. Nothing is configured at build time.

## How settings are written

The rules below are the device's own (`components/ot_config`); the page adds none of its own, so
the device's sentence is what you see when a value is refused.

- **`GET /api/config`** returns the whole document. **`POST /api/config`** takes any subset of it as
  JSON (`Content-Type: application/json`).
- A save is **all-or-nothing**: every field is validated before anything is written, and a refused
  save changes nothing. The answer is `{"saved":true}`, or `422` with
  `{"error":"<sentence>","field":"<token>"}` naming what was wrong (for example `mqtt-host`,
  `flow`, `range`, `mode-needs-broker`).
- **An absent key leaves the stored value alone.** That is why the Settings page saves its cards
  separately: the broker and device card sends its whole form, while the controller and room-source
  cards send only the values you changed, so a stale copy on screen never overwrites a value someone
  else has since changed.
- Temperatures are whole numbers in **tenths of a degree** — the `_dc` suffix: `450` is 45.0 °C.
  The four temperatures that become the flow setpoint must also sit on the half-degree grid (a
  multiple of 5).

Without a web interface password, `POST /api/config` is open to anyone on your network; with one,
every request needs it (HTTP Basic, any user name). Who may write what is in
[Security](/architecture/security).

### Secrets are write-only

Three fields are secrets: `wifi_psk`, `mqtt_password` and `ui_password`. `GET /api/config` never
returns their value — it returns `__UNCHANGED__` when one is stored and `""` when none is. On the
way in, sending `__UNCHANGED__` back keeps the stored value, an empty string clears it, and anything
else replaces it. The Settings page shows each one as an empty box that says whether something is
stored.

## Wi-Fi

Set through the **Wi-Fi** card, which sends `POST /api/provision` — never together with the other
settings, so saving the broker can never disturb the network.

| Setting | Default | Accepted |
| --- | --- | --- |
| Network name (`wifi_ssid`) | none — the device runs its own setup network | 1 to 32 bytes |
| Wi-Fi password (`wifi_psk`, secret) | none | 8 to 63 characters, exactly 64 hexadecimal digits, or empty for an open network |

The name and the password are stored as **one record under one key**, so a power cut can never leave
a new name beside an old password. Saving starts a trial connection; see
[First setup](/guide/first-setup#if-the-wi-fi-password-was-wrong) for the rollback. On a device that
is already on your network, this card needs the web interface password.

## MQTT broker

| Setting | Default | Accepted |
| --- | --- | --- |
| Broker host (`mqtt_host`) | empty — **MQTT off** | A host name or IP address, up to 253 characters of letters, digits, `.`, `-`, `_`, `:`, `[` and `]`. No scheme (`mqtt://` is refused); the port goes in its own field. |
| Broker port (`mqtt_port`) | `1883` | 1 to 65535 |
| Broker username (`mqtt_user`) | empty | up to 128 characters |
| Broker password (`mqtt_password`, secret) | empty | up to 128 characters |
| Topic prefix (`topic_prefix`) | `opentherm/<12 hex digits of the MAC>` | 1 to 64 characters; no spaces, `+` or `#`; must not start with `$` or `/`, end with `/`, or contain `//` |
| Publish Home Assistant discovery (`ha_discovery`) | **on** | on / off |

- **The connection is not encrypted** — MQTT runs without TLS in this firmware, so the broker
  password crosses your network in the clear. Do not reuse one you use elsewhere. Nothing over MQTT
  can change the mode, turn the heating season on, move the bounds or touch the watchdog, so what
  an eavesdropper can do with the topics is bounded by the same rules Home Assistant is held to.
- **Changing the host while keeping the stored password is refused** until the password is typed
  again. Otherwise the stored password would be sent, in the clear, to a host nobody had entered it
  for.
- A saved broker setting is picked up within about a second: the MQTT task notices the change and
  restarts its own client. Clearing the host turns MQTT off.
- **Changing the prefix moves every topic.** The old prefix is marked `offline` and everything is
  republished under the new one. Neither the discovery topic nor the unique ids contain the prefix,
  so Home Assistant keeps the same entities. The old prefix's retained state topics stay on the
  broker until you clear them.

## Device

| Setting | Default | Accepted |
| --- | --- | --- |
| Device name (`device_name`) | `Термостат OpenTherm xxxx` (`xxxx`: the last four hex digits of the MAC) | 1 to 48 **bytes** (a Cyrillic letter takes two), no control characters |
| Web interface password (`ui_password`, secret) | none — the device is open on your network | 8 to 128 characters, no control characters; empty clears it |

The name is a **display name only**. The broker client id, the Home Assistant device id and the
default topic prefix are derived from the MAC, so renaming cannot create a second device in Home
Assistant. The password is stored as a salted PBKDF2-HMAC-SHA256 record, never as itself.

::: danger There is no "forgot password"
The Settings page tells you to hold the device's button for five seconds to clear the password.
**That is not true of this firmware:** no button gesture is implemented. The only way back from a
lost password is erasing the settings over USB — see
[Forgotten password](/guide/troubleshooting#forgotten-password).
:::

## Controller

The **Controller** card holds the executor's eight settings. What each one does to the boiler is in
[Executor](/architecture/executor); the ranges are enforced here.

| Setting | Default | Accepted | What it is |
| --- | --- | --- | --- |
| `control_mode` | `0` — **local** | `0` local, `1` Home Assistant | Who drives. `1` is refused while `mqtt_host` is empty (`mode-needs-broker`). |
| `watchdog_s` | `900` | 60 to 7200 s | How long Home Assistant may stay silent about CH before the failsafe takes over. |
| `failsafe_setpoint_dc` | `450` (45.0 °C) | 10.0 to 90.0 °C on the 0.5 grid, **inside the flow band** | The flow setpoint the failsafe heats at — and the one held from boot. |
| `failsafe_room_target_dc` | `180` (18.0 °C) | 5.0 to 30.0 °C | The room temperature the failsafe holds while a room-role source is fresh. |
| `failsafe_heat_days` | `3` | 1 to 30 | The failsafe heats only if Home Assistant asked for heat within this many powered days (the summer bound). |
| `failsafe_min_cycle_s` | `600` | 60 to 3600 s | The failsafe's shortest on time and shortest off time. |
| `flow_min_dc` | `400` (40.0 °C) | 10.0 to 90.0 °C on the 0.5 grid, below `flow_max_dc` | No CH setpoint below this is accepted, from anyone. |
| `flow_max_dc` | `700` (70.0 °C) | 10.0 to 90.0 °C on the 0.5 grid, above `flow_min_dc` | No CH setpoint above this is accepted, from anyone. |

- **The flow band bounds everything.** A CH setpoint outside `[flow_min_dc, flow_max_dc]` is refused,
  not clamped, whoever sends it — Home Assistant, the Control page or `curl`. Home Assistant's number box
  takes its range from the band.
- **Keep `flow_min_dc` at or above the boiler's own lowest flow temperature** (on the reference
  Intergas, panel parameter `E`, about 40 °C). A request below it is not carried out by the boiler,
  and the boiler cannot report that parameter over OpenTherm, so checking it is the owner's job.
- **Narrowing the band moves the stored local setpoint** to the nearest edge of the new band rather
  than refusing the save.

## Room source (MQTT)

The **Room source (MQTT)** card configures the one MQTT room-temperature source. Home Assistant
publishes the reading to `<topic_prefix>/room/state`; the full contract and an automation to copy
are in [Room sources](/device/room-sources).

| Setting | Default | Accepted | What it is |
| --- | --- | --- | --- |
| `room_mqtt_enable` | **off** | on / off | Use the MQTT room source at all. |
| `room_mqtt_role` | `1` — room | `1` room, `0` ambient | A room source can steer the failsafe; an ambient one is shown and never steers. |
| `room_mqtt_stale_s` | `900` | 10 to 65535 s | How long with nothing published before the source counts as stale. |
| `room_mqtt_ha_forwarded` | off | on / off | When on, this source going stale while Home Assistant is alive forces the failsafe (cause `ha_blind`). |

The shield's DS18B20, when fitted, is always an **ambient** source and needs no setting.

## Settings with no card

Two settings are read and written only through `/api/config`:

| Setting | Default | Accepted | When it applies |
| --- | --- | --- | --- |
| `tz` | `MSK-3` (UTC+3) | a POSIX TZ string, 1 to 63 printable ASCII characters, no spaces | Within a second, without a reconnect |
| `ntp_server` | `pool.ntp.org` | a host name or address | At the next boot (SNTP starts once) |

The sign in a POSIX TZ string is inverted: `MSK-3` is three hours **ahead** of UTC, `CET-1CEST,M3.5.0,M10.5.0/3`
is Central European time. No decision of the executor depends on the wall clock; the time is used
for the log and the displays.

```sh
curl -u ":$OTPASS" -X POST http://<device>/api/config \
  -H 'Content-Type: application/json' -d '{"tz":"CET-1CEST,M3.5.0,M10.5.0/3"}'
```

## Values the executor owns

Five values appear in `GET /api/config` but are **refused by name** in `POST /api/config` (`422`, the key
named as the `field`). They are commands, not settings, and are written through the entity path —
the Control page, `POST /api/entities/<key>`, or Home Assistant — so each has one validator and one
owner:

| Stored as | Written as entity | Default |
| --- | --- | --- |
| `heating_season` | `heating_season` | **off** — a fresh install never asks for heat |
| `local_ch_enable` | `ch_enable` (in local mode) | off |
| `local_ch_setpoint_dc` | `ch_setpoint` (in local mode) | `450` (45.0 °C) |
| `dhw_enable` | `dhw_enable` | **on** — hot water is the boiler's own business |
| `dhw_setpoint_dc` | `dhw_setpoint` | `0` — unset; no hot-water setpoint is sent until one is written |

In local mode the web side owns all of them. In Home Assistant mode Home Assistant owns CH and hot
water, and a write from the web is refused (`409`); the **heating season** stays the person's
master switch in both modes — the web may turn it on or off at any time, while Home Assistant may
only ever turn it **off**. Details: [Commands](/protocol/commands), [Executor](/architecture/executor).

## Values the device derives

`GET /api/config` also carries three read-only facts: `ui_password_set` (a web interface password
exists), `wifi_known_good` (a stored Wi-Fi pair has actually produced an address — the only case in
which a failed new pair is rolled back) and `read_only` (see below). Sending them back is harmless;
they are ignored.

## When the stored settings cannot be used

- **A damaged value is repaired at boot.** Every stored value is checked again on the way out of
  flash; anything unusable goes back to its default and the log names the key, never the value
  (`net: stored setting '…' was not usable as it was and has been repaired`). A flow band that no
  longer contains the failsafe setpoint is reset whole to 40.0 / 70.0 / 45.0 °C. An unreadable
  web interface password record is **cleared**, leaving the device open rather than sealed with you
  locked out.
- **Settings written by a newer firmware are read-only.** If the flash holds a configuration layout
  this build does not understand (after going back to an older firmware), the device runs on what it
  can read, sets `read_only`, and refuses every settings write (`409`) rather than damage it. The
  Settings page says so at the top. Reinstall the newer firmware, or erase the settings.

How the settings are laid out in NVS — the namespaces, the schema number, the retired keys — is in
[Configuration storage](/device/config-storage).
