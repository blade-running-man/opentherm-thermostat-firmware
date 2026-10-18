// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_thermostat.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "ot_bus.h"
#include "ot_control_io.h"
#include "ot_net.h"
#include "ot_thermostat_internal.h"

static const char *TAG = "ot_thermostat";

// One second: the bus talks every 950 ms, so a decision reaches the wire within about two
// conversations. Faster buys nothing; slower makes "set the switch, watch the boiler" feel broken.
#define TICK_MS 1000

// 4096, up from an earlier 3072: the task now encodes two commands a tick, writes NVS once an hour and
// logs more than one line format -- and nobody had measured the old figure. The high-water mark
// is in GET /api/control so the next change is made on a number.
#define STACK_BYTES   4096
#define HWM_LOG_TICKS 60   // the mark at DEBUG once a minute: a trend, not a flood

// ONE executor, with the snapshot and the output of its last step, under ONE spinlock. Held only
// across the pure calls (ot_thermostat.h, LOCKING).
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static ot_control_t     s_ctl;
static ot_control_cfg_t s_cfg;
static ot_control_out_t s_out;
// What ot_thermostat_heard() keeps of the bus's replies -- the last READ-ACK of ID 56, for the
// reconciliation, and the first DATA-INVALID answer to ID 1: written on the bus task and
// read by the step, so under s_mux too. Zeroed is "nothing heard yet".
static ot_control_io_heard_t s_heard;
static bool              s_ready;     // s_ctl initialised; written once, before any reader exists
static bool              s_started;   // the task exists; written once, before ot_http starts
static volatile uint32_t s_hwm;       // bytes; 0 until the task has measured itself

// apply() and its persist as ONE step to every other command ("one path per value"):
// without it, commands A then B can land in RAM in that order and reach the store as B then A --
// and the store, which every later step reads the LOCAL values from, keeps A while the client that
// sent B was told 202. A mutex and NOT s_mux: the persist is ot_net_config_apply(), which takes
// ot_net's mutex and writes NVS, and neither may run with interrupts off. Lock order: this, then
// s_mux or ot_net's mutex, never the reverse; the task never takes it, so a tick never waits on
// flash. It does NOT cover POST /api/config, which reaches ot_net_config_apply() directly and is
// ordered against this path by ot_net's mutex alone (a field both write keeps the later persist);
// nor a step between apply and persist, which acts one more second on the stored value; nor the
// boost, which persists nothing. Static, so creating it cannot fail.
static StaticSemaphore_t s_apply_buf;
static SemaphoreHandle_t s_apply;

