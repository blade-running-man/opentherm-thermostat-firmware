# Room sources

The executor can steer its bounded failsafe by a **room** temperature instead of heating blind,
but only if it has a room reading it can trust. The firmware forms its own view of the room through
a registry of room-temperature sources, `ot_room`: up to four slots, each with its own outlier filter
and freshness state, each declared `room` or `ambient`. Two sources exist, the shield's own
DS18B20 and an MQTT reading Home Assistant publishes to the device. This page covers the registry,
the per-source filter, both adapters, the MQTT topic contract and the four settings.

What the failsafe does with a fresh room reading (the hysteresis, the cold and warm reasons, the
`ha_blind` cause) is on [Executor](/architecture/executor) and
[Failure model](/architecture/failure-model). The owner's walk is
[08 · Room sources](/hardware-verification/08-room-sources).

| Component | Kind | Role |
| --- | --- | --- |
| `ot_sensor` | pure | one source: outlier filter, `FRESH` / `STALE` / `NEVER`, the overdue accumulator. Host suite `test/test_ot_sensor` |
| `ot_room` | pure (`REQUIRES ot_sensor` only) | the 4-slot registry and its two selections. Host suite `test/test_ot_room` |
| `ot_thermostat_room.c`, `ot_thermostat_room_cfg.c` | glue / pure | the mailbox, the slot layout built from the configuration (host suite `test/test_ot_thermostat_room`), the published entities |
| `ot_onewire` / `ot_onewire_decode` | device / pure | the DS18B20 over RMT / its CRC-8 and decode. Host suite `test/test_ot_onewire` |
| `ot_mqtt` (`ot_mqtt_decide()`) | pure | recognises `<topic_prefix>/room/state` and refuses a retained reading |

## One source: `ot_sensor`

The measurement that matters is taken in the room, by a source the device does not control, so a
source going stale is a first-class state and the value itself is untrusted. `ot_sensor` is pure:
no timer, no task, no allocation; time enters only as an argument, which is what lets the 24-hour
and 49-day behaviour be tested on the host in milliseconds.

### The filter

`ot_sensor_update(s, celsius, now_ms)` returns false when a value is rejected, and a rejected value
changes nothing except a counter. In particular it does **not** refresh the deadline, or a sensor
stuck on +85 would read `FRESH` for ever.

