# Physical layer

How a 32-bit [frame](/protocol/frame) becomes 34 Manchester-coded bits on the two-wire OpenTherm
line and back. Three components share the work: `ot_encode` renders a word into half-bits,
`ot_decode` reassembles a word from samples, and `ot_master` owns the two GPIOs, the one hardware
timer and the timing of one conversation. The first two are pure and host-tested; only
`ot_master` touches hardware. The adapter between the ESP32 and the boiler's line is the
[DIYLess OpenTherm master shield](/reference/diyless-opentherm-master-shield).

## Line coding

OpenTherm uses Manchester (Bi-phase-L) coding with a nominal bit period of 1 ms:

- **`1` is an active → idle transition** in the middle of the bit;
- **`0` is an idle → active transition**.

So the first half of a bit equals the bit and the second half is its negation. A frame is a start
`1`, the 32 data bits and a stop `1`: 34 bits, 68 half-bits of 500 µs, **34 ms** on the wire. The
line idles **low** (inactive).

All levels inside the firmware are **logical**: `true` means "the line is active". The
electrical inversion of the adapter is removed in exactly one place, `ot_master`'s `drive()` and
`sense()`, using the board descriptor (see [Input inversion](#input-inversion)). Neither
`ot_encode` nor `ot_decode` knows about it, so their host tests check the protocol, not a property
of one board.

## Transmit: `ot_encode`

`ot_encode_frame(payload, out[68])` takes the 32 data bits from `ot_frame_encode()`, adds the
start and stop bits, and writes 68 logical half-bit levels, most significant bit first:
`out[2i] = bit`, `out[2i+1] = !bit`. It is pure, stateless and safe from an interrupt.

The host suite `test/test_ot_encode` checks the start and stop bits, that every pair is a
transition, the bit order, and a **loopback** through `ot_decode` — nominal, for every message
type and Data-ID, and at the edges of the timing tolerance.

## Receive: `ot_decode`

Reception is **oversampling, not an edge interrupt**: the line is sampled every
`OT_DECODE_SAMPLE_US` = **100 µs**, and the state machine counts samples between transitions.
The decoder is fed levels already normalised to logical.

### Timing windows

The specification sets the bit period at 900..1150 µs, so a half-bit is 450..575 µs. A uniform
sampler with step P counts either ⌊T/P⌋ or ⌊T/P⌋+1 samples inside an interval T, so the sampling
phase error is already inside that spread. In samples of 100 µs:

| Interval | Microseconds | Samples accepted |
| --- | --- | --- |
| half-bit | 450..575 | **4, 5, 6** (`OT_DECODE_HALF_MIN`..`_MAX`) |
| full bit | 900..1150 | **9, 10, 11, 12** (`OT_DECODE_FULL_MIN`..`_MAX`) |
| gap | — | **7 and 8** belong to neither window |

### Why 100 µs, and not 200 µs

Those two free positions between the windows are the whole reason for the 100 µs step. At 200 µs
the correct windows, {2, 3} and {4, 5, 6}, butt against each other: every interval falls into
one of them, and a slave running too fast can no longer be told apart. A frame 12 % too fast
(800..875 µs per bit) is then accepted as healthy.

The same trap exists one level up. Adding another ±1 "for the phase error" on top of the spread
counts the error twice and gives {3..7} and {8..13}. Measured with a stream at 800..875 µs: such
wide windows accept 3200 frames of 3200, the narrow ones reject 3197 of 3200. **Do not** lower the
sample rate or widen the windows.

### State machine

| State | What happens |
| --- | --- |
| `IDLE` | waits until it has seen one idle sample (*armed*), then takes the next active sample as the leading edge of the start bit. Without the arming step, a reset while the line is active would take its first sample for an edge. |
| `BUSY` | counts samples since the last transition. After a bit middle, the next transition may be a full bit away (another bit) or a half-bit away (a boundary, which carries no bit); after a boundary, only a half-bit away. Anything else is `ERROR` / `TIMING`. A level held longer than a full bit is `ERROR` / `SILENCE`: a truncated frame is rejected whole, never partly parsed. |
| `DONE` | 34 bits collected and the 34th (the stop bit) is a one; `ot_decode_payload()` returns bits 32..1. A zero stop bit is `ERROR` / `STOP_BIT`. |
| `ERROR` | frozen, like `DONE`, until `ot_decode_reset()`. |

There is deliberately **no start-bit check**. The leading edge the machine synchronises on is, by
construction, the transition into the active half of a start `1`; a zero start bit has no edge
there, and the machine would simply synchronise later. The two cases cannot be told apart, so the
branch would be unreachable; the stop bit and the parity catch the result. The host suite is
`test/test_ot_decode`.

## One conversation: `ot_master`

`ot_master_exchange(request, response)` is one frame each way. It **blocks the calling task** —
normally for at most the 34 ms frame plus the 900 ms reception budget, about 0.93 s, with a
ceiling of 1.5 s if the timer never fires (below) — and may be called only from the `ot_bus` task: two overlapping conversations lose
both frames and show up as rare, unexplained communication errors, so the protection is that
there is exactly one caller, not a lock (see [The bus](/protocol/bus)).

**One timer, one period.** A single `gptimer` ticks every 100 µs for both phases. Transmission
changes the level every fifth tick (500 µs half-bits); reception feeds every tick to `ot_decode`.
Switching the alarm period inside the interrupt handler between phases would be a race; 500
divides evenly by 100, so the handler never touches the driver configuration. The timer runs
only during a conversation.

| Phase | What the tick handler does |
| --- | --- |
| `PH_TX` | drives the next half-bit every 5 ticks; after the 68th, drives the line idle, resets the decoder and switches to receive at once — the slave may answer as early as 20 ms later |
| `PH_RX` | samples the input, counts level changes into `rx_edges`, pushes the sample to `ot_decode`; on `DONE` or `ERROR`, or after the reception budget, wakes the waiting task |

The **reception budget** is 9000 ticks, 900 ms: the slave has 800 ms to begin its reply and the
reply takes 34 ms, and 900 ms covers both while staying below the 1150 ms deadline after which the
master itself would count as silent. The waiting task gives up after 1500 ms only if the handler
never ran at all (a timer that did not start): waiting forever is not allowed.

| Result | Meaning |
| --- | --- |
| `OT_EXCHANGE_OK` | a frame was assembled and its parity checked out |
| `OT_EXCHANGE_TIMEOUT` | nothing decodable began within the budget |
| `OT_EXCHANGE_FRAME_ERROR` | edges arrived but did not form a frame: timing, Manchester, stop bit |
| `OT_EXCHANGE_PARITY_ERROR` | 34 bits arrived and the parity was wrong |

The tick handler is `IRAM_ATTR`, so that a flash write (NVS, an update) during a conversation
does not stop it — but the protection is **partial**, as the comment in `ot_master.c` says. The
pinned `sdkconfig` has `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=y`, but `CONFIG_GPTIMER_ISR_CACHE_SAFE`
and `CONFIG_GPIO_CTRL_FUNC_IN_IRAM` are not set, so the driver's interrupt path and
`gpio_set_level()` / `gpio_get_level()` stay in flash, and so does `ot_decode_push()`, which the
handler calls on every receive tick. While the cache is disabled, ticks can still be held off,
and a frame on the wire at that moment can be lost. A lost frame costs one conversation, not the
bus: the next step talks again.

### Bring-up diagnostics

Activity of 34 ms once a second cannot be seen with a multimeter, so `ot_master` exposes what a
probe cannot:

- **`rx_edges`** — input transitions counted across every reception window. Zero over hundreds of
  conversations means the wire is dead, not that a frame failed to come together.
- **`pins_shorted`** — a one-off check at init: drive the output active then idle and see whether
  the input follows. A working adapter has no loop between transmitter and receiver (with no
  boiler no current flows), so an input that follows means the two pins are shorted or swapped.
  It is logged as an error.
- **`ot_master_input_duty()`** — the share of ~64 samples over ~2 ms with the input active. With a
  connected, silent boiler it should read 0, because the line idles low; a steady 100 means the
  input polarity is the opposite of what was assumed.
- **`ot_master_drive_line()`** — holds the output in one state, for the
  [line test](/protocol/bus#the-line-test) only.

The input pin is pulled **down**, so a disconnected adapter reads as an idle line rather than as
noise the decoder would take for frames. The counters, the input duty and the shorted-pins flag
are served in the header of `GET /api/ot/raw` (see [The raw table](/protocol/bus#the-raw-table)).

## Input inversion

The board descriptor carries two flags, `ot_in_inverted` and `ot_out_inverted`. They are a
property of the board and its adapter, not of OpenTherm, which is why they live in
`components/board/` and are applied only in `ot_master`. On both supported boards
(see [Board](/device/board)) **only the output is inverted**; the input is read as is, active
high.

::: warning `ot_in_inverted` is taken from someone else's code
`ot_out_inverted = true` follows from the shield's output stage. `ot_in_inverted = false` is the
one descriptor field derived from a reference OpenTherm library rather than from the
specification. The library's `!readState()` expression reads like an inverted input, but it
concerns the sampling moment (the bit is taken after the mid-bit transition), not the polarity, so
it is no reason to set the flag to `true`. The field is still the one most likely to be wrong on
new hardware: **if not a single frame decodes, flip `ot_in_inverted` first**, before looking for a
bug in `ot_decode`. The symptom is many frame errors with plenty of `rx_edges`, or an
`input_duty` of a steady 100.
:::

## Tests

`test/test_ot_encode` and `test/test_ot_decode` are pure host suites; the loopback between them
is the end-to-end check of the line coding without hardware. `ot_master` has no host suite: its
behaviour is the glue between a timer, two pins and a boiler, and it is verified on the boiler
([First bring-up](/hardware-verification/01-first-bring-up),
[Conversation with the boiler](/hardware-verification/02-boiler-conversation)). Component
contracts: [`ot_encode`](../../components/ot_encode/README.md),
[`ot_decode`](../../components/ot_decode/README.md),
[`ot_master`](../../components/ot_master/README.md).
