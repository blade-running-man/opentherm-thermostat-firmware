// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_master.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ot_decode.h"
#include "ot_encode.h"

static const char *TAG = "ot_master";

// ONE timer and ONE period for both phases, and that is deliberate.
//
// Transmission runs in 500 us half-bits, reception in 100 us samples. The temptation
// to switch the alarm period between phases right inside the interrupt handler is
// strong and wrong: 500 divides evenly by 100, so on transmission it is enough to
// change the level every fifth tick. The timer is configured once, the handler does
// not touch the driver configuration, and there is one fewer cause for a race.
#define TICK_US         OT_DECODE_SAMPLE_US                 // 100
#define TICKS_PER_HALF  (OT_ENCODE_HALFBIT_US / TICK_US)    // 5

// Reception budget: OpenTherm gives the slave 800 ms to begin the response, and the
// response itself takes 34 ms. 900 ms covers both with margin and stays well below the
// 1150 ms deadline past which the master counts as having fallen silent.
#define RX_BUDGET_TICKS (9000u)

typedef enum { PH_IDLE = 0, PH_TX, PH_RX } phase_t;

static gpio_num_t       s_in, s_out;
static bool             s_in_inverted, s_out_inverted;
static gptimer_handle_t s_timer;

static volatile phase_t s_phase;
static bool             s_tx[OT_ENCODE_HALFBITS];
static uint16_t         s_tx_i;
static uint8_t          s_half_tick;
static uint16_t         s_rx_tick;
static ot_decode_t      s_dec;
static volatile bool    s_rx_expired;
static bool             s_last_seen;
static TaskHandle_t     s_waiter;

static ot_master_stats_t s_stats;

// Logical level -> physical. Inversion lives exactly here.
static inline void IRAM_ATTR drive(bool active)
{
    gpio_set_level(s_out, s_out_inverted ? !active : active);
}

static inline bool IRAM_ATTR sense(void)
{
    const bool raw = gpio_get_level(s_in) != 0;
    return s_in_inverted ? !raw : raw;
}

// IRAM_ATTR: the handler must execute even when the flash cache is disabled --
// otherwise an NVS write in the middle of a conversation eats a frame. This does not
// cure it fully (the gptimer driver itself is in IRAM only with
// CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM), and the remainder remains an open OTA question.
static bool IRAM_ATTR on_tick(gptimer_handle_t timer,
                              const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)timer; (void)event; (void)arg;
    BaseType_t woken = pdFALSE;

    if (s_phase == PH_TX) {
        if (s_half_tick == 0u) {
            if (s_tx_i >= OT_ENCODE_HALFBITS) {
                // The frame is out. Line to idle, and listen at once: the slave is
                // allowed to answer as early as 20 ms later.
                drive(false);
                ot_decode_reset(&s_dec);
                s_last_seen = sense();
                s_rx_tick   = 0;
                s_phase   = PH_RX;
                return false;
            }
            drive(s_tx[s_tx_i++]);
        }
        s_half_tick = (uint8_t)((s_half_tick + 1u) % TICKS_PER_HALF);
        return false;
    }

    if (s_phase == PH_RX) {
        const bool level = sense();
        if (level != s_last_seen) { s_stats.rx_edges++; s_last_seen = level; }
        const ot_decode_status_t st = ot_decode_push(&s_dec, level);
        if (st == OT_DECODE_DONE || st == OT_DECODE_ERROR) {
            s_phase = PH_IDLE;
            vTaskNotifyGiveFromISR(s_waiter, &woken);
            return woken == pdTRUE;
        }
        if (++s_rx_tick >= RX_BUDGET_TICKS) {
            s_rx_expired = true;
            s_phase      = PH_IDLE;
            vTaskNotifyGiveFromISR(s_waiter, &woken);
            return woken == pdTRUE;
        }
    }
    return false;
}

