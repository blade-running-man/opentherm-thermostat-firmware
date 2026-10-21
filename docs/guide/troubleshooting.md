# Troubleshooting

The device reports its state in four places: the **status LED** on the board, the **web interface**
(the Control page's state and reason, the Boiler page's bus counters, the State page, the Log page),
the **REST endpoints** behind them, and the **USB console**. It recovers from almost everything by
itself; what it cannot recover from is a lost password, which takes a USB cable.

Whatever goes wrong, one thing must not: the conversation with the boiler. If the boiler heats when
nobody asked it to, read [The boiler heats on its own](#the-boiler-heats-on-its-own) first.

## The status LED

One RGB LED shows one verdict about the whole chain — Wi-Fi, then the broker, then Home Assistant —
and always the **first broken link**, because that is the thing to fix. Colour is health; motion is
what the boiler is doing.

| Status LED | State | What to do |
| --- | --- | --- |
| Blue, slow blink (1 s on, 1 s off) | The setup network `opentherm-xxxx` is on the air and the device has no address | Join it and open Settings ([First setup](/guide/first-setup)) |
| Blue, breathing | Joining a network (the first 10 s after losing one); or no Wi-Fi stored and the setup window has run out | Wait; if it lasts, power-cycle to reopen the setup network |
| Red, slow blink | A network is stored but the device has been off it for more than 10 s | Check the router and the Wi-Fi password ([Wi-Fi problems](#wi-fi-problems)) |
| Amber, faint heartbeat or breathing | On Wi-Fi, a broker is configured, and the device is not connected to it | Check the broker ([Home Assistant and the broker](#home-assistant-and-the-broker)) |
| Orange, slow blink | Links up, but the executor is in the **failsafe** — Home Assistant stopped commanding | See [The failsafe banner](#the-failsafe-banner) |
| Green, faint heartbeat (one blip every 4 s) | All well, boiler idle | — |
| Green, breathing | All well, **burner lit** (central heating or hot water) | — |
| Dark | No power, no LED on this board, or the chip is held in its download mode | Power-cycle **without** holding BOOT |

Green also covers local mode, a boost and the season being off — the LED is about connectivity and
the involuntary failsafe, never about which mode you chose. A device run without a broker never
shows amber. Amber outranks orange: when the broker dies, the watchdog later fires as a consequence,
and the LED keeps showing the cause. How the LED is driven: [Indication](/device/indication).

## Reading the log

The device keeps its last **80 lines** (160 characters each) in RAM. The **Log** page shows them;
`GET /api/log` returns them as a JSON array of strings, oldest first. The lines start
`I (12345) tag: …` — the number is milliseconds since boot, not the time of day.

**Prefer the USB console** when you can reach the board:

```sh
pio device monitor -e lolin_c3_mini            # everything
pio device monitor -e lolin_c3_mini | grep ot_bus
```

- `GET /api/log` escapes every non-ASCII byte as `\u00xx`, so a line containing one — the default
  device name is Cyrillic — comes back unreadable and ungreppable. The console is plain text.
- The ring rolls: on a busy device the boot lines are gone within the hour. Read what you need
  soon after the event.
- No log line ever contains a password, the Wi-Fi key or a value that was refused.

## No conversation with the boiler

The bus never stops trying, so a boiler that does not answer shows up as counters, not as a hang:

```sh
curl -s http://<device>/api/ot/raw | jq '{cycles, ok, failed, overdue, answering}'
```

`cycles` grows by about one per second whatever happens; `answering` says whether the boiler
replies. The log says `ot_bus: boiler is not answering (<why>) after 5 tries; polling continues`,
where `<why>` is `timeout`, `frame` or `parity`.

1. **Measure the boiler terminals** with the shield disconnected: 15–24 V DC means OpenTherm. No
   voltage means these are not OpenTherm terminals.
2. **Check the two wires** to the shield's **B1 / B2** terminal (polarity does not matter) and that
   the old thermostat is off the terminals.
3. **`timeout` on every frame, never a single answer decoded → flip `ot_in_inverted`.** The
   adapter's input polarity is a field of the board descriptor (`components/board/board_lolin_c3_mini.c`
   or `board_supermini_c6.c`), and its value — `false` — was taken from another project's code, not
   from the specification. If not a single frame decodes, change it to `true`, rebuild and flash,
   **before** suspecting the decoder. It is a build-time setting, not a runtime one.
4. **`frame` or `parity` errors mixed with answers** point at the line rather than the polarity:
   cable length and routing, a loose terminal.

How the line is driven and read is in [Physical layer](/protocol/physical-layer); the counters in
[Bus](/protocol/bus).

## Values that read `invalid` or vanish

Every boiler value is `ok`, `invalid` or `unsupported`:

- **`invalid`** — the boiler answered `DATA-INVALID`: the value exists but there is no data right
  now. The reference boiler answers this way for the outside temperature (no outdoor sensor fitted)
  and the fault flags (no fault). It is shown as no value, never as a zero.
- **`unsupported`** — the boiler answered `UNKNOWN-DATAID` twice. The ID is dropped from the poll
  (`ID n is not supported by the boiler; removed from the poll ring`) and its entities are retracted
  from Home Assistant until the next boot.

Which IDs your boiler supports is its own business; the reference unit's list is in
[Intergas Kombi Kompakt HRE](/boiler/intergas-kombi-kompakt-hre).

### DATA-INVALID on the flow setpoint

```text
ot_thermostat: the boiler answers DATA-INVALID to ID 1 (45.0, raw 0x2d00): it counts as sent; compare flow_min_dc..flow_max_dc with the boiler's range
```

The boiler refused the flow setpoint as outside its own range. The device still counts it as sent —
holding CH down over a range disagreement would turn it into a silent no-heat, failsafe included —
and warns once. Narrow `flow_min_dc`/`flow_max_dc` to what the boiler accepts
([Configuration](/guide/configuration#controller)).

## The boiler does not heat

Open the Control page: it shows the executor's state and the reason in words.

- **`season_off`** — the heating season is off. That is the default on a fresh install; turn it on
  on the Control page (Home Assistant can only turn it off).
- **`local`** with CH off — local mode's own CH switch is off (also the default).
- **`ha_waiting`** — Home Assistant mode, and no CH command from Home Assistant has been accepted
  yet.
- Reason **`await_setpoint`** — CH is wanted, but the flow setpoint has not yet gone out on the bus;
  it clears within a cycle once the boiler is answering.
- **The setpoint is below the boiler's own minimum.** A request below the boiler's lowest flow
  temperature (panel parameter `E` on the reference unit) may simply not be carried out, and the
  boiler cannot report that over OpenTherm. Keep `flow_min_dc` at or above it.

## The boiler heats on its own

A boiler that hears no master for more than about five seconds reads it as a short-circuited
thermostat and demands heat. So heat nobody asked for usually means the bus was silent:

- the device was restarting or being flashed — a brief demand for heat during a restart is expected
  and should end at the first conversation after it is back;
- the device is off, or `cycles` in `/api/ot/raw` has stopped growing — that is a defect, report it.

The **line test** (`POST /api/ops/linetest`) deliberately stops the conversation for up to 30 s and
the boiler will fire; it is the only legitimate pause, and it needs the web interface password.

## The failsafe banner

A red-edged **Failsafe is active** banner on the Control page (and an orange LED) means the device
is in Home Assistant mode and Home Assistant has not sent an accepted CH command for longer than
`watchdog_s`. The reason line says which branch it is in:

| Reason | What the boiler gets |
| --- | --- |
| `fs_disarmed` | CH held **off** — Home Assistant has not asked for heat within `failsafe_heat_days` (the summer bound) |
| `fs_blind` | CH on at `failsafe_setpoint_dc` — no fresh room-role source to steer by |
| `fs_room_cold` / `fs_room_warm` / `min_cycle` | Holding `failsafe_room_target_dc` from a fresh room-role source, ±0.3 K, with a minimum cycle |

The cause is `watchdog`, or `ha_blind` when `room_mqtt_ha_forwarded` is on and the forwarded room
source went stale. The failsafe ends at the next accepted CH command from Home Assistant, or when
you switch to local mode. Afterwards the banner turns amber-edged — **The failsafe has run since this
boot** — with the count and the length of the last one. The banner is read from the device itself,
so it shows even when nothing reaches Home Assistant. If the failsafe trips while Home Assistant is
running, its automation is not re-sending often enough — see
[Driving the boiler from Home Assistant](/guide/home-assistant#driving-the-boiler-from-home-assistant).

## Home Assistant and the broker

| Log line (`mqtt: …`) | Meaning |
| --- | --- |
| `no broker configured; MQTT stays off` | `mqtt_host` is empty |
| `broker lost; retrying every 10 s, nothing else changes` | The connection dropped; it retries by itself, the boiler is unaffected |
| `broker unreachable; N attempts so far, retrying every 10 s` | Still down (repeated at most hourly) |
| `broker refused the connection: <reason> (code N)` | Usually a wrong user name or password (codes 4 and 5) |
| `the broker refused the command subscription: check its ACL` | States publish, but commands cannot arrive |
| `command refused: <why>` | A command was not carried out — `owned by the thermostat: control_mode is local`, `a retained command is ignored`, `value out of range`, `the payload is not a number`, `this entity is not Home Assistant's to write` |

- **The controls are greyed out** in Home Assistant: the device is in local mode. Switch
  **Settings → Controller → Control mode** to Home Assistant.
- **Every entity is unavailable**: the device is offline to the broker. Its last-will message,
  `offline` on `<prefix>/status` (retained), is published by the broker within about 90 s of the
  device vanishing.
- **Entities never appear**: check `ha_discovery` is on, the broker is the one Home Assistant uses,
  and the Home Assistant log (filter "mqtt") for a refused discovery message.

A dead broker never reboots the device and never pauses the bus; the device keeps driving the boiler
from its last commands until the watchdog runs out.

## Wi-Fi problems

- **The setup network does not appear.** Its window has run out (15 minutes the first time, 5 after
  that). Power-cycle the board for another five minutes.
- **The new Wi-Fi password was wrong.** A device that has been on a network before puts the last
  working one back after 90 s; a brand-new one keeps trying with its setup network still up — see
  [First setup](/guide/first-setup#if-the-wi-fi-password-was-wrong).
- **Why it will not connect.** `GET /api/status` reports `failure` as `network-not-found`,
  `wrong-password`, `refused` or `no-address` (the router let it join but gave it no address).
- **Cannot find it on the network.** There is no mDNS: look in the router's client list, or read
  the address from the console. Then open it by its **numeric IP**: a host name the router hands
  out is refused with `403 {"error":"forbidden"}`, because the device answers only requests whose
  `Host` is its own address.
- **Saving a new Wi-Fi network is refused (`403`)** on a device that is already on a network and has
  no password. Set a web interface password first.
- **Short range on the ESP32-C6 SuperMini.** Its ceramic antenna reaches noticeably less than the
  C3's; a link that holds at the desk and breaks at the boiler is the antenna.

## Forgotten password

There is **no reset button** in this firmware: the BOOT button is only the chip's download strap,
and the Settings page's advice to hold it for five seconds does not apply to this build. Holding
BOOT *while power comes up* puts the chip in its firmware loader, which looks exactly like a dead
device — release it and power-cycle.

The only way back is to erase the settings over USB. It clears **every** setting — Wi-Fi, broker,
password and the executor's settings — and the device comes up as on its first day:

```sh
pio run -e lolin_c3_mini -t erase      # wipes the whole flash
pio run -e lolin_c3_mini -t upload     # puts the firmware back
```

While you still know the password, you can remove it instead: send an empty `ui_password` in an
authenticated `POST /api/config`.

## The device looks dead

- **Nothing on the console, no setup network, LED dark**: the chip may be in its download mode
  because BOOT was held at power-up. Power-cycle with the button released.
- **The upload cannot find the board**: hold BOOT, tap RESET, release BOOT, upload again; on the C3,
  disconnect the boiler wires for the moment of reset ([Installation](/guide/installation#flashing)).
- **Restarting over and over**: read the console — a reset reason is logged at every boot. Nothing in
  this firmware reboots on purpose, so a loop is a defect. The executor keeps its watchdog count
  across soft resets, so even a loop ends in the failsafe on schedule
  ([Failure model](/architecture/failure-model)).

The scenarios that walk each of these on real hardware are in
[Hardware verification](/hardware-verification/).
