# ot_encode — render an OpenTherm frame into transmit half-bits

## Purpose

Turns the 32 data bits of an OpenTherm frame into the 68 logical line levels a
transmitter drives onto the bus, Manchester-coded.

## Responsibility

Owns the transmit-side Manchester encoding: framing the 32-bit payload with its
start and stop bits and expanding each of the resulting 34 bits into two half-bit
levels.

It does **not**:
- Build the 32-bit payload — the caller supplies it already assembled; this
  component takes the payload as given.
- Account for the board's physical output inversion. The levels here are
  **logical** (`true` = line active). A board whose transmit line is wired
  inverted has that inversion removed downstream, at the point that drives the
  GPIO, from a per-board configuration flag. **DO NOT** apply inversion here:
  doing so would make the host tests check a property of one particular board and
  would break the loopback agreement with the matching decoder.
- Do any timing, I/O or allocation. It is pure, stateless, allocation-free, and
  safe to call from an ISR.

## Public API

Header: `include/ot_encode.h`

| Symbol | Contract |
| --- | --- |
| `#define OT_ENCODE_HALFBITS 68` | Number of half-bits produced per frame (34 bits × 2). |
| `#define OT_ENCODE_HALFBIT_US 500` | Duration of one half-bit in microseconds; a full frame is 34 ms. |
| `void ot_encode_frame(uint32_t payload, bool out[OT_ENCODE_HALFBITS])` | Fills `out[]` with 68 logical levels for `payload` (the 32 data bits of the frame); the start and stop bits are added internally. `out` must not be NULL and must hold `OT_ENCODE_HALFBITS` elements. |

## Implementation

Single file `ot_encode.c` — one function.

- **Framing**: the 34-bit sequence is a start `1`, the 32 data bits, then a
  stop `1`, assembled into a `uint64_t`:
  `bits = (1ULL << 33) | ((uint64_t)payload << 1) | 1ULL`.
- **Bit order**: most-significant first — bit 33 is the start bit, emitted first.
  The loop walks `i` from 0 to 33 and reads `(bits >> (33 - i)) & 1`.
- **Manchester expansion**: a `1` is an active→idle transition in the middle of
  the bit, a `0` is idle→active. So the **first** half of a bit equals the bit
  itself and the **second** half is its negation:
  `out[2*i] = bit; out[2*i+1] = !bit`.

No mutable state, no data structures beyond the local `uint64_t` and the caller's
output buffer.

## Tests

Host suite: `test/test_ot_encode/test_ot_encode.cpp`
(`pio test -e native -f test_ot_encode`).

## Notes

- The 500 µs half-bit and 34 ms frame follow directly from Manchester coding at
  the OpenTherm bit rate: 34 bits → 68 half-bits × 500 µs.
- Logical vs. physical levels: keeping this encoder logical (and applying any
  board output inversion downstream, at the GPIO) keeps the encoder
  board-independent and lets it loop back cleanly against the receive-side
  decoder.
