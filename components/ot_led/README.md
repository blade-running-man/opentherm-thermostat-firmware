# ot_led — the status LED's decision (pure)

Decides what the on-board WS2812B RGB LED should show: colour = health of the control chain
(WiFi → MQTT → Home Assistant), motion = boiler activity. Two independent axes, decided by two
independent pure functions over the same sampled "world" — `ot_led_health()` picks the tier,
`ot_led_render()` picks the tier's colour *and* its motion at one instant.

It is **pure** — no ESP-IDF header, no timer, no task, no pin. Time enters only as
`ot_led_world_t.now_ms`, a monotonic millisecond argument, exactly like `ot_control`'s `step()`.
That is what lets the health ladder and the animation curves (breathing, the heartbeat, every
blink) be host-tested by sampling `now_ms` at chosen instants instead of waiting on a real clock.
The impure half — sampling `ot_net`, `ot_mqtt_link`, `ot_control` and `ot_state`, ticking the
animation, and writing the strip via `led_strip` (RMT) — is the sibling component `ot_led_task`
(a later task). `ot_led` is a **reader** of the rest of the firmware in spirit: it never touches
the OpenTherm bus, and it cannot, because it never runs on a task at all.

## Responsibility

`ot_led` owns, as two pure decisions:

- **The health ladder** (`ot_led_health()`) — six tiers plus OTA, evaluated first-match-wins; the
  enum order *is* the priority, exactly like `ot_control`'s ladder.
- **The render** (`ot_led_render()`) — the tier's hue at full brightness, scaled to a caller-given
  ceiling, combined with the tier's motion curve sampled at `now_ms`.
- **The animation curves** — a hard on/off blink (two periods: fast for OTA, slow for the alarm
  and setup tiers), a smooth breathing curve, and a dim "heartbeat" pulse — as deterministic pure
  functions of `now_ms` alone.

It does **not**: read a clock, a pin, WiFi/MQTT/HA state, or the OpenTherm bus itself; hold any
instance, task or lock; decide *when* to re-render (the glue ticks it, at whatever cadence it
chooses — every call at the same `now_ms` returns the same frame).

## Public API

Header: `include/ot_led.h`. Every function takes `const ot_led_world_t *w`; the caller (the glue)
owns the struct and fills it fresh each time it samples the rest of the firmware.

| Function | Contract |
| --- | --- |
| `ot_led_health(w)` | The first-match ladder below. Cannot fail; every input, including a nonsensical one (e.g. `net == CONNECTED` with `has_credentials == false`), yields one defined tier. |
| `ot_led_render(w, ceiling)` | The full frame: `ot_led_health(w)`'s tier, its hue and its motion, sampled at `w->now_ms` and scaled to `ceiling` (0–255, the board's `rgb.brightness`). `ceiling == 0` yields `{0,0,0}`. |
| `ot_led_health_name(h)` | Wire spelling for logs: `"ota"`, `"setup"`, `"connecting"`, `"wifi_down"`, `"mqtt_down"`, `"failsafe"`, `"normal"`. Out-of-range → `"unknown"`, never `NULL`. |

### Key types

- `ot_led_health_t` — the ladder: `OTA, SETUP, CONNECTING, WIFI_DOWN, MQTT_DOWN, FAILSAFE, NORMAL,
  HEALTH_COUNT`. **Do not reorder** — the numeric order is the priority a switch over it, and every
  switch in this component and its glue is guarded by `-Werror=switch` under `[env:native]`, so an
  appended tier without its `case` fails the host build rather than silently painting "unknown" on
  the device.
- `ot_led_world_t` — the sampled world: `now_ms`, `net` (see below), `net_down_since_ms`,
  `has_credentials`, `mqtt_configured`, `mqtt_connected`, `control` (`ot_control_state_t`),
  `flame`, `ota_active` (dormant in v1, always `false`).
