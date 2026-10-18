# ot_control — the executor core

The firmware's half of the controller/executor split: Home Assistant (or the web UI in LOCAL
mode) is the controller that decides *what* to heat to; `ot_control` is the executor that decides
who owns each command, what the boiler is actually told once a second, and what happens when the
controller falls silent. It is the ladder of states, the watchdog, the bounded failsafe, the boost
and the held flow setpoint (OpenTherm ID 1).

It is **pure** — no FreeRTOS, no ESP-IDF, no allocation, no globals. Time enters only as a
monotonic millisecond argument, so every duration (the watchdog timeout, the multi-day heat-hours
arm, the 49-day millisecond wrap) is host-testable in microseconds. The impure task layer that
carries out its decisions — the bus, NVS, the clock, `RTC_NOINIT` — is the sibling component
`ot_thermostat`.

All temperatures are `int16_t` tenths of a degree Celsius (the `_dc` suffix). The configuration
store has no floating-point type, and a float that crosses that store twice stops comparing equal,
so degrees never become floats here.

## Responsibility

`ot_control` owns, as one pure decision computed once a second:

- **The ladder of six states**, evaluated first-match-wins; the enum order *is* the priority.
- **The entries into HA ownership** — each one forgets whatever HA last said.
- **The watchdog and the bounded failsafe**, including the multi-day heat-hours arm.
- **The held flow setpoint (ID 1) and its re-send cadence** — ID 1 is always a valid value,
  quantised to 0.5 °C and clamped inside the configured flow bounds.
- **The DHW enable bit and the reconciliation of the DHW setpoint (ID 56)** against what the
  boiler reads back.
- **The ownership and bounds of every command**, and the boost as ladder row 2.

**The load-bearing invariant:** the CH (central-heating) bit never *rises* until the bus has
carried the currently-held ID 1 since it last changed. A boiler asked for heat with an unconfirmed
flow setpoint would heat to a value nobody chose. A bit already up is *not* dropped for a new
setpoint — that would cost a burner cycle for every slider move — the rule constrains the bit
*rising*, not the bit *staying up*.

It does **not**: talk to the bus, NVS, the clock, the room-source registry or MQTT; hold any
instance or lock; observe configuration edges outside `step()`; restore a boost across a reboot.

## Public API

Header: `include/ot_control.h`. The caller owns the `ot_control_t` value (it is not opaque in C,
because the task layer holds one by value, but every other component touches it **only** through
these functions), and **serialises every call on one instance under one spinlock**. `step()` runs
once a second from `ot_thermostat`'s task; `apply()` and the boost functions run from the httpd and
esp-mqtt tasks. A zeroed struct is **not** valid — call `ot_control_init()` first.

