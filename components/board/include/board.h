// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The only place in the tree where GPIO numbers are named (acceptance criterion 4).

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_NET_WIFI = 0,
    BOARD_NET_ETH  = 1,
} board_net_transport_t;

typedef struct {
    int8_t  gpio;      // -1 if there is no LED
    uint8_t brightness;
} board_rgb_t;

typedef struct {
    const char *name;

    // --- OpenTherm ---
    // The DIYLESS shield is laid out for the ESP8266 (IN=D2=GPIO4, OUT=D1=GPIO5). On
    // the C3 mini the same header positions are GPIO8 and GPIO10.
    // Taking the numbers straight from the DIYLESS documentation means wiring to the
    // wrong pins.
    int8_t ot_in;
    int8_t ot_out;

    // Inversion is a property of the board, not of the protocol.
    // ot_master normalises the level once; ot_decode knows nothing about the
    // inverter, otherwise the host tests would start checking a property of one
    // particular board.
    bool ot_in_inverted;
    bool ot_out_inverted;

    // --- other peripherals ---
    int8_t button;
    bool   button_inverted;
    bool   button_is_download_strap;

    int8_t      status_led;
    board_rgb_t rgb;

    // 1-Wire bus for the DIYLESS shield's DS18B20. On the C3 mini the sensor sits on
    // shield position D7 = GPIO4 (owner-assigned); GPIO4 is otherwise free (ot_in=8,
    // ot_out=10, button=9, rgb=7). Kept here so the number lives nowhere but board/.
    int8_t onewire_gpio;

    board_net_transport_t net_transport;
} board_t;

// Returns the descriptor of the single board that was built. Never NULL.
// A pointer to a static object: ownership is not transferred, nothing to free.
// Safe from any task, including before the scheduler starts.
const board_t *board_get(void);

#ifdef __cplusplus
}
#endif
