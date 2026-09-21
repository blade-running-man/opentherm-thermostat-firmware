# 07 — The watchdog and the bounded failsafe

This scenario proves the firmware's guard against the controller disappearing. When Home
Assistant goes quiet, the watchdog trips and the device enters a **bounded** failsafe rather than
either falling silent (which the boiler reads as a demand for heat) or heating the house blind
forever. It covers the three failsafe branches — disarmed, blind, and room-steered — and the rule
that an *ambient* source never steers.

Have the curl harness and the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md) running — the bus must never fall silent
through any failsafe. You need a broker and Home Assistant you can silence
([`06-home-assistant.md`](06-home-assistant.md)); the soft-reset item wants a bench supply. Set
`watchdog_s` to 60 or 120 for these tests so you are not waiting fifteen minutes.

> **Safety-critical.** Room-role failsafe steering is off by default. The failsafe entering the
> **blind** branch means the boiler fires at `failsafe_setpoint` with no room feedback — expected,
> and the point of the feature, but do it with the boiler in sight.

## 1. The watchdog trips to failsafe

- [ ] With Home Assistant driving (HA mode, season on, HA sending `ch_enable` fresh), **kill Home
      Assistant** (or stop its MQTT). After the watchdog window, `/api/control` `control_state`
      becomes `failsafe`, and the bus keeps turning throughout.
- [ ] Bring Home Assistant back → the device leaves failsafe and resumes HA control.

## 2. Disarmed vs blind

The branch depends on whether Home Assistant ever actually asked for heat.

- [ ] **Disarmed.** On a device that has *never* stored an HA CH command since boot, let the
      watchdog trip: `control_state` `failsafe`, reason `fs_disarmed` — CH is **held off** (the
      summer bound holds; no one asked for heat, so blind heating would be wrong). The web reason
      line reads "CH is held off: Home Assistant has not asked for heat within failsafe_heat_days."
- [ ] **Blind.** Turn the season on and have Home Assistant send `ch_enable = 1` at least once,
      then silence it and let the watchdog trip: reason `fs_blind` — the CH bit is **on**, the
      boiler fires at `failsafe_setpoint`, and the heat hours are stored. The web reason line reads
      "No fresh room temperature: heating blind at the failsafe setpoint."
- [ ] **Across a reboot.** With HA having asked for heat within `failsafe_heat_days`, power-cycle
      and wait out the watchdog with HA silent → `fs_blind` (the fact that HA asked survives the
      reboot). On a device that never heard HA ask, the same reboot ends in `fs_disarmed`.

## 3. The reset reason carries the overdue count

- [ ] The reset reason is logged at boot. After a **power cycle**, the boot log says the watchdog
      overdue count and the part-hour start from zero (`reset reason poweron: watchdog overdue and
      part-hour start from zero …`).
- [ ] After a **soft reset** (a panic, a watchdog, a brownout or an OTA), the overdue count is
      **restored** rather than zeroed, so the failsafe arrives sooner. With a bench supply: HA
      mode, broker up, HA silent, and dip the supply to brown out after ~60 s of a 120 s
      `watchdog_s`. The boot log names the reset `brownout`, says the overdue count was restored,
      and the failsafe arrives ~60 s after boot, not 120 s.

> The distinction matters because a device that reboots repeatedly must not keep granting itself a
> fresh full watchdog window each time — that would let a blind boiler run indefinitely across a
> reboot loop.

## 4. A retained command does not rearm the watchdog

- [ ] A retained command replayed by the broker is refused and does **not** feed the watchdog, so
      the device still reaches `failsafe` — walk the retained-command item in
      [`06-home-assistant.md`](06-home-assistant.md) and confirm `control_state` reaches `failsafe`
      with cause `watchdog`, `commands` unmoved.

## 5. The failsafe steered by the room

With a room-role MQTT source configured and fresh
([`08-room-sources.md`](08-room-sources.md)):

- [ ] Kill Home Assistant and let the watchdog trip. The failsafe now **holds the room** near
      `failsafe_room_target` with ±0.3 K hysteresis — cause `fs_room_cold` / `fs_room_warm` /
      `min_cycle`, not blind. Watch the CH bit cycle around the target as the room reading crosses
      it.

## 6. An ambient source never steers

- [ ] With only the shield DS18B20 present (always ambient), entering the failsafe still takes the
      **blind** branch (`fs_blind`) — an ambient source never steers, however fresh.
- [ ] Set the MQTT source's role to **Ambient** and repeat: even a fresh MQTT reading must not
      steer — the failsafe is `fs_blind`, and `Room source` may read `mqtt` for display while the
      CH decision ignores it. Set it back to Room afterwards.

> Steering off an ambient reading (a sensor sitting on the shield beside warm electronics, say)
> would hold the *wrong* temperature. Only a source the owner has declared a *room* source may
> drive the CH bit.

## 7. A dying room source reverts to blind, never latches

- [ ] With the failsafe steering by the room, stop publishing the reading. After
      `room_mqtt_stale_s` the source goes stale and the failsafe reverts to the **blind** branch —
      it never latches on an old room value.

## 8. The ha_forwarded guard

- [ ] (Opt-in) Set `room_mqtt_ha_forwarded` on. With Home Assistant still alive, let the room
      reading go stale (stop publishing it while HA lives). The device enters failsafe with cause
      `ha_blind` — a marked room source dying is treated as Home Assistant having gone blind. Turn
      it back off unless you want that behaviour.

## 9. The failsafe banner on the web

- [ ] While the failsafe is active, the Control page shows a red-edged "Failsafe is active" banner
      reading "No CH command from Home Assistant for longer than watchdog_s. N failsafe entry since
      this boot.", with state "Failsafe" and the reason line matching the branch (§2's disarmed vs
      blind wording).
- [ ] After the device leaves the failsafe, the banner turns amber-edged "The failsafe has run
      since this boot", reading how many entries and how long the last lasted.
- [ ] The banner appears on the web even though nothing reaches Home Assistant — the web reads the
      device directly.

## Notes

- The config keys are `watchdog_s`, `failsafe_setpoint_dc`, `failsafe_heat_days`,
  `failsafe_room_target` and `room_mqtt_stale_s`; the form's validation of the failsafe setpoint
  against the band is in [`04-live-web-and-ui.md`](04-live-web-and-ui.md).
- On a boiler with no outdoor sensor, the long-outage rows that need ID 27 are unreachable and a
  long outage ends in an indefinite stale state; connect an outdoor sensor to reach them.