// The task's own: no other task touches them, so they need no lock. Static rather than on the
// stack, which is the budget the high-water mark reports on.
static ot_config_public_t   s_pub;
static uint32_t             s_seen_id1;
// An ID 56 the bus refused, owed to the next idle slot (ot_control_io_send()). FILE-SCOPE and
// never reset: a record made fresh each tick forgets every refused ask (test_source_guards).
static ot_control_io_owed_t s_owed;
static uint8_t              s_status;   // the byte last handed to ot_bus_set_status()

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void tick(void)
{
    ot_net_config_snapshot(&s_pub, NULL);
    ot_control_cfg_t cfg;
    ot_control_io_cfg(&s_pub, &cfg);
    otth_follow_zone(s_pub.tz);

    ot_control_in_t in;
    memset(&in, 0, sizeof in);

    const uint32_t now = now_ms();
    // Once a tick (TICK_MS, so once a second): re-read the room-MQTT config and re-init the
    // registry on change (ot_thermostat_room.c's fingerprint, mirroring ot_mqtt_link's), so
    // enabling/disabling the slot or changing its role/stale/ha_forwarded takes effect without a
    // reboot. BEFORE the tick below, so a config change and the reading
    // it lets through land in the SAME step, not one tick apart.
    ot_thermostat_room_refresh_cfg();
    // The room mailbox drain, the pure ot_room step and the display publish are
    // ot_thermostat_room.c's (extracted from here, file ceiling): it fills in.room_fresh/room_dc/
    // ha_forwarded_stale from the steer selection and publishes room_temperature_effective/
    // room_source from the display selection. With only the ambient DS18B20 slot configured, the
    // role check in ot_room_select_steer (ot_room.h, "ambient never steers") keeps steer.fresh
    // false, so in.room_fresh stays unchanged here: the failsafe still heats blind.
    ot_thermostat_room_tick(now, &in);

    ot_bus_write_state_t ws;
    ot_bus_write_state(&ws);
    ot_control_io_confirm(ws.id1_seq, ws.id1_raw, &s_seen_id1, &in);

    ot_control_out_t out;
    taskENTER_CRITICAL(&s_mux);
    // The ID 56 readback from the one record ot_thermostat_heard() keeps, in the critical section
    // the bus task writes it under. DO NOT read it from ot_state_get("dhw_setpoint"): ot_state
    // keeps the WRITE-ACK of our own write like a READ-ACK, and the ID 56 reconciliation cap is then defeated.
    ot_control_io_readback_in(&s_heard.rb, &in);
    ot_control_step(&s_ctl, &cfg, &in, now, &out);
    s_cfg = cfg;
    s_out = out;
    const bool     ch_command  = ot_control_ch_command(&s_ctl, &cfg);
    const bool     id1_invalid = s_heard.id1_invalid;
    const uint16_t invalid_raw = s_heard.id1_invalid_raw;
    taskEXIT_CRITICAL(&s_mux);

    // NVS whole-hour BEFORE the RTC part-hour mirror, and this order carries load. On a
    // completion step ot_control_step() has, in RAM, already done hh_ms %= OTC_HOUR_MS and
    // heat_hours++; the two stores can never be atomic, so a reset in the window between them loses
    // one. Save first, and the surviving skew errs SAFE: NVS holds the incremented hour while RTC
    // still holds the old near-full part-hour, so a restore banks one extra hour -- heat_hours a
    // touch HIGH, and the summer failsafe disarms SOONER, i.e. less unwanted heat. Mirror
    // first, and the skew errs UNSAFE: RTC's part-hour is already reset while NVS still holds the
    // old count, so the powered hour is lost from BOTH -- heat_hours under-counts and the failsafe
    // disarms LATER, prolonging the July burn. Nothing between here and the mirror mutates `out`
    // (all reads), so moving only this save disturbs no other ordering the tick relies on.
    if (out.persist_heat_hours)
        otth_save_heat_hours(out.heat_hours);
    otth_mirror(out.overdue_ms, out.hh_ms);
    otth_report(&in, &out, s_status);
    if (out.status_high != s_status) {
        s_status = out.status_high;
        ot_bus_set_status(out.status_high);
    }
    // The executor's own writes, queued ONLY into an idle slot so a hand write is never evicted,
    // encoded by ot_control_io_dc_f88() and NOT ot_command_encode() (ot_control_io.h says
    // why). ID 1 first, and an ID 56 the bus refused owed to the next idle slot: both are
    // ot_control_io_send()'s, host-tested. DO NOT pass ot_bus_write: it evicts (test_source_guards).
    ot_control_io_send(&s_owed, &cfg, &out, ot_bus_write_if_idle);
    otth_warn_unsupported_id1();
    otth_warn_invalid_id1(id1_invalid, invalid_raw);
    otth_publish(&cfg, &out, ch_command, &ws, now);
}

