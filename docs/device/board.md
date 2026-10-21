# Board

`components/board/` is the board boundary. It is the only place in the firmware where a GPIO
number is written; every other component receives its pins, polarities and transport from a
`board_t` handed to it at start-up. The firmware ships for two boards, one per chip: the
**LOLIN C3 mini** (ESP32-C3, the primary target, with the DIYLESS shield plugged straight in) and
the **ESP32-C6 SuperMini** (the secondary target, with the shield on four flying wires). This page
covers the descriptor, the two pinouts, the traps they exist to prevent, the DS18B20 line, and the
platform pin and partition table every build relies on.

Datasheet-level detail lives in the reference section:
[LOLIN C3 mini](/reference/esp32-c3-mini), [ESP32-C6 SuperMini](/reference/esp32-c6-supermini)
and the [DIYLESS OpenTherm master shield](/reference/diyless-opentherm-master-shield).

## The two boards

| | LOLIN C3 mini | ESP32-C6 SuperMini |
| --- | --- | --- |
| Chip | ESP32-C3FH4: single-core RISC-V 160 MHz, 4 MB flash, no PSRAM, 400 KB SRAM | ESP32-C6FH4: 4 MB flash, no PSRAM |
| Role | primary; hardware acceptance runs here | secondary; must build and be checked separately |
| Shield | plugs directly into the D1-mini header | does **not** plug in; wired OT OUT, OT IN, 3V3, GND (plus DQ for a DS18B20) |
| PlatformIO env | `lolin_c3_mini` (`board = lolin_c3_mini`) | `supermini_c6` (`board = esp32-c6-devkitm-1`) |
| Console | native USB Serial/JTAG on GPIO18/19 | native USB Serial/JTAG on GPIO12/13 |

"One universal binary" means one binary **per chip**, not one for everything: the image header
carries the chip id, so a C3 image cannot run on a C6. Both boards are configured identically at
runtime.

The C6 environment uses `esp32-c6-devkitm-1` because the platform catalogue has no SuperMini
definition, and **devkitm-1, not devkitc-1**: the two board files are byte-for-byte identical
except the flash size (4 MB against 8 MB), and the wrong one silently overstates the flash and
breaks the partition table. The SuperMini is also sold with 8 MB under the same name; check
`esptool flash_id` on the unit before the first flash.

All I/O is 3.3 V and not 5 V tolerant. The shield's logic is powered from the board's 3.3 V; the
OpenTherm bus voltage (15–24 V) comes from the boiler, and the shield cannot supply it.

## Selection by `IDF_TARGET`

`components/board/CMakeLists.txt` reads `IDF_TARGET` and registers exactly one descriptor file:

| `IDF_TARGET` | Source registered |
| --- | --- |
| `esp32c3` | `board_lolin_c3_mini.c` |
| `esp32c6` | `board_supermini_c6.c` |
| anything else | `FATAL_ERROR` at configure time |

Only the selected file is compiled, so exactly one `board_get()` is linked, with no `#if` inside
the sources and no file in the tree that builds into nothing.

### Why not a build flag

A `-DBOARD_*` flag from `platformio.ini` reaches the compiler but not the CMake logic, so branching
on it would mean `#if` inside the descriptor files. CMake already knows the target. The cost is
stated in the CMake file: exactly **one board per chip**. A second board on the same chip gets an
explicit selection variable when such a board exists, not before. The `else()` must stay a
`FATAL_ERROR`: without it an unknown chip compiles an empty component and fails to link on
`board_get()` with a message that hides the cause.

## The descriptor

`include/board.h` declares `board_t`, `board_rgb_t`, `board_net_transport_t` and
`const board_t *board_get(void)`. `board_get()` never returns NULL; it returns a pointer to a
`static const` object (nothing to free, no locking, no allocation) and is safe from any task,
including before the scheduler starts.

| Field | Meaning |
| --- | --- |
| `name` | board name, `"lolin_c3_mini"` or `"supermini_c6"` |
| `ot_in`, `ot_out` | OpenTherm receive and transmit GPIO |
| `ot_in_inverted`, `ot_out_inverted` | level inversion of the **adapter**; `ot_master` normalises the line once, so `ot_decode` never sees the inverter |
| `button`, `button_inverted`, `button_is_download_strap` | the BOOT button, its active level, and whether it is the ROM download strap |
| `status_led` | a plain LED GPIO, or `-1` |
| `rgb` (`board_rgb_t`) | the WS2812 data GPIO (`-1` for none) and its brightness ceiling |
| `onewire_gpio` | the DS18B20 1-Wire line, or `-1` when none is wired |
| `net_transport` | `BOARD_NET_WIFI` or `BOARD_NET_ETH`; both boards use Wi-Fi |

