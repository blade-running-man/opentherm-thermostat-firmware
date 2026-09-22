# 10 — The status LED

This scenario proves the on-board WS2812 status LED: a cosmetic, at-a-glance indicator of the
control chain's health (colour) and the boiler's activity (motion). It is **reader-only** — it
never writes an OpenTherm frame and never takes the bus lock, so nothing in this walk should ever
be able to move `cycles` or `answering` in the bus monitor from
[`00-setup-and-safety.md`](00-setup-and-safety.md).

Both boards carry the LED: LOLIN C3 mini on **GPIO7**, ESP32-C6 SuperMini on **GPIO8**. Have the
bus monitor from `00-setup-and-safety.md` running throughout, a broker and Home Assistant
([`06-home-assistant.md`](06-home-assistant.md)), and the failsafe set up for a short window
([`07-watchdog-and-failsafe.md`](07-watchdog-and-failsafe.md), `watchdog_s` at 60–120 so you are
not waiting fifteen minutes).

The pure colour/motion model is `components/ot_led/` (host-tested, `test_ot_led`); the sampling
and the WS2812 write are `components/ot_led_task/` (no host suite — pinned by
`tools/tests/test_source_guards_led.py`). How every colour and pattern works is in
`docs/led-status-indicator.md`.

> The shield's DS18B20 logging a `rmt_receive: channel not in enable state` line is a **pre-existing,
> separate** 1-Wire issue, unrelated to the LED and present even with the LED task never started.
> It is not part of this scenario and is not fixed by anything walked here.

## 1. Boot flash

- [ ] Power on (or reset) the device. The LED shows a brief **solid blue** flash (~200 ms) before
      anything else — synchronous, before the low-priority LED task is even scheduled.

## 2. Normal operation — green heartbeat

**Already verified on hardware** (LOLIN C3 mini): with WiFi, MQTT and Home Assistant all healthy
and the boiler idle, the LED shows a **dim green heartbeat** — a faint, slow pulse, not a
nightlight.

- [ ] Confirm the same on the bench: WiFi connected, MQTT connected, HA in control, boiler not
      firing → dim green heartbeat.

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
      (`docs/led-status-indicator.md`, "Colour ladder").

## Notes

- Colour is the health of WiFi → MQTT → Home Assistant (plus the failsafe); motion is boiler
  activity. The two axes are independent — e.g. amber-breathing and green-heartbeat are both
  real, valid combinations.
- The LED never affects, delays, or is delayed by the bus: throughout every step above, `cycles`
  in the bus monitor keeps moving and `answering` stays `true` whenever the boiler is connected.
- A board with no LED wired (`rgb.gpio < 0` in its `board_*.c` descriptor) simply starts no LED
  task — nothing to check there.
