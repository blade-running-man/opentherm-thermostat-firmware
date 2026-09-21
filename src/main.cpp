// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The entry point. It installs the log, opens NVS, brings up the OpenTherm bus, then the
// network, then the web UI -- and nothing else: every decision this device makes lives in
// a component, and app_main only puts them in order.
//
// That order carries load, and each step of it is a rule rather than a habit:
//
//   * The log is installed FIRST, so the boot lines land in the buffer the web interface
//     reads and not only in the USB console, which the user may not have connected.
//   * The bus is started BEFORE the network. The boiler is the reason the device exists,
//     and a router that answers slowly must not delay the first conversation: the boiler reads
//     silence from the master as a short-circuited thermostat and drives the boiler into a
//     demand for heat.
//   * NOTHING here reboots because a peer is missing. A master that failed to initialise, a
//     network that failed to start and an HTTP server that failed to start are each logged
//     and stepped over: whatever still works, the owner is still owed.
//
// MQTT with Home Assistant discovery is started after the network and the executor, never
// before the bus, and a broker that is absent stops nothing (ot_mqtt_link.h).
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "board.h"
#include "ot_bus.h"
#include "ot_http.h"
#include "ot_log.h"
#include "ot_master.h"
#include "ot_mqtt_link.h"
#include "ot_net.h"
#include "ot_onewire.h"
#include "ot_thermostat.h"
#include "ot_time.h"
#include "ot_registry.h"
#include "ot_state.h"
#include "web_assets.h"

static const char *TAG = "opentherm";

// The poll ring comes FROM THE REGISTRY: the single list of entities names both what to
// show and what has to be asked for that. An earlier hard-coded array was a second
// place where Data-IDs were listed, and it would have diverged from the registry on the
// very first edit of the table.
//
// ID 0 is not part of it: the scheduler sends it on the very first step and on every other
// one after it.

// The seam between the transport and the model. The same one that separated CAN from the
// state in the previous firmware.
//
// DO NOT BLOCK: while this function computes, the conversation is not going on.
//
// The trade-off is stated out loud: ot_state_apply_dataid() takes ot_lock(), the same lock
// the HTTP handler holds for the whole document for the sake of a consistent snapshot. That
// means the bus task may wait for the renderer. This does not threaten the bus polling deadline
// -- 1150 ms per conversation: rendering 61 entities into JSON takes single milliseconds,
// an order of magnitude of margin. Waiting here is MORE CORRECT than reading the model
// without the lock: a torn snapshot costs more than a millisecond. If the margin is ever
// eaten up (many entities, a slow socket), what has to be fixed is the renderer -- copy the
// state under the lock and serialise without it -- not the removal of the lock here.
static void on_ot_response(uint8_t data_id, ot_msg_type_t type, uint16_t raw, void *)
{
    ot_state_apply_dataid(data_id, type, raw, (uint32_t)(esp_timer_get_time() / 1000));
    // The executor's ID 56 readback: a READ-ACK only, which ot_state cannot tell apart -- it keeps
    // a WRITE-ACK like a READ-ACK. A spinlock and a copy; it does not block (ot_thermostat.h).
    ot_thermostat_heard(data_id, type, raw);
}

