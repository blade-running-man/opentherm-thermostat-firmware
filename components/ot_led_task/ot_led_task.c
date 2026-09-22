// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Impure glue for the status LED: the ONE place WS2812/RMT and the source sampling live. Every
// decision (colour, motion, the health ladder) belongs to the pure, host-tested ot_led; this
// file only samples the world once a second, ticks the animation every ~50 ms, and writes the
// single pixel.
//
// READER ONLY. It must never write an OpenTherm frame, take the bus lock, or block on the
// network: the OpenTherm master must never fall silent, because a slave that stops hearing the
// master for >5 s reads it as a shorted thermostat and demands heat. So every source read here
// is a non-blocking snapshot getter (ot_net_get_state, ot_mqtt_link_status,
// ot_thermostat_control_get, ot_state_get) -- none of them dials, waits, or touches the bus.

#include "ot_led_task.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include "ot_led.h"
#include "ot_mqtt_link.h"   // ot_mqtt_link_status(), ot_wire_mqtt_t
#include "ot_net.h"         // ot_net_get_state(), ot_net_has_credentials(), ot_net_state_t
#include "ot_state.h"       // ot_state_get(), ot_value_t, OT_AVAIL_OK
#include "ot_thermostat.h"  // ot_thermostat_control_get() -> ot_api_control_t.state

static const char *TAG = "ot_led";

// The animation tick and the source-sample cadence. The animation must be smooth (breathing, the
// heartbeat blip); the sources change slowly and a 1 Hz sample keeps the reads off the hot path.
#define LED_TICK_MS   50u
#define LED_SAMPLE_MS 1000u

// The RMT device is created ONCE, in ot_led_task_start(), so the boot flash and the task share
// one channel on the one GPIO -- creating a second led_strip on the same pin would fail. File
// scope, not the task arg (which carries the board), because start() owns the flash before the
// task is even scheduled.
static led_strip_handle_t s_strip;

// Dormant in v1: nothing sets this true (no OTA subsystem yet). Atomic because a future OTA path
// on another task will toggle it around its write while this task reads it -- a read-only
// observation that must never alter OTA behaviour. See docs/led-status-indicator.md, "Out of scope".
static _Atomic bool s_ota_active;

void ot_led_note_ota(bool active)
{
    atomic_store(&s_ota_active, active);
}

// Milliseconds from a monotonic source, used for BOTH the animation clock and net_down_since_ms
// so the grace-window arithmetic (now_ms - net_down_since_ms) is over one time base.
static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Create the one WS2812 pixel on the board's LED GPIO over RMT. The GPIO comes from the board
// descriptor and nowhere else (the repo's one rule about pin numbers). WS2812 is GRB; the driver
// reorders from the logical (r,g,b) we hand led_strip_set_pixel(), so ot_led's colours stay RGB.
static esp_err_t led_setup(const board_t *board)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = board->rgb.gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz: fine enough for WS2812 bit timing
        // 48, NOT 64. An RMT channel on the ESP32-C3/C6 owns exactly 48 memory words
        // (SOC_RMT_MEM_WORDS_PER_CHANNEL); asking for 64 forces the driver to span TWO memory
        // blocks, i.e. two of the chip's two TX channels. The DS18B20 (onewire_bus) already holds
        // one RMT TX channel, so a 2-block request leaves no whole TX channel for the strip: the
        // WS2812 device fails to create and the LED stays dark while RMT contention breaks the
        // 1-Wire receive. One pixel is 24 bits; 48 symbols with the copy encoder is ample.
        // DO NOT raise this above 48 -- it re-creates the C3 RMT starvation found on hardware.
        .mem_block_symbols = 48,
        .flags = {
            .with_dma = false,  // one pixel -- DMA would be pure overhead
        },
    };
    return led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
}

