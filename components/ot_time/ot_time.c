// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_time.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "ot_time";

// STATIC, and not a copy taken by esp_netif_sntp_init(): ESP_NETIF_SNTP_DEFAULT_CONFIG() stores
// the POINTER it is given in `servers[0]`, and the SNTP service dereferences it long after this
// function has returned. A caller's buffer -- ot_net's projection points into a document that is
// rewritten whenever the settings page is saved -- would leave the service reading freed or
// changed bytes.
static char s_server[OT_TIME_SERVER_MAX + 1];

// Written from the SNTP callback, read from whichever task asks the time. Nothing else is
// written alongside it, so a plain volatile bool is the whole synchronisation this needs: the
// two possible values are both self-consistent and the transition happens once.
static volatile bool s_synced = false;
static bool          s_started = false;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    // INFO and once-ish: the callback fires on every resynchronisation, but the interesting
    // event for whoever reads the log is the first one.
    ESP_LOGI(TAG, "clock synchronised");
}

void ot_time_set_zone(const char *tz)
{
    // ALWAYS applied, and nothing here remembers a previous zone. An earlier version applied the zone only
    // on the first ot_time_start(), behind the SNTP guard, so a saved zone took effect after a
    // power cycle -- a defect a review found. DO NOT put a "started" guard above this.
    //
    // "UTC0" rather than unsetenv() for an empty zone: an unset TZ is UTC in newlib too, but an
    // explicit one cannot be mistaken for "the zone was never applied".
    setenv("TZ", (tz != NULL && tz[0] != '\0') ? tz : "UTC0", 1);
    tzset();
}

void ot_time_start(const char *ntp_server)
{
    if (s_started)
        return;

    if (ntp_server == NULL || ntp_server[0] == '\0') {
        // Not an error worth refusing over: ot_config_sanitize() puts the default back, so an
        // empty server here means the document was never sanitised. Say so and leave the clock
        // unsynchronised rather than inventing a server the owner did not choose.
        ESP_LOGW(TAG, "no NTP server configured; the clock will stay unknown");
        return;
    }
    snprintf(s_server, sizeof s_server, "%s", ntp_server);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_server);
    config.start             = true;
    config.sync_cb           = on_sync;
    // The station is already up when this runs, so there is nothing to wait for and the service
    // is told not to: `start = true` hands it straight to the SNTP task.
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        // We reboot nothing and we stop nothing. No decision depends on the wall clock
        // (SNTP dead): the log's timestamps stay relative, and nothing else moves.
        ESP_LOGE(TAG, "SNTP did not start: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started against %s", s_server);
}

bool ot_time_now(ot_wallclock_t *out)
{
    if (out == NULL || !s_synced)
        return false;

    const time_t now = time(NULL);
    struct tm    lt;
    if (localtime_r(&now, &lt) == NULL)
        return false;

    out->epoch = (int64_t)now;
    // tm_wday is 0 = SUNDAY. ot_wallclock_t.weekday is 0 = Monday, because that is how a weekly
    // schedule is written down and read. +6 % 7 is the conversion; DO NOT "simplify" it to -1,
    // which sends Sunday to 255.
    out->weekday       = (uint8_t)((lt.tm_wday + 6) % 7);
    out->minute_of_day = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
    return true;
}
