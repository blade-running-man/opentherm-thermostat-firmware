# 08 — Room-temperature sources

This scenario proves the registry of room-temperature sources: the shield's own sensor, an MQTT
source Home Assistant can feed, how each is configured and persisted, which one the device shows,
and that a *retained* reading is refused. How a room source **steers** the failsafe is the
safety-critical half and lives in [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md);
this file is the plumbing that feeds it.

The registry holds up to four slots, each with its own outlier filter and freshness state, each
declared `room` or `ambient`. Two independent picks are made from it: a **steer** selection
(safety-critical — only a `room`-role, fresh slot may drive the CH bit) and a **display**
selection (a room source preferred, an ambient one as fallback). Today two adapters exist —
**slot 0 is the shield DS18B20 (always ambient), slot 1 is the MQTT source (its role set by
config)**.

Have the curl harness from [`00-setup-and-safety.md`](00-setup-and-safety.md) ready, a broker and
Home Assistant ([`06-home-assistant.md`](06-home-assistant.md)), and the fields at their defaults
(`room_mqtt_enable=false`, `role=1`, `stale_s=900`, `ha_forwarded=false`).

## 1. The shield DS18B20 (always ambient)

- [ ] `Room temperature` (`room_temperature_effective`) in Home Assistant reads the shield sensor,
      and `Room source` reads `shield`.
- [ ] Warm the DS18B20 by hand → the value tracks. Unplug or stop it → after its window the value
      goes **stale** (not frozen), and `Room source` falls to `none`.

## 2. Configuring the MQTT source

- [ ] The SPA Settings "Room source (MQTT)" card shows four fields. Set `enable` on, `role` Room,
      `stale` 900, and Save. `GET /api/config` echoes them.
- [ ] Reload the page — the values are still shown — and power-cycle the device: they survive.

## 3. Feeding a reading

- [ ] From Home Assistant, publish a room temperature to `<topic_prefix>/room/state` (a bare
      float, **retain: false**; find `<topic_prefix>` in `/api/config`). `Room source` becomes
      `mqtt` and `Room temperature` shows the published value — a room source wins over the shield
      for display.

## 4. A retained reading is refused

- [ ] Publish the same topic with **retain: true**. The device does **not** adopt it — it stays on
      the last live value or goes stale — and nothing steers off a retained value.

> A retained reading is a stale reading the broker hands back on every reconnect; steering the
> boiler off it would hold a temperature from minutes or hours ago. The refusal is counted under
> `mqtt.rejected` and logged as a "command refused" (a known cosmetic mislabel — it is a reading,
> not a command).

## 5. Room vs ambient, for display

- [ ] With the MQTT source at role **Room** and fresh, `Room source` reads `mqtt` (the room source
      is preferred for display over the ambient shield).
- [ ] Set the MQTT source's role to **Ambient** and save: it may still show for display as a
      fallback, but it must never steer — that guard is proven in
      [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md). Set it back to Room afterwards.

## 6. Web ↔ API parity

- [ ] Everything the Room-source card does, curl can do: set the four keys
      (`room_mqtt_enable`, `room_mqtt_role`, `room_mqtt_stale_s`, `room_mqtt_ha_forwarded`) with
      `POST /api/config` and confirm the card reflects them on reload — the web has no privileged
      handle.
- [ ] An out-of-range value from the card (e.g. `stale` 5) is refused by the device and the
      refusal is surfaced in the card's notice, not silently swallowed.

## Notes

- The MQTT room topic and payload format are documented in `docs/ha-room-source.md`.
- WiFi push/pull, BLE and general 1-Wire sources are planned but not yet built; only the shield
  DS18B20 and the MQTT source exist as adapters today.
- The steering behaviour that consumes this registry — room-steered vs blind failsafe, the
  ambient-never-steers guard, the stale-reverts-to-blind rule, and the `ha_forwarded` option — is
  all in [`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md).
