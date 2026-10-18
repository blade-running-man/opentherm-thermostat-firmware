# LOLIN (WEMOS) C3 mini — hardware reference

A hardware note for the **LOLIN C3 mini** (WEMOS), an ESP32-C3 development board in the
**WeMos D1 mini form factor**. This is the primary board this firmware runs on (PlatformIO
env `lolin_c3_mini`, IDF target `esp32c3`); the DIYLess OpenTherm Master Shield stacks
directly on its D1-mini header.

**Scope and sourcing.** This doc separates **(a) general vendor/chip facts** — from the WEMOS
product page, the Espressif ESP32-C3 datasheet, and the esptool boot docs — from **(b) how THIS
project uses the board**, which is authoritative because the firmware is flashed and talks to a
real boiler. The full **D-label → GPIO mapping is read from the LOLIN schematic** (§4.2, local copy
under `docs/ESP32-C3_mini/`), which settled an old dispute between community pinouts; the firmware
addresses pins by GPIO number regardless. The single
in-repo source of the pin numbers is `components/board/board_lolin_c3_mini.c`; every other
component reaches pins through `board_get()`. Sources are linked at the end.

---

## 1. Overview

The LOLIN C3 mini is a small (34.3 × 25.4 mm) Wi-Fi + Bluetooth LE development board built on
Espressif's **ESP32-C3** RISC-V single-core SoC, packaged in the **WeMos D1 mini footprint** so
it is mechanically and (mostly) pin-compatible with the large ecosystem of D1-mini shields. It
has **4 MB flash**, a **USB-C** connector wired to the ESP32-C3's **native USB Serial/JTAG**
(no external USB-UART bridge), an on-board **WS2812B addressable RGB LED**, and **RST + BOOT**
buttons. Radio is Wi-Fi 802.11 b/g/n (2.4 GHz) and Bluetooth 5 LE.

One line: *a D1-mini-shaped ESP32-C3 board, 4 MB flash, USB-C with native USB Serial/JTAG,
RGB LED on GPIO7, 11 usable GPIOs on the header.*

---

## 2. Chip / module specs (ESP32-C3)

General facts from the Espressif ESP32-C3 datasheet:

| Item | Value |
| --- | --- |
| Core | 32-bit **RISC-V single-core** (RV32IMC), 4-stage pipeline |
| Clock | up to **160 MHz** |
| SRAM | **400 KB** on-chip (16 KB configurable as cache) |
| ROM | 384 KB |
| Flash | on-board **4 MB** SPI flash (the SoC supports external flash up to 16 MB) |
| RTC memory | 8 KB SRAM |
| Wi-Fi | 802.11 b/g/n, 2.4 GHz |
| Bluetooth | **Bluetooth 5 (LE)**, Bluetooth mesh |
| GPIO | 22 GPIOs total (GPIO0–GPIO21); several used internally for flash |
| ADC | **two 12-bit SAR ADCs**. ADC1 = GPIO0–GPIO4, ADC2 = GPIO5 (ADC2 is unreliable while Wi-Fi is active — a known SoC caveat) |
| USB | integrated **USB Serial/JTAG controller** (CDC-ACM virtual serial + JTAG), on **GPIO18 (D−) / GPIO19 (D+)** |
| Peripherals | SPI, UART ×2, I²C, I²S, RMT (used to drive WS2812B), LEDC/PWM, TWAI (CAN), TWDT |
| Security | secure boot, flash encryption, RSA/AES/SHA/HMAC/DS accelerators |

The on-board SPI flash occupies the internal flash pins (roughly GPIO11–GPIO17 on the
QFN/module); those are **not** broken out to the header.

---

## 3. Board specs (LOLIN C3 mini)

General facts from the WEMOS product page and community teardowns:

| Item | Value |
| --- | --- |
| Dimensions | **34.3 × 25.4 mm**, weight ~2.6 g |
| Form factor | **WeMos D1 mini** (2 × 8-pin headers, 2.54 mm pitch) — pin-compatible with D1-mini shields |
| Flash | 4 MB |
| USB | **USB-C**, wired to the ESP32-C3 **native USB Serial/JTAG** (no CP2104/CH340 bridge) |
| Regulator | **ME6211** LDO (5 V → 3.3 V) |
| Antenna | **ceramic chip antenna** on **v2.1**; the earlier **v1.0** used a PCB trace antenna |
| On-board LED | **1 × WS2812B addressable RGB LED on GPIO7** (v2.1). No separate plain single-colour status LED. (v1.0 had a plain LED on GPIO7 instead.) |
| Buttons | **RST** (reset) and **BOOT** (GPIO9, boot-mode select / download) |
| Usable I/O | **11 GPIOs** broken out to the header |
| Operating voltage | 3.3 V logic; powered from USB-C 5 V or 3V3 pin |

