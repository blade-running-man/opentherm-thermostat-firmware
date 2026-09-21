# ot_control_io — the executor's translations between `ot_control` and its neighbours

`ot_control` decides and `ot_thermostat` carries the decision out; `ot_control_io` is the pure
arithmetic *between* them and their neighbours — the configuration store, the bus, the reset that
just happened, the state model and the document `GET /api/control` renders. It exists so that logic
which would otherwise sit inside an impure FreeRTOS task (which no host suite can reach) is instead a
set of pure mappings that `test_ot_control_io` pins against the real components on either side.

## Responsibility

This component **owns** the following translations:

- **Configuration snapshot** — `ot_config_public_t` → `ot_control_cfg_t`, the one builder every step
  uses for both `ot_command_check()` and `ot_control_apply()`, so the early refusal and the final
  decision read the same mapping of the same store.
- **Persist patch** — an accepted command's `ot_control_persist_t` → the `ot_config_patch_t` that
  `ot_net_config_apply()` takes.
- **Bus report** — `ot_bus_write_state()`'s ID 1 sequence/value → `ot_control_in_t` confirmation.
- **Reset-reason restore** — decides which counts survive a reboot loop, and seeds
  `ot_control_restore_t` at boot from RTC RAM + NVS.
- **`RTC_NOINIT` record** — packs/checks the two counts (watchdog overdue, heat-hours part-hour) that
  must survive a soft reset, with a magic word and a check word.
- **Synthetic entities** — the executor's virtual entities and their values for
  `ot_state_set_virtual()`.
- **`GET /api/control` document** — one step's snapshot + output → `ot_api_control_t`.
- **ID 56 (TdhwSet) rules** — the readback (READ-ACK only), the *owed* one-shot write ladder
  (`ot_control_io_send()`, ID 1 first), and the *heard* record (the first DATA-INVALID-on-ID-1).
- **Number parsing from outside** — Celsius float → tenths, and boost minutes → whole `uint32_t`.

It does **NOT**:

- Have any task, lock, NVS call, clock, allocation or global — it is strictly pure. Adding any
  ESP-IDF header or a `REQUIRES` for the clock/NVS/bus would stop `test_ot_control_io` linking
  (PlatformIO compiles every source of a library whose header a suite includes). The impure half is
  `ot_thermostat`.
- Fail. Every function gives a defined answer for every input, damaged configuration included.
  `ot_control_io_dc()` and `ot_control_io_minutes()` are the only two that *refuse*, and they leave
  their output untouched when they do.
