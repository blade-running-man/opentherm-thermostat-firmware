# Feeding the room temperature from Home Assistant (MQTT room source)

The executor can steer the boiler's bounded failsafe by a **room** temperature instead of heating
blind — but only if it has a room reading. Home Assistant publishes that reading to the device over
MQTT. This is an **inbound** topic: the device subscribes, Home Assistant publishes. It is **not**
part of MQTT discovery (discovery describes what the device *reports*, not what it *listens to*), so
you wire the publish yourself, once.

The consolidated reference is `docs/firmware-design.md` (room-source section). The `ot_room`
component (a 4-slot registry), the MQTT ingest, and the Settings **Room (MQTT)** card make up
this feature.

## The topic contract

```
<topic_prefix>/room/state
```

- **Payload:** a bare number, degrees Celsius — e.g. `21.4`. No JSON, no units. The strict grammar is
  `-?[0-9]+(\.[0-9]+)?`; `nan`, `inf`, `1e9`, `0x..`, empty, or a JSON object are rejected.
- **Retain:** must be **`retain: false`**. A retained reading is *refused* by the firmware. Reason:
  a retained value is re-delivered on every reconnect looking freshly-arrived, while being
  arbitrarily old — and this value steers the boiler. The device measures arrival cadence, not
  measurement age, so it cannot tell a stale-retained value from a live one. Publish live.
- **Out-of-range** values (outside −40…60 °C) and implausible jumps are dropped by the source filter;
  a source that stops publishing goes *stale* after `room_mqtt_stale_s` and the failsafe reverts to
  blind heating (it never latches on an old number).

### Finding your `<topic_prefix>`

The default is `opentherm/<device_id>` where `<device_id>` is the device's MAC, e.g.
`opentherm/aabbccddeeff` — so the room topic is `opentherm/aabbccddeeff/room/state`. The exact prefix
is shown on the device's Settings page and in `GET /api/config` (`topic_prefix`). If you changed the
prefix, use your value.

## Enabling the source on the device

The MQTT room source is **off by default** — enabling it is a deliberate act, because a fresh room
source changes what the failsafe does (heats to a room target with hysteresis instead of blind).
Enable it either from the **Settings → Room (MQTT)** card in the web UI, or by setting the four
config fields via `POST /api/config`:

| Field | Meaning | Default | Accepted range | For a room source |
| --- | --- | --- | --- | --- |
| `room_mqtt_enable` | turn the slot on | `false` | `false`/`true` | `true` |
| `room_mqtt_role` | `1` = room (steers), `0` = ambient (display only) | `1` | `0`–`1` | `1` |
| `room_mqtt_stale_s` | seconds before a silent source is stale | `900` | `10`–`65535` | e.g. `900` |
| `room_mqtt_ha_forwarded` | if `true`, a *stale* room source forces the failsafe into its `ha_blind` state | `false` | `false`/`true` | optional |

```sh
curl -X POST http://<device>/api/config \
  -H 'Content-Type: application/json' \
  -d '{"room_mqtt_enable": true, "room_mqtt_role": 1, "room_mqtt_stale_s": 900}'
```

(If the device requires a password, add it as your setup does for other config writes.) After this,
`GET /api/config` echoes the four values, and the device's `room_source` entity reads **`mqtt`** while
a fresh reading is arriving (the enum's values are `none` / `shield` / `mqtt`), and
`room_temperature_effective` shows the room value.

## Home Assistant: publishing the reading

Republish an existing room sensor (here `sensor.living_room_temperature`) to the topic. Use **one**
of the following.

### As an automation (recommended — tracks the sensor)

```yaml
automation:
  - alias: "Forward living-room temperature to the boiler"
    trigger:
      - platform: state
        entity_id: sensor.living_room_temperature
    condition:
      - condition: template            # skip unknown/unavailable
        value_template: "{{ states('sensor.living_room_temperature') not in ['unknown', 'unavailable'] }}"
    action:
      - service: mqtt.publish
        data:
          topic: "opentherm/aabbccddeeff/room/state"
          payload: "{{ states('sensor.living_room_temperature') }}"
          retain: false               # REQUIRED — a retained reading is refused
```

Replace the topic with your `<topic_prefix>/room/state` and the entity with your sensor. A
`state`-triggered automation fires on every change, so the device sees the room continuously and the
`room_mqtt_stale_s` window never lapses while the sensor is alive.

### As a one-off service call (for testing)

```yaml
service: mqtt.publish
data:
  topic: "opentherm/aabbccddeeff/room/state"
  payload: "21.4"
  retain: false
```

## What the device does with it

- `room_mqtt_role: 1` (room) → the reading feeds the failsafe. When Home Assistant stops sending
  commands and the watchdog trips, the failsafe holds the room near its target with ±0.3 K hysteresis
  instead of heating blind.
- `room_mqtt_role: 0` (ambient) → the reading is shown (`room_temperature_effective`/`room_source`)
  but **never** steers — appropriate for a sensor sitting by the boiler, not in a room.
- The device's own shield DS18B20 is always an **ambient** source and never steers (it appears as
  `room_source: shield`); a room-role MQTT source takes precedence for both steering and display.
- Under the hood these are slots in the `ot_room` registry (4 slots: slot 0 = the shield DS18B20,
  slot 1 = this MQTT source; BLE / general 1-Wire / Wi-Fi-push are planned, not yet built). Only a
  `room`-role, **fresh** slot is ever eligible to steer; a stale slot reverts the failsafe to blind
  heating (or, with `room_mqtt_ha_forwarded`, to the `ha_blind` state).