**Version note.** This project targets **v2.1.0** (WS2812B RGB LED + ceramic antenna). On v1.0,
GPIO7 drives a plain LED and the board is otherwise pin-identical. The board descriptor treats
GPIO7 as a WS2812B (`.rgb = { .gpio = 7, ... }`), which is correct for v2.1.

---

## 4. Pinout

### 4.1 GPIO-level function table (authoritative)

This table is keyed by **GPIO number**, which is what the firmware and the datasheet use and
what is cross-verified across sources. Only the GPIOs broken out to the D1-mini header (plus the
USB and button pins) are listed.

| GPIO | ADC | Special / default function | Strapping? | Safe to use? |
| --- | --- | --- | --- | --- |
| GPIO0 | ADC1_CH0 | XTAL_32K_P | no | yes (ADC-capable) |
| GPIO1 | ADC1_CH1 | XTAL_32K_N | no | **yes — used here for DS18B20 1-Wire** |
| GPIO2 | ADC1_CH2 | FSPIQ; **ROM-log enable** | **STRAPPING** | usable, but **must be HIGH at reset** (see §5) |
| GPIO3 | ADC1_CH3 | — | no | yes |
| GPIO4 | ADC1_CH4 | FSPIHD / MTMS (JTAG) | no | yes |
| GPIO5 | ADC2_CH0 | FSPIWP / MTDI (JTAG) | no | usable (ADC2 unreliable with Wi-Fi) |
| GPIO6 | — | FSPICLK / MTCK (JTAG) | no | yes |
| GPIO7 | — | FSPID / MTDO (JTAG) | no | **taken — on-board WS2812B RGB LED** |
| GPIO8 | — | default I²C SDA | **STRAPPING** | **used here for OpenTherm IN** — boot caveat, see §5 |
| GPIO9 | — | — | **STRAPPING** | **BOOT button** (download-mode select) |
| GPIO10 | — | default I²C SCL / FSPICS0 | no | **used here for OpenTherm OUT** |
| GPIO18 | — | **USB D−** (USB Serial/JTAG) | no | **TRAP — native USB; never repurpose** (see §6) |
| GPIO19 | — | **USB D+** (USB Serial/JTAG) | no | **TRAP — native USB; never repurpose** (see §6) |
| GPIO20 | — | **U0RXD** (UART0 RX) | no | UART0 console RX |
| GPIO21 | — | **U0TXD** (UART0 TX) | no | UART0 console TX |

GPIO11–GPIO17 are consumed by the on-module SPI flash and are **not** available.

### 4.2 D1-mini header labels → GPIO (resolved from the LOLIN schematic)

The board carries **D1-mini silkscreen labels** (D0–D8, A0, TX, RX, RST, 3V3, 5V, GND). Community
pinouts historically disagreed about several positions; the mapping below is read **directly from
the LOLIN schematic** (`sch_c3_mini_v2.1.0.pdf` — KiCad `C3_mini.kicad_sch`, local copy under
`docs/ESP32-C3_mini/`), so it is authoritative. Left header **J2**, right header **J3**:

| D1-mini label | Header pin | GPIO / signal | Note |
| --- | --- | --- | --- |
| RST | J2-1 | CHIP_EN | reset |
| A0 | J2-2 | **GPIO3** | ADC1_CH3 |
| D0 | J2-3 | **GPIO2** | strapping; must be HIGH at reset |
| D5 | J2-4 | **GPIO1** | DS18B20 DQ wired here (net SCK) |
| D6 | J2-5 | **GPIO0** | net MISO |
| D7 | J2-6 | **GPIO4** | net MOSI |
| D8 | J2-7 | **GPIO5** | |
| 3V3 | J2-8 | +3V3 | power |
| TX | J3-1 | **GPIO21** | U0TXD |
| RX | J3-2 | **GPIO20** | U0RXD |
| D1 | J3-3 | **GPIO10** | OpenTherm OUT (net SCL) |
| D2 | J3-4 | **GPIO8** | OpenTherm IN (net SDA) |
| D3 | J3-5 | **GPIO7** | WS2812B RGB LED |
| D4 | J3-6 | **GPIO6** | |
| GND | J3-7 | GND | power |
| 5V | J3-8 | VBUS | power |

