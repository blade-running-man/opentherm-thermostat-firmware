<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# Home Assistant

The device joins Home Assistant through MQTT discovery. Point it at the broker Home Assistant uses
and every entity appears under one device, with no YAML and no custom integration. This page covers
connecting it, what appears, and what Home Assistant has to do once it is the controller. The topic
layout is in [MQTT](/interfaces/mqtt); the discovery documents in
[Home Assistant discovery](/interfaces/home-assistant-discovery).

## What you need

- An MQTT broker — for example the Mosquitto add-on — reachable from the device on port 1883 (MQTT
  without TLS; TLS is not supported).
- Home Assistant's **MQTT integration**, connected to the same broker, with discovery on (it is by
  default; the discovery prefix is `homeassistant`).
- A broker user for the device, if your broker requires one.

## Connecting the device

In **Settings → MQTT broker**, enter the broker's host, port, user name and password, leave
**Publish Home Assistant discovery** on, and press **Save settings**. Within a couple of seconds the
log shows:

```text
mqtt: broker mqtt://<host>:1883, prefix opentherm/aabbccddeeff, discovery on
mqtt: broker connected
```

`GET /api/status` carries the link's state in its `mqtt` block — `configured`, `connected`,
`published`, `commands` (accepted), `rejected`, `reconnects`, and the broker's last connection
refusal (`last_connack`, `last_connack_reason`). User names and passwords never appear in any log line.

## What appears in Home Assistant

One device, named after **Settings → Device → Device name**, model `opentherm-thermostat`, with its
firmware version, a **Visit** link to the device's web interface and its MAC address. Under it:

- **Every boiler reading the boiler supports** — temperatures, modulation, pressure, flow, the
  status and fault flags — as sensors and binary sensors.
- **The executor's state**: `Control mode` and `Control state` (enum sensors), what is actually
  asked of the boiler (`Heat demand to boiler`, `Effective CH setpoint`), `Failsafe entries` and
  `Last failsafe duration`, a `Heating season` binary sensor, and the room temperature the device sees with
  the source it came from (`Room source`: `none`, `shield` or `mqtt`).
- **Five controls**: the `Central heating enabled` and `Hot water enabled` switches, the
  `CH setpoint` and `Hot water setpoint` numbers, and a `Heating season` button that turns the
  season off.

The registry has **{{ data.total }} entities** in all; the full list with keys, units and ranges is
[Entities](/reference/entities). Each discovery document suggests an entity id built from the key —
`switch.opentherm_ch_enable`, `number.opentherm_ch_setpoint`, `sensor.opentherm_control_state` —
whatever the device is called.

Entities the boiler does not support are **retracted**: after two `UNKNOWN-DATAID` answers to a
Data-ID, its entities are removed from Home Assistant (an empty retained discovery message). The
controls stay, with one exception: `Hot water setpoint` is read back from the boiler (ID 56), so
it is removed from a boiler that does not support ID 56. The `CH setpoint` number box's range is the device's flow band
(`flow_min_dc`..`flow_max_dc`) and follows it within a couple of seconds when you change it, so Home
Assistant never offers a value the device would refuse.

## Local mode and Home Assistant mode

The control mode is set on the device — **Settings → Controller → Control mode** — and nowhere else.
It is deliberately not reachable over MQTT: the controller cannot hand itself the boiler.

| | Local mode (default) | Home Assistant mode |
| --- | --- | --- |
| Who drives CH and hot water | The device's Control page (or REST) | Home Assistant |
| The five controls in Home Assistant | **Unavailable** (greyed out) | Available |
| A command from Home Assistant | Refused (`owned by the thermostat: control_mode is local`) | Carried out, within the bounds |
| Readings and the executor's state | Available | Available |
| Watchdog and failsafe | Not running | Running |

The switch happens through a second availability topic, `<prefix>/control/owner`, which is
`online` only in Home Assistant mode. Home Assistant then does not even send a command to an
unavailable control, which is better than letting it send one and snapping the switch back.

Home Assistant mode is refused while no broker host is set. Switching to it lands in `ha_waiting`:
CH stays off until Home Assistant's first CH command arrives (or the watchdog runs out first).

## Driving the boiler from Home Assistant

Home Assistant is the control law. It decides **whether** to heat (`switch.opentherm_ch_enable`)
and **how hot** the flow should be (`number.opentherm_ch_setpoint`); the device carries that out and
holds it. A few rules decide whether Home Assistant counts as present:

