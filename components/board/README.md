# board — the single source of GPIO numbers

Board descriptors for the firmware. This is the **only** place in the tree where GPIO
numbers are named: every other component reaches its pins through `board_get()`, never a
literal. One board per chip, selected at build time by `IDF_TARGET`.

## Purpose

Give the rest of the firmware a hardware-neutral view of the board it runs on. A driver that
needs a pin asks the descriptor for it and works unchanged across boards; no GPIO number is
copied into a driver, so moving to another board is a data change in one file, not a hunt
across the tree.

## Responsibility

- **Owns** the physical description of the board the firmware runs on: the OpenTherm
  in/out pins and their inversion, the button, the status LED, the WS2812 RGB LED, the
  1-Wire (DS18B20) pin, and the network transport.
- **Owns** the rule that a GPIO number lives nowhere but here. If a peripheral needs a pin,
  the pin comes from the descriptor, not from a `#define` beside the driver.
- **Does NOT** drive any peripheral, configure any GPIO, or contain any runtime logic — it
  is pure data plus one accessor. It does not touch NVS, the bus, or the network.
- **Does NOT** support more than one board per chip: the build picks a single descriptor from
  `IDF_TARGET` and compiles only that one `.c` file.

## Public API

Declared in `include/board.h`, wrapped in `extern "C"` so C++ callers (e.g. `main.cpp`) can
use it.

| Symbol | Contract |
| --- | --- |
| `const board_t *board_get(void)` | Returns the descriptor of the single board that was built. **Never NULL.** Returns a pointer to a `static const` object — ownership is not transferred, there is nothing to free. Safe from any task, including before the scheduler starts (no locking, no allocation). |
| `board_t` (struct) | The board descriptor. Fields below. |
| `board_rgb_t` (struct) | WS2812 RGB LED: `int8_t gpio` (`-1` if there is no LED) and `uint8_t brightness`. |
| `board_net_transport_t` (enum) | `BOARD_NET_WIFI = 0`, `BOARD_NET_ETH = 1`. |

### `board_t` fields

| Field | Meaning |
| --- | --- |
| `const char *name` | Board name (e.g. `"lolin_c3_mini"`). |
| `int8_t ot_in`, `int8_t ot_out` | OpenTherm receive / transmit GPIO. |
| `bool ot_in_inverted`, `bool ot_out_inverted` | Level inversion. This is a property of the board and its OpenTherm adapter, not of the OpenTherm protocol. The OpenTherm master normalises the line level once, using these flags; the receive decoder never sees the inverter and knows nothing about it, so its logic stays board-independent. |
| `int8_t button`, `bool button_inverted`, `bool button_is_download_strap` | The button pin, its active level, and whether it doubles as the boot-select strapping pin. |
| `int8_t status_led` | Plain status LED GPIO, or `-1` if there is none. |
| `board_rgb_t rgb` | WS2812B RGB LED pin and brightness. |
| `int8_t onewire_gpio` | 1-Wire bus for the DIYLESS shield's DS18B20. `-1` means "not wired"; the DS18B20 task is gated on `>= 0` and is skipped when unset. |
| `board_net_transport_t net_transport` | Wi-Fi or Ethernet. Both current boards use `BOARD_NET_WIFI`. |

## Implementation

Three source files, no headers beyond the public one.

- **`include/board.h`** — the contract: the enums, the two structs, and `board_get()`.
- **`board_lolin_c3_mini.c`** — the descriptor for the LOLIN C3 mini + DIYLESS ESP8266
  Thermostat Shield (target `esp32c3`). A single `static const board_t k_lolin_c3_mini` and a
  `board_get()` that returns its address.
- **`board_supermini_c6.c`** — the descriptor for the ESP32-C6 SuperMini (target `esp32c6`).
  Same shape: one `static const board_t k_supermini_c6` and a `board_get()`.

**Selection (`CMakeLists.txt`).** The board is chosen by `IDF_TARGET`, not by a build flag:
`idf_build_get_property(board_target IDF_TARGET)` then `if/elseif` picks exactly one `.c` file
(`esp32c3` → C3 mini, `esp32c6` → SuperMini). Only the selected file is registered as a
source, so exactly one `board_get()` is linked — no `#if` inside the files, no dead sources in
the tree. The reason it is keyed on the target rather than a `-DBOARD_*` flag: such flags reach
the compiler but not the CMake logic, so branching on them would force `#if` inside the source
files and keep sources in the tree that build into nothing. The `else()` branch is a
**mandatory `FATAL_ERROR`**: without it, an unknown chip would compile an empty component and
fail to link on `board_get()` with a message that hides the cause.

**Key per-board values and why they differ:**

| | LOLIN C3 mini | SuperMini C6 |
| --- | --- | --- |
| `ot_in` / `ot_out` | 8 / 10 (shield header D2 / D1) | 18 / 19 (adjacent right-row pads, four flying wires) |
| `ot_in_inverted` / `ot_out_inverted` | false / true | false / true |
| `button` | 9 (boot strap) | 9 (boot strap) |
| `status_led` | -1 | 15 |
| `rgb` gpio / brightness | 7 / 32 | 8 / 16 |
| `onewire_gpio` | 1 | -1 (none wired yet) |

