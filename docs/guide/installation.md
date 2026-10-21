# Installation

The firmware is built from source and flashed over USB with PlatformIO. There is no published
binary and no browser installer: a checkout of the repository, a USB cable and the board are what
an installation takes. Nothing is entered at build time — Wi-Fi, the broker and every other setting
are set in a browser afterwards ([First setup](/guide/first-setup)).

## What you need

| | |
| --- | --- |
| Board | A **LOLIN C3 mini** (main board) or an **ESP32-C6 SuperMini** |
| Adapter | A **DIYLESS OpenTherm master shield** (the ESP8266 thermostat shield, D1-mini form factor) |
| Cable | A USB-C cable that carries data, not only power |
| Boiler | One with an OpenTherm interface — two terminals carrying 15–24 V DC with nothing connected |
| Computer | [PlatformIO Core](https://platformio.org/install/cli) (`pio`), **Node.js 22.6 or newer** with npm, and Git |

Node is not optional: the web interface is compiled into the firmware image, and the firmware build
runs `npm ci` (once) and `npm run build` in `web/` at its configure stage. A build without `npm` on
the path stops with "npm not found". A pre-build hook (`tools/regenerate.py`) also regenerates the
entity registry from `tools/opentherm_ids.py`; it runs under PlatformIO's own Python.

`pio` may not be on your `PATH` after installing PlatformIO; it lives at
`~/.platformio/penv/bin/pio`.

## Building

```sh
git clone https://github.com/blade-running-man/opentherm-thermostat-firmware.git
cd opentherm-thermostat-firmware
pio run -e lolin_c3_mini      # LOLIN C3 mini
pio run -e supermini_c6       # ESP32-C6 SuperMini
```

Each environment builds the binary for one chip; the board descriptor is selected by the chip
(`IDF_TARGET`), so the right GPIO numbers come with the right environment. The platform is pinned
in `platformio.ini` (`pioarduino/platform-espressif32#55.03.311`, which carries ESP-IDF 5.5.5); the
first build downloads that toolchain and takes a while.

::: warning Judge the build by its exit code
`pio run | tail` returns the exit code of `tail`, not of the build, so a failed build looks
successful. Write the output to a file and check `$?`. More in [Building](/development/building).
:::

## Flashing

1. Connect the board to the computer **with the boiler not connected** to the shield (see below for
   why).
2. Run the upload for your board:

   ```sh
   pio run -e lolin_c3_mini -t upload
   pio run -e supermini_c6 -t upload
   ```

   Both chips have USB built in (USB Serial/JTAG, `303a:1001`), so there is no driver to install.
3. Open the console to watch it start:

   ```sh
   pio device monitor -e lolin_c3_mini
   ```

   You should see the version banner, a board line such as
   `board lolin_c3_mini: OpenTherm in=8 out=10, button=9 (download strap: hold AFTER boot only)`,
   `web UI compiled in: … B`, and a warning that the device is serving its own open access point.
   Wrong pin numbers in the board line mean the other environment was flashed.

::: tip If the board is not detected
Put it into the chip's download mode by hand: hold **BOOT**, tap **RESET**, release BOOT, and run
the upload again. On the C3 the download strap also needs GPIO8 — the OpenTherm input — to be high,
and a connected boiler can hold that line low; that is why the first flash is done with the boiler
disconnected. If you must reflash a C3 in place, disconnect the boiler wires for the moment of
reset. The C6's OpenTherm input is on GPIO18, which is not a strapping pin.
:::

The upload writes the bootloader, the partition table and the application. It does not touch the
NVS partition (`0x9000`, 16 KiB) where the settings live, so reflashing keeps Wi-Fi, the password and
the broker. `pio run -e <env> -t erase` wipes the whole flash, settings included.

### ESP32-C6 SuperMini: 4 MB or 8 MB

The SuperMini is sold with 4 MB and with 8 MB of flash under the same name. The image is built for
**4 MB** (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`, `board = esp32-c6-devkitm-1`) and the partition table
ends at `0x3F0000`, so it fits either size: an 8 MB unit runs the same image unchanged and simply
leaves its upper half unused. The opposite mistake is the one to avoid — describing a 4 MB unit as
8 MB (the `esp32-c6-devkitc-1` board definition) overstates the flash.

## Connecting the shield

**LOLIN C3 mini.** The shield plugs onto the board's D1-mini header with no soldering. The firmware
uses header **D1** (GPIO10) for OpenTherm out, **D2** (GPIO8) for OpenTherm in, and **D5** (GPIO1)
for the shield's DS18B20, if one is fitted. Header **D0** (GPIO2) carries a recommended pull-up (the
board's 10 kΩ R4) and should not be driven low at reset; the shield leaves it alone.

**ESP32-C6 SuperMini.** The shield does not fit; wire it with four wires:

```text
Shield (D1 mini header)        ESP32-C6 SuperMini
  D1  (OpenTherm OUT)  ------>  IO19
  D2  (OpenTherm IN)   ------>  IO18
  3V3                  ------>  3V3
  GND                  ------>  GND
```

Do **not** take GPIO numbers from the C3 for the C6 or back: on the C3, GPIO18/19 are the USB
console; on the C6 the USB console is on IO12/IO13, and a stray wire there costs the board its
console and its flashability. No DS18B20 pin is assigned on the C6. Pin tables for both boards:
[Board](/device/board).

## Connecting the boiler

1. **Measure first.** With the shield *not* connected, measure the DC voltage across the boiler's
   OpenTherm terminals. **15–24 V** means OpenTherm. No voltage means these are not OpenTherm
   terminals (perhaps an on/off contact) and the shield will not talk to them.
2. **Remove the old thermostat** from those terminals. This device is the only master on the bus; it
   does not share it.
3. **Connect two wires** from the boiler terminals to the shield's **B1 / B2** screw terminal.
   Polarity does not matter; any two-core cable will do. The bus voltage comes from the boiler — the
   shield's logic runs from the board's 3.3 V and must not be given anything else.
4. **Power the board over USB.**

Within a few seconds the log should report `ot_bus: boiler is answering`. If it reports
`boiler is not answering … polling continues` instead, see
[Troubleshooting](/guide/troubleshooting#no-conversation-with-the-boiler). The device keeps polling
either way and serves its web interface regardless.

::: warning Out of the box it asks for no heat
A fresh install is in local mode, with the heating season off and its own CH switch off, so it asks
the boiler for no central heating until you turn both on. Hot water is allowed by default (the
boiler manages it itself), and no hot-water setpoint is written until somebody sets one.
:::

Where the boiler's own panel parameters matter — the lowest flow temperature it will carry out, for
one — see [Boiler](/boiler/) and the reference unit's profile,
[Intergas Kombi Kompakt HRE](/boiler/intergas-kombi-kompakt-hre).

## Next

[First setup](/guide/first-setup) — put the device on your Wi-Fi.