| Function | Contract |
| --- | --- |
| `ot_control_init(c, cfg, r, now_ms)` | Zeroes and seeds `*c` from the config snapshot; restores the watchdog accumulator, heat-hours and the powered part-hour from `r` (NULL = nothing restored). Booting in HA mode counts as an entry into HA ownership. Arms one DHW write if a setpoint is stored, so it reaches even a boiler that never answers the readback. |
| `ot_control_check(cfg, origin, cmd, value)` | **Pure and stateless.** Ownership + bounds of one command. Returns `ot_control_err_t`; changes nothing. Used both as the early answer at the request surface and as the first thing `apply()` does. |
| `ot_control_apply(c, cfg, origin, cmd, value, now_ms, persist)` | The final answer under the spinlock: re-checks ownership against the mode the executor **last observed** (not the possibly-stale snapshot), bounds against the snapshot, applies the command, feeds the watchdog for accepted HA CH commands, fills `*persist`. Observes no configuration edge. `*c` is untouched on refusal; `*persist` is always written (all-false on refusal). `now_ms` is part of the signature but deliberately unused — no moment is ever stamped. |
| `ot_control_step(c, cfg, in, now_ms, out)` | Once a second: advances the clocks, observes the ownership edges, runs the ladder, the failsafe, the invariant and the re-send cadence. Cannot fail; `*out` is written in full — even a garbage config yields one defined state. |
| `ot_control_boost_start(c, cfg, setpoint_dc, minutes, now_ms)` | Row 2 of the ladder, **LOCAL mode only**. Validation lives here (pure) so a host suite can reach it. Refuses in a fixed order: owned-by-HA, season-off, bad-minutes, out-of-range. A start replaces a running boost. |
| `ot_control_boost_cancel(c)` | Ends any boost. |
| `ot_control_boost_active(c)` | Reads the flag only. |
| `ot_control_boost_setpoint_dc(c)` | 0 when no boost runs. |
| `ot_control_boost_remaining_s(c, now_ms)` | 0 from the deadline on, rounded **up** before it; never ends a boost by being read. |
| `ot_control_ch_command(c, cfg)` | The CH *command* of whoever owns it (the stored LOCAL switch, or HA's value held in RAM), judged by the snapshot's mode — **not** the actual bit, which the invariant, the season or the failsafe may hold down. |
| `ot_control_state_name(s)` / `ot_control_reason_name(r)` | Wire spellings; out-of-range → `"unknown"`, never NULL (renderers print straight into JSON). |

### Key types

- `ot_control_state_t` — the ladder: `SEASON_OFF, BOOST, LOCAL, HA_WAITING, FAILSAFE, HA`.
  **Do not reorder:** the generated `control_state` entity lists its options in this order, and
  Home Assistant stores the option *string*, so a renumbering rewrites the owner's recorder history.
- `ot_control_reason_t` — why the state decides as it does, published as `control_state`
  attributes (`reason` = what the CH bit is doing; `cause` = why the state is failsafe).
- `ot_control_mode_t` — `LOCAL` / `HA`.
- `ot_origin_t` — `OT_ORIGIN_WEB` / `OT_ORIGIN_HA`, who is writing.
- `ot_control_cmd_t` — the five commands, with explicit numeric values (the registry generator
  emits them): `CH_ENABLE`, `CH_SETPOINT`, `DHW_ENABLE`, `DHW_SETPOINT`, `SEASON`.
- `ot_control_err_t` — the refusal codes.
- `ot_control_cfg_t` — a snapshot of the configuration (mode, season, watchdog, failsafe
  parameters, flow bounds, the LOCAL switch and setpoint, the DHW enable and setpoint).
- `ot_control_in_t` — the non-config inputs of a step: the effective room source, the stale-room
  flag, the ID 56 readback, and the ID 1 confirmation from the bus.
- `ot_control_restore_t` — what survives a reset (the watchdog accumulator, heat-hours, the
  powered part-hour).
- `ot_control_persist_t` — what an accepted command asks the task layer to write to NVS.
- `ot_control_out_t` — what `step()` tells the task to send, publish and persist.

Constants: `OT_CONTROL_BOOST_MAX_MINUTES` (480, i.e. eight hours) and `OT_CONTROL_RESEND_MS`
(10000 — the ID 1 re-send interval once confirmed).

### Why `check` and `apply` are split (ownership vs. staleness)

The config snapshot is built **outside** the caller's spinlock (building it takes the config
store's mutex), so the snapshot handed to `apply()` or `boost_start()` can predate a mode flip that
`step()` has already seen. Therefore **ownership** (which mode, which season) is judged against what
the executor itself last observed inside `step()`; the snapshot supplies only **value bounds**. The
ownership edges are observed by `step()` **alone** — `apply()` and `boost_start()` must never treat
the snapshot's mode as an edge, or a stale snapshot would be taken as a mode change on one step and
taken back on the next, silently forgetting HA's accepted command. A mode flip therefore reaches
HA's commands at most one step (≤ 1 s) after it reaches the store.

## Implementation

Cut along seams; the private glue between the sources is declared in `ot_control_internal.h`
(deliberately *not* under `include/`, so no other component or suite can reach the `otc_*` helpers —
suites test the public contract only).

| File | Holds |
| --- | --- |
| `ot_control.c` | The ladder, the edge observation, `step()`, the invariant, the DHW-readback reconciliation, and `ot_control_ch_command()`. Also `otc_bound()` (quantise-then-clamp) and `otc_forget_ha()`. |
| `ot_control_apply.c` | `ot_control_apply()` — the re-check, the command effects, the watchdog feed, the `*persist` fill, and the "leaving failsafe is an entry into HA ownership" path. |
| `ot_control_check.c` | `ot_control_check()` — the stateless ownership + bounds function, in its own file so a caller that only needs the answer links one object. |
| `ot_control_failsafe.c` | The watchdog and heat-hours accumulators (`otc_advance()`), the saturating add (`otc_add_sat()`) and the failsafe's CH verdict (`otc_failsafe_ch()`). |
| `ot_control_boost.c` | The boost lifecycle and `otc_boost_expire()`; the wrap-safe `reached()` helper. |
| `ot_control_names.c` | The wire spellings, as designated-initialiser tables tied to each enumerator. |

### The ladder (`ladder()`, first match wins)

1. `SEASON_OFF` — the heating season is off. This is the person's master kill and outranks
   everything.
2. `BOOST` — not in HA ownership, and a boost is active.
3. `LOCAL` — not in HA ownership.
4. `HA_WAITING` — HA owns but has not spoken since ownership began, and the watchdog has **not**
   expired.
5. `FAILSAFE` — the failsafe is latched, or the watchdog has expired, or HA is heating blind (its
   forwarded room source went stale).
6. `HA` — HA owns, has been heard, is fresh, and is unlatched.

`step()` runs these substeps in order, and the order matters: advance the clocks for the interval
that just elapsed → observe the ownership edges → expire the boost → sample the blind flag →
compute watchdog expiry → run the ladder → track the failsafe → pick want/target by state → apply
the invariant → emit `*out`. The advance must precede the observe, or the first HA step would count
the last LOCAL second against HA's watchdog.

### Data structures and invariants

- **Durations are accumulators, never `now - then`.** A `uint32` millisecond clock wraps after
  49.7 days, and a subtraction across the wrap reads as "HA spoke a moment ago". The watchdog
  (`overdue_ms`), the confirmation age, the CH-bit age, the DHW-write age, the failsafe duration and
  the powered part-hour are all advanced each step by a **saturating** add, so a peer dead for 50
  days stays dead rather than wrapping back to fresh. The **only** stored moment is the boost
  deadline, whose ≤ 8 h span sits far inside the ±24.8 days a signed difference covers — hence the
  wrap-safe `reached()` helper.
- **The watchdog** counts only while the last-observed mode is HA, and is held at 0 in LOCAL (HA
  cannot feed a watchdog it does not own). It expires when the accumulator reaches
  `watchdog_s · 1000`. It is fed (reset to 0) by every accepted HA CH command.
- **The failsafe** (`otc_failsafe_ch()`) decides the CH bit in order: first, if HA has not asked
  for heat within `failsafe_heat_days` (measured in *powered* hours), the failsafe *disarms* and
  holds CH down — the "it is summer, whatever the switch says" case. Otherwise, if there is no fresh
  room source it heats *blind* at the failsafe setpoint. Otherwise it applies ±0.3 K hysteresis
  around `failsafe_room_target_dc`, with `failsafe_min_cycle_s` enforced as both a minimum-on and a
  minimum-off time, measured on the bit actually sent. The hysteresis keeps its own verdict
  (`fs_want`) apart from the bit sent (`ch_out`), because the invariant can hold the bit down for a
  step while the hysteresis wants it up.
- **The failsafe cause is recomputed every step**, not latched with the state: a failsafe held only
  by the ownership latch — the acute cause has cleared but HA has not yet re-sent CH_ENABLE —
  reports cause `NONE`, not a stale `HA_BLIND`.
- **The invariant:** the held setpoint is `otc_bound(cfg, target)`; a change un-confirms it, and a
  confirmation from the bus re-confirms only if the confirmed value equals the held one (a foreign
  value on the wire un-confirms too). The CH bit is forced down, with reason `AWAIT_SETPOINT`,
  whenever heat is wanted, the bit is currently down, and the setpoint is unconfirmed.
- **The re-send cadence:** the ID 1 request is asked on every step until the bus confirms it, then
  again once `OT_CONTROL_RESEND_MS` has elapsed since the last confirmation. A request the task had
  to skip (because a hand write was pending on the bus) is simply asked again next step — the caller
  keeps no memory.
- **DHW reconciliation (`dhw_due()`):** the DHW setpoint (ID 56) is rewritten only when the boiler's
  readback differs, at most once a minute, never while unset, and capped at three unanswered writes
  per target — a boiler that stores whole degrees would otherwise be rewritten 1440×/day. An
  accepted `DHW_SETPOINT` command reopens its target (matched by value, since the snapshot lags the
  command). The comparison is exact, not tolerant, or a half-degree change would never be written.
- **Heat-hours** are *powered* hours since HA last asked for heat (not wall-clock — SNTP may be as
  dead as HA); persisted only while below the limit, saturating at `0xFFFF`.

### "DO NOT" constraints (enforced by comments and tests)

- No `esp_timer.h` / `nvs.h` includes and no `REQUIRES` for the clock, NVS or the bus in this
  component. PlatformIO compiles every source of a library whose header a suite includes (and
  `ot_command`'s suite includes this header), so one impure include stops `test_ot_control*` and
  `test_ot_command` from linking. The only dependency is `ot_bus_sched` — for the `OT_STATUS_*` bit
  names, and that is pure.
- Do not reorder `ot_control_state_t` (it rewrites Home Assistant recorder history) or return NULL
  from the name functions (callers print into JSON).
- Do not observe configuration edges in `apply()` / `boost_start()`; do not stamp a moment in
  `apply()`.
- Do not "simplify" the wrap-safe `reached()` to a plain comparison, and do not drop the
  `!boost_active` guard in `remaining_s()`.
- Do not clamp an out-of-range command "to be helpful" — refuse it, so a broken HA automation that
  keeps being refused feeds no watchdog and lands in failsafe (the correct outcome) rather than
  hiding behind a plausible flow temperature.
- The season is the person's master kill, not a value HA owns: the web writes it 0/1 in either
  mode; HA may send 0 in HA mode (its summer logic is the failsafe's first bound) and **never** 1.

## Tests

Host suites under `test/` (pure, host-built with `-Werror=switch`):

- `test_ot_control` — the ladder, the ownership edges, the step, the invariant and the DHW
  reconciliation.
- `test_ot_control_check` — the ownership/bounds truth table (all eight season cells; ownership is
  answered before the value).
- `test_ot_control_failsafe` — the watchdog, the heat-hours arm and the failsafe CH verdict.
- `test_ot_control_boost` — the boost lifecycle and the millisecond-wrap cases.
- `test_ot_control_bus` — the re-send cadence and bus interaction against this core.

Run one suite: `pio test -e native -f test_ot_control`.

(The `test_ot_control_io*` suites belong to the sibling `ot_control_io` component, not to this one.)

## Notes

- HA's setpoint falls back to the failsafe setpoint when ownership is forgotten, not to "none" —
  there is no "no setpoint" state, so if HA's first command is `CH_ENABLE = 1` alone the boiler
  heats at the one value the owner configured for that case.
- CH setpoints are quantised to the store's 0.5 °C grid before persist or use; where the bounds and
  the quantisation conflict, the bounds win (a bound need not be a multiple of 5 dc).
- The failsafe entry count and the last-failsafe duration are RAM-only (there is no field for them
  in `ot_control_restore_t`); the count is republished retained over MQTT.
