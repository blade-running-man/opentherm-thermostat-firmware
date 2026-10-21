# Indication

Both boards carry one addressable WS2812 RGB LED (**GPIO7 on the C3** mini, header label D3;
**GPIO8 on the C6** SuperMini), named only in `components/board/`. The firmware turns it into an
at-a-glance status indicator with **two independent axes on one lamp**:

- **colour** is the health of the control chain: Wi-Fi → MQTT → Home Assistant, plus the
  involuntary failsafe;
- **motion** is what the boiler is doing: idle, or burning gas.

The LED is a cosmetic **reader**: it only reads state other subsystems already publish and never
affects them. It is hardware-verified on the LOLIN C3 mini with this firmware: a green heartbeat
in normal operation. This page is the as-built reference for it; the owner's walk on real hardware is
[10 · Status LED](/hardware-verification/10-status-led).

## Components

Split the repository's way: a pure, host-tested core decides everything; a thin impure glue
samples the world and drives the pixel.

| Component | Kind | Role |
| --- | --- | --- |
| `ot_led` | pure | `ot_led_health()` runs the first-match ladder; `ot_led_render()` returns the RGB frame for the tier, its motion and the current time, scaled to a ceiling; `ot_led_health_name()` gives the tier's name. Host suite `test/test_ot_led` (26 tests: every tier, both grace edges, both flame states, the amber/orange hue gap, ceiling scaling) |
| `ot_led_task` | impure glue | one task that samples the world once a second, renders every ~50 ms and pushes the pixel through Espressif's `led_strip` (RMT backend, pinned `^3` in `idf_component.yml`). No host suite; pinned by `tools/tests/test_source_guards_led.py` |

`ot_led` takes everything as an argument (`ot_led_world_t`, including `now_ms`) and reads no
clock, global or pin, so every combination of inputs is a host test case. It declares its **own**
`ot_led_net_state_t` instead of including `ot_net.h`: `ot_net` is an impure component whose header
pulls `esp_err.h`, and PlatformIO's host build compiles every source of a library the moment a
suite reaches one of its headers, so reaching it would break `test_ot_led`. The enum is numbered
like `ot_net_state_t` (`DOWN`, `ACCESS_POINT`, `CONNECTED`), but the glue converts with an
explicit `switch`, never a cast, and an unknown value maps to `DOWN`, failing toward "show a
problem".

`app_main` starts the task last, guarded by `if (b->rgb.gpio >= 0)`: a board with no LED simply
gets none. `ot_led_task_start()` creates the RMT device once, shows a **solid-blue boot flash** at
the ceiling for 200 ms synchronously on `app_main`'s context (so the owner sees the LED is alive
even if higher-priority tasks keep the LED task off the CPU for a while), then creates task `led`
(4096-byte stack, `tskIDLE_PRIORITY + 1`). A failed `led_strip` init or task creation is logged and
stepped over.

## Colour ladder (health)

One lamp shows one state at a time. The ladder is **first broken link wins**: it shows the
earliest broken link in the chain, because that is the thing to fix, and suppresses downstream
symptoms. `ot_led_health_t` is declared in priority order and `ot_led_health()` is a cascade in
that order, the same shape as the executor's ladder.

