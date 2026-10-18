# ot_onewire — RMT-backed 1-Wire transport for the shield's DS18B20

Reads one DS18B20 room/ambient sensor over a 1-Wire bus driven by the ESP32 RMT
peripheral, so the read never fights the OpenTherm master for the CPU.

## Responsibility

Owns:

- The lifecycle of one RMT-backed 1-Wire bus (a pair of RMT channels) behind an
  opaque handle.
- One temperature read of a single DS18B20 using `SKIP_ROM`: reset/presence,
  `CONVERT_T`, the ~750 ms conversion wait, reset, `READ_SCRATCHPAD`, CRC check.
- Distinct error mapping so the caller can tell "no sensor" from "wired but noisy"
  from "bus/RMT fault".

Does NOT:

- Decode the scratchpad. The CRC check and the Celsius conversion are the sibling
  **`ot_onewire_decode`** component (`ot_onewire_scratchpad_crc_ok()`,
  `ot_onewire_temp_c()`) — pure and host-tested. This component is transport only.
- Enumerate ROM codes / address multiple sensors. It assumes exactly one sensor and
  uses `SKIP_ROM`.
- Own a task, retry, average, cache, or publish. The caller runs it from a dedicated
  task and decides what to do with the reading.
- Enable the chip's internal pull-up (the shield carries its own ~4.7 kΩ pull-up).
- Get host-built: it reaches the driver and the managed `onewire_bus`, so it compiles
  only for the board.

## Public API

Header: `include/ot_onewire.h`. All C linkage (`extern "C"`).

| Symbol | Contract |
| --- | --- |
| `ot_onewire_handle_t` | Opaque handle over one RMT-backed 1-Wire bus. Owns a pair of RMT channels until closed. |
| `ot_onewire_reading_t` | `{ float temp_c; uint8_t scratchpad[9]; }` — the decoded Celsius value plus the raw 9-byte scratchpad it came from (kept for bench logging). |
| `esp_err_t ot_onewire_open(int gpio_num, ot_onewire_handle_t *out)` | Bring up the bus on `gpio_num`; internal pull-up left OFF. On `ESP_OK` the caller owns `*out` and must `ot_onewire_close()` it. `ESP_ERR_INVALID_ARG` if `out` NULL or `gpio_num < 0`; `ESP_ERR_NO_MEM` if the handle/RMT channel can't be allocated; otherwise the driver's error. Failure must not stop boot or the bus — the caller logs and carries on without a room reading. |
| `esp_err_t ot_onewire_read_temp(ot_onewire_handle_t h, ot_onewire_reading_t *out)` | One blocking read (see below). `ESP_OK` → `*out` is a CRC-valid reading; `ESP_ERR_NOT_FOUND` → presence pulse saw no sensor; `ESP_ERR_INVALID_CRC` → scratchpad failed CRC (wired but unreliable); other → an RMT transaction error. `ESP_ERR_INVALID_ARG` if `h` or `out` NULL. |
| `void ot_onewire_close(ot_onewire_handle_t h)` | Release the RMT channels and free the handle. Safe on NULL. |

## Implementation

Key files:

- `ot_onewire.c` (~122 lines) — the whole transport.
- `include/ot_onewire.h` — the contract, with the RMT rationale.
- `CMakeLists.txt` — `REQUIRES driver ot_onewire_decode espressif__onewire_bus`
  (the managed dep registers under the double-underscore name `espressif__onewire_bus`,
  which is what `REQUIRES` must name).
- `idf_component.yml` — pulls `espressif/onewire_bus: "~1.1.2"` at cmake-configure time.

Bus / read protocol (`ot_onewire_read_temp`, following the DS18B20 datasheet):

1. `onewire_bus_reset()` — reset + presence. `ESP_ERR_NOT_FOUND` here is passed
   straight up as the "no sensor" answer.
2. `SKIP_ROM` + `CONVERT_T` (`0x44`) via `skip_rom_cmd()`, then
   `vTaskDelay(pdMS_TO_TICKS(800))` to wait out the 12-bit conversion.
3. `onewire_bus_reset()` again, then `SKIP_ROM` + `READ_SCRATCHPAD` (`0xBE`), then
   `onewire_bus_read_bytes()` pulls the 9 scratchpad bytes.
4. `ot_onewire_scratchpad_crc_ok(sp)` — on failure log at DEBUG and return
   `ESP_ERR_INVALID_CRC`; otherwise `memcpy` the scratchpad and set
   `temp_c = ot_onewire_temp_c(sp)`.

Data structures: `struct ot_onewire { onewire_bus_handle_t bus; }` — a thin wrapper
over the managed bus handle, heap-allocated with `calloc`.

Constants:

- `DS18B20_CONVERT_MS 800` — 750 ms worst-case 12-bit conversion from the datasheet
  plus margin. The bus is left idle (released to the external pull-up) during the wait;
  the shield powers the DS18B20 from Vdd, so an idle wait is correct (no parasitic-power
  strong-pull-up needed).
- `OW_MAX_RX_BYTES 10` — RMT rx buffer sized for the 9-byte scratchpad plus framing slack.
- `DS18B20_CMD_CONVERT_T 0x44`, `DS18B20_CMD_READ_SCRATCH 0xBE` — the two function
  opcodes kept locally (ROM commands come from `onewire_cmd.h`), to avoid depending on
  the `espressif/ds18b20` component for just two opcodes.

Invariants / **DO NOT**:

- **DO NOT** replace the RMT path with a hand-timed / bit-banged GPIO toggle. A
  bit-banged 1-Wire reset holds interrupts off for ~480 µs and would smear an
  OpenTherm half-bit (the OT master's Manchester encoder ticks a gptimer ~every 100 µs
  on GPIO8/10). The managed `onewire_bus` RMT driver generates every reset/write/read
  slot as hardware RMT symbols — no `portENTER_CRITICAL`, no CPU busy-wait — so the two
  buses don't fight for the CPU.
- `ot_onewire_read_temp` blocks the calling task ~750 ms via `vTaskDelay` — run it from
  a dedicated task, **never** from the bus task.
- The internal pull-up stays OFF (`en_pull_up` = 0): the shield's external ~4.7 kΩ
  pull-up drives the line, and the internal one is too weak for 1-Wire.
- The CRC is the only guard between a smeared frame and a logged temperature; a
  wired-but-noisy read is rejected, never averaged into a plausible number.

## Tests

No dedicated host suite — this component is transport only and is NOT host-built (it
reaches the driver and the managed `onewire_bus`). The pure CRC/temperature decode is
the sibling `ot_onewire_decode` component, which IS host-built and carries the tests.

## Notes

- The RMT choice is the reason the component exists; the rationale is recorded in
  `ot_onewire.h`, `CMakeLists.txt`, and `idf_component.yml`.
- The managed dependency is version-pinned to a minor range (`~1.1.2`); a floating
  major could move the `onewire_new_bus_rmt` signature out from under `ot_onewire.c`.
- `SKIP_ROM` assumes exactly one sensor on the bus; there is no ROM search here.
- The shield's DS18B20 is meant to serve as a room-temperature source, classified as
  `room` or `ambient` by configuration (default `ambient`, since the device may sit by
  the boiler rather than in a room). That classification and any registry wiring live
  outside this transport component, which only opens the bus and returns one reading.
