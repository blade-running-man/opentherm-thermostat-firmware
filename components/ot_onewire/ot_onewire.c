// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_onewire.h"
#include "ot_onewire_decode.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "onewire_bus.h"
#include "onewire_cmd.h"

static const char *TAG = "ot_onewire";

// DS18B20 function commands (the ROM commands live in onewire_cmd.h). Kept here rather than
// pulled from the espressif/ds18b20 component: we only need two opcodes, and this keeps the
// dependency to the transport (onewire_bus) alone.
#define DS18B20_CMD_CONVERT_T     0x44
#define DS18B20_CMD_READ_SCRATCH  0xBE

// 750 ms is the worst-case 12-bit conversion time from the datasheet; a small margin on top
// because the sensor may run its parasitic-power timing slightly long. The bus is left idle
// (released to the pull-up) during the wait -- parasitic power would need a strong pull-up
// held high here, but the DIYLESS shield powers the DS18B20 from Vdd, so an idle wait is right.
#define DS18B20_CONVERT_MS        800

// The largest single receive is the 9-byte scratchpad; size the RMT rx buffer for it with a
// little slack for the framing symbols.
#define OW_MAX_RX_BYTES           10

struct ot_onewire {
    onewire_bus_handle_t bus;
};

esp_err_t ot_onewire_open(int gpio_num, ot_onewire_handle_t *out)
{
    if (!out || gpio_num < 0)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;

    struct ot_onewire *h = calloc(1, sizeof(*h));
    if (!h)
        return ESP_ERR_NO_MEM;

    // en_pull_up left 0: the shield's external ~4.7k pull-up drives the line; the chip's
    // internal pull-up is both too weak for 1-Wire and, on the C3, an extra thing to get
    // wrong. See ot_onewire.h.
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = gpio_num,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = OW_MAX_RX_BYTES,
    };

    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &h->bus);
    if (err != ESP_OK) {
        free(h);
        return err;
    }
    *out = h;
    return ESP_OK;
}

// Issue SKIP_ROM followed by one function command on an already-reset bus.
static esp_err_t skip_rom_cmd(onewire_bus_handle_t bus, uint8_t cmd)
{
    uint8_t tx[2] = { ONEWIRE_CMD_SKIP_ROM, cmd };
    return onewire_bus_write_bytes(bus, tx, sizeof(tx));
}

esp_err_t ot_onewire_read_temp(ot_onewire_handle_t h, ot_onewire_reading_t *out)
{
    if (!h || !out)
        return ESP_ERR_INVALID_ARG;

    // 1. Reset + presence. ESP_ERR_NOT_FOUND here is the "no sensor" answer, passed straight
    //    up so the caller can log a distinct warning rather than a generic failure.
    esp_err_t err = onewire_bus_reset(h->bus);
    if (err != ESP_OK)
        return err;

    // 2. SKIP_ROM + CONVERT_T, then wait out the conversion.
    err = skip_rom_cmd(h->bus, DS18B20_CMD_CONVERT_T);
    if (err != ESP_OK)
        return err;
    vTaskDelay(pdMS_TO_TICKS(DS18B20_CONVERT_MS));

    // 3. Reset again, then SKIP_ROM + READ_SCRATCHPAD and pull the 9 bytes.
    err = onewire_bus_reset(h->bus);
    if (err != ESP_OK)
        return err;
    err = skip_rom_cmd(h->bus, DS18B20_CMD_READ_SCRATCH);
    if (err != ESP_OK)
        return err;

    uint8_t sp[9];
    err = onewire_bus_read_bytes(h->bus, sp, sizeof(sp));
    if (err != ESP_OK)
        return err;

    // 4. The CRC is the only thing standing between a smeared frame and a logged temperature.
    //    A wired-but-noisy read must be rejected here, not averaged into a plausible number.
    if (!ot_onewire_scratchpad_crc_ok(sp)) {
        ESP_LOGD(TAG, "scratchpad CRC mismatch (%02x %02x .. %02x)", sp[0], sp[1], sp[8]);
        return ESP_ERR_INVALID_CRC;
    }

    memcpy(out->scratchpad, sp, sizeof(sp));
    out->temp_c = ot_onewire_temp_c(sp);
    return ESP_OK;
}

void ot_onewire_close(ot_onewire_handle_t h)
{
    if (!h)
        return;
    if (h->bus)
        onewire_bus_del(h->bus);
    free(h);
}