### Who reads it

`app_main` (`src/main.cpp`) calls `board_get()` once and hands the pointer down; no other
component calls it.

| Consumer | Fields it reads |
| --- | --- |
| `src/main.cpp` | `name`, `ot_in`, `ot_out`, `button`, `button_is_download_strap` (one boot log line); `onewire_gpio` (starts the DS18B20 task when `>= 0`); `rgb.gpio` (starts the LED task when `>= 0`) |
| `ot_master` (`ot_master_init(board)`) | `ot_in`, `ot_out`, `ot_in_inverted`, `ot_out_inverted` |
| `ot_net` (`ot_net_start(board, …)`) | `net_transport`, `name` (log) |
| `ot_led_task` (`ot_led_task_start(board)`) | `rgb.gpio`, `rgb.brightness` |

`button_inverted` and `status_led` are described but read by nothing: no component drives
the button or the plain LED. A non-Wi-Fi `net_transport` makes `ot_net_start()` refuse with
`ESP_ERR_NOT_SUPPORTED`; the bus keeps running, because nothing in `app_main` stops on a failed
network start.

## Pinout

Inversion belongs to the adapter, so it is the same on both boards: **only the output is
inverted** (`ot_out_inverted = true`, `ot_in_inverted = false`). The active line state reads HIGH,
rest reads LOW, and `ot_master` enables an internal **pull-down** on the input, so a disconnected
adapter reads as rest rather than as noise the decoder could mistake for frames.

::: warning `ot_in_inverted` is borrowed, not derived
It is the one descriptor field taken from another project's code rather than from the OpenTherm
specification. (The reference library's `!readState()` is about the sampling moment after the
mid-bit transition, not about polarity.) **If not a single frame decodes, flip `ot_in_inverted`
first**, before looking for a bug in `ot_decode`.
:::

### LOLIN C3 mini (`board_lolin_c3_mini.c`)

| Function | GPIO | Header / note |
| --- | --- | --- |
| OpenTherm IN (`ot_in`) | **8** | D2; strapping pin, but does not block a normal boot from flash |
| OpenTherm OUT (`ot_out`) | **10** | D1 |
| BOOT button (`button`) | 9 | active low; the ROM download strap |
| RGB LED (`rgb`) | 7 | D3; WS2812B, brightness ceiling 32 |
| Plain LED (`status_led`) | −1 | none |
| DS18B20 (`onewire_gpio`) | **1** | shield position D5 |
| USB Serial/JTAG console | 18 / 19 | not on the header |

The shield is laid out for the ESP8266 D1 mini (IN = D2 = GPIO4, OUT = D1 = GPIO5 there). On the
C3 mini the same header positions are GPIO8 and GPIO10; taking the numbers straight from the
DIYLESS documentation wires to the wrong pins.

### ESP32-C6 SuperMini (`board_supermini_c6.c`)

| Function | GPIO | Note |
| --- | --- | --- |
| OpenTherm IN (`ot_in`) | **18** | chosen; a flying wire to the shield's D2 pad |
| OpenTherm OUT (`ot_out`) | **19** | adjacent pad; a flying wire to the shield's D1 pad |
| BOOT button (`button`) | 9 | active low; the ROM download strap |
| RGB LED (`rgb`) | 8 | WS2812, brightness ceiling 16 (it draws current even when "off") |
| Plain LED (`status_led`) | 15 | a strapping pin, but safe as an output (no internal pull-up; the LED does not override the level at reset) |
| DS18B20 (`onewire_gpio`) | −1 | none wired; the DS18B20 task is not started |
| USB Serial/JTAG console | 12 / 13 | solder pads on the board face, not on the pin rows |
| UART0 | 16 / 17 | reserved |

The C6 has no shield header, so its OpenTherm pins are free choices: two adjacent pads on the
right-hand row that are neither strapping (4, 5, 8, 9, 15), nor USB (12, 13), nor UART0 (16, 17),
nor an LED, nor the button. The board has no official schematic; its pinout was cross-checked
against two vendor sources, and everything about strapping and USB against Espressif's own
documentation, because secondary sources are wrong there (one calls GPIO2 a strapping pin).

## The USB / GPIO trap

This is the reason the per-board descriptors exist. **On the C3, GPIO18 and GPIO19 are the USB
console; on the C6 those same pins are free, and USB moved to GPIO12/13.** Copying the C6's
OpenTherm pins onto the C3 kills the console and flashing. Copying the C3's 8/10 onto the C6 is
also wrong: there they were dictated by the shield header, which the C6 does not have, and on the
C6 GPIO8 is both a strapping pin and the WS2812.

