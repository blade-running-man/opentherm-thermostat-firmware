# ot_sensor — one room-temperature source: outlier filter and a FRESH/STALE/NEVER freshness machine (pure)

A single room-temperature source. It filters the measurements offered to it (out-of-range and
sudden-jump outliers) and tracks how long it has been since a value was last accepted, exposing
that as one of three states: `NEVER`, `FRESH`, `STALE`. The measurement is taken in the room from
a source the device does not control (for example an MQTT publisher, a WiFi push/pull sensor, a
BLE thermometer heard directly, or the shield's own DS18B20). Because the device does not own that
source, a source going stale is a first-class state, and the value itself is untrusted.

The instance holds no globals and starts no work of its own; a consumer keeps one `ot_sensor_t`
per room source and drives it.

## Responsibility

Owns:
- The last accepted value (`NaN` until one arrives).
- The outlier filter: range check and the bounded jump filter.
- The freshness state machine and the overdue-time accumulator.

Does NOT do:
- No timers, no tasks, no allocation, no locking, no globals. Time enters only as a `now_ms`
  argument — which is what lets the 24-hour STALE behaviour and the 49.7-day uint32 millisecond
  wrap be tested on the host in milliseconds. (`CMakeLists.txt`: **DO NOT** add a `REQUIRES` to
  reach the clock; the moment this component includes `esp_timer.h`, PlatformIO — which compiles
  every source of a library — can no longer build the host test suite at all.)
- No policy on what a stale value is worth — the value is kept and handed out even while STALE;
  the consumer decides.

**Overdue time is an ACCUMULATOR, never `now - last_ok`.** It is a sum of wrap-safe deltas that
only grows and saturates at `UINT32_MAX`. A subtraction spanning the uint32 millisecond wrap
(49.7 days) would read a long-absent sensor as fresh — in January, unwatched. The accumulator has
no such moment. **DO NOT** simplify it back into storing the last-accepted moment and subtracting.

## Public API

All functions take the `ot_sensor_t *` explicitly; the caller owns the instance and each instance
is owned by exactly one task (no internal locking). Nothing here can fail — `ot_sensor_update()`
returning `false` is the filter having done its job, not an error to handle.

| Function | Contract |
| --- | --- |
| `void ot_sensor_init(ot_sensor_t *s, uint32_t stale_after_ms)` | Zeroes the struct, sets the FRESH deadline and `value = NaN`. Takes no timestamp. A zeroed sensor is safe to read: `NEVER`, `NaN`, zero overdue. |
| `bool ot_sensor_update(ot_sensor_t *s, float celsius, uint32_t now_ms)` | Offer a measurement. Returns `false` when rejected. Also acts as a tick (advances the accumulator by the interval since the previous tick/update before deciding). A rejected value changes nothing except the reject counter — it does NOT refresh the deadline. |
| `void ot_sensor_tick(ot_sensor_t *s, uint32_t now_ms)` | Advance time without offering a value (the loop's periodic step). `now_ms` must come from a monotonic clock (for example `esp_timer_get_time() / 1000`). |
| `ot_sensor_state_t ot_sensor_state(const ot_sensor_t *s)` | `NEVER` if no value ever accepted; else `STALE` when accumulated overdue `>= stale_after_ms` (the boundary belongs to STALE), else `FRESH`. |
| `float ot_sensor_value(const ot_sensor_t *s)` | `NaN` until a value is accepted; the last accepted value afterwards, including while STALE. |
| `uint32_t ot_sensor_overdue_ms(const ot_sensor_t *s)` | Milliseconds since the last accepted value, or since the first tick when there has never been one. |

Types / constants:
- `ot_sensor_state_t` — `OT_SENSOR_NEVER`, `OT_SENSOR_FRESH`, `OT_SENSOR_STALE`.
- `ot_sensor_t` — public struct (fields: `stale_after_ms`, `value`, `have_value`, `overdue_ms`,
  `last_tick_ms`, `tick_seeded`, `reject_streak`).
- `OT_SENSOR_MIN_C` (-40.0), `OT_SENSOR_MAX_C` (60.0), `OT_SENSOR_MAX_JUMP_C` (5.0),
  `OT_SENSOR_MAX_JUMP_REJECTS` (3).

## Implementation

Files: `include/ot_sensor.h` (contract), `ot_sensor.c` (~81 lines), `CMakeLists.txt`.

**Filter (all three parts, in `ot_sensor_update()`):**
1. `!isfinite(celsius)` rejects NaN and both infinities — checked first, because the numeric
   comparisons below are all false for NaN and would let it slip through.
2. `celsius < OT_SENSOR_MIN_C || celsius > OT_SENSOR_MAX_C` — rejected ALWAYS. This alone catches
   the +85 / -127 a real DS18B20 emits on a bad read. An out-of-range rejection never advances the
   jump-reject streak, so no run of +85 can ever reach the escape hatch.
3. Jump filter: a value more than `OT_SENSOR_MAX_JUMP_C` from the last accepted one is rejected,
   but at most `OT_SENSOR_MAX_JUMP_REJECTS` (3) times in a row. The 4th consecutive jump falls
   through to acceptance — the **escape hatch**, so a sensor that was moved to another room or
   replaced cannot lock the loop out of the room temperature forever. **DO NOT** remove it. The
   streak is reset only by an accepted value.

An accepted value sets `value`, `have_value`, resets `overdue_ms = 0` and `reject_streak = 0`.

**Time accumulator (`advance()`, shared by tick and update):**
- The first call after init only seeds `last_tick_ms` (init takes no timestamp — the owner's task
  has not started), so there is no interval to measure until a second moment is known.
- Subsequent calls: `overdue_ms = add_sat(overdue_ms, since(now_ms, last_tick_ms))`, then update
  `last_tick_ms`.
- `since()` uses unsigned subtraction, which wraps correctly across the uint32 boundary;
  `add_sat()` saturates at `UINT32_MAX` instead of wrapping.
- A `now_ms` earlier than the previous call is indistinguishable from a wrap and is read as a
  ~49-day forward jump → the accumulator saturates and the sensor latches STALE until the next
  accepted value clears it. That is the safe direction: a STALE sensor never switches heating on.
- Update and tick share `advance()` so an update cannot forget to advance the clock; calling both
  at the same instant counts the interval once.

**Invariants:**
- A rejected value never refreshes the deadline (else a sensor stuck on +85 would read FRESH
  forever).
- The STALE boundary is inclusive (`>=`).

## Tests

Host suite `test_ot_sensor` (referenced by `CMakeLists.txt` and the header). No test sources live
inside this component directory; the suite lives under the repository's `test/` tree. Because the
clock enters only as an argument, the 24-hour STALE limit and the 49.7-day wrap are exercised in
milliseconds on the host.

## Notes

- `NEVER` is deliberately not folded into `STALE` even though a consumer may treat them the same
  for heating: only `NEVER` means "this device has never had a room measurement," and the UI must
  distinguish a broken sensor from an unfinished setup (the room-temperature entity is unavailable
  in both states, so the person needs to know whether to look for a broken sensor or finish setup).
- The value is kept and returned even while STALE; deciding what a stale value is worth belongs to
  the consumer, which receives the state alongside the value. Erasing it here would take that
  decision away.
- `ot_sensor_update()` returning `false` is not an error — it is the filter having done its job;
  nothing here can fail.
- Purity is enforced by `CMakeLists.txt`: no ESP-IDF headers, no `REQUIRES`.
