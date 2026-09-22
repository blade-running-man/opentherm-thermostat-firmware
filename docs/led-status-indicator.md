# The RGB status LED — how it works

**As-built reference for the on-board WS2812B status indicator.** Hardware-verified on the
ESP32-C3 (green heartbeat in normal operation). The deep per-component contract lives in
`components/ot_led/README.md` and `components/ot_led_task/README.md`; the device-level summary is in
`docs/firmware-design.md` §2.7/§3.4 and `docs/implementation-reference.md`. This document ties them
together and records *why* the indicator is shaped the way it is.

## What it is

The board carries one addressable WS2812B RGB LED — **GPIO7 on the C3** (header label `D3`),
**GPIO8 on the C6** — described only in `components/board/board_*.c`. The firmware turns it into a
status indicator with **two axes on one lamp**:

- **Colour = health of the control chain** (WiFi → MQTT → Home Assistant, plus the failsafe).
- **Motion = what the boiler is doing** (idle vs. burning gas).

It is a **cosmetic reader**: it only reads state other subsystems already publish and never affects
them. See [Invariants](#invariants).

## Colour ladder (health)

One lamp shows one state at a time. The ladder is **first-broken-link wins** — it shows the
earliest broken link in the chain, because that is the thing to fix; downstream symptoms are
suppressed. Priority, highest first:

| Colour | State | When |
|--------|-------|------|
| 🔵 blue | boot / OTA / setup / connecting | AP config portal open, or associating (see below) |
| 🔴 red | **no WiFi** | credentials stored, net down past the grace window |
| 🟡 amber | **no MQTT** | WiFi up, a broker is configured but not connected |
| 🟠 orange | **HA silent → failsafe** | links up, but the executor fell to `OT_CONTROL_FAILSAFE` |
| 🟢 green | **normal** | HA/local/boost/season-off, all links up |

Notes:

- **Green covers intentional non-HA operation** (local, boost, season-off) — the ladder is about
  connectivity and the *involuntary* failsafe, never about which mode the owner chose.
- **Amber requires a broker to be configured.** A device deliberately run without MQTT never sits
  amber; MQTT is simply not part of its chain.
- **Connecting vs. no-WiFi.** `ot_net` exposes only `DOWN`/`ACCESS_POINT`/`CONNECTED`, so `DOWN`
  alone cannot tell "still associating after boot" from "the network is gone". `ot_led` resolves it
  by duration: `DOWN`-with-credentials for **less than `OT_LED_WIFI_GRACE_MS` (10 s)** is the
  transitional blue "connecting"; past the grace it becomes red.
- **First-broken-link precedence** means when MQTT drops *and* the watchdog later fires, the LED
  shows amber (the actionable cause), not orange (its consequence). Orange is therefore the signal
  for the case where the links are healthy but HA itself stopped commanding.

## Motion (boiler activity)

Boiler activity is read from **OpenTherm Status ID 0**, the slave-status **flame bit** — "the
burner is firing" for either central heating or hot water ("any flame"). Motion depends on the
health colour:

- **Green and amber (operational):** boiler **idle** → a faint **dim green/amber heartbeat**
  (proves the device is alive without being a nightlight); **flame on** → smooth **breathing** up
  to full brightness ("burning gas now").
- **Orange (failsafe) and red (no WiFi) — alarm:** **slow blink** to draw attention. Flame is *not*
  shown here — once you are in failsafe or off-network, the actionable message is the fault.
- **Blue (transitional):** AP portal waiting → slow blink; connecting → breathing; boot → a brief
  solid-blue flash; **OTA update → fast blink** ("do not power off"). OTA is **dormant in v1** — the
  firmware has no OTA subsystem yet, so the flag that drives it is always false; the pattern is
  wired and ready for when OTA lands.

All curves (breathe / heartbeat / blink) are deterministic functions of a monotonic millisecond
time; the animation ticks at ~20 Hz so motion is smooth. Every colour scales to the board's
brightness ceiling (`board->rgb.brightness`: 32 on the C3, 16 on the C6).

## Components

Split the repo's way: a **pure, host-tested core** decides everything; a **thin impure glue**
samples the world and drives the hardware.

- **`components/ot_led`** (pure) — the health ladder and the animation curves. `ot_led_health()`
  runs the first-match ladder; `ot_led_render()` returns the RGB frame for the tier, its motion,
  and the current time, scaled to a ceiling. Curves are pure functions of `now_ms`. Host suite
  `test/test_ot_led` (26 tests: every tier, both grace edges, both flame states, the amber/orange
  hue gap, ceiling scaling). It declares its own `ot_led_net_state_t` enum rather than including
  `ot_net.h` — `ot_net` is an impure ESP-IDF component whose header cannot compile on the host, so
  reaching it would break the suite; the enum is numbered to match `ot_net_state_t`.

- **`components/ot_led_task`** (impure glue) — a low-priority task (`tskIDLE_PRIORITY + 1`) that
  once a second samples the world, every ~50 ms calls `ot_led_render()` and pushes the pixel to the
  WS2812 via Espressif's `led_strip` (RMT backend). It owns the explicit `ot_net_state_t` →
  `ot_led_net_state_t` conversion (a `switch`, never a cast; unknown → DOWN, i.e. fails toward
  "show a problem"). Started from the end of `app_main` in `src/main.cpp`, guarded by
  `if (b->rgb.gpio >= 0)`. No host suite; pinned by `tools/tests/test_source_guards_led.py`.

### Where each signal comes from

| World field | Source |
|-------------|--------|
| `net` + grace | `ot_net_get_state()` (converted), stamp-once transition time |
| `has_credentials` | `ot_net_has_credentials()` |
| `mqtt_configured` / `mqtt_connected` | `ot_mqtt_link_status()` `.configured` / `.connected` |
| `control` | `ot_thermostat_control_get().state` (read-only snapshot) |
| `flame` | `ot_state_get("flame", …)` with `availability == OT_AVAIL_OK && boolean` |
| `ota_active` | atomic set by `ot_led_note_ota()` (dormant, always false in v1) |

`net_down_since_ms` is a **stamp-once accumulator**: set to the current time only on the
CONNECTED→down transition, reset to 0 on CONNECTED — never re-stamped each sample (which would make
the grace window never expire, the same "never now-minus-last" rake as `ot_sensor`).

## Invariants

- **The LED never touches the OpenTherm bus.** It writes no frame, takes no bus lock, makes no
  blocking network call. A WiFi/MQTT/HA failure only changes the colour. The bus must never fall
  silent (a silent master reads to the boiler as a heat demand). This is enforced by the source
  guard.
- **No GPIO number outside `components/board/`** — the pin comes from `board->rgb.gpio`, the
  brightness ceiling from `board->rgb.brightness`.
- **English everywhere** (comments, logs).

## The C3 RMT constraint (learned on hardware)

An RMT channel on the C3/C6 owns exactly **48 memory words** (`SOC_RMT_MEM_WORDS_PER_CHANNEL`),
and the chip has two TX-capable channels. The DS18B20 driver (`onewire_bus`) already holds one RMT
TX channel. `led_strip` is therefore configured with **`mem_block_symbols = 48`** (one block): a
larger value (64 was the first attempt) forces the driver to span a second block into the other TX
channel, the WS2812 device fails to create, and the LED stays dark. One pixel is 24 bits, so 48
symbols with the copy encoder is ample. There is a `DO NOT` on this in `ot_led_task.c`; do not raise
it. With 48, the budget fits exactly: TX = onewire + led = 2/2, RX = onewire = 1/2.

> **Unrelated known issue:** the shield DS18B20 logs a pre-existing 1-Wire RMT error
> (`rmt_receive: channel not in enable state`) that is **independent of the LED** — it appears even
> when the LED task never starts. It affects only the shield temperature reading (slot 0, ambient),
> not the LED, the bus, or the MQTT room source. Tracked separately.

## Why this shape (rationale)

Comparable devices (Nest Heat Link, Particle, ESPHome, Ubiquiti, Espressif's `led_indicator`)
converge on: colour = meaning (green healthy, blue setup, amber degraded, red fault), motion =
a second axis (breathing = a healthy ongoing activity, blink = attention). Nest overloads green for
both "connected" and "heating" and you cannot tell them apart — so here the two axes are kept on
*different* channels (colour vs. motion), and one never hides the other. `led_strip` (not the
heavier `led_indicator`) drives the pixel because the priority/animation logic lives in our own pure
`ot_led`, matching the repo's "pure core + thin glue, host-tested" pattern.

## Out of scope for v1 (possible future)

- A config entity / HA switch to disable the LED or set a night brightness (the device lives by the
  boiler, not a bedroom). If added it must go through the single entity list
  (`tools/opentherm_ids.py`), never a private path.
- Distinct motions for DHW vs. CH (v1 shows "any flame").
- Wiring `ot_led_note_ota()` to a real OTA subsystem.

## Verifying on hardware

See `docs/hardware-verification/scenarios/10-status-led.md` for the owner's walk (boot flash →
green heartbeat → force flame → stop broker → drop WiFi → watchdog failsafe → restore).
