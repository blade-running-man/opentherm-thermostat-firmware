// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>

#include "esp_err.h"

// A single DS18B20 on a 1-Wire bus, read over the RMT peripheral.
//
// WHY RMT and not a bit-bang: this firmware is the OpenTherm master (ot_master), whose
// Manchester encoder ticks a gptimer roughly every 100 us on GPIO8/10. A bit-banged 1-Wire
// reset holds interrupts off for ~480 us and would smear an OT half-bit, corrupting the
// frame the boiler is mid-way through. The managed espressif/onewire_bus driver generates
// every reset/write/read slot as RMT symbols in hardware -- no portENTER_CRITICAL, no CPU
// busy-wait over the line -- so the two buses do not fight for the CPU. This component MUST
// stay on that path; do not replace it with a hand-timed GPIO toggle (see idf_component.yml
// and the decision recorded in the spike commit).
//
// The temperature CRC and decode are the pure ot_onewire_decode component, host-tested; this
// component is the transport only and is NOT host-built.

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle over one RMT-backed 1-Wire bus. Owns a pair of RMT channels until closed.
typedef struct ot_onewire *ot_onewire_handle_t;

// The result of one read: the Celsius value and the raw 9-byte scratchpad it was decoded
// from (kept so the caller can log the ROM-less scratchpad for a bench trace).
typedef struct {
    float   temp_c;
    uint8_t scratchpad[9];
} ot_onewire_reading_t;

// Bring up the RMT 1-Wire bus on `gpio_num`. The internal pull-up is left OFF: the DIYLESS
// shield carries its own ~4.7k pull-up on the DS18B20 line, and the internal one is too weak
// for the bus anyway. On ESP_OK the caller owns *out and must ot_onewire_close() it.
//
// Returns ESP_ERR_NO_MEM if the handle or an RMT channel cannot be allocated, or the driver's
// own error if the RMT channels are exhausted. Failure here must not stop boot or the bus:
// the caller logs it and carries on without a room reading.
esp_err_t ot_onewire_open(int gpio_num, ot_onewire_handle_t *out);

// One temperature read using SKIP_ROM (a single sensor on the bus is assumed): reset, convert,
// wait for the 12-bit conversion, reset, read the scratchpad, check its CRC, decode. Blocks
// the CALLING task for the ~750 ms conversion via vTaskDelay -- run it from a dedicated task,
// never from the bus task.
//
//   ESP_OK              -- *out is a CRC-valid reading.
//   ESP_ERR_NOT_FOUND   -- the reset pulse saw no presence: no sensor on the line.
//   ESP_ERR_INVALID_CRC -- the scratchpad failed its CRC: a wired but unreliable read.
//   other esp_err_t     -- an RMT transaction error from the underlying bus.
esp_err_t ot_onewire_read_temp(ot_onewire_handle_t h, ot_onewire_reading_t *out);

// Release the RMT channels. Safe on NULL.
void ot_onewire_close(ot_onewire_handle_t h);

#ifdef __cplusplus
}
#endif