| Rule | Constant |
| --- | --- |
| NaN and infinity are rejected | — |
| outside −40 … +60 °C is always rejected (this alone catches a DS18B20's +85 and −127 error values) | `OT_SENSOR_MIN_C`, `OT_SENSOR_MAX_C` |
| more than 5 °C from the last accepted value is rejected … | `OT_SENSOR_MAX_JUMP_C` |
| … but at most 3 times in a row; the fourth is accepted whatever it says | `OT_SENSOR_MAX_JUMP_REJECTS` |

The escape hatch must stay: a sensor moved to another room, or replaced, would otherwise lock the
loop out of the room temperature permanently, and a thermostat that cannot be corrected is a worse
failure than one that briefly believes an outlier. The streak is reset only by an accepted value,
and an out-of-range rejection never advances it, so no run of +85 can reach the escape hatch.

### Freshness

| State | Meaning |
| --- | --- |
| `NEVER` | no value has been accepted since boot |
| `FRESH` | the overdue time is below the slot's `stale_after_ms` |
| `STALE` | it has reached it (the boundary belongs to `STALE`) |

`NEVER` is not folded into `STALE` although the failsafe treats both the same: only one of them
means "this device has never had a room measurement", and the owner needs to know whether to look
for a broken sensor or an unfinished setup. The last accepted value is kept while `STALE`;
deciding what it is worth belongs to the consumer.

**Overdue time is an accumulator, never `now − last_ok`.** `ot_sensor_tick()` adds wrap-safe deltas
and saturates at `UINT32_MAX`. Monotonic `uint32` milliseconds wrap after 49.7 days, so a
subtraction spanning the wrap would read as "fresh", in January, with nobody watching; the
accumulator has no such moment. An update is also a tick, so the arrival path and the periodic path
never count an interval twice. A `now_ms` earlier than the previous call is indistinguishable from
a wrap and is read as a forward jump: the source latches `STALE` until the next accepted value,
which is the safe direction (stale never switches on heating that is off).

## The registry: `ot_room`

`OT_ROOM_MAX_SLOTS` is 4: the shield plus three more sources, without committing to an NVS record
shape for more. Each slot is an `ot_sensor_t` plus its configuration:

| Slot field | Meaning |
| --- | --- |
| `role` | `OT_ROOM_ROOM` (a genuine room reading) or `OT_ROOM_AMBIENT` (near the boiler, or otherwise not trusted to represent the room). `AMBIENT` is the zero value, so an uncommitted slot never steers |
| `ha_forwarded` | the slot is Home Assistant's own forwarded reading; it feeds the `ha_blind` check |
| `stale_after_ms` | the slot's freshness window |

Priority is the slot index. `ot_room_init()` clamps a count above 4 rather than failing (a
configuration typo must not crash the caller); `ot_room_submit()` into a slot beyond the count
returns false and changes nothing.

### Two selections

| | Steer (`ot_room_select_steer()`) | Display (`ot_room_select_display()`) |
| --- | --- | --- |
| Purpose | the safety-critical input to the failsafe | `room_temperature_effective` and `room_source` |
| Eligible | **only** `room`-role, `FRESH` slots | `room`-role first; when none is fresh, a `FRESH` `ambient` slot |
| Pick | the lowest-index eligible slot | the lowest-index eligible slot in that pass |
| Output | `fresh`, `value_dc` (round(°C × 10)), `active_slot`, `ha_forwarded_stale` | `have`, `value_dc`, `active_slot`, `active_role` |

The steer selection filters ambient slots out **before** the pick, so a boiler-side sensor can
never be chosen however the other slots are configured. `ha_forwarded_stale` is computed over every
slot independently of the pick: true when any `room`-role, `ha_forwarded` slot is `STALE`.

#### Why not "prefer room, fall back to ambient" for steering

That fallback is exactly the mistake the component exists to prevent: a warm boiler-room reading
holding the heat off in a cold house. The display may fall back (so the shield's reading stays
visible when nothing else is configured), and `active_role` tells the panel which kind of reading
it is showing. The role defaults follow the same asymmetry: a room-placed source left `ambient`
merely heats at the safe setpoint, whereas a boiler-side source mistaken for `room` could hold
heating off.

## The wiring

`ot_room` is single-owner: only the `ot_thermostat` task touches it. Readings arrive on other tasks
(the `ds18b20` task, the MQTT link's task), so they land first in a **mailbox** in
`ot_thermostat_room.c`, one entry per slot under a spinlock of its own (not the executor's), and
are drained into `ot_room_submit()` on the owner's 1 s tick. A newer reading overwrites an undrained
one. Each tick then:

1. drains the mailbox and submits each pending value, then ticks every slot;
2. runs the steer selection into the executor's inputs `room_fresh`, `room_dc` and
   `ha_forwarded_stale`;
3. runs the display selection: when something is fresh, publishes `room_temperature_effective`
   and sets `room_source` to `shield` (slot 0) or `mqtt` (slot 1); when nothing is fresh, sets
   `room_source` to `none` and leaves `room_temperature_effective` untouched, so the entity's own
   age drives it stale.

The slot layout is rebuilt from the configuration by the pure `ot_thermostat_room_build_cfg()`:

| Slot | Source | Role | `ha_forwarded` | Freshness window |
| --- | --- | --- | --- | --- |
| 0 | the shield DS18B20 | always `ambient` | false | 60 s |
| 1 | the MQTT source, **present only when `room_mqtt_enable`** | `room_mqtt_role` | `room_mqtt_ha_forwarded` | `room_mqtt_stale_s` |

The registry follows the configuration by a FNV-1a fingerprint of the four `room_mqtt_*` fields,
checked every tick. A change re-initialises the registry, which zeroes every slot, the shield's
included; a reading in flight is lost and each slot starts from `NEVER`. That is an accepted cost:
`ot_room_init()` has no partial mode, and inventing one here would duplicate what it promises not
to do.

With the MQTT source at its default (disabled), steering is always empty, `room_fresh` stays false
and the failsafe stays in its blind branch. Turning the MQTT source on with role `room` is what
makes the room-steered branch reachable.

## Slot 0: the shield DS18B20

On the LOLIN C3 mini the shield's DS18B20 is on GPIO1 (see [Board](/device/board)); on the C6 no
sensor is wired and the task does not start. The `ds18b20` task in `src/main.cpp` (4096-byte stack,
`tskIDLE_PRIORITY + 1`, started after HTTP and just before the LED task) opens the bus once and then, every 3 s, reads the sensor and
calls `ot_thermostat_room_submit(0, celsius)`. It does not filter: a rejected value is still handed
over, and `ot_room_submit()` is the one place that decides.

- **Why a task.** A read blocks about 800 ms on the conversion; in `app_main` it would delay HTTP,
  and on the bus task it is forbidden.
- **Why RMT.** A bit-banged 1-Wire reset holds interrupts off for about 480 µs and would corrupt
  the OpenTherm frame the boiler is mid-way through (`ot_master` ticks a gptimer about every
  100 µs). `ot_onewire` uses the managed `espressif/onewire_bus` driver (pinned `~1.1.2`), which
  clocks every slot as RMT symbols in hardware. Do not replace it with a hand-timed GPIO toggle.
- **One read.** `ot_onewire_read_temp()` is reset, `SKIP_ROM` + `CONVERT_T` (a single sensor on the
  bus is assumed), an 800 ms wait (the datasheet's 750 ms worst case for 12 bits, plus margin),
  reset, `SKIP_ROM` + `READ_SCRATCHPAD`, the CRC check, the decode. It answers `ESP_OK`,
  `ESP_ERR_NOT_FOUND` (no presence pulse: nothing wired), `ESP_ERR_INVALID_CRC` (the sensor answered
  but the frame was corrupt: noise) or an RMT error.
- **The decode** is the pure `ot_onewire_decode`: Dallas/Maxim CRC-8 (reflected polynomial `0x8C`,
  init 0), the signed 1/16 °C register, and °C as the one float.
- **Failure never blocks boot or the bus.** An open failure logs once and the task exits. A failed
  read submits nothing and logs a warning on the first failure after a success and then every
  100th (about once per 5 minutes), so a dead sensor cannot flood the 80-line log ring; freshness is
  left to the slot's own accumulator.

## Slot 1: the MQTT room source

Home Assistant publishes the room temperature to the device. This is an **inbound** topic, not part
of discovery (discovery describes what the device reports, not what it listens to), so the owner
wires the publish once.

```
<topic_prefix>/room/state
```

- **Payload:** a bare number in °C, for example `21.4`. The grammar is `-?[0-9]+(\.[0-9]+)?` and
  the result must be finite; `nan`, `inf`, `1e9`, `0x…`, an empty payload or JSON are refused.
- **Retain must be false.** A retained reading is **refused**: the broker re-delivers it on every
  reconnect looking freshly arrived while being arbitrarily old, and this value steers the boiler.
  The device measures arrival cadence, not measurement age, so it cannot tell a retained value from
  a live one.
- The topic is matched exactly by `ot_mqtt_decide()`, before the `/set` command topics, and the
  reading goes **straight into the mailbox**, never through `ot_command_check()`, an origin or the
  Home Assistant watchdog: a room reading is a measurement, not an owned command. It is subscribed
  at QoS 0 whether or not the source is enabled; with the slot absent, a valid reading is simply
  dropped.
- Out-of-range values and implausible jumps are dropped by the slot's filter; a source that stops
  publishing goes stale after `room_mqtt_stale_s`, and the failsafe reverts to blind heating instead
  of latching on an old number.
- The default `<topic_prefix>` is `opentherm/<device_id>` (the station MAC, for example
  `opentherm/aabbccddeeff`); the exact value is in `GET /api/config` (`topic_prefix`) and on the
  Settings page.

A refused reading (retained or not a number) is logged as `command refused: …` and counted in the
shared `mqtt.rejected` tally of `GET /api/status`, not in a room-specific counter. It has no effect
on control.

### The four settings

The source is **off by default**: turning it on changes what the failsafe does. Set it in the
web UI's **Settings → Room source (MQTT)** card or with `POST /api/config`:

| Field | Meaning | Default | Accepted |
| --- | --- | --- | --- |
| `room_mqtt_enable` | the slot exists | `false` | `false` / `true` |
| `room_mqtt_role` | `1` = room (steers), `0` = ambient (display only) | `1` | `0`–`1` |
| `room_mqtt_stale_s` | seconds before a silent source is stale | `900` | `10`–`65535` |
| `room_mqtt_ha_forwarded` | the reading is Home Assistant's own; a stale one forces the failsafe into `ha_blind` | `false` | `false` / `true` |

```sh
curl -X POST http://<device>/api/config \
  -H 'Content-Type: application/json' \
  -d '{"room_mqtt_enable": true, "room_mqtt_role": 1, "room_mqtt_stale_s": 900}'
```

When the device has a web password set, every write needs it as HTTP Basic authentication; the
user name is ignored (the device has no accounts), so add `-u any:<password>` to the command above.
Without a password, a configuration write is accepted from the LAN as shown.

An out-of-range number is refused with the `range` error and changes nothing. After a save,
`GET /api/config` echoes the four values, the registry follows within a second, and
`room_source` reads `mqtt` while a fresh reading arrives.

- **Role room** feeds the failsafe: when Home Assistant stops commanding and the watchdog trips,
  the failsafe holds the room near `failsafe_room_target_dc` with ±0.3 K hysteresis instead of
  heating blind.
- **Role ambient** is shown (`room_temperature_effective`, `room_source`) but never steers; right
  for a sensor that sits by the boiler.
- **`ha_forwarded`** opts into a second, more aggressive layer: if the marked room source goes stale
  while Home Assistant's commands are still fresh, the executor treats Home Assistant as blind and
  enters the failsafe with cause `ha_blind`. The default keeps it dormant; the command watchdog
  already covers "Home Assistant is gone".

### Home Assistant: publishing the reading

As an automation that tracks an existing sensor (replace the topic and the entity):

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
          retain: false               # REQUIRED: a retained reading is refused
```

A state-triggered automation fires on every change, so the `room_mqtt_stale_s` window does not
lapse while the sensor is alive (choose a window longer than the sensor's own report interval). For
a one-off test, call `mqtt.publish` with `payload: "21.4"` and `retain: false`.

## Not implemented

Only the shield DS18B20 and the MQTT source exist as adapters. Not implemented: Wi-Fi sensors pushed
or polled (Shelly, Tasmota, ESPHome), BLE sensors heard directly (Xiaomi, BTHome, ATC), 1-Wire as a
general room-source adapter beyond the shield, more than one MQTT room slot, a per-source
calibration offset, and a flap hold-time between two or more `room`-role sources in a fallback
chain.
