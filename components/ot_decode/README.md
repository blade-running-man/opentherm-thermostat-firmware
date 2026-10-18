# ot_decode — the receive Manchester state machine (pure)

Reception of an OpenTherm frame by oversampling: the state machine is fed the line level
exactly every `OT_DECODE_SAMPLE_US` (100 µs) and returns a status. No timer, no GPIO, no
allocations — a pure component, host-tested, safe to call from an ISR.

## Purpose

OpenTherm carries each frame as a Manchester-encoded, biphase-mark stream: 34 bits total
(a leading start bit, 32 data bits, a trailing stop bit), each bit is 900..1150 µs long
with a mandatory level transition in its middle. This component recovers those 32 data bits
from a uniform stream of line-level samples, without any hardware access of its own.

## Responsibility

Owns the receive-side Manchester demodulation over a uniform sampler:

- Catches synchronisation on the leading edge, measures the intervals between transitions,
  classifies each as a half-bit or a full bit, and reconstructs the 34 bits (start + 32 data
  + stop) into a shift register.
- Enforces framing: rejects on bad timing, a zero stop bit, and mid-frame silence.
- Exposes the 32 data bits on `DONE`.

Does **not**:

- Touch hardware — it is fed a normalised `bool level` (true = line active); it owns no
  timer, GPIO or buffer. Sampling and the timer belong to the caller (`ot_master`).
- Account for adapter inversion. The line level arrives already normalised (true = active).
  The caller (`ot_master`) removes any board-specific electrical inversion before feeding
  samples in. **DO NOT** handle inversion here — otherwise the host tests would start
  checking a property of one particular board's wiring.
- Parse the payload, check parity, or validate the message. That is `ot_frame`'s job; this
  component only reconstructs the raw 32-bit word.

## Public API

All in `include/ot_decode.h`. An instance belongs to a single task; concurrent use from two
contexts is a caller defect. All functions are non-blocking and allocation-free, hence
ISR-safe.

| Symbol | Contract |
| --- | --- |
| `ot_decode_reset(d)` | Reset the state machine. Call before every wait for a reply. Safe while the line is active — it waits for an idle sample first, then begins a frame. |
| `ot_decode_push(d, level)` | Feed one sample; returns the current status. After `DONE` or `ERROR` further calls change nothing until reset. |
| `ot_decode_payload(d)` | The 32 data bits without framing. Meaningful only on `DONE`. |
| `ot_decode_error(d)` | The rejection cause (`ot_decode_error_t`). |

Types and constants:

- `ot_decode_status_t` — `IDLE` (waiting for the leading edge), `BUSY` (frame in progress),
  `DONE` (34 bits collected, payload available), `ERROR` (rejected).
- `ot_decode_error_t` — `NONE`, `TIMING` (interval in no window), `STOP_BIT` (bit 34 was
  zero), `SILENCE` (no transitions for longer than a full bit).
- `ot_decode_t` — the instance: `status`, `error`, `armed`, `last_level`, `since` (samples
  since the previous transition), `phase`, `bits`, `shift` (collected bits, MSB first).
- `OT_DECODE_SAMPLE_US` = 100, `OT_DECODE_BITS` = 34.
- Interval windows in samples: `HALF_MIN` 4 / `HALF_MAX` 6, `FULL_MIN` 9 / `FULL_MAX` 12.

## Implementation

`ot_decode.c` holds the whole machine; the state struct lives in the header.

### State machine

- **IDLE** — `push` arms on the first idle (`!level`) sample. Only once armed does an active
  level start a frame (→ `BUSY`). Without the arm step, a reset while the line is active
  would mistake its very first sample for the leading edge.
- **BUSY** — each `push` increments `since`. When the level is unchanged and `since` exceeds
  `FULL_MAX`, the slave went silent mid-frame → `ERROR`/`SILENCE` (a truncated frame is
  rejected whole; no partial parsing). On a transition, the interval `n` (in samples) is
  classified by `phase`:
  - `PHASE_MID_ONLY` — the next transition can only be a bit middle, a half-bit away. `n`
    must be `IS_HALF`; else `ERROR`/`TIMING`. A valid half records a bit, then `phase → ANY`.
  - `PHASE_ANY` — after a bit middle: `IS_FULL` records a bit (stays `ANY`); `IS_HALF` is a
    boundary transition that carries no bit (`phase → MID_ONLY`); anything else is
    `ERROR`/`TIMING`.
- **DONE / ERROR** — frozen; `push` returns the frozen status until `ot_decode_reset`.

### Bit decoding and framing

- Bit value: an active→idle transition is a one, idle→active is a zero
  (`to_idle` = `last_level && !level`).
- `shift` is `(shift << 1) | bit`, MSB first. `record_bit` counts to `OT_DECODE_BITS`; on
  the 34th bit a zero is `ERROR`/`STOP_BIT` (a valid frame ends with a one), otherwise `DONE`.
- `ot_decode_payload` returns `(shift >> 1) & 0xFFFFFFFF`: bit 33 is the start bit, bits
  32..1 the data, bit 0 the stop bit.

### Invariants / DO NOTs

- **Electrical inversion is removed upstream** by `ot_master` before samples are pushed in.
  This component always sees true = active. Do not reintroduce inversion handling here.
- **No start-bit check, deliberately.** The leading edge is by construction the transition
  into the active half of the start bit, and a valid start bit is one. A zero start bit has
  no edge there, so the machine simply syncs later — the two cases are indistinguishable
  here and are caught by the stop bit and by `ot_frame`'s parity. **DO NOT** add
  `if (bits == 1 && !bit)`: the branch is unreachable.
- **Do not widen the timing windows.** The bit period is 900..1150 µs (half-bit 450..575).
  A uniform sampler with step P already carries the sampling phase error inside the spread
  between `floor(T/P)` and `floor(T/P)+1` samples, giving half `{4,5,6}` and full
  `{9,10,11,12}`. Adding another ±1 "for phase error" double-counts it: with the wider
  windows `{3..7}`/`{8..13}`, a stream running at 800..875 µs — 12 % too fast — was accepted
  at 100 % (3200 frames of 3200), whereas the narrow windows reject 3197 of 3200. The gap
  between `HALF_MAX` 6 and `FULL_MIN` 9 is the whole reason for the 100 µs step; at a 200 µs
  step the windows butt together and "too fast" can no longer be told apart.

## Tests

Host suite `test/test_ot_decode` (pure, framework-free). The test is written before the
implementation, and the same sources compile on host and device.

## Notes

- The measured justification for the narrow windows (3197/3200 of an out-of-tolerance stream
  rejected, vs 3200/3200 accepted with the wide windows) is the reason `OT_DECODE_SAMPLE_US`
  is 100 and not 200; the same analysis is recorded in the header.
- `CMakeLists.txt` registers a single source with a public `include/` dir; no dependencies.
