# ot_frame — the OpenTherm frame and the DATA-VALUE codecs (pure)

The lowest data layer of the firmware: it turns the 32 data bits of an OpenTherm
2.2 frame into a struct and back, and it interprets the 16-bit DATA-VALUE field in
each of the encodings the entity registry uses. Everything here is pure, stateless and
allocation-free, so it is safe from any context, including an ISR.

## Responsibility

**Owns:**
- The layout of the 32 middle bits of an OpenTherm frame — parity, MSG-TYPE,
  SPARE, DATA-ID, DATA-VALUE — and the even-parity rule. On the wire a frame is 34
  bits: a start `1`, the 32 data bits, a stop `1`. The bit layout of the 32 data bits
  (MSB first) is: bit 31 parity; bits 30..28 MSG-TYPE; bits 27..24 SPARE; bits 23..16
  DATA-ID; bits 15..0 DATA-VALUE.
- Encode/decode between a `uint32_t` word and `ot_frame_t`.
- The DATA-VALUE codecs: `f8.8`, signed 16-bit, unsigned/signed high and low byte,
  and single flag bits.

**Does NOT do:**
- Framing — the start `1`, the stop `1` and the Manchester line coding belong to the
  `ot_decode` component. Only the 32 data bits live here.
- Any knowledge of which DATA-ID uses which codec — that mapping is the entity
  registry's (`ot_registry` and its generated tables); this component just supplies
  the codec primitives every entity names.
- I/O, state, timing, allocation.

## Public API

### `ot_frame.h` — the frame

| Symbol | Contract |
| --- | --- |
| `ot_msg_type_t` | The eight MSG-TYPE values (`OT_MSG_READ_DATA=0`, `WRITE_DATA=1`, `INVALID_DATA=2`, `RESERVED=3`, `READ_ACK=4`, `WRITE_ACK=5`, `DATA_INVALID=6`, `UNKNOWN_DATAID=7`). `OT_MSG_RESERVED = 3` is accepted while parsing and passed to the caller (not turned into an error), because it is the only sign a slave is off-spec. |
| `ot_frame_t` | `{ ot_msg_type_t type; uint8_t data_id; uint16_t data_value; }`. |
| `bool ot_frame_parity_ok(uint32_t raw)` | True when the number of one-bits over all 32 bits is even (EVEN parity). |
| `uint32_t ot_frame_encode(const ot_frame_t *f)` | Assembles a word with a correct parity bit; SPARE is always zero. |
| `bool ot_frame_decode(uint32_t raw, ot_frame_t *out)` | Parses a word. Returns false and leaves `*out` untouched on wrong parity. `out` may not be NULL. |

### `ot_codec.h` — the DATA-VALUE codecs

| Symbol | Contract |
| --- | --- |
| `float ot_codec_f88_to_float(uint16_t raw)` | `f8.8` → float. SIGNED fixed point, divisor 256. |
| `uint16_t ot_codec_float_to_f88(float value)` | float → `f8.8`. Saturates at the int16 bounds (`0x7FFF` / `0x8000`); a non-finite value returns 0. |
| `int16_t ot_codec_s16(uint16_t raw)` | Reinterpret the word as signed 16-bit. |
| `uint8_t ot_codec_u8_hb(uint16_t raw)` / `_lb` | Unsigned high / low byte. |
| `int8_t ot_codec_s8_hb(uint16_t raw)` / `_lb` | Signed high / low byte. |
| `bool ot_codec_flag(uint16_t raw, bool high_byte, uint8_t bit)` | One flag bit; `bit` is 0..7 within the selected byte (masked with `& 7`). |

## Implementation

Two source files, both trivial and branch-light:

- **`ot_frame.c`** — bit layout via the `OT_*` masks/shifts at the top
  (`OT_PARITY_BIT 0x80000000`, type at shift 28 / mask 0x7, id at shift 16 / mask
  0xFF, value mask 0xFFFF, spare mask 0x0F000000). Parity is computed by
  `even_ones()`, an XOR fold (`>>16, >>8, >>4, >>2, >>1`) that returns true when the
  popcount is even. The fold is used deliberately instead of `__builtin_popcount`,
  which a foreign compiler may lack. `ot_frame_encode()` builds the word from
  `type | id | value` — SPARE stays zero because its four bits take no part in the
  expression — then sets the parity bit only if the word is not already even.
  `ot_frame_decode()` checks parity first and returns false before writing anything,
  so a caller can never use half a parse; SPARE is ignored on read because the
  specification does not require zeros there from the slave.

- **`ot_codec.c`** — pure arithmetic over the 16-bit value. `f8.8` decode casts to
  `int16_t` before the divide (signedness is load-bearing: an unsigned parse turns
  −1.0 into +255.996 silently, which matters for the outside and exhaust
  temperatures, which can be negative). Encode multiplies by 256, saturates to
  `0x7FFF` / `0x8000` at the int16 bounds, and rounds with `lrintf`. Byte extractors
  are plain shifts/masks with the signed variants routed through `uint8_t` first.

**Invariants / DO NOT:**
- Parity is EVEN, not odd. DO NOT "fix" it to odd on the strength of the third-party
  `opentherm_library`: its helper is named `parity` and commented "odd", but its
  `isValidResponse` actually rejects a frame when the number of ones is odd — i.e. it
  implements even parity. The name lies, the behaviour is right; the behaviour is what
  is matched here.
- `ot_frame_decode` must not touch `*out` when parity is wrong.
- A non-finite float into `ot_codec_float_to_f88` returns 0, which is deliberately
  "off" (0 °C). A NaN reaching this function is already a defect upstream; the caller
  must not let it get here.

## Tests

The component has no host suite of its own inside this directory. Because it is a pure
ESP-IDF component and PlatformIO library, any host suite in another directory that
exercises it compiles these same sources unchanged, so the device and the tests run
identical code.

## Notes

- `OT_MSG_RESERVED` is intentionally surfaced rather than rejected — losing it would
  hide a slave misbehaving against the specification.
- `CMakeLists.txt` registers both sources (`ot_frame.c`, `ot_codec.c`) with `include/`
  as the public include dir, and declares no dependencies — matching the pure,
  self-contained nature of the code.
</content>
</invoke>