- Encode hand writes (that stays `ot_command_encode()`'s job) or make bounds decisions that already
  belong to `ot_control`.

## Public API

All in `include/ot_control_io.h`. Ownership: every pointer is the caller's and nothing is kept.

### Configuration

| Function | Contract |
| --- | --- |
| `ot_control_io_cfg(pub, out)` | Builds the `ot_control_cfg_t` snapshot. A `control_mode` other than `OT_CONFIG_MODE_HA` is treated as LOCAL; oversize numbers saturate (not wrap); `dhw_setpoint_set = dhw_setpoint_dc != 0` (0 is the store's "nobody has written it"). The one builder for both the early and the final answer, so both read one mapping. |
| `ot_control_io_patch(persist, patch) → bool` | Emits only the fields the executor asked to persist; everything else stays zero ("absent, leave it alone"). Returns `persist->any`. Does **no** comparison with the stored value, deliberately: two writers each comparing against their own snapshot would lose an update when they race, and an unchanged value costs nothing because NVS compares and skips an identical item itself. |

### Bus

| Function | Contract |
| --- | --- |
| `OT_CONTROL_IO_ID_TSET` / `_ID_TDHW_SET` | OpenTherm Data-ID `1` (TSet, CH setpoint) / `56` (TdhwSet, DHW setpoint). |
| `ot_control_io_f88_dc(raw) → int16_t` | f8.8 word → tenths of a degree, rounded half away from zero, all-integer. Exact for every multiple of 0.5. |
| `ot_control_io_dc_f88(dc) → uint16_t` | Tenths → f8.8, matching `ot_codec_float_to_f88()`, clamped to `0x7FFF`/`0x8000` beyond ±127.99. **The executor's own writes are encoded with this, not `ot_command_encode()`** — that gate would refuse an ID the state model marked unsupported, which would break the held ID 1 forever (see Notes). |
| `ot_control_io_confirm(id1_seq, id1_raw, *seen, in)` | Turns the bus's ID 1 count/value into `in->setpoint_confirmed` / `in->confirmed_dc`; updates `*seen`. A moved count means an ID 1 went out (ours *or anybody's*); the value comparison is `ot_control_step()`'s job, not here. |
| `ot_control_io_readback_t` | `{bool valid; uint16_t raw;}` — the boiler's stored ID 56, zeroed = none yet. |
| `ot_control_io_readback(rb, data_id, type, raw)` | Records a reply into `rb`. **Only a READ-ACK of ID 56** counts (a WRITE-ACK echoes what we wrote). |
| `ot_control_io_readback_in(rb, in)` | Fills `in->dhw_readback_valid` / `in->dhw_readback_dc`. |
| `ot_control_io_heard_t` | `{readback_t rb; bool id1_invalid; uint16_t id1_invalid_raw;}` — the readback plus the *first* DATA-INVALID answer to ID 1. |
| `ot_control_io_heard(h, data_id, type, raw)` | The one call `ot_thermostat_heard()` makes inside its spinlock: does the readback, then keeps the first DATA-INVALID-on-ID-1 value. Does **not** treat UNKNOWN-DATAID (that is `ot_state`'s mark, and it says the boiler does not know ID 1 at all, not that the value is out of range). |
| `ot_control_io_owed_t` | `{bool dhw;}` — an ID 56 asked for but not yet queued. |
| `ot_control_io_write_fn` | `bool (*)(uint8_t, uint16_t)` — queues one write only if the bus is idle (`ot_bus_write_if_idle()`), so a hand write is never evicted. |
| `ot_control_io_send(owed, cfg, out, write)` | Offers the step's own writes: **ID 1 first** (the CH bit waits on it, asked every step until the bus confirms it), then the *owed* one-shot ID 56 with the *current* target. An unset target (0) drops what is owed. |

### Numbers from outside

| Function | Contract |
| --- | --- |
| `ot_control_io_dc(celsius, *dc) → bool` | `lroundf(celsius*10.0f)` **in float** (measured correct at 20000/20000 x.x5 values; the double form is wrong 8000×). `false` (output untouched) for NaN/inf/out-of-`int16_t`. |
| `ot_control_io_minutes(minutes, *out) → bool` | A whole `minutes >= 0` fitting `uint32_t`, else `false`. `1.5` is refused (it would silently become a shorter boost than asked); `0` and over-max are *not* refused here — that is `ot_control_boost_start()`'s one place to say so. |

### What survives a reset

| Function / type | Contract |
| --- | --- |
| `ot_control_io_rtc_t` | `{magic; overdue_ms; hh_ms; check;}` — the two counts held in `RTC_NOINIT` memory (RAM a software reset, panic or watchdog leaves alone and a power-on fills with noise). `OT_CONTROL_IO_RTC_MAGIC = 0x4F544332` ("OTC2"). Written and believed together, or not at all. |
| `ot_control_io_rtc_store(rtc, overdue_ms, hh_ms)` | Fills the record and its check word `~(magic ^ overdue_ms ^ hh_ms)` — a torn or random blob fails it. |
| `OT_CONTROL_IO_RST_*` enum | Mirrors `esp_reset_reason_t` by value; `ot_thermostat` pins each with a `_Static_assert` (this header may not include `esp_system.h`). |
| `ot_control_io_restore(reset_reason, rtc, hh_found, hh, out)` | Restores overdue + part-hour (both or neither) only after a reboot-loop reason (SW, PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT) *and* an intact blob. `heat_hours` from NVS, or `UINT16_MAX` when none stored (a device HA never asked for heat is at the *disarmed* end). |
| `ot_control_io_reset_name(reset_reason) → const char *` | `"poweron"`, `"sw"`, … for the boot log; `"other"` outside the list. Never NULL. |

### What the executor shows

| Function / type | Contract |
| --- | --- |
| `ot_control_io_virtual_t` / `OT_CONTROL_IO_VIRTUAL_MAX` (9) | `{const char *key; float value;}` — a synthetic entity. |
| `ot_control_io_virtuals(cfg, out, ch_command, id1_known, id1_dc, v[]) → size_t` | Writes the synthetic entities and returns the count. Enum values are option indices (the `ot_control` ordinals). `ch_setpoint_effective` is the last ID 1 actually on the wire, omitted until one has gone out. |
| `ot_control_io_document(cfg, out, doc)` | Builds `GET /api/control`'s `ot_api_control_t`. The three boost fields and the stack mark are zeroed for the caller to fill from `ot_control`'s boost accessors and the task's own high-water mark. |

## Implementation

Single translation unit `ot_control_io.c` (284 lines); the contract lives in `include/ot_control_io.h`
(244 lines). `CMakeLists.txt` registers it with `REQUIRES ot_control ot_config ot_api ot_frame` — all
public because the header's signatures carry their types, and all pure.

Key pieces and invariants:

- **Mode static asserts** — `OT_CONFIG_MODE_LOCAL == OT_CONTROL_MODE_LOCAL` and `..._HA == ..._HA`
  are pinned here, the one place both enums are in view, because `ot_config` may not include
  `ot_control.h` (that would drag the executor into every settings suite).
- **`dc_of()` / `stored()`** — saturate `uint16_t`→`int16_t` (a wrap would become a negative
  setpoint `ot_control` would clamp up and hold as if meant); `stored()` maps a negative tenths to
  `UINT32_MAX` so the store's narrow `u16` fields reject it rather than wrap into an accepted value.
- **`ot_control_io_f88_dc()`** — signed f8.8 conversion done without the implementation-defined
  `uint16_t → int16_t` cast; C truncation toward zero corrected by adding the half on the sign side.
- **`ot_control_io_dc_f88()`** — `dc * 25.6` in integers; `256*dc` is always even so no tie exists and
  half-away-from-zero agrees with the codec's `lrintf()` everywhere.
- **`ot_control_io_send()`** — the one-shot ID 56 is recorded as *owed* **before** anything is offered
  (the bus may refuse the offer); ID 56 is dropped when `dhw_setpoint_set` is false; ID 1 is written
  first (the CH bit waits on it); the owed ID 56 is written with the *current* `cfg->dhw_setpoint_dc`
  and cleared only when the bus took it. `DO NOT` reorder ID 56 ahead of ID 1: owed, it is no longer a
  one-shot, and CH would wait a slot longer behind every DHW write.
- **`ot_control_io_dc()` / `_minutes()`** — `!isfinite`/`!(>= 0)` fold NaN into the refusal before the
  UB-prone casts; `minutes != floorf(minutes)` rejects fractional boosts.
- **`keeps_ram()`** — the reboot-loop reasons that keep RTC RAM; BROWNOUT is included (a brown-out
  loop — WiFi transmitting on a weak supply — is the commonest reboot loop of an ESP32-C3) and the
  *check word*, not the reason, rejects RAM a voltage dip corrupted. `DO NOT` add POWERON: a power-on
  is not a loop. Anything unlisted starts from zero, which only costs a watchdog period.
- **`ot_control_io_restore()`** — `intact` requires a non-NULL blob with matching magic and check;
  `overdue_valid = keeps_ram && intact`, and `hh_valid` follows it (both or neither).
- **`ot_control_io_reset_name()`** — a designated-initialiser table so each name is tied to its
  number, not its position.
- **`ot_control_io_virtuals()`** — emits up to 9 entities: `ch_enable` (the owner's command),
  `dhw_enable`, `heating_season`, `control_mode`, `control_state`, `ch_enable_effective` (the bit
  actually sent, from `status_high & OT_STATUS_CH_ENABLE`), `failsafe_count`,
  `last_failsafe_duration_s`, and `ch_setpoint_effective` only when `id1_known`.

## Tests

Host suite **`test_ot_control_io`** (referenced throughout the header and source; the header notes a
probe counting the once-a-minute ID 56 re-write, and pins the registry option order to
`ot_control_state_name()` and the mode enum). No hardware or framework dependency — the whole point
of the component is that these translations are host-testable.

## Notes

- **Why the executor encodes its own writes, not via `ot_command_encode()`** — that gate refuses an ID
  the state model marked unsupported, and the mark is never cleared. Two UNKNOWN-DATAIDs for ID 1
  (line garbage suffices) would make every re-send of the held ID 1 refused, never confirmed — and by
  the rule that the CH bit never rises before the held ID 1 has been confirmed, CH would never rise
  again, failsafe included, until a reboot. Bounds are already `ot_control`'s (held value inside
  `[flow_min, flow_max]`; DHW target passed ID 48's bounds when it was written). `ot_command_encode()`
  remains the gate for *hand* writes.
- **Why no value filter in `ot_control_io_confirm()`** — a filter of "only report ours" is exactly how
  a foreign ID 1 on the wire would fail to un-confirm the held value; the comparison is
  `ot_control_step()`'s decision.
- **Why the DATA-INVALID-on-ID-1 write still counts as sent** — holding CH down on a range
  disagreement would turn it into a silent no-heat, failsafe included; instead the task warns once,
  with the value, from its own step (the bus task must not block or log).
- **Why `heat_hours` defaults to `UINT16_MAX`** — no NVS key means HA has never asked for heat, which
  is the *disarmed* end of the scale; a fresh device whose HA dies before it ever asked must not heat
  blind on the strength of a missing key.
- **Why `RTC_NOINIT` carries both counts under one magic/check** — the watchdog overdue count stops a
  reboot loop faster than `watchdog_s` from holding the "HA waiting" state (CH off) forever; the
  heat-hours part-hour stops a loop faster than an hour from keeping the summer-lockout bound from
  ever disarming.
- **Owed ≠ queued** — a *queued* ID 56 that a later hand write evicts before its slot is still lost;
  the 60 s readback retry heals that. `owed` only covers "not queued yet".
