// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The executor's memory across a reset: the watchdog's overdue count and the
// heat-hours part-hour in RTC_NOINIT memory, and the whole heat hours in NVS. A file of its own
// because these are the only two places outside ot_config where this firmware remembers anything
// across a reboot, and each has a trap written next to it. What to believe after which reset is
// ot_control_io_restore()'s decision, host-tested; this file only reads and writes.
#include "ot_thermostat_internal.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

#include "ot_control_io.h"

static const char *TAG = "ot_thermostat";

// ot_control_io speaks esp_reset_reason_t by value, because a pure header may not include
// esp_system.h. An ESP-IDF release that renumbered the enum would turn a power-on into a
// "software reset" and restore noise as a watchdog count, so every value it names is pinned here.
_Static_assert((int)ESP_RST_UNKNOWN == OT_CONTROL_IO_RST_UNKNOWN, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_POWERON == OT_CONTROL_IO_RST_POWERON, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_EXT == OT_CONTROL_IO_RST_EXT, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_SW == OT_CONTROL_IO_RST_SW, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_PANIC == OT_CONTROL_IO_RST_PANIC, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_INT_WDT == OT_CONTROL_IO_RST_INT_WDT, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_TASK_WDT == OT_CONTROL_IO_RST_TASK_WDT, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_WDT == OT_CONTROL_IO_RST_WDT, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_DEEPSLEEP == OT_CONTROL_IO_RST_DEEPSLEEP, "esp_reset_reason_t moved");
_Static_assert((int)ESP_RST_BROWNOUT == OT_CONTROL_IO_RST_BROWNOUT, "esp_reset_reason_t moved");

// RTC_NOINIT: neither zeroed nor loaded at boot, so a software reset, a panic and every watchdog
// leave it as it was, and a power-on fills it with noise. Nothing else in this firmware uses
// RTC_NOINIT or esp_reset_reason() -- hence the rule, spelled out: the blob is believed only after
// a reset that ot_control_io_restore() lists AND with its magic and check word intact.
//
// DO NOT move it to .bss "to be safe". Zeroed at every boot, it restarts the watchdog on every
// reboot, and a reboot loop faster than watchdog_s -- a panic on an MQTT payload, an OTA rollback
// -- then holds ha_waiting with CH off for ever, one master-silence heat pulse per boot; and
// a loop faster than an hour never completes a powered hour, so summer bound 2 never disarms.
static RTC_NOINIT_ATTR ot_control_io_rtc_t s_rtc;

// Its own namespace, not a field of ot_config: written by the task once an hour, by no request,
// and a settings document that carried it would be one more writer of a value the executor owns.
// The factory reset erases it with the rest of the partition; the soft reset keeps it, as it
// keeps the broker.
#define NVS_NS  "ctl"
#define NVS_KEY "heat_h"

static bool load_heat_hours(uint16_t *out)
{
    nvs_handle_t h;
    // ESP_ERR_NVS_NOT_FOUND on a device that has never written it: "none stored", which
    // ot_control_io_restore() reads as disarmed. So is every other failure -- the safe end.
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    const esp_err_t e = nvs_get_u16(h, NVS_KEY, out);
    nvs_close(h);
    return e == ESP_OK;
}

void otth_restore(ot_control_restore_t *out)
{
    const int reason = (int)esp_reset_reason();
    uint16_t  hh     = 0;
    const bool found = load_heat_hours(&hh);
    ot_control_io_restore(reason, &s_rtc, found, hh, out);
    // The reset reason is the one clue a reboot loop leaves.
    ESP_LOGI(TAG, "reset reason %s: watchdog overdue and part-hour %s (%u s, %u s), heat hours "
                  "%u%s",
             ot_control_io_reset_name(reason),
             out->overdue_valid ? "restored" : "start from zero",
             (unsigned)(out->overdue_ms / 1000u), (unsigned)(out->hh_ms / 1000u),
             (unsigned)out->heat_hours,
             found ? "" : " (none stored: disarmed until HA asks for heat)");
}

void otth_mirror(uint32_t overdue_ms, uint32_t hh_ms)
{
    ot_control_io_rtc_store(&s_rtc, overdue_ms, hh_ms);
}

void otth_save_heat_hours(uint16_t heat_hours)
{
    nvs_handle_t h;
    esp_err_t    e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_u16(h, NVS_KEY, heat_hours);
        if (e == ESP_OK)
            e = nvs_commit(h);
        nvs_close(h);
    }
    // Not fatal and not retried: the next hour asks again, and a lost hour only shortens the arm.
    if (e != ESP_OK)
        ESP_LOGW(TAG, "heat hours not persisted: %s", esp_err_to_name(e));
}