static void thermostat_task(void *arg)
{
    (void)arg;
    // The first tick runs at once: waiting a second before asking is a second of a cold house.
    for (uint32_t n = 0;; n++) {
        tick();
        s_hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);   // bytes in ESP-IDF
        if (n % HWM_LOG_TICKS == 0)
            ESP_LOGD(TAG, "stack high-water mark %u of %u bytes", (unsigned)s_hwm, STACK_BYTES);
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

esp_err_t ot_thermostat_start(void)
{
    if (s_started)
        return ESP_OK;
    if (s_apply == NULL)
        s_apply = xSemaphoreCreateMutexStatic(&s_apply_buf);
    if (!s_ready) {
        ot_control_restore_t r;
        otth_restore(&r);
        ot_net_config_snapshot(&s_pub, NULL);
        ot_control_io_cfg(&s_pub, &s_cfg);
        ot_control_init(&s_ctl, &s_cfg, &r, now_ms());
        ot_thermostat_room_init();   // ot_thermostat_room.c: the DS18B20 slot (+ the MQTT slot)
        s_ready = true;
    }
    // Priority 4: BELOW ot_bus (10) and ot_net (5). Master silence is a heat demand, so nothing
    // may be able to starve the bus task -- that is the reason and not a preference.
    if (xTaskCreate(thermostat_task, "ot_thermostat", STACK_BYTES, NULL, 4, NULL) != pdPASS) {
        // Not fatal, and not a reason to reboot: without this task nothing asks for heat.
        ESP_LOGE(TAG, "thermostat task did not start; the device will not ask for heat");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

// On the bus task, for every reply (ot_thermostat.h): the spinlock and ONE pure call. DO NOT
// decide anything here -- not "skip the lock unless it is ID 56", not "note the ID 1 refusal
// myself": what a reply means lives once, in ot_control_io_heard(), where a host suite pins it.
// And no log line: ESP_LOG takes ot_log's lock and waits on the console, and this is the
// conversation's path (ot_bus.h, DO NOT BLOCK). The tick says what needs saying, once.
void ot_thermostat_heard(uint8_t data_id, ot_msg_type_t type, uint16_t raw)
{
    taskENTER_CRITICAL(&s_mux);
    ot_control_io_heard(&s_heard, data_id, type, raw);
    taskEXIT_CRITICAL(&s_mux);
}

void ot_thermostat_control_cfg(ot_control_cfg_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    const bool ready = s_ready;
    if (ready)
        *out = s_cfg;
    taskEXIT_CRITICAL(&s_mux);
    if (!ready)
        memset(out, 0, sizeof *out);
}

ot_thermostat_err_t ot_thermostat_control_apply(ot_origin_t origin, ot_control_cmd_t cmd,
                                                int16_t value, ot_config_err_t *store_err)
{
    if (store_err != NULL)
        *store_err = OT_CONFIG_OK;
    // DO NOT accept "because the store will keep it": with no task nothing would carry it out,
    // and a 202 over a command nothing executes is the lie the NO_TASK code exists to prevent.
    if (!s_started)
        return OT_THERMOSTAT_NO_TASK;

    ot_control_persist_t persist;
    ot_config_patch_t    patch;
    ot_config_err_t      ce = OT_CONFIG_OK;
    xSemaphoreTake(s_apply, portMAX_DELAY);   // with the persist, as one step (s_apply says why)
    const uint32_t now = now_ms();
    taskENTER_CRITICAL(&s_mux);
    const ot_control_err_t e = ot_control_apply(&s_ctl, &s_cfg, origin, cmd, value, now, &persist);
    taskEXIT_CRITICAL(&s_mux);
    // OUTSIDE s_mux: a mutex and NVS. One path per value: the LOCAL command becomes the
    // stored one, and the next step reads it from the store like every other setting.
    if (e == OT_CONTROL_OK && ot_control_io_patch(&persist, &patch))
        ce = ot_net_config_apply(&patch);
    xSemaphoreGive(s_apply);

    if (e != OT_CONTROL_OK)
        return (ot_thermostat_err_t)e;
    if (ce == OT_CONFIG_OK)
        return OT_THERMOSTAT_OK;
    if (store_err != NULL)
        *store_err = ce;
    ESP_LOGW(TAG, "command %u accepted, not saved: %s", (unsigned)cmd, ot_config_strerror(ce));
    return OT_THERMOSTAT_NOT_SAVED;
}

ot_thermostat_err_t ot_thermostat_boost_start(int16_t setpoint_dc, uint32_t minutes)
{
    // DO NOT accept a boost "because it is harmless without the task": nothing would expire it,
    // and GET /api/control would report it running for the rest of the uptime.
    if (!s_started)
        return OT_THERMOSTAT_NO_TASK;
    const uint32_t now = now_ms();
    taskENTER_CRITICAL(&s_mux);
    const ot_control_err_t e = ot_control_boost_start(&s_ctl, &s_cfg, setpoint_dc, minutes, now);
    taskEXIT_CRITICAL(&s_mux);
    // The only record of what the owner asked for and when: the boost itself is gone after it
    // ends, and the log is where "why was the boiler at 50 last night" gets answered.
    if (e == OT_CONTROL_OK)
        ESP_LOGI(TAG, "boost started: ch_setpoint " DC_FMT " for %u min", DC_ARG(setpoint_dc),
                 (unsigned)minutes);
    return (ot_thermostat_err_t)e;
}

void ot_thermostat_boost_cancel(void)
{
    // Before the executor is initialised there is no boost to end -- boost_start() refuses with
    // NO_TASK -- and s_ctl is not yet a thing ot_control may be handed.
    if (!s_ready)
        return;
    taskENTER_CRITICAL(&s_mux);
    const bool was = ot_control_boost_active(&s_ctl);
    ot_control_boost_cancel(&s_ctl);
    taskEXIT_CRITICAL(&s_mux);
    if (was)
        ESP_LOGI(TAG, "boost cancelled by request");
}

void ot_thermostat_control_get(ot_thermostat_control_info_t *out)
{
    // Zeroed before the executor is initialised, as ot_thermostat_control_cfg() is: s_ctl is not
    // yet a thing ot_control's accessors may be handed, and there is nothing true to show.
    if (!s_ready) {
        memset(out, 0, sizeof *out);
        return;
    }
    const uint32_t now = now_ms();
    taskENTER_CRITICAL(&s_mux);
    ot_control_io_document(&s_cfg, &s_out, out);
    out->boost_active      = ot_control_boost_active(&s_ctl);
    out->boost_setpoint_dc = ot_control_boost_setpoint_dc(&s_ctl);
    out->boost_remaining_s = ot_control_boost_remaining_s(&s_ctl, now);
    taskEXIT_CRITICAL(&s_mux);
    out->stack_hwm   = s_hwm;
    out->stack_known = out->stack_hwm != 0;
}