> **The old third-party disagreement is now settled by the schematic — and the repo descriptor was
> right.** Several community pinouts (RIOT-OS, Mischianti, espboards) were **wrong**: they placed the
> RGB LED (GPIO7) on "D7" and GPIO1 on "D0". The schematic shows **D3 = GPIO7** (the WS2812B),
> **D5 = GPIO1** (where the DS18B20 is bench-probed here), **D7 = GPIO4**, and **D0 = GPIO2** — exactly
> what `board_lolin_c3_mini.c` asserts. **GPIO9 (BOOT)** and **GPIO18/GPIO19 (native USB)** are *not*
> on either header. The firmware still addresses pins by GPIO number; the label column above is for
> reading a shield silkscreen.

---

## 5. Strapping pins & boot

Per the Espressif ESP32-C3 datasheet, the ESP32-C3 has **exactly three strapping pins**, sampled
at reset:

| Pin | Role at reset | Required level for normal boot |
| --- | --- | --- |
| **GPIO2** | must be HIGH; also gated by GPIO8 for the mode decision | **HIGH** |
| **GPIO8** | boot-mode select | **HIGH** for reliable operation |
| **GPIO9** | boot-mode select / download trigger (BOOT button) | **HIGH** (internal pull-up) to boot from flash |

Boot-mode truth:

- **Boot from SPI flash (normal run):** the datasheet requires **GPIO2 = 1** *and* **GPIO8 = 1
  and GPIO9 = 1** at reset (GPIO8 and GPIO9 both high). GPIO2 must be high in both boot modes.
- **Download (serial/USB bootloader):** hold **GPIO9 = 0** (press BOOT) at reset, with GPIO8 = 1.
- The combination **GPIO8 = 0 with GPIO9 = 0 is invalid** and causes undefined behaviour.
- **GPIO2** additionally controls whether the ROM prints its init log to UART, but it must be
  HIGH at reset either way — an external circuit pulling D0/GPIO2 low can prevent boot.

**How to enter download mode:** hold **BOOT (GPIO9)**, tap **RST**, release BOOT. With the USB-C
cable this exposes the native USB bootloader; `esptool` / PlatformIO upload then work normally.
On a fresh/erased chip the USB Serial/JTAG bootloader usually comes up without any button, but
the boiler caveat below often forces the manual sequence.

---

## 6. Native USB (GPIO18 / GPIO19)

The USB-C port is wired directly to the ESP32-C3's **integrated USB Serial/JTAG controller** on
**GPIO18 (D−)** and **GPIO19 (D+)** — there is **no external USB-UART bridge chip**. Implications:

- **GPIO18 and GPIO19 are off-limits for any other use.** By default the SoC enables the USB
  function on these two pins; repurposing them kills the USB console and USB flashing. **This is
  a documented trap in this project**: do NOT copy the ESP32-C6 SuperMini's OpenTherm pins
  (GPIO18/19 on that board) onto the C3 — on the C3 those are USB. (See the C6 descriptor and
  `docs/diyless-opentherm-master-shield.md`.)
- The console/monitor and flashing both ride the native USB CDC-ACM port. A classic UART0
  console is also available on **GPIO21 (TX) / GPIO20 (RX)** if a shield needs the USB pins freed
  — but this project uses native USB.

---

## 7. This project's usage

Authoritative from `components/board/board_lolin_c3_mini.c` (target `esp32c3`), the sole place in
the codebase where GPIO numbers are named. The DIYLess OpenTherm Master Shield stacks on the
D1-mini header; the shield hard-wires OpenTherm **OUT to header position D1** and **IN to header
position D2**, which on this board land on GPIO10 and GPIO8.

| Function | Header pos. | GPIO | Descriptor field | Notes |
| --- | --- | --- | --- | --- |
| OpenTherm **OUT** (master → boiler) | D1 | **GPIO10** | `ot_out = 10`, `ot_out_inverted = true` | only the OUT line is inverted |
| OpenTherm **IN** (boiler → master) | D2 | **GPIO8** | `ot_in = 8`, `ot_in_inverted = false` | read as-is; active level HIGH. If nothing decodes, flip `ot_in_inverted` first |
| DS18B20 1-Wire (shield `T` pad) | D5 | **GPIO1** | `onewire_gpio = 1` | bench-confirmed |
| BOOT button | — | **GPIO9** | `button = 9`, `button_inverted = true`, `button_is_download_strap = true` | active-low; also the download strapping pin |
| WS2812B RGB LED | D3 | **GPIO7** | `rgb = { .gpio = 7, .brightness = 32 }` | on-board; `status_led = -1` (no plain LED) |
| Network transport | — | — | `net_transport = BOARD_NET_WIFI` | Wi-Fi |

**Why D1/D2 land on GPIO10/GPIO8.** The shield fixes OUT→D1 and IN→D2 on the silkscreen; the
LOLIN C3 mini maps header D1→GPIO10 and D2→GPIO8. This pairing is proven correct because the
firmware exchanges OpenTherm frames with a real boiler over exactly these lines.

