# 10 — The status LED

This scenario proves the on-board WS2812 status LED: a cosmetic, at-a-glance indicator of the
control chain's health (colour) and the boiler's activity (motion). It is **reader-only** — it
never writes an OpenTherm frame and never takes the bus lock, so nothing in this walk should ever
be able to move `cycles` or `answering` in the bus monitor from
[`00-setup-and-safety.md`](/hardware-verification/00-setup-and-safety).

Both boards carry the LED: LOLIN C3 mini on **GPIO7**, ESP32-C6 SuperMini on **GPIO8**. Have the
bus monitor from `00-setup-and-safety.md` running throughout, a broker and Home Assistant
([`06-home-assistant.md`](/hardware-verification/06-home-assistant)), and the failsafe set up for a short window
([`07-watchdog-and-failsafe.md`](/hardware-verification/07-watchdog-and-failsafe), `watchdog_s` at 60–120 so you are
not waiting fifteen minutes).

The pure colour/motion model is `components/ot_led/` (host-tested, `test_ot_led`); the sampling
and the WS2812 write are `components/ot_led_task/` (no host suite — pinned by
`tools/tests/test_source_guards_led.py`). How every colour and pattern works is in
[Indication](/device/indication).

> An `rmt_receive: …` line in the log belongs to the shield DS18B20's 1-Wire reader
> (`ot_onewire`, which also drives the RMT peripheral), not to the LED task. It is outside this
> scenario; the DS18B20 is walked in [`08-room-sources.md`](/hardware-verification/08-room-sources).

## 1. Boot flash

- [ ] Power on (or reset) the device. The LED shows a brief **solid blue** flash (~200 ms) before
      anything else — synchronous, before the low-priority LED task is even scheduled.

## 2. Normal operation — green heartbeat

- [ ] WiFi connected, MQTT connected, Home Assistant in control, boiler not firing → the LED
      shows a **dim green heartbeat** — a faint, slow pulse, not a steady nightlight.

## 3. A flame call — green breathing

- [ ] With everything still healthy, force a heat call (raise the HA setpoint, or use the boost)
      until the boiler's burner fires (`flame` true in `/api/state`). The LED switches from the dim
      heartbeat to a **green breathing** pulse — smoothly rising and falling between low and full
      brightness — for as long as the flame is on, and drops back to the heartbeat once it goes
      out.

## 4. Stop the broker — amber

- [ ] With a broker configured, stop it (or block it) while WiFi stays up. Once
      `ot_mqtt_link_status()` reports not-connected, the LED turns **amber**, motion following the
      idle/breathing rule from §2/§3 (amber is a health colour like green, not an alarm state).
- [ ] Restart the broker → the LED returns to green once MQTT reconnects.

## 5. Drop WiFi — red blink

- [ ] Take the device out of WiFi range (or disable the AP) for more than the connecting grace
      window (~10 s) while it already holds credentials. The LED turns **red** with a **slow
      blink** — an alarm pattern, not breathing; flame state is not shown once WiFi is down.
- [ ] Restore WiFi → the LED passes back through the transitional blue (briefly, while
      reconnecting) to green.

## 6. Let the watchdog fire — orange failsafe blink

- [ ] With WiFi and MQTT healthy but Home Assistant silenced past `watchdog_s`
      (`07-watchdog-and-failsafe.md` §1), the device enters `control_state` `failsafe`. The LED
      turns **orange** with a **slow blink** — the alarm pattern for "links are up, but HA itself
      stopped commanding."
- [ ] Bring Home Assistant back → the device leaves failsafe and the LED returns to green (via
      breathing if a flame is still on from the failsafe's own heat call).

## 7. Priority order, if more than one is broken at once

- [ ] With WiFi down **and** the broker also unreachable, the LED shows **red**, not amber — WiFi
      is the first broken link in the chain, and downstream symptoms are suppressed
      ([Indication](/device/indication#colour-ladder-health)).

## Notes

- Colour is the health of WiFi → MQTT → Home Assistant (plus the failsafe); motion is boiler
  activity. The two axes are independent — e.g. amber-breathing and green-heartbeat are both
  real, valid combinations.
- The LED never affects, delays, or is delayed by the bus: throughout every step above, `cycles`
  in the bus monitor keeps moving and `answering` stays `true` whenever the boiler is connected.
- A board with no LED wired (`rgb.gpio < 0` in its `board_*.c` descriptor) simply starts no LED
  task — nothing to check there.
