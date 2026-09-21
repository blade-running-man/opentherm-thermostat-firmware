# ot_room — the registry of room-temperature sources

## Purpose

A pure, host-testable registry of a fixed number of room-temperature slots. Each slot wraps an
`ot_sensor` (which does the outlier filtering and the FRESH/STALE/NEVER freshness state machine).
From those slots the registry makes two independent picks: one reading trusted to **steer** the
heating failsafe, and one reading to **display** in the UI. The component composes `ot_sensor`
rather than re-implementing its filter and freshness rules.

## Status

Fully implemented, not a stub. The directory contains:

- `include/ot_room.h` — the public contract (119 lines).
- `ot_room.c` — the implementation (108 lines).
- `CMakeLists.txt` — `idf_component_register` with `REQUIRES ot_sensor` only.

The pure core is complete: the slot registry, submit/tick, and the two selections (steer and
display). A matching host test suite exists at `test/test_ot_room/`.

What this component deliberately does **not** contain, and which is **not yet implemented** here:

- Per-source adapters that feed readings in (MQTT, WiFi push/pull, BLE, and the shield's own 1-Wire
  DS18B20 sensor). None exist in this directory.
- The locked mailbox in `ot_thermostat.c` that would drain readings arriving on other tasks into
  `ot_room_submit()`. Not in this component.
- Any NVS/persistence record shape for the slot configuration. Not in this component.

By design the component stays pure: no adapters, no persistence, no task glue live here.

## Responsibility

**Owns:**

- A fixed array of `OT_ROOM_MAX_SLOTS` (4) slots. Each slot is an `ot_sensor_t` plus per-slot
  metadata: its `role`, an `ha_forwarded` flag, and its `stale_after_ms` threshold.
- The **steer** selection: the safety-critical pick of a room-role, FRESH slot to drive the
  failsafe, plus an independent `ha_forwarded_stale` flag.
- The **display** selection: a two-pass pick (room-role preferred, ambient as fallback) for what
  the UI shows.
- deci-°C rounding shared by both selections.

**Does NOT do:**

- No ESP-IDF headers, timers, tasks, locks, or dynamic allocation. Time enters only as a
  `now_ms` argument. `REQUIRES` is `ot_sensor` and nothing more — a deliberate constraint so the
  component compiles and links on the host test build.
- No outlier filtering or FRESH/STALE/NEVER logic of its own — it delegates all of that to
  `ot_sensor`.
- No source adapters, no mailbox, no NVS, no concurrency handling. Serialising concurrent access
  is the caller's responsibility, exactly as for `ot_sensor`.

## Public API

Types:

- `ot_room_role_t` — `OT_ROOM_AMBIENT` (value 0, the default: near the boiler, or otherwise not
  trusted to represent the room) or `OT_ROOM_ROOM` (value 1: a genuine room reading — the only role
  the steer pick honours). AMBIENT being 0 means a zero-initialised slot defaults to "never
  steers", the safe default for an unconfigured slot.
- `ot_room_slot_cfg_t` — per-slot config: `role`, `ha_forwarded` (marks a slot as Home Assistant's
  own forwarded reading), and `stale_after_ms` (passed straight to `ot_sensor_init()`).
- `ot_room_cfg_t` — an array of slot configs plus `count` (clamped to `OT_ROOM_MAX_SLOTS`).
- `ot_room_t` — the registry state: an `ot_sensor_t` per slot, the slot configs, and `count`.
- `ot_room_steer_t` — steer result: `fresh` (a room-role slot is FRESH), `value_dc` (deci-°C,
  meaningful only when `fresh`), `active_slot` (-1 if none), `ha_forwarded_stale`.
- `ot_room_display_t` — display result: `have`, `value_dc` (deci-°C), `active_slot` (-1 if none),
  `active_role` (which kind of reading was picked).

Functions:

- `void ot_room_init(ot_room_t *r, const ot_room_cfg_t *cfg)` — zeroes the whole struct, then
  copies `cfg[0..count)` and `ot_sensor_init()`s each configured slot. `count` above the maximum is
  clamped, not rejected, so a config typo cannot crash the caller.
- `bool ot_room_submit(ot_room_t *r, size_t slot, float celsius, uint32_t now_ms)` — offers one
  measurement to one slot; returns `ot_sensor_update()`'s verdict. Returns `false` for an
  out-of-range slot or a filter rejection — in neither case does anything change, and neither is an
  error to propagate.