**The boiler-idle-LOW BOOT caveat.** GPIO8 (OpenTherm IN) is a strapping pin. The OpenTherm line
**idles LOW**, so a connected-but-silent boiler holds GPIO8 down
**permanently**. Ordinary boot from flash is *not* blocked by this (that needs GPIO2 = 1 and
GPIO9 = 1; GPIO8 only participates in *entering* the bootloader), **but entering download/flash
mode with the boiler connected usually requires holding BOOT (GPIO9)** — hold BOOT, tap RST,
release. The pin to watch for a *boot* failure is a different one: **GPIO2 (header position D0)
must be HIGH at reset**; the shield does not occupy D0, but verify with a multimeter if a board
refuses to boot.

**Cross-references in-repo:**
- `components/board/board_lolin_c3_mini.c` — the pin descriptor (authority).
- `components/board/board_supermini_c6.c` — the second target (ESP32-C6; OpenTherm on GPIO18/19
  there, which are USB on the C3 — do not copy across).
- `components/board/README.md` — the board-abstraction contract.
- `docs/diyless-opentherm-master-shield.md` — the shield's electronics and its D1/D2 wiring.
- `docs/firmware-design.md` — header-order derivation, line polarity, and the strapping/idle-LOW
  analysis.

---

## 8. Sources

- WEMOS official product page — LOLIN C3 mini: <https://www.wemos.cc/en/latest/c3/c3_mini.html>
  (form factor, 4 MB flash, USB-C, WS2812B RGB LED, 160 MHz, dimensions, D1-mini compatibility).
- Espressif **ESP32-C3 Series Datasheet**:
  <https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf>
  (RISC-V single-core, 400 KB SRAM, 160 MHz, ADC, USB Serial/JTAG on GPIO18/19, strapping pins).
- Espressif esptool — **Boot Mode Selection, ESP32-C3**:
  <https://docs.espressif.com/projects/esptool/en/latest/esp32c3/advanced-topics/boot-mode-selection.html>
  (GPIO2/8/9 strapping levels, download-mode entry, invalid GPIO8=0/GPIO9=0 combination).
- Espressif **ESP-IDF GPIO reference, ESP32-C3**:
  <https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/peripherals/gpio.html>
- espboards — LOLIN C3 mini specs & GPIO functions:
  <https://www.espboards.dev/esp32/lolin-c3-mini/> (ME6211 regulator, ceramic antenna, GPIO7 RGB,
  default I²C SDA=GPIO8/SCL=GPIO10, UART GPIO20/21, ADC pins).
- RIOT-OS — Wemos ESP32-C3 mini board (a full but **conflicting** D-label table):
  <https://api.riot-os.org/group__boards__esp32c3__wemos__mini.html>
- Mischianti — WeMos LOLIN ESP32 C3 mini v2.1 pinout (another **conflicting** D-label table;
  v2.1 antenna/LED notes): <https://mischianti.org/wemos-lolin-esp32-c3-mini-v2-1-high-resolution-pinout-and-specs/>
- espboards — ESP32 strapping-pins overview: <https://www.espboards.dev/blog/esp32-strapping-pins/>

**Local reference files** (checked into `docs/ESP32-C3_mini/`, the primary source for §4.2):
- `sch_c3_mini_v2.1.0-2.pdf` — the **LOLIN C3 mini schematic** (KiCad `C3_mini.kicad_sch`, A4; the
  header J2/J3 → GPIO mapping in §4.2 is read from it). Title-block Rev field is blank — the
  "v2.1.0" comes from the filename, not the sheet.
- `esp32-c3_datasheet_en-2.pdf` — a local copy of the Espressif ESP32-C3 datasheet.
- `dim_c3_mini_v1.0.0.pdf` — mechanical dimensions.

**In-repo authorities** (override any external source for pin numbers this project uses):
`components/board/board_lolin_c3_mini.c`, `components/board/README.md`,
`docs/diyless-opentherm-master-shield.md`, `docs/firmware-design.md`.

### Facts that could NOT be fully verified

- **The full D-label → GPIO mapping is resolved** from the local LOLIN schematic (§4.2), which
  confirms the board descriptor's D0/D3/D5 assertions against the disputing third-party pinouts.
- The exact **module part number** (bare ESP32-C3 QFN vs. an ESP32-C3-WROOM-0x module) is quoted
  differently by sources; the WEMOS page states "ESP32-C3" and 4 MB flash without naming a module
  variant, so the module marking is left unstated here rather than guessed.
- The schematic's **title block carries no revision** (blank Rev field); "v2.1.0" is taken from the
  PDF filename only.