The rule that follows is load-bearing: **no GPIO number outside `components/board/`**. Nothing
automated checks it; a `grep` gives candidates and each is checked by eye to come from
`board_get()`.

### Strapping on the C3

- **GPIO2** (header D0) does not select the boot mode, but the datasheet recommends a pull-up
  against glitches; the board has a 10 kΩ pull-up (R4). Keep it from being driven low at reset.
  The shield does not occupy D0; verify with a multimeter.
- **GPIO8** (`ot_in`) does not block a normal boot (that is selected by GPIO9 = 1), but the
  OpenTherm line idles LOW, so a connected, silent boiler holds GPIO8 low permanently. Booting is
  unaffected; **entering flashing mode with the boiler connected may need BOOT held** (or the
  boiler briefly disconnected).
- **GPIO9** (BOOT, both boards) held low at reset drops the chip into the ROM bootloader. The boot
  log line says so: `(download strap: hold AFTER boot only)`.

## The DS18B20 line

The shield has a footprint for a DS18B20, populated on the owner's unit and carrying its own
~4.7 kΩ pull-up (the internal pull-up stays off). On the C3 the data line is **GPIO1** (shield
position D5), placed by a bench probe. The C6 has no sensor wired, so its `onewire_gpio` is `-1`:
an unverified number would only make the DS18B20 task warn forever against nothing.

When `onewire_gpio >= 0`, `app_main` starts the `ds18b20` task after HTTP, just before the LED
task, at the lowest priority. It reads the sensor over RMT about every 3 s and hands the value to
the room-source registry as slot 0, an `ambient` source. See [Room sources](/device/room-sources) for what happens next.

## Platform pin

`platformio.ini` pins the platform for both board environments:

```ini
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.311
framework = espidf
```

The pin carries ESP-IDF 5.5.5. A bare git URL floats, and a floating URL once moved the toolchain
out from under a design verified against a different tree; move the pin deliberately and say so.
There is no `framework = arduino` in any environment; the word `arduino` appears only inside the
`pioarduino` vendor URL and comments. Both environments run `tools/regenerate.py` as a pre-build
script (the entity registry is generated before every firmware build) and set
`test_ignore = *`, because the host suites belong to `[env:native]`.

`sdkconfig.defaults` selects the custom partition table, 4 MB flash, and the USB Serial/JTAG
console (the C3 mini has no UART bridge; without that line the console is silent and the board
looks dead although it runs).

## Partition table

`partitions.csv` holds two OTA application slots and no filesystem; the web UI is compiled into
the application (`components/web_assets`, `EMBED_FILES`).

| Name | Type / subtype | Offset | Size |
| --- | --- | --- | --- |
| `nvs` | data / nvs | `0x9000` | `0x4000` (16 KiB) |
| `otadata` | data / ota | `0xd000` | `0x2000` (8 KiB) |
| `phy_init` | data / phy | `0xf000` | `0x1000` (4 KiB) |
| `ota_0` | app / ota_0 | **`0x10000`** | `0x1F0000` (1984 KiB) |
| `ota_1` | app / ota_1 | `0x200000` | `0x1F0000` (1984 KiB) |

The top 64 KiB of the 4 MB flash is unused.

::: warning `ota_0` must stay at `0x10000`
The stock two-OTA table gives `nvs` 24 KiB (`0x6000`), which pushes the first application slot to
`0x20000` once aligned. PlatformIO assembles `firmware.factory.bin` with the application at
`0x10000` regardless, and the merge then aborts with an overlap at `0x10000`. `nvs` is 16 KiB here
for exactly that reason, and the slots grow to 1984 KiB each (against the stock 1700 KiB) from
the space that starting at `0x10000` leaves.
:::

The second slot exists, but the firmware has no over-the-air update path; see
[OTA and system](/device/ota-and-system).

## Adding a board

1. A descriptor `board_<name>.c` in `components/board/`, defining its own `board_t` and
   `board_get()`, with every pin choice explained beside it.
2. A branch in `components/board/CMakeLists.txt`. A second board on an existing chip needs the
   explicit selection variable described above.
3. A PlatformIO environment with the right `board =` (the flash size matters) and the same
   platform pin.
4. A check that `partitions.csv` still fits the flash.

Nothing above the board boundary changes: the protocol, state, executor, MQTT, HTTP and LED
logic receive their pins through the descriptor and contain no pin number.