- `ot_led_net_state_t` — **this component's own copy** of `ot_net_state_t`'s three values
  (`NET_DOWN`, `NET_ACCESS_POINT`, `NET_CONNECTED`), not `ot_net_state_t` itself. See "Why not
  `ot_net_state_t`" below — this is a deliberate, verified deviation from an earlier draft of the
  contract, not an oversight.
- `ot_led_rgb_t` — `{ r, g, b }`, already scaled to the ceiling; the glue writes it straight to
  the strip.
- `OT_LED_WIFI_GRACE_MS` (10000) — the only tunable exposed as a macro, so the test and this doc
  cannot drift from the code.

## The health ladder (`ot_led_health()`, first match wins)

1. `ota_active` → **OTA**. Preempts everything, including a WiFi that is itself down — an OTA
   write in progress must never be mistaken for a WiFi alarm.
2. `net == ACCESS_POINT` → **SETUP**. The config portal is open; the owner is expected at the
   device, not at Home Assistant.
3. `net != CONNECTED` (down):
   - `has_credentials && (now_ms - net_down_since_ms) >= OT_LED_WIFI_GRACE_MS` → **WIFI_DOWN**.
   - else → **CONNECTING**. Covers both "no credentials yet" (fresh out of the box — never
     escalates, however long it stays down) and "down, but still inside the grace window" (an
     ordinary reassociation must not flash red).
4. `net == CONNECTED`:
   - `mqtt_configured && !mqtt_connected` → **MQTT_DOWN**. A broker that was never configured
     (a local-only install) is a choice, not a fault, and reads NORMAL instead.
   - else `control == OT_CONTROL_FAILSAFE` → **FAILSAFE**.
   - else → **NORMAL**.

A dead broker and a failsafed executor often have the same root cause (HA cannot be heard through
a dead broker) — row 4 checks MQTT first, so the LED shows the *cause* (amber) rather than the
*symptom* (orange).

## The render: colour + motion per tier

| Tier | Hue at full (`C` = ceiling) | Motion |
| --- | --- | --- |
| `OTA` | blue `(0,0,C)` | fast blink, 150 ms period, hard on/off |
| `SETUP` | blue | slow blink, 2000 ms period (1 s on / 1 s off) |
| `CONNECTING` | blue | breathing, 3000 ms period |
| `WIFI_DOWN` | red `(C,0,0)` | slow blink |
| `MQTT_DOWN` | amber `(C, 0.47·C, 0)` | flame → breathing; idle → heartbeat |
| `FAILSAFE` | orange `(C, 0.20·C, 0)` | slow blink |
| `NORMAL` | green `(0,C,0)` | flame → breathing; idle → heartbeat |

- **Breathing** (`breathe_level`): `(1 - cos(2π·t/period))/2`, mapped onto `[0.10·C, C]` — trough
  (`0.10·C`) at `t = 0`, peak (`C`, exactly) at the half period. A cosine, not a triangle: the
  flame it represents has no sharp edges, and a triangle's corner is exactly where an animation
  reads as synthetic.
- **Heartbeat** (`heartbeat_level`): mostly the floor (`0.12·C`), with one gentle sine-shaped blip
  of width 400 ms at the start of every 4000 ms cycle, peaking at `0.35·C` — "alive and idle",
  deliberately far short of a full pulse, so idle never reads as urgent and is visibly dimmer than
  breathing (the owner can tell "firing" from "idle" without reading the colour).
- **Blink** (`blink_level`): a hard square wave, `C` for the first half of the period, `0` for the
  second.
- **Amber vs. orange** never read the same: separated on the green fraction (0.47 vs. 0.20 of `C`)
  *and* on motion (amber breathes or heartbeats, orange always blinks) — insurance against both a
  colour-vision deficiency and a screenshot in black and white.
