# ESP32-C6 SuperMini — hardware reference

A hardware note for the **"ESP32-C6 SuperMini"**, the second target board this firmware runs on
(`platformio.ini` env `supermini_c6`, `IDF_TARGET=esp32c6`). Written from the Espressif ESP32-C6
datasheet (for chip-level facts), two independent vendor pinout pages (for board-level pads), and
this project's own board descriptor `components/board/board_supermini_c6.c`, which is the authority
for the pins actually used.

**Scope.** This documents the board as hardware: the chip inside it, its exposed pads, its
strapping pins and native-USB pins, and how this firmware wires the OpenTherm shield to it. It is
**not** a schematic — the "SuperMini" is a generic, nameless board sold by many vendors with **no
official schematic**, so every board-level pin claim is cross-checked against at least two sources
and every chip-level fact (strapping, USB, ADC) is taken **only** from Espressif. Secondary sources
are known to be wrong on chip facts (e.g. one calls GPIO2 a strapping pin — it is not; see
[Strapping pins](#strapping-pins--boot)).

---

## 1. Overview

The ESP32-C6 SuperMini is a tiny (~22.5 × 18 mm) development board built around Espressif's
**ESP32-C6** RISC-V SoC. It exposes 22 GPIOs on a 2.54 mm dual-row header, is powered and programmed
over a **USB-C** port using the chip's **native USB Serial/JTAG** (no USB-to-UART bridge chip), and
carries an addressable WS2812 RGB LED, a plain status LED, and a BOOT and RESET button. Radios: Wi-Fi
6, Bluetooth 5 LE, and 802.15.4 (Thread/Zigbee).

The specific board this project uses is a bare **ESP32-C6FH4** variant: **4 MB in-package flash, no
PSRAM**, a PCB **ceramic antenna**, and native USB Serial/JTAG only. There is **no *vendor-branded,
versioned* schematic** for the SuperMini family, but an **unbranded schematic sheet is available**
(local copy under `docs/esp32-c6-supermini/`; its title block is an empty placeholder — no vendor
name or revision). The pad→GPIO layout, the LiPo charger and the LED wiring in this doc are read
from that schematic; chip-level facts come from the Espressif datasheet. Clones from different
sellers may still differ, so treat a single vendor page as indicative.

---

## 2. Chip / module specs (ESP32-C6)

All figures from the Espressif ESP32-C6 datasheet.

| Item | Value |
| --- | --- |
| High-performance CPU | 32-bit RISC-V single core, up to **160 MHz** |
| Low-power CPU | 32-bit RISC-V, up to **20 MHz** (LP core, runs while HP core sleeps) |
| HP SRAM | **512 KB** |
| LP SRAM | **16 KB** |
| ROM | **320 KB** |
| Flash | in-package 4 MB or 8 MB (QFN32); off-package up to 16 MB. **This board: 4 MB, no PSRAM** |
| Wi-Fi | **Wi-Fi 6** (802.11ax) in the 2.4 GHz band (also b/g/n) |
| Bluetooth | **Bluetooth 5 (LE)** |
| 802.15.4 | **Thread 1.3** and **Zigbee 3.0** |
| ADC | 12-bit SAR ADC, **up to 7 channels** (ADC1) |
| USB | native **USB Serial/JTAG** controller (CDC + JTAG), no external bridge |
| GPIOs | 30 (QFN40) or **22 (QFN32)** — the SuperMini exposes the 22-GPIO set |

The ESP32-C6 has a single ADC unit (ADC1); ADC channels 0–6 map to GPIO0–GPIO6.

---

## 3. Board specs

| Item | Value | Confidence |
| --- | --- | --- |
| Dimensions | **~22.5 × 18 mm** | both vendor sources agree |
| Header pitch | 2.54 mm, dual-row | both agree |
| Chip | ESP32-C6 (this board: ESP32-C6FH4, 4 MB flash, no PSRAM) | repo-verified |
| Antenna | PCB ceramic antenna | repo-verified |
| USB | USB-C, native USB Serial/JTAG, **no UART bridge** | both agree |
| RGB LED | WS2812 addressable, on **GPIO8** | both agree |
| Status/user LED | plain LED on **GPIO15** | both agree |
| Charge LED | green LED, indicates LiPo charging; not GPIO-controllable | schematic-confirmed |
| BAT pad + LiPo charger | **TP4054** (SOT23-5) LiPo charger + BAT pad | schematic-confirmed |
| BOOT button | **GPIO9** (active low, 10 kΩ pull-up) | schematic-confirmed |
| RESET button | resets the chip (CHIP_EN, 10 kΩ pull-up); no GPIO | schematic-confirmed |
| Power pads | 5V/VBUS, GND, 3V3 (LDO output) | schematic-confirmed |
| USB connector | **USB-C** (16-pin SMD) | schematic-confirmed |
| Logic level | 3.3 V, **not 5 V tolerant** | vendor |

**Reliability caveats of the generic board.** No *vendor-branded* schematic exists (an unbranded
sheet does — see §8), the 3V3 LDO is small (vendor pages cite roughly a few hundred mA), the ceramic
antenna's RF performance varies between clones, and silkscreen/pad layout differs slightly between
sellers. Verify power rails and pad positions with a multimeter before trusting a picture.

---

## 4. Pinout table

Exposed pads with the USB-C port oriented **up**. Left/right rows and GPIO numbers are the intersection
of the two cross-checked vendor sources; where the two disagree or only one lists a pad it is marked.
"Trap"/"safe" is from **this project's** point of view (a safe pin is free of strapping, USB, UART0
and on-board LED/button duties).

### Left row (USB up)

| Pad | GPIO | Function / notes | For this project |
| --- | --- | --- | --- |
| TX | GPIO16 | UART0 U0TXD (default console TX) | avoid (UART0) |
| RX | GPIO17 | UART0 U0RXD (default console RX) | avoid (UART0) |
| 0 | GPIO0 | ADC1_CH0, LP GPIO | safe |
| 1 | GPIO1 | ADC1_CH1, LP GPIO | safe |
| 2 | GPIO2 | ADC1_CH2, LP GPIO. **NOT a strapping pin** (see §5) | safe |
| 3 | GPIO3 | ADC1_CH3, LP GPIO | safe |
| 4 | GPIO4 | ADC1_CH4, **strapping pin** (MTMS/JTAG) | trap (strapping) |
| 5 | GPIO5 | ADC1_CH5, **strapping pin** (MTDI/JTAG) | trap (strapping) |
| 6 | GPIO6 | ADC1_CH6, JTAG (MTCK) | care (JTAG) |
| 7 | GPIO7 | LP GPIO, JTAG (MTDO) | care (JTAG) |

Per the board schematic, header **H1** (left row) is exactly GPIO7, 6, 5, 4, 3, 2, 1, 0, then
U0RXD (GPIO17) and U0TXD (GPIO16). **GPIO21, GPIO22 and GPIO23 are NOT on the pin header** — the
schematic draws them as castellated "Microbit" edge pads marked NC. Earlier vendor images that put
GPIO22/23 on the header were misleading.

### Right row (USB up)

| Pad | GPIO | Function / notes | For this project |
| --- | --- | --- | --- |
| 5V | — | USB 5V in / out | power |
| GND | — | ground | power |
| 3V3 | — | 3V3 LDO output | power |
| 20 | GPIO20 | general IO (often labelled I2C SDA) | safe |
| 19 | GPIO19 | general IO (often labelled I2C SCL) | **used: OpenTherm OUT** |
| 18 | GPIO18 | general IO | **used: OpenTherm IN** |
| 15 | GPIO15 | **status LED**, **strapping pin** (JTAG source select) | on-board LED / strapping |
| 14 | GPIO14 | general IO | safe |
| 9 | GPIO9 | **BOOT button**, **strapping pin** (boot mode) | button |
| 8 | GPIO8 | **WS2812 RGB LED**, **strapping pin** (boot mode) | on-board LED / strapping |

**Note on GPIO12/GPIO13 (native USB).** The schematic wires GPIO12 = USB_D− and GPIO13 = USB_D+
through 22 Ω series resistors to the USB-C connector. On this board they are **castellated "Microbit"
NC pads, not broken out on the 2.54 mm header** — and must not be repurposed (see §6).

---

## 5. Strapping pins & boot

**Authoritative (Espressif ESP32-C6 datasheet) — the strapping pins are exactly:**

```
GPIO4, GPIO5, GPIO8, GPIO9, GPIO15
```

(The datasheet lists them as GPIO8, GPIO9, MTMS, MTDI, GPIO15; on the C6, MTMS = GPIO4 and
MTDI = GPIO5.)

- **GPIO8 + GPIO9** select the boot mode after reset is released. **GPIO9 has an internal pull-up**
  and is the BOOT button: holding it **low at reset** drops the chip into the ROM download bootloader.
- **GPIO15** selects the source of the JTAG signals during early boot. On this board it also drives
  the plain status LED — safe as an output because GPIO15 has **no internal pull-up** and the LED load
  does not override the pin's level at reset.
- **GPIO4, GPIO5** are strapping pins (also JTAG MTMS/MTDI).

**Do NOT trust secondary sources on this.** At least one popular pinout page (mischianti) labels
**GPIO2 a strapping pin** — it is **not** one per Espressif; GPIO2 is a plain ADC-capable IO. Another
(espboards) lists GPIO6/GPIO7 among "strapping" pins — those are JTAG (MTCK/MTDO), not strapping. The
repo's board descriptor carries this exact warning in its header comment.

**Entering download mode:** hold **BOOT (GPIO9)** while pressing/releasing **RESET (EN)**, then release
BOOT. With native USB Serial/JTAG the toolchain can usually reset into the bootloader automatically, but
a held BOOT is the reliable fallback — and is often needed when an external circuit holds a strapping
input at reset.

---

## 6. Native USB

The ESP32-C6's **USB Serial/JTAG** peripheral is wired to the USB-C connector on:

```
GPIO12 = USB_D-      GPIO13 = USB_D+
```

Consequences for this board:

- **No USB-to-UART bridge chip.** The console (CDC) and flashing both run over native USB directly to
  the chip. There is no CP2102/CH340; ESP-IDF's `esp_tinyusb`/USB-Serial-JTAG console is used.
- **GPIO12 and GPIO13 are reserved for USB.** Repurposing either kills the USB console and flashing.
- UART0 (GPIO16/GPIO17) is still available as a *secondary* serial console on the TX/RX header pads if
  wired to an external adapter, but it is not the primary path.

---

## 7. This project's usage

The DIYLess OpenTherm Master Shield (D1-mini form factor) **does not plug into** the SuperMini — the
board has a different header. It is connected with **four flying wires** to the shield's **D1 (OUT)**,
**D2 (IN)**, **3V3** and **GND** pads, so the GPIOs are chosen freely rather than dictated by a header.

Assignments, from `components/board/board_supermini_c6.c` (the single source of truth):

| Function | GPIO | Descriptor field | Notes |
| --- | --- | --- | --- |
| OpenTherm **OUT** (master → boiler) | **GPIO19** | `ot_out`, `ot_out_inverted = true` | only the OUT line is inverted |
| OpenTherm **IN** (boiler → master) | **GPIO18** | `ot_in`, `ot_in_inverted = false` | read as-is |
| BOOT button | **GPIO9** | `button`, `button_inverted = true`, `button_is_download_strap = true` | strapping pin, active low |
| Status LED (plain) | **GPIO15** | `status_led` | strapping pin, safe as output (no pull-up) |
| WS2812 RGB LED | **GPIO8** | `rgb.gpio`, `brightness = 16` | draws current even when "off" → lower brightness than the C3 |
| DS18B20 1-Wire | none | `onewire_gpio = -1` | not wired yet; `-1` makes `app_main` skip the 1-Wire task |
| Net transport | Wi-Fi | `net_transport = BOARD_NET_WIFI` | |

**Why GPIO18 / GPIO19 for OpenTherm:**

- adjacent pads on the right-hand row — two wires side by side, less chance of a mistake;
- neither is a strapping pin (those are 4, 5, 8, 9, 15);
- neither is USB (USB Serial/JTAG is GPIO12/GPIO13);
- neither is UART0 (GPIO16/GPIO17), nor an on-board LED (GPIO8/GPIO15), nor the button (GPIO9).

**The C3 ⇄ C6 pin trap — do NOT copy pin numbers between boards.** On the **LOLIN C3 mini**,
GPIO18 and GPIO19 are the **native USB** pins; on the **ESP32-C6** they are free general IO, and the
C6's USB moved to GPIO12/GPIO13. Copying `18/19` from this C6 descriptor into the C3 descriptor would
**kill the C3 console and flashing**. Equally, do **not** carry the C3's shield-dictated `GPIO8/GPIO10`
here — on the C6, GPIO8 is at once a strapping pin and the WS2812 LED. The inversion (`ot_out_inverted`)
is a property of the *adapter*, not the board, so it matches the C3.

> All GPIO numbers live only in `components/board/`. Every other component reaches these pins through
> `board_get()`. To change wiring, edit the descriptor — nothing else.

**Cross-references:**
- `components/board/board_supermini_c6.c` — the authoritative descriptor (with the same warnings inline).
- `docs/diyless-opentherm-master-shield.md` — the shield this board drives (the flying-wire adaptation
  is documented there too), the OpenTherm line behaviour, and the inversion rationale.
- `components/board/README.md` — the board-descriptor contract.

---

## 8. Sources

**Authoritative (chip-level facts — strapping, USB, ADC, memory, radios):**

- [Espressif ESP32-C6 Datasheet (documentation.espressif.com)](https://documentation.espressif.com/esp32-c6_datasheet_en.html)
- [Espressif ESP32-C6 Datasheet PDF v1.5](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [ESP-IDF GPIO & RTC GPIO — ESP32-C6 (docs.espressif.com)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html)

**Secondary — board-level pads (cross-checked against each other; treated as unreliable on chip-level
strapping/USB claims):**

- [ESP32-C6 SuperMini high-resolution pinout — Mischianti](https://mischianti.org/esp32-c6-supermini-high-resolution-pinout-datasheet-schema-and-specs/) — **note: incorrectly labels GPIO2 a strapping pin.**
- [ESP32-C6 Super Mini pinout & specs — espboards.dev](https://www.espboards.dev/esp32/esp32-c6-super-mini/) — lists GPIO6/GPIO7 among "strapping" pins; those are JTAG, not strapping.
- [ESP32 Strapping Pins guide (ESP32/S3/C3/C6) — espboards.dev](https://www.espboards.dev/blog/esp32-strapping-pins/)

**Local reference files** (checked into `docs/esp32-c6-supermini/`, the primary source for the pad
layout, the charger and the LED wiring):

- `esp32-c6-supermini-schematic.jpg` — the board **schematic** (ESP32-C6FH4, USB-C, TP4054 LiPo
  charger, LDO, WS2812, buttons, antenna matching, headers H1/H2). Unbranded — the title block is a
  blank placeholder, so there is no vendor name or revision.
- `esp32-c6-supermini-pinout.jpg` — the vendor pinout diagram.

**In-repo (the authority for pins actually used):**

- `components/board/board_supermini_c6.c`, `components/board/README.md`
- `docs/diyless-opentherm-master-shield.md`

---

*Resolved since the first draft, by the local board schematic (`docs/esp32-c6-supermini/`):* the
header layout is settled — **H1** (left) = GPIO7…0 + U0RXD/U0TXD; **H2** (right) = 5V, GND, 3V3,
GPIO20, 19, 18, 15, 14, 9, 8. **GPIO12/13 (native USB) and GPIO21/22/23 are castellated "Microbit"
NC pads, not header pins.** The BAT pad + **TP4054** LiPo charger and the green charge LED **are
present** on the unit documented here (no longer "variant-dependent"). Remaining caveat: the
schematic's title block is a blank placeholder (unbranded, no revision), so other SuperMini clones
from different sellers may still differ — verify with a multimeter. The four pads this project uses
(GPIO18, GPIO19, 3V3, GND) are schematic-confirmed and repo-verified.
