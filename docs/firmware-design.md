# OpenTherm Thermostat Firmware — Design and Reference

**A room thermostat firmware for a gas boiler with an OpenTherm interface.**
The device is the sole OpenTherm **master** and acts as an **executor**: Home Assistant
holds the control law, and the firmware carries out the commanded state, guards it against
nonsense values, and keeps the boiler safe when Home Assistant, the broker, or the network
disappears.

This document is the single, self-contained source of truth for the firmware's design and
behaviour. It describes the firmware exactly as built.

Platform: C, ESP-IDF 5.5.x (carried by a pinned PlatformIO platform), two ESP32-C3 / ESP32-C6
targets, one universal binary per chip, everything configured at runtime.

---

## Table of contents

1. [Overview and the executor model](#1-overview-and-the-executor-model)
2. [Hardware and supported platforms](#2-hardware-and-supported-platforms)
3. [The OpenTherm layer](#3-the-opentherm-layer)
4. [The executor core](#4-the-executor-core)
5. [The bus](#5-the-bus)
6. [Entities and the registry](#6-entities-and-the-registry)
7. [MQTT and Home Assistant](#7-mqtt-and-home-assistant)
8. [Room-temperature sources (`ot_room`)](#8-room-temperature-sources-ot_room)
9. [Failure handling](#9-failure-handling)
10. [Configuration](#10-configuration)
11. [Building and testing](#11-building-and-testing)
12. [Acceptance criteria](#12-acceptance-criteria)
13. [Not in scope / delegated to Home Assistant](#13-not-in-scope--delegated-to-home-assistant)
- [Appendix A: Boiler reference — Intergas Kombi Kompakt HRE](#appendix-a-boiler-reference--intergas-kombi-kompakt-hre)

---

## 1. Overview and the executor model

### 1.1 What the device is

The firmware turns an ESP32 board plus an OpenTherm adapter shield into a room thermostat for
a modulating gas boiler. It speaks the OpenTherm 2.2 protocol as the **only master on the
bus**. It is **not** an OpenTherm Gateway (OTGW): it does not sit between an existing
thermostat and the boiler and does not relay a third party's frames — the adapter has no slave
side. The device is itself the master.

### 1.2 The roles

The control law does not live on the device. Home Assistant does. The firmware is an executor
and a guard.

| Who | Owns |
| --- | --- |
| **Home Assistant** | The control law, schedules, scenarios, the owner's sensors, all "when to heat and how hot" decisions. |
| **The firmware** | The bus (sole master, never silent), the commanded state, value bounds, the watchdog, the bounded failsafe, and its own view of the room (`ot_room`, §8). |
| **The web interface** | Diagnostics (the Boiler and State pages) and minimal control: the control card and boost in LOCAL mode. It has **no privileged API handle** — everything it does is a normal REST call. |

What the firmware deliberately does **not** do: no PI/PID control loop, no anti-windup, no
weekly schedule, no modes, no anti-cycling logic, no room-temperature control loop. Those were
removed on purpose; see §13.

### 1.3 The two modes

The firmware runs in one of two `control_mode` values at any moment:

- **LOCAL** — the default on a fresh flash. The device is driven from its own web
  interface / REST: a stored CH enable, a stored flow setpoint, and an optional boost. Home
  Assistant's commands are refused in this mode.
- **HA** — Home Assistant is the controller. The firmware carries out HA's commands (subject
  to bounds and ownership), runs the watchdog, and enters the failsafe if HA falls silent.
  HA mode is refused while no MQTT broker host is configured.

### 1.4 The safety spine

Three invariants sit above everything and must never be broken:

1. **The bus never goes silent.** A slave that hears no correct master frame for 5–15 s
   interprets it as a shorted thermostat and **demands heat** (OpenTherm §3.5). Falling
   silent is not a safe state; it is the hottest one. No peripheral failure — WiFi, broker,
   a stale sensor, a dead Home Assistant — may ever stop the bus.
2. **CH is never requested before the held flow setpoint has gone out.** A boiler asked for
   heat with no chosen TSet heats to whatever it last had.
3. **Nothing reboots because a peer is absent.** No "the broker has been unreachable for N
   minutes → restart".

---

## 2. Hardware and supported platforms

### 2.1 The two boards

Two chips are supported, each with its own binary. "One universal binary" means one per chip,
not one for everything; both are configured identically at runtime.

- **LOLIN C3 mini** (ESP32-C3FH4) — the **primary** target. The DIYLESS shield plugs directly
  into its D1-mini-form-factor header. Single-core RISC-V at 160 MHz, 4 MB flash, no PSRAM,
  400 KB SRAM, 3.3 V I/O (not 5 V tolerant). Acceptance runs on this board.
- **ESP32-C6 SuperMini** (ESP32-C6FH4) — the **secondary** target. The shield does **not**
  plug in; it is wired with four wires (OT OUT, OT IN, 3V3, GND), five when a DS18B20 is used.
  It must build and be checked separately, but it is not the acceptance target.

Board selection is by `IDF_TARGET` in CMake (`esp32c3` → the C3 descriptor, `esp32c6` → the
C6 descriptor, anything else → a fatal error). This allows exactly one board per chip; a build
flag would not reach the CMake logic.

### 2.2 The adapter shield

The **DIYLESS ESP8266 Thermostat Shield** (WeMos D1 mini form factor) connects to the boiler
by a two-wire terminal block; **polarity does not matter**. The shield's logic is powered from
the board's 3.3 V; the **bus voltage (15–24 V) is supplied by the boiler**, and the two supplies
must not be confused — the shield physically cannot deliver the bus voltage and is not meant to.
The decisive diagnostic when there is no communication is to measure the DC voltage on the
boiler's terminals **with the shield disconnected**: no 15–24 V means it is not OpenTherm.

### 2.3 Pinout

**Not a single GPIO number appears anywhere outside `components/board/`.** This is a load-bearing
rule (§11), because copying a number from one board descriptor into another can kill the console:
on the C3 the USB console is on GPIO18/19, on the C6 it moved to GPIO12/13.

Inversion is a property of the **adapter**, not the board or the protocol, so it is identical on
both boards: **only the output is inverted.** `ot_out_inverted = true`, `ot_in_inverted = false`.
The active line state reads as HIGH, rest as LOW, and an internal pull-**down** is placed on the
input so a disconnected adapter reads as rest rather than as noise the decoder mistakes for
frames. This one field is derived from another project's code, not from the specification —
**if not a single frame decodes, flip `ot_in_inverted` first, before looking for a bug in the
decoder.**

**LOLIN C3 mini** (`board_lolin_c3_mini.c`, name `"lolin_c3_mini"`):

| Function | GPIO | Header / note |
| --- | --- | --- |
| OpenTherm IN (`ot_in`) | **8** | D2. Strapping pin, but does not block normal flash boot. |
| OpenTherm OUT (`ot_out`) | **10** | D1 |
| BOOT button (`button`) | 9 | inverted; is the download strap |
| RGB LED (`rgb`) | 7 | D3, WS2812B, brightness 32 |
| plain LED (`status_led`) | −1 | none |
| USB Serial/JTAG console | 18 / 19 | not brought out to the header (occupied by USB) |

**ESP32-C6 SuperMini** (`board_supermini_c6.c`, name `"supermini_c6"`):

| Function | GPIO | Note |
| --- | --- | --- |
| OpenTherm IN (`ot_in`) | **18** | chosen (wired, no header) |
| OpenTherm OUT (`ot_out`) | **19** | adjacent pad to 18 |
| BOOT button (`button`) | 9 | inverted; is the download strap |
| RGB LED (`rgb`) | 8 | WS2812, brightness 16 |
| plain LED (`status_led`) | 15 | strapping pin, safe as output |
| USB Serial/JTAG console | 12 / 13 | on the header |
| UART0 | 16 / 17 | reserved |

The C6 OT pins were chosen as adjacent pads that are neither strapping (4, 5, 8, 9, 15), USB
(12, 13), UART0 (16, 17), the LED, nor the button.

### 2.4 The USB / GPIO trap

The trap the per-board descriptors exist to prevent: **on the C3, GPIO18 and GPIO19 are the USB
console; on the C6 those same pins are free (USB moved to 12/13).** Copying the C3's OT pins
(8/10, dictated by the shield header) onto the C6, or the C6's numbers back onto the C3, would
kill the console and flashing. Additionally on the C6, GPIO8 is both a strapping pin and the
WS2812 LED, so it must not be reused for OT.

Strapping note (C3): GPIO2 (header D0) must be HIGH at reset for a normal boot — the shield does
not occupy D0, but this must be checked. The OT-IN pin GPIO8 does not block a normal boot, but a
connected, silent boiler holds it LOW, so **entering flashing mode with the boiler connected may
require holding BOOT** (or briefly disconnecting the boiler).

### 2.5 The DS18B20

The shield has a spot for a DS18B20 one-wire temperature sensor, soldered on the owner's unit. It
is **built and wired**: `board.onewire_gpio` (LOLIN C3 mini: GPIO1, shield position D5;
ESP32-C6 SuperMini: `-1`, none wired yet) drives a dedicated `ds18b20_task`
(`src/main.cpp`) that reads it over RMT (`ot_onewire`) roughly every 3 s and submits into `ot_room`
slot 0 as an `ambient` source (§8). A board with `onewire_gpio < 0` simply does not start the
task — no room readings from the shield, no error.

### 2.6 Platform pin and partitions

`platformio.ini` pins the platform: `pioarduino/platform-espressif32#55.03.311`. The pin is
deliberate — a floating URL once moved the toolchain out from under a verified design. There is
**no `framework = arduino`** in any environment (the word `arduino` appears only in the
`pioarduino` vendor URL and a comment); `framework = espidf` throughout.

`partitions.csv` — two OTA app slots, no filesystem (the web UI is compiled into the app):

| Name | Type / subtype | Offset | Size |
| --- | --- | --- | --- |
| nvs | data / nvs | 0x9000 | 0x4000 (16 KiB) |
| otadata | data / ota | 0xd000 | 0x2000 |
| phy_init | data / phy | 0xf000 | 0x1000 |
| ota_0 | app / ota_0 | **0x10000** | 0x1F0000 (1984 KiB) |
| ota_1 | app / ota_1 | 0x200000 | 0x1F0000 (1984 KiB) |

`ota_0` must stay at `0x10000` and `nvs` is deliberately 0x4000 (not the stock 0x6000): a larger
nvs would push the first app partition off 0x10000, where the merged flashable image expects it.

### 2.7 The status LED

Both boards carry an onboard WS2812 (`board.rgb.gpio`: LOLIN C3 mini GPIO7, ESP32-C6 SuperMini
GPIO8) the firmware drives as a cosmetic, at-a-glance status indicator. Colour and motion are two
independent axes: **colour** is the health of the WiFi → MQTT → Home Assistant chain, including the
failsafe (blue while provisioning/connecting, red with no WiFi, amber with no MQTT, orange in the
bounded failsafe, green when everything upstream is healthy); **motion** reflects boiler activity —
a dim green heartbeat blip when idle, a breathing pulse while the burner is calling for heat, and a
blink for the alarm-coloured states. The pure health ladder and animation curves live in
`components/ot_led/` (host-tested, `test_ot_led`); the impure sampling and the WS2812/RMT write
live in `components/ot_led_task/`, a single low-priority task that samples the network, MQTT and
control state once a second. It is **reader-only**: it never writes an OpenTherm frame, never
takes the bus lock, and never blocks on the network, so it cannot be the reason the master falls
silent (§3.4). A board with `rgb.gpio < 0` simply gets no LED task.

---

## 3. The OpenTherm layer

All facts here come from the OpenTherm 2.2 specification. The layer is split into small pure,
host-tested components plus a thin hardware-touching master.

### 3.1 The frame

34 bits: start `1`, 32 data bits, stop `1`. The data, most-significant first:

```
P | MSG-TYPE (3) | SPARE (4) | DATA-ID (8) | DATA-VALUE (16)
```

Parity is **even** across all 32 bits. (A common trap: a widely copied helper is *named* "odd"
in a comment but implements even parity. Tests pin the behaviour, not the name.)

MSG-TYPE values: `000` READ-DATA, `001` WRITE-DATA, `010` INVALID-DATA, `011` reserved,
`100` READ-ACK, `101` WRITE-ACK, `110` DATA-INVALID, `111` UNKNOWN-DATAID.

Value codecs: `f8.8` (signed fixed point, divisor 256), `u16`, `s16`, `u8_hb`, `u8_lb`,
`s8_hb`, `s8_lb`, `flag8_hb_N`, `flag8_lb_N`.

### 3.2 The Manchester decoder

Encoding is Manchester (Bi-phase-L): **`1` is active→rest, `0` is rest→active**. A Manchester
violation rejects the whole frame. The mid-bit period is 900–1150 µs (nominal 1000), and the
count is reset on every transition so error does not accumulate.

Reception is **oversampling by a hardware timer, no edge ISR**: a `gptimer` at **10 kHz
(100 µs)** samples the level, and the decoder counts samples between transitions. From the
tolerance (half-bit 450–575 µs, full bit 900–1150 µs), the sample-count windows are:

| | at 100 µs step |
| --- | --- |
| half-bit | **{4, 5, 6}** |
| full bit | **{9, 10, 11, 12}** |
| gap between windows | **7 and 8 — two free positions** |

The 100 µs step is deliberate, not 200 µs and not 5 kHz. At 200 µs the half-bit and full-bit
windows butt together, so a frame 12 % too fast (800–875 µs) is accepted as normal and a faulty
slave looks healthy. **Do not lower the sample rate or widen the windows.** The sampling timer
runs only during a conversation, not continuously.

Transmission uses a `gptimer` at 500 µs to emit half-bits: 34 bits = 68 half-bits ≈ 34 ms.

### 3.3 The master and one conversation

`ot_master` owns bits and timings and does not know what to ask; `ot_bus` knows what to ask and
does not know about bits. This seam keeps the polling schedule a pure, host-tested function.

One conversation is exactly one frame each way. Timings:

- The slave's response arrives 20–800 ms after the end of the master's frame.
- The master holds at least 100 ms between conversations.
- The master must speak at least once per 1 s (+15 %, i.e. ≤ 1.15 s).

Scheduling is a rule, not a fixed period:

```
next_start = max(end_of_previous_response + 100 ms, start_of_previous + 950 ms)
```

with the checked assertion `next_start ≤ start_of_previous + 1150 ms`. All frame errors are
unrecoverable: the frame is rejected and the conversation ends.

Bus timing constants: `OT_BUS_PERIOD_MS = 950`, `OT_BUS_MIN_GAP_MS = 100`,
`OT_BUS_DEADLINE_MS = 1150`, `OT_BUS_MAX_POLL = 32`.

### 3.4 The silence rule

Because a slave reads master silence (5–15 s) as a shorted thermostat and demands heat, the bus
does not stop under any peripheral failure. The one unavoidable silence is a **reboot** (routine
during OTA): the device is absent for longer than 5 s, the boiler briefly demands heat, and this
is expected boiler behaviour, not a firmware defect. The bus starts its first conversation as
early as possible after boot, before waiting for WiFi.

---

## 4. The executor core

`ot_control` is a pure, host-tested component (`components/ot_control/`). Time enters only as
an argument; no ESP-IDF header is included. Its inputs are a configuration snapshot, commands
tagged with an **origin** (`WEB` or `HA`), room-source state (§8, fed from `ot_room` —
all-zero unless a `room`-role source is configured and fresh), and a
monotonic tick. Its outputs are the ID 0 high byte, the held ID 1 (flow setpoint) value, the
desired ID 56 (DHW setpoint) value, and a `control_state` with its `reason` and `cause`.

The task layer `ot_thermostat` steps the core once per second (`TICK_MS = 1000`) and is the
impure glue: the spinlock, the bus calls, NVS, and `RTC_NOINIT`. The translations between the
core and its neighbours live in `ot_control_io`. Both `ot_control` and `ot_control_io` are pure
and host-tested; the decisions are theirs, and `ot_thermostat` only carries them out.

### 4.1 The held setpoint is always valid

`held_ch_setpoint` exists from boot, initialised to `failsafe_setpoint`, quantised to 0.5 K then
clamped to `[flow_min, flow_max]`. There is no "no setpoint" state.

**The invariant (host-tested): the CH bit never rises unless the held ID 1 has gone out on the
bus at least once since it was last changed.** It is enforced in the step: a change to the held
value un-confirms it; the CH bit is gated only on a **rising** edge (a bit already up is not
dropped by a new setpoint), and while unconfirmed the reason is `await_setpoint`. A *foreign* ID 1
value seen on the wire also un-confirms the held value (the confirmation compares the last ID 1
the bus reported against the held value). ID 1 is re-asserted in **every** state, including those
with the CH bit down — harmless, because the CH-enable bit overrides the setpoint.

### 4.2 The state ladder

`control_state` is an ordered ladder; **first match wins** (a straight sequence of `if … return`).
The enum order is also the priority and must not be reordered.

| # | State | Condition | CH bit | ID 1 |
| --- | --- | --- | --- | --- |
| 1 | `season_off` | `heating_season` is false | 0 | held (harmless) |
| 2 | `boost` | mode LOCAL and a boost is running | 1 | the boost setpoint |
| 3 | `local` | mode LOCAL | `local_ch_enable` | `local_ch_setpoint` |
| 4 | `ha_waiting` | mode HA, no accepted HA CH command yet, watchdog not expired | 0 | held |
| 5 | `failsafe` | mode HA, and the watchdog expired or HA is blind (§4.5) | §4.4 | `failsafe_setpoint` |
| 6 | `ha` | mode HA, fresh | HA's | HA's (held until HA sends a setpoint) |

Season-off outranks everything, including HA and failsafe: it is the person's "off". The ladder
is written as an ordered ladder because rows overlap (a boost with the season off; a blind HA with
a fresh watchdog), and every boundary between adjacent rows has a test.

### 4.3 Transitions and the watchdog

**Entry into HA ownership** clears any HA command held in RAM and passes through `ha_waiting`:
boot in HA mode, `LOCAL → HA`, season off→on while in HA mode. No value HA sent before the entry
comes back after it. `ha_waiting` ends on the first accepted HA CH command, or becomes `failsafe`
when the watchdog expires.

**Leaving `failsafe`** requires an accepted HA `ch_enable` command (a DHW toggle is a command but
not a decision about heat). Note the exact code behaviour: an accepted HA CH command clears the
failsafe latch **and** is itself heard in the same call, so the next step resolves **straight to
`ha`** — it does **not** pass back through `ha_waiting`.

**The watchdog is fed only by accepted HA commands to `ch_enable` or `ch_setpoint`** — nothing
else: not DHW, not season, not retained messages, not HA's birth message, and there is no
dedicated heartbeat (a heartbeat proves HA is alive, not that its controller is producing
outputs). The deadline is `watchdog_s` (default 900 s). The overdue counter is a **saturating
accumulator** (never `now − then`), advanced only while HA mode is active. It is mirrored into
`RTC_NOINIT` memory with a magic word (`"OTC2"`) and a check word, so a fast reboot loop (a panic,
an OTA rollback) does not reset it and hold the boiler in `ha_waiting` forever. A genuine power-on
starts it from zero; a brownout with an intact check word is treated as a soft reset.

### 4.4 The bounded failsafe

When HA is gone (watchdog expired) or blind (§4.5), the executor enters `failsafe` and may raise
the CH bit itself. This deliberately reverses a naive "a failsafe changes the setpoint, never
raises CH", because `heating_season` is the person's off and HA's CH=0 is a duty-cycle decision
of a controller that has since died. Two bounds keep it from heating a house in summer:

1. **`heating_season` must be true** — guaranteed by row 1 of the ladder.
2. **HA must have asked for CH within `failsafe_heat_days`** — a persisted count of **powered
   hours** since HA last requested heat (`failsafe_heat_days × 24` hours). The count resets to 0
   when an accepted HA `ch_enable=1` arrives. Powered-off time does not count; the part-hour
   survives a soft reset in `RTC_NOINIT` so a fast reboot loop cannot stall it. Past the limit the
   failsafe disarms (reason `fs_disarmed`, CH down).

Within those bounds:

- If a **room source is fresh** (§8): hysteresis around `failsafe_room_target`, **±0.3 K** (±3
  deci-°C, both edges inclusive), with `failsafe_min_cycle_s` as both the minimum on-time and the
  minimum off-time, measured on the bit actually sent. Inside the dead-band the last verdict holds.
- If **no room source is fresh**: CH on at `failsafe_setpoint`, blind (reason `fs_blind`), applied
  before the hysteresis and without the minimum-cycle hold — a freeze costs the house more than a
  week of gas.

> A fresh `room`-role source (§8) — the MQTT room source, when the owner sets
> `room_mqtt_enable=true` and `room_mqtt_role=1`, or `room` for the shield DS18B20 — flips this to the
> hysteresis branch. By default this is dormant: `room_mqtt_enable` defaults `false` and
> the shield DS18B20 defaults `ambient` (never steers), so on a fresh flash — and until the owner
> opts in — the failsafe is always the blind branch: CH on at
> `failsafe_setpoint` while both bounds hold.

The `cause` reported in `failsafe` is recomputed every step, never latched with the
state: `watchdog` if the watchdog is currently expired (reported even if HA is also blind), else
`ha_blind` if a marked room source is currently dead, else `none`. `none` is a valid cause **while
still in `failsafe`**: the state latches (§4.3) until an accepted HA `ch_enable`, so a failsafe held
only by that latch — the acute cause has cleared, e.g. HA feeds the watchdog with a `ch_setpoint`
without re-sending `ch_enable` — reads `failsafe` with `cause = none`, not a stale `watchdog`/`ha_blind`.

**Visibility.** The failsafe fires exactly when HA and perhaps the broker are gone, so publishing
state reaches nobody at that moment. The firmware keeps `failsafe_count` (entries since boot) and
`last_failsafe_duration_s`, published retained on every reconnect, and logs every transition.
(A `last_failsafe_at` timestamp is **not emitted**.)

### 4.5 Hot water and ID 56

This boiler is a combi with no storage tank, so the DHW enable bit most likely means "hot water
at the tap". `dhw_enable` (default true) is persisted, written by the current owner, and is **not**
fed to the watchdog and **not** touched by the season, `ha_waiting`, or the failsafe.

The DHW setpoint (ID 56) is the one value with a possibly non-volatile boiler-side memory, so it
is reconciled against an ID 56 **read-ack** readback (only a READ-ACK counts, never the WRITE-ACK
echo of our own write):

- Written when the readback differs from the target, re-tried at most **once per 60 s** and at
  most **3 times** per unconfirmed target — bounding flash wear on a boiler that stores it.
- A new target (or a fresh accepted command) resets the tries and writes at once.
- With no valid readback, the target is written once per accepted command and then left alone.
- With no valid readback, a persisted DHW setpoint is also re-sent **once at boot**, so
  it survives a reboot on a boiler that never answers the readback (there is nothing to reconcile
  against there). It is a one-shot armed in `ot_control_init()`, exactly as an accepted command
  arms it; on a readback boiler the readback then reconciles (or, if it already agrees, consumes
  the arm with no write), bounded by the 3-try cap either way.

ID 56 is always sent after ID 1. DHW reconciliation is inactive while `dhw_setpoint` is unset
(stored as 0).

### 4.6 The single refusal point

**One entry decides every refusal:** `ot_command_check(key, value, origin, ctx, out)`, wrapping the
pure `ot_control_check()`. Ownership is decided here, not on the HTTP surface, and ownership is
checked **before** value bounds. Origins are `WEB` (0) and `HA` (1); anything not HA is judged as
web. Refusal codes and their HTTP mapping:

| Code | Meaning | HTTP |
| --- | --- | --- |
| `OWNED_BY_HA` | a web write while mode is HA | **409** |
| `OWNED_BY_LOCAL` | an HA write while mode is LOCAL | **409** |
| `SEASON_ON_IS_LOCAL` | HA tried to turn the season on | **409** |
| `OUT_OF_RANGE` (and other value errors) | value outside bounds / NaN | **422** |

Because ownership is asked first, `ch_setpoint = 99` is a 409 in HA mode but a 422 in LOCAL mode.
`ch_setpoint` outside `[flow_min, flow_max]` is **refused, not clamped** — HA cannot send it,
because discovery publishes the runtime bounds and HA's number service rejects out-of-range values.
`heating_season` is exempt from ownership (the web may write it 0 or 1 in either mode; HA may only
write it 0, never 1).

`ot_control_apply()` re-checks ownership under the caller's spinlock and stamps the watchdog in the
same critical section, against the mode the executor last observed — because a check-then-apply on
the HTTP or MQTT task can straddle a mode flip. The command layer's answer is the early one; the
executor's is final.

**Boost** is row 2 of the ladder, LOCAL only. In HA mode a boost request is a 409 like any other
web write; with the season off it is refused with a reason.

---

## 5. The bus

`ot_bus` owns the conversation with the boiler and never stops it (§3.4). The scheduler steps
about every 950 ms; every other step is the mandatory ID 0, so the ID 0 fills every meaningful
slot even when the poll ring is empty.

- **The held ID 1 is re-asserted every 10 s** (`OT_CONTROL_RESEND_MS = 10000`), on every change,
  and in the tick before the CH bit rises. Until first confirmed it is offered every step. It is
  encoded **directly with the f8.8 codec**, not through `ot_command_encode()` — because the command
  layer would refuse an ID the state model had marked unsupported, which (via the §4.1 invariant)
  would permanently prevent CH from rising. An UNKNOWN-DATAID for ID 1 is logged once, not latched.
- **The executor re-asserts through the non-evicting writer `ot_bus_write_if_idle`, never
  `ot_bus_write`.** The bus holds exactly one pending write; a hand write must not be evicted by a
  routine re-send, so when a write is pending the re-send is deferred one tick.
- **Write generation / re-arm** (`ot_bus_track`, under the bus lock): a `gen` counter bumps on
  every queued write; a write queued while an exchange is already on the wire is re-armed (not
  lost) by comparing `gen` against the value captured when the step was built. `id1_seq` and
  `id1_raw` record the count and value of answered ID 1 writes (any parsed reply counts, including
  DATA-INVALID / UNKNOWN-DATAID), which the §4.1 confirmation reads.
- **The single writer of the ID 0 high byte is `ot_thermostat`.** The high byte is composed only
  in the executor step (the CH-enable and DHW-enable bits) and pushed to the scheduler by the one
  task; the scheduler emits it on every ID 0 READ-DATA.

**Identity writes at bus start** (once, not periodic): ID 2 = `0x0000` (Master configuration /
MemberID both zero — the device has no master flags and is **not** a member of the OpenTherm
association, so it writes zero, **not** the boiler's MemberID back); ID 124 = the master protocol
version (f8.8 of 2.2); ID 126 = `0x0101` the master product version.

IDs the boiler answers UNKNOWN-DATAID (14, 57 on this boiler) are never written and never
published to HA. IDs 16 and 24 are not exposed to HA (a room value sent once and never refreshed
is worse than none).

---

## 6. Entities and the registry

**One list of entities, ever.** REST, MQTT topics, Home Assistant discovery, and the frontend
TypeScript types are all generated from a single file, `tools/opentherm_ids.py`. The generated
outputs — `components/ot_registry/include/registry_generated.h`, `web/src/api/entities.ts`, and
the HA discovery header — are outputs and **must not be hand-edited**; fix the generator, not the
file.

The registry describes the **specification**, not a specific boiler. What a boiler actually
supports is discovered at runtime: two UNKNOWN-DATAID answers mark an entity unsupported and drop
it from the poll ring until reboot. Availability is three-valued: `ok` (READ-ACK), `invalid`
(DATA-INVALID — "supported, no data now"), and `unsupported` (UNKNOWN-DATAID).

### 6.1 Entity kinds and fields

Three kinds are defined in `opentherm_ids.py`:

- **`Value`** — a numeric entity over one DATA-VALUE field: `data_id`, `key`, `name`, `codec`,
  `unit`, `device_class`, `state_class`, `icon`, `entity_category`, `readable`, `writable`,
  `write_id`, `min_value`, `max_value`, `default`, `bounds_from` (a Data-ID whose response replaces
  the min/max at runtime), and `control` (the executor command a write maps to, or none for a plain
  frame write).
- **`Flag`** — one bit of a flag byte: `data_id`, `key`, `name`, `high_byte`, `bit`,
  `device_class`, `icon`, `entity_category`, `readable`. Flags are read-only.
- **`Virtual`** — a synthetic entity with no Data-ID (`data_id = −1`, so it never enters the poll
  ring): `key`, `name`, `kind` (`switch` / `sensor` / `binary` / `enum`), `writable`, `control`,
  `options` (enum only), and the usual metadata.

There are roughly 68 entities: ~25 flags, ~34 values, 9 virtuals, plus a schema-version
pseudo-entity. **Entity display `name` strings are Russian** — one of only two places Russian is
kept in the whole codebase, because renaming them would rename the owner's Home Assistant entities
and break every automation referencing them.

### 6.2 The synthetic (executor) entities

The ID 0 high byte itself is deliberately **not** an entity — it is the firmware's own decision,
held once in `ot_control`. Its *inputs* are entities instead:

| Key | Kind | Writable | Control mapping / note |
| --- | --- | --- | --- |
| `ch_enable` | switch | yes | `CH_ENABLE`; feeds the watchdog; gated on owner = HA |
| `dhw_enable` | switch | yes | `DHW_ENABLE`; persisted; not watchdog-fed; gated |
| `heating_season` | switch | yes | `SEASON`; HA may set only 0 |
| `control_mode` | enum (`local`, `ha`) | no | — |
| `control_state` | enum (`season_off`, `boost`, `local`, `ha_waiting`, `failsafe`, `ha`) | no | reason and cause as attributes |
| `ch_setpoint_effective` | sensor (°C) | no | the ID 1 value actually on the wire |
| `ch_enable_effective` | binary | no | the CH bit actually sent |
| `failsafe_count` | sensor | no | entries since boot; retained |
| `last_failsafe_duration_s` | sensor (s) | no | retained |

The CH flow setpoint is the **real** ID 1 `Value` entity (`ch_setpoint`, f8.8, `control =
CH_SETPOINT`), not a separate synthetic. The DHW setpoint is the ID 56 `Value` entity
(`dhw_setpoint`, `bounds_from = 48`, `control = DHW_SETPOINT`).

`room_temperature_effective` (sensor, °C) and `room_source` (enum: `none`/`shield`/`mqtt`) are
emitted (§8): the display selection over `ot_room`'s slots. A
`last_failsafe_at` timestamp is **not emitted**.

There is no `climate` entity (the firmware has no room temperature of its own to be a thermostat
with) and no `water_heater`. There is deliberately no per-second "command age" entity — the
`control_state` already distinguishes `ha`, `ha_waiting`, and `failsafe`.

### 6.3 Two standing rules

- **The web interface has no privileged handle.** If a screen needs a path of its own, the API is
  wrong. The `/ws` push carries exactly what `GET /api/state` carries and not one field more.
- **Secrets are write-only.** A stored password (UI, MQTT) is returned by no API projection; a read
  hands back a sentinel. This is what makes one universal binary possible.

---

## 7. MQTT and Home Assistant

MQTT and Home Assistant discovery are handled by the pure `ot_mqtt` and `ot_ha`
components, the impure `ot_mqtt_link` task that owns the esp-mqtt client, and the generator
`tools/render_discovery.py`. MQTT stays without TLS: every value is checked by the firmware, and
the mode, turning the season on, the bounds, and the watchdog are not reachable over MQTT at all.

### 7.1 Topics and QoS

The prefix defaults to `opentherm/<device_id>` (e.g. `opentherm/aabbccddeeff`); the broker port
defaults to 1883.

| Topic | Payload | Retain / QoS |
| --- | --- | --- |
| `<prefix>/status` | `online` / `offline` | retained, QoS 1 — this is the LWT (`offline`) |
| `<prefix>/control/owner` | `online` / `offline` | retained, QoS 1 — `online` iff mode is HA |
| `<prefix>/<key>/state` | bare scalar | retained, QoS 0 |
| `<prefix>/control_state/attributes` | `{"reason","cause"}` | retained, QoS 0 |
| `<prefix>/<key>/set` | a number | subscribed as `<prefix>/+/set`, QoS 1 |
| `homeassistant/status` | `online` | subscribed, QoS 1 |

The LWT is `offline` on `<prefix>/status`, retained, keepalive 60 s; on a clean disconnect the
firmware publishes `offline` itself (a broker does not send the will for a clean DISCONNECT).
Auto-reconnect (10 s) is **not** disabled — nothing reboots because the broker is absent.

### 7.2 Commands

- **A retained command is ignored.** The broker sets RETAIN only when replaying to a new
  subscription, so a retained command is by definition old; accepted, it would feed the watchdog on
  every reconnect and make a dead HA look alive. Discovery never sets retain on a command topic.
- **In LOCAL mode HA's control entities are unavailable.** Gated entities (the switches, the CH/DHW
  numbers, the season control) carry a second availability topic `<prefix>/control/owner` with
  `availability_mode: all`, which is `online` only in HA mode. HA's service layer then skips those
  entities, so the command is never even published — better than refusing and re-publishing, which
  fails in HA's UI (a switch snaps back; a number whose echo equals its state never redraws). The
  read-only `*_effective` mirrors are available in every mode.
- Every inbound command goes through `ot_command_check` with `origin = HA`, like every other writer.

### 7.3 Discovery

- Generated from the single entity list into the HA discovery header, filled with runtime tokens
  (prefix, device id, device block, min, max). Discovery prefix `homeassistant`.
- **`ch_setpoint`'s min/max come from the runtime `flow_min`/`flow_max`** (step 0.5), and discovery
  is re-published when the band changes. A static 10–90 would let HA send a value the firmware
  refuses. The DHW setpoint's bounds come from the boiler's ID 48 (step 1).
- On `homeassistant/status = online`, discovery is re-published (HA's birth). This feeds **nothing**
  to the watchdog. HA's `offline` LWT is deliberately **not** used to hasten the failsafe (it also
  fires on a clean shutdown).
- Entities the boiler does not support are dropped cleanly (an empty retained payload) after two
  UNKNOWN-DATAID answers; control entities are never dropped.

---

## 8. Room-temperature sources (`ot_room`)

`ot_room` (`components/ot_room/`) is wired into
`ot_thermostat`. It has two sources: the shield's own DS18B20 (`ambient`, always present) and
an optional MQTT room reading (`room` by default, opt-in). The executor's room inputs
(`room_fresh`, `room_dc`, `ha_forwarded_stale`) are not hard-wired to zero — they reflect
`ot_room`'s steering selection every tick. With the MQTT source at its default `room_mqtt_enable =
false`, the inputs are still effectively all-zero and the failsafe stays in its blind branch
(§4.4); turning the MQTT source on and role `room` is what makes the hysteresis branch reachable.
The owner-facing topic contract is `docs/ha-room-source.md`.

- **`ot_sensor` is kept** as the per-source foundation: an outlier filter with a three-refusals
  escape hatch, a `FRESH` / `STALE` / `NEVER` state, and a saturating overdue accumulator (never
  `now − last_ok`).
- **`ot_room`** is a pure component (host-tested like `ot_control`/`ot_sensor`) with
  `OT_ROOM_MAX_SLOTS = 4` fixed slots, each wrapping its own `ot_sensor_t` plus role and
  `ha_forwarded` metadata, priority = slot index. Because readings arrive from other tasks (the
  DS18B20 reader, the MQTT link task) and `ot_room` is single-owner, they land first in a
  spinlock-guarded mailbox in `ot_thermostat_room.c`, drained into the pure `ot_room_submit()` on
  the owner task's own tick. **Two sources are wired today:**
  - **Slot 0 — the shield's DS18B20**, read over RMT (`ot_onewire`), role **`ambient`**, always
    present, never steers.
  - **Slot 1 — an MQTT room source.** Home Assistant publishes a bare float, non-retained, to
    `<topic_prefix>/room/state`; a retained reading is refused (§7.2's retained-command rule has a
    stronger analogue here — a retained value would arrive looking fresh while being arbitrarily
    old, and this value can steer the boiler). `ot_mqtt_decide()` recognises the topic as
    `OT_MQTT_ROOM` and dispatches it **straight into the `ot_room` mailbox — never through
    `ot_command_check`, an origin, or the HA watchdog** (a room reading is a measurement, not an
    owned command). The slot exists only when `room_mqtt_enable = true`.
  - **Not yet built:** WiFi-sensor push/pull (Shelly/Tasmota/ESPHome), BLE heard directly
    (Xiaomi/BTHome/ATC), and 1-Wire as a general room-source adapter beyond the shield. Multiple
    MQTT room slots, a per-source calibration offset, and flap hold-time between two or more
    `room`-role sources in a fallback chain are also not built.
- **Two selections, not one value at a time naively** — `ot_room_select_steer()` (room-role only,
  the safety-critical input to the failsafe) and `ot_room_select_display()` (any role, prefers a
  fresh `room` slot and falls back to a fresh `ambient` slot, feeding
  `room_temperature_effective`/`room_source`, §6.2). The highest-priority fresh slot within each
  selection's role filter wins; when it goes stale the next takes over.
- **Every source carries a role, `room` or `ambient`,** and only `room` sources steer the failsafe
  and the blind-HA check — enforced structurally: `ot_room_select_steer()` filters out non-`room`
  slots *before* the pick. The shield's DS18B20 is `ambient` (compile-time; the device may sit
  by the boiler); the MQTT source defaults to `room_mqtt_role = 1` (room) but is configurable to
  `ambient`. The safer default: a room-placed device left `ambient` merely heats at the safe
  setpoint, whereas a boiler-side device mistaken for `room` could hold heating off in a cold house.
- **`ha_blind` is a real, reachable path (opt-in).** A source marked `ha_forwarded`
  (`room_mqtt_ha_forwarded`, default `false`) is Home Assistant's own forwarded reading; if it goes
  STALE while HA's commands are still fresh, the executor treats HA as blind and enters `failsafe`
  with cause `ha_blind` (§4.4). Default `false` keeps this dormant — the existing HA-command
  watchdog already covers "HA is gone"; `ha_blind` is a second, more aggressive layer the owner
  opts into.

**Known minor (deferred, cosmetic):** a rejected room reading (retained or non-numeric) is logged
as "command refused" and counted in the shared `mqtt.rejected` tally, sharing the command-rejection
counter rather than a room-specific one — no effect on control, fix only if that counter is used
diagnostically.

The whole MQTT path, including the room source, is covered by the native suites
(`test_ot_room`, `test_ot_mqtt`), and both boards build warning-free.

---

## 9. Failure handling

Two rules stand above the matrix: **nothing reboots because a peer is absent**, and **`ot_bus`
never stops** (§3.4).

| What failed | What the boiler gets | What is visible |
| --- | --- | --- |
| HA dead, broker up | after `watchdog_s`: failsafe (§4.4) | `control_state = failsafe`; retained counters on reconnect |
| Broker dead | same, on the same deadline | the web banner; the log |
| WiFi dead | same; an access-point captive portal after its timeout | the web banner via the AP |
| HA alive, its forwarded sensor dead | failsafe via `ha_blind` (§8) — reachable, opt-in (`room_mqtt_ha_forwarded=true`) | `control_state = failsafe`, cause `ha_blind` |
| HA alive, automation wrong | whatever HA asks, within `[flow_min, flow_max]` | nothing — outside the firmware's knowledge |
| A room source dead | the next fresh source by priority; none fresh → blind failsafe | `room_source`, `room_temperature_effective` |
| Boiler silent | polling continues, `NO_BOILER` | the boiler-link entity |
| SNTP dead | nothing changes; no decision depends on the wall clock | — |
| Reboot | a brief demand for heat (§3.4), then `ha_waiting` in HA mode until HA speaks | the reset reason in the log |
| Reboot loop | the `RTC_NOINIT` accumulators carry on counting (the watchdog overdue time and the failsafe heat-hours part-hour), so the failsafe still fires on schedule and the summer bound still disarms | the reset reason in the log |

The `RTC_NOINIT` accumulators are the mechanism that makes a fast reboot loop safe: without them a
loop faster than `watchdog_s` would hold `ha_waiting` (CH off) forever, and a loop shorter than an
hour would never complete a powered hour so the failsafe's summer bound would never disarm. A
power-on reset starts them from zero (a genuine cold start is not a loop); a brownout with an intact
check word is treated as a soft reset.

---

## 10. Configuration

Everything is configured at runtime and persisted in NVS. Temperatures are stored in
**tenths of a degree** (the `_dc` suffix = deci-°C; value ÷ 10 = °C); there is no floating-point
config type. A fresh flash sets every unlisted field to 0 / false / empty. Defaults verified
against `ot_config_defaults.c`:

**Executor / control**

| Field | Default | Meaning |
| --- | --- | --- |
| `control_mode` | LOCAL | LOCAL or HA; HA refused while `mqtt_host` is empty |
| `heating_season` | **false** | a fresh flash never asks for heat; HA may set it false, never true |
| `watchdog_s` | 900 | seconds since the last accepted HA CH command before failsafe |
| `failsafe_setpoint_dc` | 450 (45.0 °C) | validated `flow_min ≤ failsafe_setpoint ≤ flow_max` |
| `failsafe_room_target_dc` | 180 (18.0 °C) | used only while a room source is fresh |
| `failsafe_heat_days` | 3 | the summer bound (powered days since HA last asked for heat) |
| `failsafe_min_cycle_s` | 600 | minimum on- and off-time of the failsafe hysteresis |
| `flow_min_dc`, `flow_max_dc` | 400, 700 (40.0, 70.0 °C) | the bounds of every CH setpoint |
| `local_ch_enable` | false | LOCAL mode's CH switch |
| `local_ch_setpoint_dc` | 450 (45.0 °C) | LOCAL mode's flow setpoint |
| `dhw_enable` | **true** | persisted in every mode; the only non-zero ID 0 bit on a fresh flash |
| `dhw_setpoint_dc` | 0 (unset) | no ID 56 goes out until someone writes one |

The `local_*` and `dhw_*` fields are read-only in `/api/config` and written through the entity path,
so each value has one validator. Narrowing the flow band moves a stored LOCAL setpoint to the
nearest 0.5 K edge and reports the repair. `failsafe_setpoint` at or above the boiler's own minimum
(panel parameter `E`, Appendix A) is the owner's duty — it is not readable over OpenTherm.

**Networking / MQTT / identity / time**

| Field | Default |
| --- | --- |
| `mqtt_host`, `mqtt_user`, `mqtt_password` | empty (`mqtt_password` is write-only: sentinel on read) |
| `mqtt_port` | 1883 |
| `topic_prefix` | `opentherm/<device_id>` (fallback `opentherm`) |
| `ha_discovery` | true |
| `wifi.ssid`, `wifi.psk` | empty (device runs its own AP with a captive portal) |
| `device_name` | `Термостат OpenTherm <last-4-hex>` (Russian — the second and last place Russian is kept; renaming it would rename the owner's HA device) |
| `tz` | `MSK-3` (POSIX TZ, UTC+3) |
| `ntp_server` | `pool.ntp.org` |
| `ui_pw_hash` | empty (no password → the device is open on a trusted LAN) |

The config schema is version 2. Retired keys from the deleted PI/manual era are erased on migration
and forbidden from reuse by a test.

---

## 11. Building and testing

The protocol, state, the executor core, the command layer, JSON, and the room-source registry
(`ot_room`) depend on neither hardware nor a framework: they are host-tested, and the test is
written **before** the implementation. The components are ESP-IDF components and PlatformIO
libraries at once, so host tests compile exactly the sources the device runs.

Commands (`pio` may live at `~/.platformio/penv/bin/pio`):

```sh
pio run -e lolin_c3_mini            # primary board; the SPA build is part of it
pio run -e supermini_c6             # second target, ESP32-C6 SuperMini
pio test -e native                  # host suites — the barrier for any protocol change
pio test -e native -f test_ot_decode  # a single suite
python3 -m pytest tools/tests -q    # tests of the registry generator
cd web && npm run build             # the real web gate (not `tsc --noEmit`)
```

Notes that carry load:

- **Judge a build by its exit code, not the tail of its output.** `pio run | tail` returns the exit
  code of `tail`; a failed build then looks successful. Write to a file and check `$?`.
- One directory is one host suite (PlatformIO links all `.cpp` in a directory into one binary, so
  two `main()`s collide).
- `[env:native]` builds with `-Werror=switch`: an enumerator appended without its `case` fails the
  host build. Do not drop the flag to silence a failure; name the new code.
- Fakes for ESP-IDF are per-suite and must **not** be shared, so one suite cannot quietly weaken
  another.
- Glue that no host suite compiles (in `ot_bus` and `ot_thermostat`) is pinned by source guards in
  `tools/tests/test_source_guards.py`.
- **Hand-written source files are capped at 350 lines** (600 per Unity suite); generated files do
  not count. A component is a directory; cuts follow responsibility seams with the public header
  unchanged.
- Everything the owner must verify on real hardware lives in a hardware checklist; a successful
  build is not readiness.

---

## 12. Acceptance criteria

1. `pio test -e native` green (output attached, not asserted); both boards build; `npm run build`
   green; the generator's pytest green.
2. A build for an unknown target fails with a clear message from `components/board`, not a link
   failure.
3. No `framework = arduino` in any environment.
4. Not a single GPIO number outside `components/board/`.
5. No second hand-written entity list; the registry header and `entities.ts` are generated.
6. No API projection returns a stored password.
7. The web interface has no privileged handle.
8. No code path reboots because a peer is unreachable.
9. `ot_bus` has no path on which the conversation with the boiler stops for good.
10. Every boundary between adjacent rows of the state ladder (§4.2) has a host test, and the held-
    setpoint-before-CH invariant (§4.1) is a named test.
11. Home Assistant discovers every entity of §6.2 without hand-written YAML; in LOCAL mode HA's
    control entities are unavailable.
12. The hardware checklist has been passed on a real boiler, including: HA stopped → failsafe after
    `watchdog_s`; the summer scenario (season on, no heat request for `failsafe_heat_days`) → no
    heat; a reboot loop → failsafe on schedule; broker and WiFi each killed for an hour → heating
    continues with zero reboots; ten resets with the boiler running.

---

## 13. Not in scope / delegated to Home Assistant

These were deliberately removed or never built. They are listed so a reader knows the omission is
intentional, not an oversight:

- **The PI/PID control law** (formerly `ot_climate`) — deleted. The control law lives in Home
  Assistant.
- **The weekly schedule and modes** (formerly `ot_schedule`) — never built; schedules live in HA.
- **Anti-cycling logic in the firmware** — removed; the boiler's own anti-cycling (panel parameter
  `P`) and HA's control handle it.
- **Room temperature as a firmware control input** — the firmware has no control loop outside the
  failsafe; the room-source registry (§8) is for the failsafe's eyes and diagnostics only.
- **Room temperature only from MQTT** — the room-source design (§8) takes
  multiple transports, though today only MQTT and the shield's own DS18B20 are wired — WiFi
  push/pull and BLE sources remain future work (§8).
- **A weather-compensation curve, a second heating circuit (CH2), the solar/ventilation Data-ID
  blocks, TLS for MQTT, and OTGW mode** — out of scope for v1.

Some pieces are **built but incomplete or planned**: within the room-source registry (§8), WiFi
push/pull sources, BLE sources, multiple MQTT room slots, a per-source calibration offset, and
flap hold-time between two or more `room`-role sources are not built; `last_failsafe_at` (§6.2) is
not emitted; an HA configuration package is future work. The four `room_mqtt_*` fields are settable
both via `POST /api/config` and a **Settings card** in the SPA
(`web/src/pages/settings/sections/RoomMqttSection.tsx`).

---

## Appendix A: Boiler reference — Intergas Kombi Kompakt HRE

The owner's boiler. A full Data-ID sweep (0–127) found all 128 IDs answered:
**24 supported, 104 UNKNOWN-DATAID.** Boiler identity: ID 3 = `0x41AD` → DHW present, MemberID 173.
A fault-code table should be tied to MemberID 173, not applied to every boiler.

### A.1 Supported Data-IDs (24)

| ID | Name | Answer | Measured |
| --- | --- | --- | --- |
| 0 | Status | read-ack | — |
| 1 | TSet (flow setpoint) | write-ack | writable |
| 2 | Master config / MemberID | write-ack | firmware writes `0x0000` |
| 3 | Slave config / MemberID | read-ack | `0x41AD` (DHW present, MemberID 173) |
| 5 | Fault flags / OEM code | data-invalid | supported; no fault present |
| 6 | Remote-parameter RW flags | read-ack | `0x0101` (transfer/write for DHW setpoint only) |
| 9 | Remote override room setpoint | read-ack | — |
| 16 | TrSet (room setpoint) | **write-ack** | accepts writes |
| 17 | Relative modulation level | read-ack | — |
| 18 | CH water pressure | read-ack | 1.49 bar |
| 19 | DHW flow rate | read-ack | — |
| 24 | Tr (room temperature) | **write-ack** | accepts writes |
| 25 | Tboiler (flow) | read-ack | 43.0 °C |
| 26 | Tdhw (DHW temperature) | read-ack | 34.4 °C |
| 27 | Toutside (outside) | **data-invalid** | no outdoor sensor connected |
| 48 | TdhwSet bounds | read-ack | `0x4128` → DHW range 40–65 °C |
| 56 | TdhwSet (DHW setpoint) | read-ack | 60.0 °C |
| 100 | Remote override function | read-ack | — |
| 113 | Unsuccessful burner starts | read-ack | — |
| 114 | Flame losses | read-ack | — |
| 116 | Burner starts | read-ack | — |
| 120 | Burner operation hours | read-ack | — |
| 123 | Burner operation hours (DHW) | read-ack | — |
| 126 | Master product version | write-ack | firmware writes `0x0101` |

Notable for the firmware:

- **ID 14 (max relative modulation) — not supported.** No modulation-limit ceiling exists.
- **ID 57 (MaxTSet, max CH setpoint) — not supported.** The CH flow ceiling exists **only in the
  boiler's panel config** (parameter `5.` / the user-menu max), invisible and silently clamping over
  the bus.
- **ID 27 (outside) — data-invalid.** No outdoor sensor; weather compensation needs an external
  source. **ID 28 (return temp) — not supported.**

### A.2 Fault codes (ID 5 low byte / panel display)

The UI shows both the number and the decoded text; the low-byte-of-ID-5 code is assumed to match
the panel display code but is only confirmable during a real fault. All are **locking faults**,
cleared with the panel **Reset** key once the cause is dealt with; the last locking fault is
readable and clearable at the panel.

| Code | Meaning |
| --- | --- |
| 0 | Sensor failure during self-test |
| 1 | Temperature too high (air in system, pump not turning, low flow) |
| 2 | S1 and S2 sensors swapped |
| 4 | No flame signal (gas closed, low pressure, condensate/ignition/earthing) |
| 5 | Poor flame signal |
| 6 | Flame detection error |
| 8 | Incorrect fan speed |
| 10–14 | Sensor S1 (flow) failure |
| 20–24 | Sensor S2 (return) failure |
| 27 | Outdoor-sensor short circuit |
| 29, 30 | Gas-valve relay faulty |

### A.3 Panel parameters that bound what the firmware can ask

Over OpenTherm the firmware can write ID 1 (flow setpoint) and ID 56 (DHW setpoint). The effective
ceiling and floor are **panel-only, invisible over the bus, and clamp silently** — a room that
will not warm up should be checked at the panel before the executor is debugged.

| Parameter | Meaning | Range / factory |
| --- | --- | --- |
| Max CH flow temperature (user menu) | silent ceiling over ID 1 | 30–90 °C / 80 °C |
| DHW temperature (user menu) | overridden by ID 56 | 40–65 °C / 60 °C |
| **`E`** | **minimum flow temperature with an OT thermostat — not readable over OpenTherm** | 10–60 °C / **40 °C** |
| `E.` | reaction to an OT demand below `E`: `0` ignore, `1` (factory) clamp up to `E`, `2` answer at maximum | — / 1 |
| `5.` | max flow temperature settable from the panel (the ceiling ID 1 is clamped to) | 30–90 °C / 90 °C |
| `5` | minimum flow temperature of the heat curve | 10 °C…`5.` / 25 °C |
| `P` | anti-cycling time during CH | 0–15 min / 5 min |
| `3`, `3.`, `4` | max CH power, max modulating-pump capacity, max DHW power | per model |
| `o.` | eco days; `0` switches tapcomfort over OpenTherm | 0–10 / 3 |

Key consequences: with `E.` = 1 (factory), an ID 1 below 40 °C is served at 40 °C, not obeyed —
so `flow_min` should be set at or above `E`. The DHW web range 40–65 °C comes directly from ID 48
(`0x4128`) and matches the panel. The DHW bit in the ID 0 high byte does not turn the tap off; it
switches tapcomfort, and only when the panel is in Eco with `o.` = 0.
