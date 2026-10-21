# The OpenTherm frame

Everything the firmware says to the boiler, and everything it hears back, is one 32-bit
OpenTherm 2.2 word. This page covers the layout of that word, the parity rule, the eight message
types, and the codecs that turn the 16-bit DATA-VALUE into a number or a flag. The code is
`components/ot_frame/`: two small source files, pure, stateless and allocation-free, safe from
any context including an interrupt. How the word gets onto the wire is on
[Physical layer](/protocol/physical-layer); what the firmware asks and when is on
[The bus](/protocol/bus).

## Layout

On the line a frame is **34 bits**: a start `1`, the 32 data bits, a stop `1`. Only the 32
middle bits live in `ot_frame`; the framing belongs to the Manchester layer (`ot_encode` adds
it, `ot_decode` checks it). The data bits, most significant first:

```text
 31   30..28     27..24   23..16    15..0
 P  | MSG-TYPE | SPARE  | DATA-ID | DATA-VALUE
 1      3         4        8         16
```

| Field | Bits | Notes |
| --- | --- | --- |
| Parity `P` | 31 | makes the number of ones in all 32 bits **even** |
| MSG-TYPE | 30..28 | one of eight values, below |
| SPARE | 27..24 | always zero when the firmware transmits; ignored when parsing, because the specification does not require the slave to send zeros there |
| DATA-ID | 23..16 | the message number; the specification defines 0..127 |
| DATA-VALUE | 15..0 | sixteen bits whose meaning depends on the Data-ID (see [Value codecs](#value-codecs)) |

`ot_frame_t` holds the parsed form: `{ type, data_id, data_value }`.

| Function | Contract |
| --- | --- |
| `ot_frame_encode(f)` | builds the word with a correct parity bit; SPARE is zero by construction, because its bits take no part in the expression |
| `ot_frame_decode(raw, out)` | checks parity **first** and returns `false` without touching `*out` on a mismatch, so a caller can never use half a parse |
| `ot_frame_parity_ok(raw)` | true when the number of ones is even |

Parity is computed by an XOR fold (`>>16, >>8, >>4, >>2, >>1`) rather than
`__builtin_popcount`, which a foreign compiler may not have.

### Why even parity, and not odd

A widely copied third-party OpenTherm library has a helper named `parity` with a comment saying
"odd". Its `isValidResponse`, however, rejects a frame when the number of ones is odd, which is
**even** parity. The name lies and the behaviour is right, so the behaviour is what is matched
here. The host suite `test/test_ot_frame` pins the behaviour, not the name. **Do not** "fix" the
parity to odd on the strength of that comment.

## Message types

| Value | Name | Direction | What the firmware does with it |
| --- | --- | --- | --- |
| `000` | READ-DATA | master → slave | every poll, and every ID 0 status frame |
| `001` | WRITE-DATA | master → slave | every write: ID 1, ID 56, the identity writes, hand writes |
| `010` | INVALID-DATA | master → slave | never sent |
| `011` | reserved | — | accepted while parsing and passed on (below) |
| `100` | READ-ACK | slave → master | a value; availability `ok` |
| `101` | WRITE-ACK | slave → master | the write was taken; availability `ok` |
| `110` | DATA-INVALID | slave → master | "the ID exists, there is no valid data now"; availability `invalid` |
| `111` | UNKNOWN-DATAID | slave → master | "I do not have this ID"; two in a row mark the ID unsupported |

`OT_MSG_RESERVED` (3) is deliberately **not** turned into a parse error. A reply of that type is
the only sign that the slave is not behaving according to the protocol, and rejecting it would
hide exactly that. The state model then takes no value from it (`ot_state_apply_dataid()`
ignores a type it does not expect), but the raw table behind `GET /api/ot/raw` still records it
(see [The bus](/protocol/bus#the-raw-table)).

DATA-INVALID and UNKNOWN-DATAID are different diagnoses, and neither carries data. The
specification does not fix their DATA-VALUE: to a read the slave *may* answer either one with
`0x0000`, and to a write it sends a DATA-VALUE back that it may have modified, so a boiler may
echo the value it was asked to take. The firmware therefore decides on the message type alone and
takes no value from either reply; the one place the value appears is the executor's log line for
a DATA-INVALID answer to ID 1, which prints whatever the boiler sent back as a diagnostic (see
[The bus](/protocol/bus#the-answered-id-1-record)). Collapsing the two would show "no faults" or
"0 °C outside" where the boiler said neither. How the firmware keeps them apart is on
[The bus](/protocol/bus#availability-and-unsupported-ids).

## Value codecs

The sixteen DATA-VALUE bits mean different things for different Data-IDs, and nothing in the
number says which. So every entity in `tools/opentherm_ids.py` names its codec explicitly, and
`components/ot_frame/include/ot_codec.h` supplies the primitives. Which Data-ID uses which codec
is the registry's knowledge, not this component's; [Entities](/reference/entities) lists the
codec of every entity.

| Codec | Function | Meaning |
| --- | --- | --- |
| `f88` | `ot_codec_f88_to_float()` / `ot_codec_float_to_f88()` | **signed** fixed point 8.8, divisor 256 |
| `u16` | the raw word | unsigned 16-bit: counters and hours |
| `s16` | `ot_codec_s16()` | signed 16-bit (the exhaust temperature) |
| `u8_hb`, `u8_lb` | `ot_codec_u8_hb()` / `_lb()` | unsigned high or low byte: two values packed in one frame |
| `s8_hb`, `s8_lb` | `ot_codec_s8_hb()` / `_lb()` | signed high or low byte: the upper and lower bounds of IDs 48 and 49 |
| `flag` | `ot_codec_flag(raw, high_byte, bit)` | one bit (0..7) of the high or low byte; each flag is an entity of its own |
| `none` | — | a synthetic entity with no Data-ID, never decoded from the wire |

Details that carry load:

- **`f8.8` is signed.** The decode casts the word to `int16_t` before dividing. An unsigned parse
  turns −1.0 into +255.996 silently, and the outside and exhaust temperatures can be negative.
- **The `f8.8` encode saturates** at `0x7FFF` / `0x8000` instead of overflowing: a setpoint
  calculation gone absurd should hit the ceiling, not change sign. It rounds with `lrintf`.
- **A non-finite value encodes as `0`.** NaN has no representation in f8.8, and 0 is chosen as
  "off". A NaN that reaches the encoder is already a defect further up: the command layer refuses
  NaN and infinity before it encodes anything (see [Commands](/protocol/commands#bounds)).
- **A flag byte is not shown whole.** Home Assistant cannot show eight meanings in one number, so
  every bit the registry names is a separate binary entity with its own byte and bit.

### Writing is whole-word only

The part-byte codecs (`u8_*`, `s8_*`) describe half of DATA-VALUE, while a write frame carries
all sixteen bits. Assembling the word by taking the other half from the last read would send the
boiler a stale byte disguised as a fresh one, silently. So `ot_command_encode()` encodes only
`f88`, `u16` and `s16`, and refuses a write through any other codec with
`OT_CMD_HALF_WORD_CODEC`. No writable entity has a half-word codec; the refusal keeps one from
being added without that question being answered.

## Tests

`test/test_ot_frame` covers the layout, the parity and the codecs; the codecs are exercised again
by every suite that decodes values (`test_state`, `test_ot_command`). Being an ESP-IDF component
and a PlatformIO library at once, `ot_frame` is compiled unchanged for the host and for the board.
The component's own contract is in
[`components/ot_frame/README.md`](../../components/ot_frame/README.md).