// Refresh the slow-moving sample into w's non-time fields. Called once a second; w->now_ms is set
// fresh every tick by the caller, so it is left alone here.
static void led_sample(const board_t *board, ot_led_world_t *w, uint32_t t)
{
    (void)board;

    // Convert ot_net_state_t -> ot_led_net_state_t with an EXPLICIT switch, never a cast: the two
    // enums live in different headers (ot_led owns its own copy so its host tests do not drag in
    // ot_net's impure sources) and nothing enforces that they stay numbered alike. An unknown
    // value fails toward "show a problem" (DOWN), never "connected".
    ot_led_net_state_t led_net;
    switch (ot_net_get_state()) {
    case OT_NET_DOWN:         led_net = OT_LED_NET_DOWN;         break;
    case OT_NET_ACCESS_POINT: led_net = OT_LED_NET_ACCESS_POINT; break;
    case OT_NET_CONNECTED:    led_net = OT_LED_NET_CONNECTED;    break;
    default:                  led_net = OT_LED_NET_DOWN;         break;
    }
    w->net = led_net;

    // net_down_since_ms is a transition STAMP, not now-minus-last: record the instant net left
    // CONNECTED exactly ONCE, and clear it while CONNECTED. DO NOT restamp it every sample -- that
    // would keep pushing the down-since forward so (now - since) never crosses the grace window and
    // WIFI_DOWN never fires (the same "never now-minus-last" rake ot_sensor's accumulator avoids).
    if (led_net == OT_LED_NET_CONNECTED)
        w->net_down_since_ms = 0;
    else if (w->net_down_since_ms == 0)
        w->net_down_since_ms = t;

    w->has_credentials = ot_net_has_credentials();

    // A non-blocking snapshot of the publisher's counters. configured != connected -- do not swap:
    // configured is "a broker host is stored", connected is "the client is up right now".
    ot_wire_mqtt_t m = {0};
    ot_mqtt_link_status(&m);
    w->mqtt_configured = m.configured;
    w->mqtt_connected = m.connected;

    // The current control state as one consistent, task-safe copy. ot_thermostat_control_get() is
    // the only clean read-only getter; the LED must NOT recompute control or call apply()/step().
    ot_thermostat_control_info_t ci;
    ot_thermostat_control_get(&ci);
    w->control = ci.state;

    // Flame on only when the value is genuinely present and true: a STALE or NEVER reading must
    // read false, so the LED shows "idle" (heartbeat), never a phantom "firing" (breathing).
    ot_value_t v;
    w->flame = ot_state_get("flame", &v) && v.availability == OT_AVAIL_OK && v.boolean;

    w->ota_active = atomic_load(&s_ota_active);
}

static void ot_led_task(void *arg)
{
    const board_t *board = (const board_t *)arg;

    ot_led_world_t w = {0};
    uint32_t last_sample = 0;
    bool sampled = false;

    for (;;) {
        uint32_t t = now_ms();

        // Sample the world at ~1 Hz; render at ~20 Hz. The first pass always samples so the very
        // first frame is real, not the zeroed struct.
        if (!sampled || (t - last_sample) >= LED_SAMPLE_MS) {
            led_sample(board, &w, t);
            last_sample = t;
            sampled = true;
        }

        // The animation clock advances every tick even between samples, so breathing and the
        // heartbeat stay smooth. ot_led scales to the board's brightness ceiling.
        w.now_ms = t;
        ot_led_rgb_t c = ot_led_render(&w, board->rgb.brightness);
        led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b);
        led_strip_refresh(s_strip);

        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

esp_err_t ot_led_task_start(const board_t *board)
{
    esp_err_t err = led_setup(board);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }

    // A brief solid-blue boot flash, synchronously on the caller's (app_main's) context BEFORE the
    // low-priority task is scheduled -- so the owner sees the LED is alive at once, even if higher
    // priority tasks keep the LED task off the CPU for a while after boot. Blue at the ceiling.
    led_strip_set_pixel(s_strip, 0, 0, 0, board->rgb.brightness);
    led_strip_refresh(s_strip);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Lowest sensible priority and a small stack: this is cosmetic and must yield to the bus, the
    // network and HTTP. Mirrors the DS18B20 bring-up task in main.cpp.
    BaseType_t ok = xTaskCreate(ot_led_task, "led", 4096, (void *)board, tskIDLE_PRIORITY + 1, NULL);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "status LED task not created");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