- **Every accepted CH command feeds the watchdog** — a CH switch or a CH setpoint command. If none
  arrives for `watchdog_s` (900 s by default), the device enters the **bounded failsafe**.
- **Hot water does not feed it.** An automation that only drives hot water is a controller that has
  stopped steering the heating.
- **Refused commands do not feed it.** A setpoint outside the flow band is refused, not clamped, so
  a broken automation ends in the failsafe instead of hiding behind a plausible number.
- **Retained commands are ignored.** A broker replays a retained message on every reconnect, so it
  would make a dead Home Assistant look alive. Home Assistant's own entities never retain commands;
  do not publish to `<prefix>/<key>/set` with `retain: true` yourself.

So the automation that drives the boiler must re-send its decision **at least every `watchdog_s`**,
not only when it changes. A minimal shape, with your own heat-demand logic in place of the template:

```yaml
automation:
  - alias: "Boiler: send the heating decision"
    trigger:
      - platform: time_pattern
        minutes: "/5"                       # well inside watchdog_s (900 s)
      - platform: state
        entity_id: input_number.flow_target  # your controller's output
    action:
      - service: number.set_value
        target:
          entity_id: number.opentherm_ch_setpoint
        data:
          value: "{{ states('input_number.flow_target') }}"
      - service: "switch.turn_{{ 'on' if is_state('binary_sensor.heat_demand', 'on') else 'off' }}"
        target:
          entity_id: switch.opentherm_ch_enable
```

The device raises the CH bit only once the held flow setpoint has actually gone out on the bus, so
sending both in one action is safe in either order.

### The heating season

`Heating season` is the person's master switch. Home Assistant may **turn it off** (the
`Heating season` button, `button.opentherm_heating_season_off`, for summer logic) but never on — turning it back on is done on the device, on
the Control page or over REST. With the season off, the device asks for no central heating in any
mode, failsafe included.

### What the failsafe looks like from Home Assistant

`Control state` reads `failsafe`, its attributes (`<prefix>/control_state/attributes`) carry the
`reason` and `cause`, and `Failsafe entries` goes up. Depending on the case, the device holds CH
off (Home Assistant has not asked for heat within `failsafe_heat_days` — the summer bound), heats
blind at `failsafe_setpoint_dc`, or holds `failsafe_room_target_dc` from a fresh room-role source.
The next accepted CH command ends it. The cases and their reasons are in
[Executor](/architecture/executor); what happens when the broker or the Wi-Fi is the one that died
is in [Failure model](/architecture/failure-model).

When Home Assistant restarts, its birth message (`homeassistant/status online`) makes the device
publish discovery again. That feeds **nothing** to the watchdog, and Home Assistant's own `offline`
message is deliberately not used to hasten the failsafe — it is also sent on a clean shutdown.

## What Home Assistant may not write

Home Assistant may write **no OpenTherm frame of its own**. Entities such as the maximum CH
setpoint, the room setpoint, the room temperature reported to the boiler or the maximum relative
modulation are writable over REST but refused over MQTT (`this entity is not Home Assistant's to
write`). Home Assistant drives the boiler only through the executor's five controls, where the
bounds and the watchdog apply. The rule is decided in one place —
[Commands](/protocol/commands).

## Giving the device a room temperature

The failsafe can hold a room target instead of heating blind, if it has a fresh room reading. Home
Assistant provides one by publishing a bare number in °C to `<prefix>/room/state`, **not retained**,
on every change of your room sensor, after you turn the source on in
**Settings → Room source (MQTT)**. This topic is an input, so it is not part of discovery — you
write the automation once. The contract and a ready automation are in
[Room sources](/device/room-sources).

## Turning discovery off, renaming, moving

- **Discovery off** (`ha_discovery`) removes every entity from Home Assistant; turning it on again
  brings them back under the same ids.
- **Renaming the device** changes the device's name in Home Assistant and nothing else — identity is
  the MAC.
- **Changing the topic prefix** marks the old prefix `offline` and republishes everything under the
  new one, onto the same Home Assistant entities: neither the discovery topic
  (`homeassistant/<component>/<device_id>/<object_id>/config`) nor the unique id
  (`<device_id>_<object_id>`) contains the prefix. The old prefix's retained state topics stay on
  the broker until you clear them.

If the device does not appear, or its entities stay unavailable, see
[Troubleshooting](/guide/troubleshooting#home-assistant-and-the-broker). The scenario that proves
all of this on a real installation is
[06 · Home Assistant](/hardware-verification/06-home-assistant).
