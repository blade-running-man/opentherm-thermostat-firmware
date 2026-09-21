// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "board.h"

// ESP32-C6 SuperMini -- a nameless board built around a bare ESP32-C6FH4 die (4 MB
// flash, no PSRAM), a ceramic antenna, and only the native USB Serial/JTAG with no
// bridge. No official schematic exists for it: the pinout below is cross-checked
// against two independent vendor sources, and everything concerning strapping and USB
// against Espressif documentation, because the secondary sources lie here (mischianti,
// for example, declares GPIO2 a strapping pin, which is nowhere in the Espressif
// documentation).
//
// The DIYLESS shield DOES NOT PLUG IN here: it is D1 mini form factor, and the
// SuperMini has a different header. The connection is four wires to the shield's D1
// (OUT), D2 (IN), 3V3 and GND pads. That is why the pin numbers here are chosen by us
// rather than dictated by the shield's layout.
//
// Why GPIO18 and GPIO19:
//   * adjacent pads on the right-hand row -- two wires side by side, less chance of
//     getting it wrong;
//   * neither is a strapping pin. On the C6 those are 4, 5, 8, 9 and 15 (ESP-IDF, GPIO
//     for esp32c6);
//   * neither is taken by USB. On the C6 the USB Serial/JTAG sits on 12 and 13 -- and
//     this is exactly the trap this file exists for: ON THE C3 THESE SAME 18 AND 19
//     ARE TAKEN BY USB. Copying the numbers from the C3 descriptor would kill the
//     console and flashing;
//   * neither is UART0 (16, 17), nor an LED (8 -- WS2812, 15 -- plain), nor the
//     button (9).
//
// DO NOT carry GPIO8/GPIO10 over here from the LOLIN C3 mini descriptor. There they
// are dictated by the shield's header, here there is no header, and on the C6 GPIO8 is
// at once a strapping pin and the WS2812.
static const board_t k_supermini_c6 = {
    .name = "supermini_c6",

    .ot_in  = 18,
    .ot_out = 19,

    // Inversion is a property of the ADAPTER, not of the board, so it is the same as
    // on the C3: only the output is inverted. If not a single frame decodes on this board
    // -- flip ot_in_inverted first of all, before searching in
    // ot_decode.
    .ot_in_inverted  = false,
    .ot_out_inverted = true,

    // BOOT on GPIO9, which is also the boot-mode-select strapping pin: holding it low
    // at the moment of reset drops the chip into the ROM bootloader.
    .button                   = 9,
    .button_inverted          = true,
    .button_is_download_strap = true,

    // The board's plain LED is GPIO15. That is a strapping pin too (JTAG source
    // select), but it has no internal pull-up, and the LED load does not override the
    // level at reset: as an output it is safe.
    .status_led = 15,
    // WS2812 on GPIO8. At rest it draws current even when "off" -- hence the lower
    // brightness than on the C3.
    .rgb = { .gpio = 8, .brightness = 16 },

    // 1-Wire DS18B20: no DS18B20 wired on the C6 yet; set to the confirmed GPIO when one is
    // added. The DIYLESS shield is D1-mini form factor and does not plug into the SuperMini, so
    // there is no dictated pin, and an unverified number here would only make ds18b20_task
    // warn-spam forever against nothing. -1 makes app_main skip the task (its gate is >= 0).
    .onewire_gpio = -1,

    .net_transport = BOARD_NET_WIFI,
};

const board_t *board_get(void) { return &k_supermini_c6; }