| # | Tier (`ot_led_health_name`) | Colour | Condition |
| --- | --- | --- | --- |
| 0 | `ota` | blue | `ota_active` (dormant: nothing sets it, see [Out of scope](#out-of-scope)) |
| 1 | `setup` | blue | the network state is `ACCESS_POINT`: the device's own access point and setup portal are up |
| 2 | `connecting` | blue | the network is down and either no credentials are stored, or it has been down for less than `OT_LED_WIFI_GRACE_MS` (10 s) |
| 3 | `wifi_down` | red | the network has been down for 10 s or more **and** credentials are stored |
| 4 | `mqtt_down` | amber | connected, a broker host is configured, the MQTT client is not connected |
| 5 | `failsafe` | orange | links up, but the executor is in `OT_CONTROL_FAILSAFE` |
| 6 | `normal` | green | none of the above |

- **Green covers intentional non-HA operation** (LOCAL mode, a boost, heating season off): the
  ladder is about connectivity and the *involuntary* failsafe, never about which mode the owner
  chose.
- **Amber requires a configured broker.** A device deliberately run without MQTT never sits
  amber; MQTT is simply not part of its chain.
- **Connecting against no Wi-Fi.** `ot_net` exposes only `DOWN` / `ACCESS_POINT` / `CONNECTED`,
  so `DOWN` alone cannot tell "still associating after boot" from "the network is gone". `ot_led`
  resolves it by duration: down with credentials for less than 10 s is the transitional blue,
  past the grace it turns red. The grace window exists so that an ordinary reassociation (an
  access point hiccup, roaming) does not flash red. A down state with **no** stored credentials
  never turns red: that is the out-of-the-box state, not a fault.
- **First broken link** means that when MQTT drops and the watchdog later fires, the LED shows
  amber (the actionable cause), not orange (its consequence). Orange is therefore the signal for
  links that are healthy while Home Assistant itself stopped commanding.

### The colours

Each colour is a fixed share of the brightness ceiling per channel; the animation level then
scales it. Rounding happens once, so a channel that is nominally zero stays exactly zero.

| Colour | Red | Green | Blue |
| --- | --- | --- | --- |
| blue | 0 | 0 | 1.0 |
| red | 1.0 | 0 | 0 |
| amber | 1.0 | 120/255 (≈ 0.47) | 0 |
| orange | 1.0 | 50/255 (≈ 0.20) | 0 |
| green | 0 | 1.0 | 0 |

Amber and orange share a hue family and are separated on the green axis **and** on motion, so a
colour-vision deficiency that flattens one axis still leaves the other.

The ceiling is the board's `rgb.brightness`: **32 on the C3, 16 on the C6** (the C6 LED draws
current even when "off"). It is a compile-time descriptor value; there is no runtime brightness
setting.

## Motion (boiler activity)

Boiler activity is the OpenTherm Status (ID 0) slave **flame** bit: "the burner is firing", for
central heating or hot water alike. It counts only when the `flame` entity's value is present
(`availability == OT_AVAIL_OK`) and true; a stale or never-seen reading reads as idle, never as a
phantom "firing".

| Tier | Motion |
| --- | --- |
| `ota` | fast blink: 150 ms period, half on ("do not power off") |
| `setup` | slow blink: 2 s period, 1 s on, 1 s off |
| `connecting` | breathing: 3 s period, floor 10 % of the ceiling |
| `wifi_down`, `failsafe` | slow blink, 2 s period; flame is **not** shown, the fault is the message |
| `mqtt_down`, `normal` | flame on: breathing, 3 s period, floor 10 %, up to the full ceiling; idle: heartbeat |

The **heartbeat** is a 4 s cycle that sits at 12 % of the ceiling with one sine-shaped blip of
400 ms rising to 35 %. Its peak is deliberately far below full: a full-brightness pulse every four
seconds reads as a night-light, and the point of a heartbeat rather than a breath is that "boiler
idle" stays visibly dimmer than "boiler firing", readable from the corner of an eye without
reading the colour. `mqtt_down` and `normal` share one function for this (`activity_level()`), so
the two tiers can only differ in hue, never in motion.

All three curves are pure functions of `now_ms`: a hard square wave for the blink and
`(1 − cos)/2` for breathing (a triangle's corners are exactly where the eye notices a synthetic
animation). Two calls at the same instant always agree, which lets the glue re-render at any
cadence.

## Where each signal comes from

`led_sample()` in `ot_led_task.c` refreshes the world once a second; the animation clock advances
every ~50 ms tick between samples. Every getter is a non-blocking snapshot.

| World field | Source |
| --- | --- |
| `net` | `ot_net_get_state()`, converted by the explicit switch |
| `net_down_since_ms` | stamped once on the transition out of `CONNECTED`, reset to 0 while connected |
| `has_credentials` | `ot_net_has_credentials()` |
| `mqtt_configured` / `mqtt_connected` | `ot_mqtt_link_status()` `.configured` (a broker host is stored) / `.connected` (the client is up now) |
| `control` | `ot_thermostat_control_get()` `.state`, one consistent copy |
| `flame` | `ot_state_get("flame", …)` with `availability == OT_AVAIL_OK && boolean` |
| `ota_active` | an atomic set by `ot_led_note_ota()`; nothing calls it |
| `now_ms` | `esp_timer_get_time() / 1000`, the same base as `net_down_since_ms` |

`net_down_since_ms` is a **stamp, not now-minus-last**. Re-stamping it on every sample would keep
pushing it forward, so `now − since` would never cross the grace window and `wifi_down` would
never fire. The subtraction is unsigned over one monotonic base, so it stays correct across the
49.7-day millisecond wrap.

## Invariants

- **The LED never touches the OpenTherm bus.** It writes no frame, takes no bus lock and makes no
  blocking network call; a Wi-Fi, MQTT or Home Assistant failure only changes the colour. The
  master must never fall silent (a slave that stops hearing it for more than 5 s reads a shorted
  thermostat and demands heat). `test_source_guards_led.py` pins the shape: all five world sources
  sampled, flame read only after the availability check, the switch conversion, no bus call in
  the glue, and `app_main` starting the task only when the board has an LED.
- **No GPIO number outside `components/board/`.** The pin comes from `board->rgb.gpio`, the
  ceiling from `board->rgb.brightness`.
- **The control state is read, never recomputed.** The LED must not call into the executor's
  step.

## The RMT budget on the C3

An RMT channel on the ESP32-C3/C6 owns exactly **48 memory words**
(`SOC_RMT_MEM_WORDS_PER_CHANNEL`), and the chip has two TX-capable channels. The DS18B20 driver
(`onewire_bus`, through `ot_onewire`) already holds one RMT TX channel. `led_strip` is therefore
configured with **`mem_block_symbols = 48`**, one block. Any larger value, 64 for example, forces
the driver to span a second block, which is the other TX channel; the WS2812 device then
fails to create and the LED stays dark while the contention also breaks the 1-Wire receive. One
pixel is 24 bits, so 48 symbols with the copy encoder is ample. With 48 the budget fits exactly:
TX = 1-Wire + LED = 2 of 2, RX = 1-Wire = 1 of 2. There is a `DO NOT` on this in
`ot_led_task.c`; do not raise it. DMA is off (one pixel), the RMT resolution is 10 MHz, and the
colour format is GRB, so `ot_led`'s colours stay logical RGB.

::: info Known issue, unrelated to the LED
The shield DS18B20 has been seen logging `rmt_receive: channel not in enable state`. It is
independent of the LED (it appears even when the LED task never starts) and affects only the
shield temperature reading (slot 0, ambient), not the LED, the bus or the MQTT room source.
:::

## Why this shape

Comparable devices (Nest Heat Link, Particle, ESPHome, Ubiquiti, Espressif's `led_indicator`)
converge on colour as meaning (green healthy, blue setup, amber degraded, red fault) and motion as
a second axis (breathing for a healthy ongoing activity, blinking for attention). Nest overloads
green for both "connected" and "heating", and the two cannot be told apart; here the two axes are
kept on different channels, colour and motion, and one never hides the other.

### Why not `led_indicator`

Espressif's heavier `led_indicator` component brings its own priority and animation engine. The
priority and animation logic here lives in the pure `ot_led` instead, where it is host-tested like
the rest of the firmware's decisions; `led_strip` only moves the bits, generating the WS2812
timing in hardware so the LED task never bit-bangs or holds interrupts off next to an OpenTherm
master that must not fall silent.

## Out of scope

Deliberately not built:

- A setting or Home Assistant switch to disable the LED or set a night brightness (the device
  lives by the boiler, not in a bedroom). If added, it must go through the single entity list
  (`tools/opentherm_ids.py`), never a private path.
- Distinct motions for hot water against central heating; the LED shows "any flame".
- Wiring `ot_led_note_ota()` to a real update path. The fast-blue `ota` tier and its atomic flag
  exist and are host-tested, but the firmware has no over-the-air update (see
  [OTA and system](/device/ota-and-system)), so the flag is always false.
