# ot_led_task — the status LED's glue (impure)

Drives the on-board WS2812B RGB LED. The **decisions** — the health ladder (colour) and the
animation curves (motion) — belong entirely to the pure, host-tested sibling `ot_led`. This
component is the impure half: it samples the rest of the firmware once a second, ticks the
animation ~20 times a second, and writes the one pixel over RMT via Espressif's `led_strip`
managed component. It holds no policy of its own.

## Purpose

Turn `ot_led`'s pure frame into light. The **one** place in the firmware where WS2812/RMT and the
source sampling live, so that everything decision-shaped stays in `ot_led` and stays testable.

## Responsibility

- Set up one WS2812 pixel on `board->rgb.gpio` (RMT backend, GRB order), brightness ceiling from
  `board->rgb.brightness`.
- Sample the world at ~1 Hz into an `ot_led_world_t`: net state, the WiFi-down stamp, credentials,
  MQTT configured/connected, the control state, flame, and the OTA flag.
- Render at ~20 Hz (`ot_led_render()`) and push the pixel — smooth breathing/heartbeat between
  samples because the animation clock advances every tick.
- Do a synchronous solid-blue boot flash in `ot_led_task_start()`, before the low-priority task is
  scheduled, so the LED is visibly alive at once.

**READER ONLY.** It never writes an OpenTherm frame, never takes the bus lock, never blocks on the
network — a WiFi/MQTT/HA failure only changes the colour shown. Every source read is a non-blocking
snapshot getter. This is load-bearing: a silent OpenTherm master reads to the boiler as a demand
for heat, so nothing cosmetic may ever stall the bus.

## Public API

Header: `include/ot_led_task.h`.

| Function | Contract |
| --- | --- |
| `ot_led_task_start(board)` | Sets up the strip, does the boot flash, spawns the task. Call only when `board->rgb.gpio >= 0` (the caller guards). Returns the `led_strip` init error, `ESP_ERR_NO_MEM` if the task did not spawn, else `ESP_OK`. |
| `ot_led_note_ota(active)` | Sets/clears the OTA-active flag the task reads. **Dormant in v1** — nothing calls it true (no OTA subsystem yet). A future OTA write path calls it `true` around the write to get the fast-blue "do not power off" pattern; a read-only observation that must not alter OTA behaviour. See `docs/led-status-indicator.md`, "Out of scope for v1". |

## Implementation

Single file, `ot_led_task.c` (well inside the 350-line ceiling).

- **The strip is created once**, in `ot_led_task_start()`, into a file-scope handle the task then
  reuses — the boot flash and the loop share one RMT channel on the one GPIO; a second `led_strip`
  on the same pin would fail.
- **`now_ms()`** is one monotonic base (`esp_timer_get_time()/1000`) used for both the animation
  clock and the WiFi-down stamp, so the grace-window arithmetic is over one clock.
- **`net_down_since_ms` is a transition stamp**, recorded once when net leaves CONNECTED and
  cleared while CONNECTED — never `now - last`, which would keep the grace window from ever
  expiring (the same rake `ot_sensor`'s accumulator avoids).
- **The `ot_net_state_t` → `ot_led_net_state_t` conversion is an explicit `switch`, never a cast**:
  the two enums live in different headers (`ot_led` keeps its own copy so its host suite does not
  drag in `ot_net`'s impure sources) and nothing enforces they stay numbered alike. Unknown fails
  toward DOWN, never CONNECTED.
- **The control state** comes from `ot_thermostat_control_get()` — the only clean, task-safe,
  read-only getter of the current `ot_control_state_t` (its `.state` field). The LED never
  recomputes control or calls `ot_control_step()`/`apply()`. This is why `CMakeLists.txt` requires
  `ot_thermostat` beyond the plan's first sketch (no cycle: `ot_thermostat` does not depend here).
- **`flame`** reads true only when `ot_state_get("flame", …)` succeeds, is `OT_AVAIL_OK`, and is
  boolean-true — a stale/never value reads false, so idle never shows as firing.
- The task runs at `tskIDLE_PRIORITY + 1` with a 4 KiB stack, mirroring the DS18B20 bring-up task.

The `led_strip` managed dependency is pinned to the 3.x major in `idf_component.yml`; the resolved
version is recorded in the repo's `dependencies.lock` and vendored under `managed_components/`.

## Tests

**No host suite** — impure by construction (RMT, tasks, `esp_timer`, the four source getters). The
decisions it carries out are `ot_led`'s and are host-tested there (`test/test_ot_led`). This glue
is pinned by a source guard (a later task) that reads this file and fails if a read a fix depends
on is replaced by its obvious neighbour (e.g. `mqtt_configured`/`mqtt_connected` swapped, the net
`switch` turned into a cast, or `net_down_since_ms` restamped every sample). What the owner must
verify on real hardware lives under `docs/hardware-verification/`.

## Notes

- The LED answers "is the control chain healthy", not "which mode is active": `LOCAL`, `BOOST` and
  `SEASON_OFF` all read NORMAL when the links are up (see `ot_led`'s README).
- `ota_active` is dormant: the ladder and render already handle it; wiring a real OTA path only
  means a future caller starts passing `true` to `ot_led_note_ota()` while a write is in flight.