esp_err_t ot_master_init(const board_t *board)
{
    s_in            = (gpio_num_t)board->ot_in;
    s_out           = (gpio_num_t)board->ot_out;
    s_in_inverted   = board->ot_in_inverted;
    s_out_inverted  = board->ot_out_inverted;

    const gpio_config_t out_cfg = {
        .pin_bit_mask = 1ULL << (unsigned)s_out,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&out_cfg);
    if (err != ESP_OK) return err;
    drive(false);

    // Pull DOWN, not up: the idle state of the OpenTherm line is LOW,
    // and a disconnected adapter then reads as idle rather than as noise that the
    // receive state machine would take for the start of frames.
    const gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << (unsigned)s_in,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&in_cfg);
    if (err != ESP_OK) return err;

    const gptimer_config_t tcfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,   // 1 tick = 1 us
    };
    err = gptimer_new_timer(&tcfg, &s_timer);
    if (err != ESP_OK) return err;

    const gptimer_alarm_config_t alarm = {
        .alarm_count                = TICK_US,
        .reload_count               = 0,
        .flags.auto_reload_on_alarm = true,
    };
    err = gptimer_set_alarm_action(s_timer, &alarm);
    if (err != ESP_OK) return err;

    const gptimer_event_callbacks_t cbs = { .on_alarm = on_tick };
    err = gptimer_register_event_callbacks(s_timer, &cbs, NULL);
    if (err != ESP_OK) return err;

    err = gptimer_enable(s_timer);
    if (err != ESP_OK) return err;

    // A one-off check: a working adapter has NO loop between output and input --
    // transmission changes the voltage on the bus, reception senses current, and
    // without a boiler no current flows. If the input repeats the output, then the two
    // wires are shorted together or swapped on the board. Cheaper to catch here than to
    // guess from silence.
    {
        drive(true);
        esp_rom_delay_us(200);
        const bool follows_active = sense();
        drive(false);
        esp_rom_delay_us(200);
        const bool follows_idle = sense();
        s_stats.pins_shorted = (follows_active && !follows_idle);
        if (s_stats.pins_shorted)
            ESP_LOGE(TAG, "INPUT FOLLOWS OUTPUT: pins shorted or swapped");
    }

    ESP_LOGI(TAG, "in=%d%s out=%d%s, tick %u us", (int)s_in,
             s_in_inverted ? " (inverted)" : "", (int)s_out,
             s_out_inverted ? " (inverted)" : "", (unsigned)TICK_US);
    return ESP_OK;
}

ot_exchange_result_t ot_master_exchange(const ot_frame_t *request, ot_frame_t *response)
{
    ot_encode_frame(ot_frame_encode(request), s_tx);
    s_tx_i       = 0;
    s_half_tick  = 0;
    s_rx_tick    = 0;
    s_rx_expired = false;
    s_waiter     = xTaskGetCurrentTaskHandle();

    // Clear a notification left over from an interrupted conversation: otherwise the
    // next one returns instantly and with someone else's result.
    (void)ulTaskNotifyTake(pdTRUE, 0);

    s_stats.sent++;
    (void)gptimer_set_raw_count(s_timer, 0);
    s_phase = PH_TX;
    (void)gptimer_start(s_timer);

    // The wait ceiling is deliberately larger than the reception budget: we get here
    // only if the handler never came at all -- for example, the timer did not start.
    // Waiting silently forever is not allowed, the bus must keep cycling.
    const uint32_t woken = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));

    (void)gptimer_stop(s_timer);
    s_phase = PH_IDLE;
    drive(false);

    if (woken == 0u) {
        ESP_LOGW(TAG, "timer gave no notification -- conversation did not happen");
        s_stats.timeout++;
        return OT_EXCHANGE_TIMEOUT;
    }
    if (s_rx_expired) {
        s_stats.timeout++;
        return OT_EXCHANGE_TIMEOUT;
    }
    if (s_dec.status != OT_DECODE_DONE) {
        s_stats.frame_error++;
        return OT_EXCHANGE_FRAME_ERROR;
    }
    if (!ot_frame_decode(ot_decode_payload(&s_dec), response)) {
        s_stats.parity_error++;
        return OT_EXCHANGE_PARITY_ERROR;
    }
    s_stats.ok++;
    return OT_EXCHANGE_OK;
}

void ot_master_drive_line(bool active) { drive(active); }

uint8_t ot_master_input_duty(void)
{
    // Sixty-four samples over roughly 2 ms: two bit periods. That is enough to tell a
    // steady level from a switching line, and little enough not to hold up the caller.
    unsigned high = 0;
    for (unsigned i = 0; i < 64u; ++i) {
        if (sense()) high++;
        esp_rom_delay_us(30);
    }
    return (uint8_t)((high * 100u) / 64u);
}

void ot_master_stats(ot_master_stats_t *out) { *out = s_stats; }

const char *ot_exchange_result_str(ot_exchange_result_t r)
{
    switch (r) {
    case OT_EXCHANGE_OK:           return "ok";
    case OT_EXCHANGE_TIMEOUT:      return "timeout";
    case OT_EXCHANGE_FRAME_ERROR:  return "frame";
    case OT_EXCHANGE_PARITY_ERROR: return "parity";
    }
    return "?";
}