- `void ot_room_tick(ot_room_t *r, uint32_t now_ms)` — advances every configured slot's `ot_sensor`
  by one tick. Intended to be called once per owner-task tick, after any pending readings have been
  submitted.
- `void ot_room_select_steer(const ot_room_t *r, ot_room_steer_t *out)` — safety-critical pick: the
  first slot that is BOTH `OT_ROOM_ROOM` AND FRESH. Ambient slots are excluded from the scan
  entirely.
- `void ot_room_select_display(const ot_room_t *r, ot_room_display_t *out)` — two-pass pick: the
  lowest-index FRESH room slot, else the lowest-index FRESH ambient slot.

## Implementation

- `to_dc(float)` — deci-°C rounding (`lroundf(celsius * 10)`), shared by both selections.
- `ot_room_init` — `memset`s the whole struct to zero first, so slots beyond `count` hold a benign
  default (a zeroed `ot_sensor` reads as NEVER/NaN, a zeroed config reads as AMBIENT, which never
  steers), then clamps `count` and calls `ot_sensor_init()` on each configured slot.
- `ot_room_submit` / `ot_room_tick` — thin passthroughs to `ot_sensor_update` / `ot_sensor_tick`
  over the configured slots.
- `ot_room_select_steer` — **one full pass over all slots, not a break on the first match**,
  because the picked slot and the `ha_forwarded_stale` flag are independent questions: a slot after
  the winner can still raise `ha_forwarded_stale`. Ambient slots are skipped (`continue`) before
  any pick is considered — this is the structural safety guard, so a boiler-side sensor can never
  steer regardless of how the other slots are configured. The first FRESH room slot sets
  `fresh` / `value_dc` / `active_slot`. `ha_forwarded_stale` is set true if any room-role,
  `ha_forwarded` slot is STALE, independent of whether a fresh slot was found.
- `ot_room_select_display` — a `pick_fresh_by_role` helper scans in index order for the first FRESH
  slot of a given role; it is called for `OT_ROOM_ROOM` first, then for `OT_ROOM_AMBIENT` as a
  fallback. `active_role` records which kind of reading was picked, so the UI never mistakes a
  boiler-room reading for the room. This is a display-only preference; the steer pick still excludes
  ambient entirely.

## Tests

The host suite lives at `test/test_ot_room/test_ot_room.cpp` (14 test cases). It links only
`ot_room` and `ot_sensor` on the host — which is the concrete reason the `ot_sensor`-only `REQUIRES`
must be kept. Coverage includes:

- Ambient-only registries never steer; a room slot steers.
- Steer priority is the lowest slot index, and a room slot is preferred over an ambient one for
  steering regardless of index.
- Display prefers a room reading over an ambient one, falls back to ambient when no room slot is
  fresh, and picks the lowest-index room slot.
- A STALE room slot is not treated as fresh.
- The `ha_forwarded_stale` flag: set when an `ha_forwarded` room slot is STALE, and not set when
  such a slot is NEVER or FRESH.
- `ot_room_submit` rejects an out-of-range or bad slot index.
- A registry with no fresh sources reports nothing fresh.

## Notes

- **Slot count** is `OT_ROOM_MAX_SLOTS == 4` (the shield's own sensor plus three
  MQTT/BLE sources). This avoids committing to a persistence record shape before the source
  adapters and their configuration are designed.
- **The core gotcha the whole component exists to prevent:** never weaken the steer role check to
  "prefer room, fall back to ambient". A warm boiler-room reading would then hold heat off in a cold
  house. The ambient exclusion in `ot_room_select_steer` is structural and must stay before the
  FRESH test.
- **Ownership / concurrency:** single-owner, like `ot_sensor`. Readings that would arrive from other
  tasks are not submitted here directly; the intended flow is that they land in a locked mailbox in
  the owner task and are drained into `ot_room_submit()` on the owner's tick (that mailbox is not
  yet implemented). Serialising concurrent `ot_room_*` calls is the caller's responsibility.
- **Purity is load-bearing:** do NOT add a `REQUIRES` beyond `ot_sensor`, and do NOT reach for
  `esp_timer` or any lock. PlatformIO compiles every source of a library whose header a test suite
  includes, so any impure include here would stop `test_ot_room` from linking on the host.
