# ot_onewire_decode — pure DS18B20 scratchpad + CRC decode (host-tested)

The framework-free half of a DS18B20 1-Wire temperature read: the Dallas/Maxim CRC-8 and the
temperature decode. No GPIO, no RMT, no ESP-IDF header — so it compiles and runs on the host
exactly as it does on the device.

## Responsibility

Owns:
- The Dallas/Maxim CRC-8 (reflected polynomial `0x8C`, init `0x00`, no final xor).
- The scratchpad integrity check — the single guard against a half-read frame.
- The two's-complement 16-bit temperature register decode (1/16 C units), both raw and as float Celsius.

Does NOT do:
- Any 1-Wire transport: reset pulse, ROM commands, timing, RMT/GPIO. That lives in `ot_onewire.c`,
  which is NOT host-built.
- Any ESP-IDF interaction. It has no `REQUIRES` and pulls in no driver — deliberately, so the host
  suite can link exactly these bytes (see the `DO NOT` notes).

## Public API

`include/ot_onewire_decode.h`, all `extern "C"`:

| Function | Contract |
| --- | --- |
| `uint8_t ot_onewire_crc8(const uint8_t *data, int len)` | Dallas/Maxim CRC-8 over `len` bytes at `data`. Computing over the whole frame (9 scratchpad bytes, or 8 ROM bytes) yields 0 when intact. |
| `bool ot_onewire_scratchpad_crc_ok(const uint8_t scratchpad[9])` | True when byte 8 equals CRC-8 of bytes 0..7. A false means "no trustworthy value", never "the sensor read 0 C". |
| `int16_t ot_onewire_temp_raw(uint8_t lsb, uint8_t msb)` | Raw signed temperature register from byte 0 (LSB) / byte 1 (MSB) as a 16-bit two's-complement count in 1/16 C. Exact, float-free. `lsb`/`msb` passed directly so a test needs no scratchpad array. |
| `float ot_onewire_temp_c(const uint8_t scratchpad[9])` | Scratchpad temperature in degrees Celsius: `ot_onewire_temp_raw(byte0, byte1) / 16.0`. The only place a float appears. |

## Implementation

Single source file `ot_onewire_decode.c`.

- **CRC-8 algorithm**: bit-at-a-time, reflected polynomial. For each byte, xor into `crc`, then eight
  times: if the low bit is set, shift right and xor `0x8C`, else just shift right. Chosen over a
  256-byte table because it runs a handful of times every few seconds — the table would cost more RAM
  than the loop costs cycles.
- **Scratchpad check**: `ot_onewire_crc8(scratchpad, 8) == scratchpad[8]` — CRC over bytes 0..7 compared
  against the sensor's byte 8.
- **Temperature decode**: builds `(msb << 8) | lsb` in a `uint16_t` and reinterprets as `int16_t`, rather
  than sign-extending by hand, so the negative range (-55 C .. +125 C) is exact and portable. Celsius is
  the raw count divided by `16.0f`.

Invariants / `DO NOT`:
- **DO NOT add an ESP-IDF include to the header** or a `REQUIRES` to `CMakeLists.txt`. The moment this
  component reaches `driver/` or `esp_timer.h`, PlatformIO — which compiles ALL the sources of a library
  it pulls in — can no longer link the host suite. This is the recorded `ot_sensor` / `ot_bus_sched` rake.

## Tests

Host suite `test_ot_onewire` (referenced in the header, source, and CMakeLists comments). The exact
byte-for-byte decode lets the negative and edge cases (-55 C .. +125 C) be pinned on the host without a
float delta.

## Notes

- The DS18B20 uses this CRC in scratchpad byte 8 (over bytes 0..7) and in ROM byte 7 (over bytes 0..6).
- The split from `ot_onewire.c` exists purely so the host build never drags in the RMT transport; the
  timing-critical 1-Wire code is device-only.
- `ot_onewire_temp_c` is the only float-producing entry point; callers that log `"%.2f C"` use it.