static void nvs_init()
{
    esp_err_t err = nvs_flash_init();
    // A truncated NVS, or one that changed version, we recover, and it must be that way:
    // EVERY runtime setting lives there, and a refusal to boot would leave dead a device
    // that merely needs its settings reset. Nothing is set at compile time, so the price is
    // the settings, but never the operation.
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// Where the device is reachable -- said out loud once per transition.
//
// The function PUSHES nothing into the HTTP layer. The access policy asks
// ot_net_is_provisioned() on every request: a pushed copy lags by one tick, and what
// decides here is THE OPEN ACCESS POINT ON THE AIR, whose state changes at the moment the
// station received an address. The push variant left a passer-by who had just supplied the
// network password able to set the device's first password.
static void on_net_state(ot_net_state_t state, void *)
{
    switch (state) {
    case OT_NET_CONNECTED: {
        ESP_LOGI(TAG, "on the owner's network");
        // SNTP is started HERE and not in app_main, because there is nothing to synchronise
        // against before the station has an address. ot_time_start() is idempotent, which
        // matters: this callback fires again on every reconnection.
        //
        // It returns immediately and nothing waits behind it. The conversation
        // with the boiler must not be delayed by a peer, and a master that falls silent for
        // more than five seconds is read by the slave as a shorted thermostat -- a demand for
        // heat, which is the hottest state and not a safe one.
        ot_config_public_t pub;
        bool               known_good = false;
        ot_net_config_snapshot(&pub, &known_good);
        // The zone is NOT applied here: ot_thermostat follows the stored one every second
        // (ot_time_set_zone), so a saved zone applies without a reconnect or a power cycle.
        ot_time_start(pub.ntp_server);
        break;
    }
    case OT_NET_ACCESS_POINT:
        // WARN, not INFO: an open access point is on the air, and while it is on the air
        // the device counts as unclaimed. This has to be visible.
        ESP_LOGW(TAG, "serving its own open access point; the device counts as unclaimed");
        break;
    case OT_NET_DOWN:
        ESP_LOGW(TAG, "no network");
        break;
    }
}

// The DS18B20 read: every ~3 s, read the shield's sensor over RMT, log it and deposit the value
// into ot_room's slot 0 via ot_thermostat_room_submit(). This task
// does NOT filter or publish itself any more -- that moved into ot_room, which owns an ot_sensor
// per slot (outlier/jump rejection, FRESH/STALE/NEVER) and is stepped on the ot_thermostat task's
// own tick, the only place that may touch it (ot_room.h, OWNERSHIP). This task only reads the
// hardware and hands the raw celsius value across; ot_thermostat_room_submit() is safe to call
// from any task and merely stashes it under a spinlock (ot_thermostat.h).
//
// WHY a task and not app_main: the read blocks ~800 ms on the conversion (ot_onewire.h). Doing
// that in app_main would delay HTTP; doing it on the bus task is forbidden -- the bus must not
// be held. This task owns the sensor and touches nothing else.
//
// WHY RMT (ot_onewire): a bit-banged 1-Wire reset disables interrupts for ~480 us and would
// corrupt the OpenTherm frame the boiler is mid-way through (ot_master ticks a gptimer ~every
// 100 us). RMT clocks the slots in hardware, so this task never contends with the bus timing.
//
// A missing or failing sensor NEVER blocks boot or the bus: open failure logs once and the
// task exits; a failed read logs a warning and the task keeps trying.
static void ds18b20_task(void *arg)
{
    const int gpio = (int)(intptr_t)arg;

    ot_onewire_handle_t ow = NULL;
    esp_err_t err = ot_onewire_open(gpio, &ow);
    if (err != ESP_OK) {
        ESP_LOGW("ds18b20", "GPIO%d: bus not opened (%s); no readings", gpio,
                 esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // A run of failed reads would otherwise log a WARN every 3 s -- ~28,800 lines a day, which
    // evicts everything else from the bounded /api/log ring. So WARN on the FIRST failure after
    // any success and then only every 100th (~once per 5 min), and reset the counter on any
    // successful read: the transition into failure and the fact that it persists are both
    // visible, without drowning the ring.
    int fails = 0;

    for (;;) {
        ot_onewire_reading_t r;
        esp_err_t rerr = ot_onewire_read_temp(ow, &r);
        if (rerr == ESP_OK) {
            fails = 0;
            // SKIP_ROM reads no ROM, so there is no ROM byte to print: scratchpad[0] is the
            // temperature LSB, not an address. Log the decoded value honestly.
            ESP_LOGI("ds18b20", "GPIO%d: %.2f C (crc ok)", gpio, r.temp_c);
            // Slot 0: the shield is ot_room's AMBIENT source (ot_thermostat.c). The filter,
            // freshness and the entity publish now all happen on the drain, not here -- a
            // rejected (out-of-range/jumping) value is still handed over, and ot_room_submit()
            // is the one place that decides to keep it or not (ot_room.h).
            ot_thermostat_room_submit(0, r.temp_c);
        } else if (++fails == 1 || fails % 100 == 0) {
            // Distinguish the two failure modes ot_onewire returns (ot_onewire.h): a CRC failure
            // means the sensor answered but the frame was corrupt (noise on the line), while no
            // presence pulse means nothing is wired. They point at different repairs.
            if (rerr == ESP_ERR_INVALID_CRC)
                ESP_LOGW("ds18b20", "GPIO%d: sensor present but read failed CRC (noise?)", gpio);
            else if (rerr == ESP_ERR_NOT_FOUND)
                ESP_LOGW("ds18b20", "GPIO%d: no DS18B20 present (no presence pulse)", gpio);
            else
                ESP_LOGW("ds18b20", "GPIO%d: read failed: %s", gpio, esp_err_to_name(rerr));
        }
        // A failed read submits nothing: ot_room's own freshness accumulator (ot_sensor.h,
        // ticked once per ot_thermostat tick) is what drives the entity stale, not this loop.
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

extern "C" void app_main(void)
{
    ot_log_install();

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s %s", app->project_name, app->version);

    nvs_init();

    const board_t *b = board_get();
    ESP_LOGI(TAG, "board %s: OpenTherm in=%d out=%d, button=%d%s", b->name, b->ot_in,
             b->ot_out, b->button,
             b->button_is_download_strap ? " (download strap: hold AFTER boot only)" : "");

    size_t total = 0;
    for (const web_asset_t *a = web_assets(); a->path; a++)
        total += a->len;
    ESP_LOGI(TAG, "web UI compiled in: %u B", (unsigned)total);

    // The bus is brought up BEFORE the network, and the order carries load. The boiler is
    // the reason the device exists, and a router that answers slowly must not delay the
    // first conversation: silence from the master turns into a demand for heat.
    const esp_err_t ot_err = ot_master_init(b);
    if (ot_err != ESP_OK) {
        // We reboot nothing and do not stop: without the boiler the device is still
        // obliged to bring up the web interface and show what broke.
        ESP_LOGE(TAG, "OpenTherm did not initialise: %s", esp_err_to_name(ot_err));
    } else {
        // The model is zeroed BEFORE the bus starts: the boiler's first response may
        // arrive before ot_bus_start() returns, and a reset after it would erase it.
        ot_state_reset();

        // static, not on app_main's stack: ot_bus_sched_init() copies the array to
        // itself, but relying on that means depending on its internals.
        static uint8_t poll[OT_POLL_ID_COUNT];
        for (uint16_t i = 0; i < ot_registry_poll_count(); i++)
            poll[i] = ot_registry_poll_at(i);

        const esp_err_t bus_err =
            ot_bus_start(poll, (uint8_t)ot_registry_poll_count(), on_ot_response, nullptr);
        if (bus_err != ESP_OK)
            ESP_LOGE(TAG, "OpenTherm bus did not start: %s", esp_err_to_name(bus_err));
    }

    const esp_err_t net_err = ot_net_start(b, on_net_state, nullptr);
    if (net_err != ESP_OK) {
        // The absence of a network is no reason not to come up: there is
        // a boiler here that must keep being polled, and the failure must be visible over
        // USB rather than look like a brick. We reboot nothing -- no peer has the right to
        // cause a restart.
        ESP_LOGE(TAG, "network did not start: %s", esp_err_to_name(net_err));
    }

    // AFTER the bus and after the network, BEFORE HTTP: the first owns the destination of the
    // ID 0 high byte, the second the configuration document the executor reads, and the third
    // calls into it. It raises the CH bit only where the owner asked for it -- heating_season
    // and local_ch_enable are both false by default (ot_config_defaults), so a freshly flashed
    // device asks for nothing.
    const esp_err_t th_err = ot_thermostat_start();
    if (th_err != ESP_OK) {
        // Not a reason to reboot or to stop. Without this task the device simply never
        // asks for heat, which is the state it was in for four phases.
        ESP_LOGE(TAG, "thermostat did not start: %s", esp_err_to_name(th_err));
    }

    // AFTER the executor, whose commands it carries and whose snapshot it publishes; BEFORE HTTP,
    // whose GET /api/status reads its counters (all zero until then). It dials nothing itself:
    // its task starts the client on its first pass, if a broker host is stored.
    const esp_err_t mqtt_err = ot_mqtt_link_start();
    if (mqtt_err != ESP_OK) {
        // Not a reason to reboot or to stop: the device then simply has no MQTT.
        ESP_LOGE(TAG, "MQTT did not start: %s", esp_err_to_name(mqtt_err));
    }

    ot_http_config_t http = OT_HTTP_DEFAULT_CONFIG();
    const esp_err_t http_err = ot_http_start(&http);
    if (http_err != ESP_OK)
        ESP_LOGE(TAG, "HTTP server did not start: %s", esp_err_to_name(http_err));

    // DS18B20 bring-up read, LAST and lowest priority: it must never delay the bus, the
    // network or HTTP. The GPIO comes from the board descriptor -- the number lives nowhere
    // but board/. A NULL/failed task start is stepped over like every other peer here.
    if (b->onewire_gpio >= 0) {
        BaseType_t ok = xTaskCreate(ds18b20_task, "ds18b20", 4096,
                                    (void *)(intptr_t)b->onewire_gpio, tskIDLE_PRIORITY + 1,
                                    NULL);
        if (ok != pdPASS)
            ESP_LOGW(TAG, "DS18B20 task not created; no room readings");
    }
}
