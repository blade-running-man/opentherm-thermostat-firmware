// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "board.h"

// LOLIN C3 mini + DIYLESS ESP8266 Thermostat Shield.
//
// The mapping of D1 mini header labels onto GPIO numbers is derived from the
// sch_c3_mini_v2.1.0.pdf schematic and the D1 mini v4 header order:
//   D1 (shield: OT OUT) -> GPIO10
//   D2 (shield: OT IN)  -> GPIO8
//
// GPIO8 is a strapping pin, but it does NOT interfere with the ordinary boot from
// flash: that needs GPIO2 = 1 and GPIO9 = 1, and GPIO8 only takes part in entering
// flashing mode. The dangerous pin here is a different one -- GPIO2, also known as
// header position D0, and it must be HIGH at reset. The shield does not occupy D0;
// verify with a multimeter.
//
// At the same time the idle state of the OpenTherm line is LOW, which means a
// connected but silent boiler holds GPIO8 down PERMANENTLY. That does not affect
// booting, but entering flashing mode with the boiler connected will most likely
// require holding BOOT.
static const board_t k_lolin_c3_mini = {
    .name = "lolin_c3_mini",

    .ot_in  = 8,
    .ot_out = 10,

    // ONLY the output is inverted. The input is read as is: the active
    // state of the line is HIGH. The `!readState()` expression in opentherm_library,
    // because of which the first edition of the design put true here, refers to the
    // sampling moment (the bit is taken AFTER the mid-bit transition), not to the
    // polarity.
    //
    // This is the only field of the descriptor derived from someone else's code
    // rather than from the OpenTherm spec. If NOT A SINGLE frame decodes --
    // flip it first of all, before looking for a bug in ot_decode.
    .ot_in_inverted  = false,
    .ot_out_inverted = true,

    // The BOOT button on the C3 mini is GPIO9, which is also the boot-select
    // strapping pin.
    .button                   = 9,
    .button_inverted          = true,
    .button_is_download_strap = true,

    .status_led = -1,
    // The on-board WS2812B is GPIO7, also known as header label D3. The shield does
    // not use D3.
    .rgb = { .gpio = 7, .brightness = 32 },

    // DIYLESS shield DS18B20 on shield position D5 = GPIO1 (confirmed on hardware).
    // Was assumed GPIO4 (position D7); the bench probe placed the DQ line on GPIO1.
    .onewire_gpio = 1,

    .net_transport = BOARD_NET_WIFI,
};

const board_t *board_get(void) { return &k_lolin_c3_mini; }