- Every channel is rounded exactly once (`chan()`), from `frac · level` where `frac` is the hue's
  share of `C` and `level` is already expressed in the `0..C` domain the curves compute in — so a
  channel that is nominally zero for a tier (e.g. blue for every non-blue tier) stays exactly zero
  regardless of the motion, and `ceiling == 0` collapses every channel to `0`.

## Implementation

Single file, deliberately not split (197 lines, well inside the 350-line ceiling — a genuine split
point would be "the curve maths", per the plan, but there is no third consumer of it yet):

| File | Holds |
| --- | --- |
| `ot_led.c` | `ot_led_health()`, `ot_led_health_name()`, the three curve functions (`blink_level`, `breathe_level`, `heartbeat_level`), the rounding helper `chan()`, and `ot_led_render()`. |

### Why not `ot_net_state_t`

The frozen draft of this contract had `ot_led_world_t.net` typed `ot_net_state_t` and
`ot_led.h` including `"ot_net.h"`. Verified by running the host build, not by reasoning about it
(the same way the `ot_bus` / `ot_bus_sched` split was found, see that component's
`CMakeLists.txt`): `ot_net.h` unconditionally includes `"esp_err.h"`, which does not exist for
`[env:native]`, and even past that, `ot_net` is a genuinely impure component
(`REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash`) with no lean, dependency-free header
of its own. PlatformIO's host build compiles **every** `.c` file of a library the moment a suite
reaches one of its headers, so `test_ot_led` would have pulled `ot_net.c`, `ot_net_prov.c`,
`ot_net_radio.c` and `ot_net_config.c` into the link and failed. `ot_control.h` has no such
problem — it is genuinely pure and already reached by `test_ot_command`'s suite today.

The fix kept inside this component's own scope: `ot_led_net_state_t`, a three-value enum this
header owns outright, numbered the same way as `ot_net_state_t` (`DOWN, ACCESS_POINT, CONNECTED`)
so the glue's conversion reads as an identity map — but the glue must still convert with an
explicit `switch`, never a cast, because nothing enforces that the two enums stay numbered alike
if either grows a member. `CMakeLists.txt` reflects this: `REQUIRES ot_control` only, not `ot_net`.
If a future change makes `ot_net_state_t` genuinely reusable (for example if it moves into its own
pure leaf component, the way `ot_bus_sched` did for `ot_bus`), revisiting this split is reasonable
— but that is a change to `ot_net`, outside this component's scope, and needs its own review.

## Tests

Host suite `test/test_ot_led` (`pio test -e native -f test_ot_led`), built under `[env:native]`
with `-Werror=switch`:

- The ladder: one test per row (including "first broken link wins" — MQTT_DOWN over FAILSAFE — and
  the two grace-window edges, `GRACE-1` vs. `GRACE`).
- The render: colour identity per tier (which channels are non-zero) sampled at the tier's own
  peak/trough or on/off phase, plus a ceiling-scaling check (breathing's peak equals the ceiling
  exactly, so `32/255` scaling is exact) and a `ceiling == 0` all-zero check.

Sample times are picked from `ot_led.c`'s own period constants (documented in the test file's own
comment, not re-exported — a caller of `ot_led_render()` never needs to name them): 150 ms fast
blink, 2000 ms slow blink, 3000 ms breathing, 4000 ms heartbeat with a 400 ms blip. If those
constants change, the samples must move with them.

## Notes

- `ota_active` is dormant in v1 — nothing in the firmware sets it `true` yet (no OTA subsystem
  exists). The ladder and render already handle it correctly; wiring a real OTA path only means
  a future caller starts passing `true` while a write is in flight.
- The LED is not a control-state indicator. `LOCAL`, `BOOST` and `SEASON_OFF` all read NORMAL when
  the links are up — the LED answers "is the chain healthy", not "which mode is active".
- `chan()` rounds once, from `frac · level`, specifically so a colour-identity assertion
  (`channel == 0`) never becomes a rounding flake: a `frac` of exactly `0.0` produces exactly `0`
  regardless of `level`.