On the C3 the OpenTherm pins are fixed by the DIYLESS shield's D1-mini header (its D2 and D1
positions land on GPIO8 and GPIO10). The DIYLESS shield does not plug into the SuperMini —
that board has a different header — so the C6 is wired to the shield's D1 (OUT), D2 (IN), 3V3
and GND pads with four flying wires, and its OpenTherm pins are chosen freely: GPIO18 and
GPIO19 are adjacent pads on the right-hand row, and on the C6 neither is a strapping pin, nor
taken by USB, nor UART0, nor an LED, nor the button.

**Invariants / DO NOT constraints:**

- **DO NOT** carry `GPIO8`/`GPIO10` from the C3 descriptor into the C6: on the C3 they are
  dictated by the shield's D1-mini header; the SuperMini has no such header, and on the C6
  `GPIO8` is at once a strapping pin and the WS2812.
- **DO NOT** copy `18`/`19` from the C6 to the C3: on the C6 those are free, but on the C3 they
  are taken by the native USB — copying them would kill the console and flashing. This trap is
  the reason the component exists.
- Only the **output** is inverted on both boards; the input is read as-is (its active level is
  HIGH). `ot_in_inverted = false` is the one descriptor field taken from a reference OpenTherm
  library rather than derived from first principles. It is the most likely thing to be wrong:
  if the receive path decodes not a single frame, flip `ot_in_inverted` first, before hunting a
  bug in the decoder.

## Tests

**No dedicated host suite.** The component is pure static data plus one accessor; there is no
`test/` suite for it. Correctness of the GPIO assignments is a hardware concern, verified on
the boards themselves (the bench-probe notes in the source comments). The project-wide rule
that no GPIO number appears outside this directory is enforced by review (grep), not by a
compiled test.

## Notes

- **The OpenTherm adapter these pins drive** — the DIYLess OpenTherm Master Shield — is
  documented in `docs/diyless-opentherm-master-shield.md`: its form factor, the D1-mini header
  positions the OT lines land on, the boiler (`B1/B2`) and DS18B20 (`T`) terminals, and the
  verified BOM (it is the SMD build of Ihor Melnyk's OpenTherm Adapter). Read it for *why* the
  pin mapping below is what it is.
- **The two boards themselves** are documented in `docs/esp32-c3-mini.md` (LOLIN/WEMOS C3 mini,
  target `esp32c3`) and `docs/esp32-c6-supermini.md` (ESP32-C6 SuperMini, target `esp32c6`):
  chip/board specs, the full pinout with strapping and native-USB traps, and a "this project's
  usage" table that reproduces each descriptor. They anchor on GPIO numbers (the C3's silkscreen
  D-labels are disputed between vendor sources — see that doc).
- **Strapping-pin cautions.** C3: `GPIO8` (`ot_in`) is a strapping pin but does not block
  ordinary boot from flash (that needs GPIO2 = 1 and GPIO9 = 1); the OpenTherm line idles LOW,
  so a connected-but-silent boiler holds `GPIO8` down permanently — booting is unaffected, but
  entering flashing mode with the boiler connected will likely need BOOT held. On both boards
  `GPIO9` (button) is the boot-mode-select strapping pin: holding it low at reset drops the
  chip into the ROM bootloader. C6: `GPIO15` (status LED) is a JTAG-select strapping pin, but
  it has no internal pull-up and the LED load does not override its level at reset, so it is
  safe as an output.
- On the C3, the strapping hazard to watch is GPIO2 (D1-mini header position D0), which must be
  HIGH at reset. The shield does not occupy D0; verify with a multimeter.
- The C3 `onewire_gpio` is **GPIO1** (shield position D5), confirmed by a bench probe; an
  earlier assumption of GPIO4 (position D7) was wrong. The C6 has no DS18B20 wired yet, so its
  `onewire_gpio` is `-1`; set it to a confirmed GPIO if one is added. Leaving an unverified pin
  there would only make the DS18B20 task warn against nothing.
- The C6 SuperMini is a bare ESP32-C6FH4 board (4 MB flash, no PSRAM, native USB Serial/JTAG,
  no bridge) with **no official schematic**. Its pinout was cross-checked against two
  independent vendor sources for the general mapping, and against Espressif documentation for
  everything about strapping and USB, because the secondary sources are unreliable there (one
  wrongly calls GPIO2 a strapping pin — the actual C6 strapping pins are 4, 5, 8, 9 and 15, and
  its USB Serial/JTAG is on GPIO12 and GPIO13).
- The C6 RGB LED (`GPIO8`, WS2812) draws current even when "off", hence its lower default
  brightness (16) than the C3 (32).
- Adding a second board on the same chip is deliberately not supported until such a board
  exists: an explicit selection variable will be added to `CMakeLists.txt` then, and not
  before.
