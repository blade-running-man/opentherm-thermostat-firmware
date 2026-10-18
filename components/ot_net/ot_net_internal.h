// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Private to components/ot_net: what its four sources share, and nothing outside may use.
//
// Beside the sources and NOT in include/, the arrangement of ot_config_internal.h: INCLUDE_DIRS
// "include" is all another component is given, so no name declared here can be reached from
// outside this directory. DO NOT move it into include/ -- that makes the machine's lock and the
// stored document public API, and the cut that created this file was allowed only because ot_net.h
// did not change.
//
// WHERE THINGS LIVE. State more than one source touches is DEFINED once, in ot_net.c, and declared
// here; a static only one source touches stays in that source. The shared names kept their `s_`
// because every function body that reads one moved byte for byte -- in this component the prefix
// means "the component's state", not "file-local". A function one source calls in another is
// exported as `otnet_<old name>`, never `ot_net_`, so a grep for the public API does not find it.
// The one-line helpers and radio_try() are static inline here under their old names, the way
// ot_config_internal.h keeps is_hex(): every call site stays exactly as it was.
//
// The three load-bearing arrangements at the top of ot_net.c bind every source that includes this.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ot_net.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// One tag for the whole component, as before the cut, so the log ring still greps as "net". A
// static in a header is a copy per source; every source that includes this logs, so none is unused.
static const char *TAG = "net";

// The facts the event handler queues (ot_net_prov.c) and the queue ot_net_start() sizes by them.
typedef enum {
    EV_STA_READY,
    EV_STA_STOPPED,
    EV_AP_STARTED,
    EV_AP_STOPPED,
    EV_ASSOCIATED,
    EV_GOT_IP,
    EV_DISCONNECTED,
    EV_CREDENTIALS_SAVED,
    EV_CLIENT_SEEN,
} ev_kind_t;

typedef struct {
    ev_kind_t kind;
    uint8_t   reason;  // EV_DISCONNECTED only
} ev_t;

// --- state: defined in ot_net.c, where each one's reason is written ------------------------------

extern ot_net_cb_t s_cb;
extern void       *s_ctx;
extern ot_config_t s_config;  // NEVER to ESP_LOG*: it holds the broker password (ot_net.c)
extern ot_prov_t   s_prov;
extern SemaphoreHandle_t s_machine;
extern SemaphoreHandle_t s_radio;
extern QueueHandle_t     s_events;
extern TaskHandle_t      s_task;
extern esp_netif_t      *s_ap_netif;
extern bool s_sta_ready;
extern bool s_connect_wanted;
extern bool s_associated;
extern bool s_have_address;
extern bool s_stored_window_closed;
extern bool s_stored_no_address;
extern char s_ap_ssid[33];
extern char s_ip[16];

// --- the locks, the clock, the radio's error rule, the mode mapping ------------------------------
//
// Where both locks are held it is radio, then machine: apply_sta_config() takes the machine inside
// apply_mode(). No path takes them the other way round. DO NOT take the radio lock while holding
// the machine: from a request handler that deadlocks against the task inside apply_mode(), and
// from the task it makes every handler wait out a scan. radio_try()'s rule is written at the top of
// ot_net_radio.c, beside most calls.

static inline void lock_machine(void) { xSemaphoreTakeRecursive(s_machine, portMAX_DELAY); }
static inline void unlock_machine(void) { xSemaphoreGiveRecursive(s_machine); }
static inline void lock_radio(void) { xSemaphoreTakeRecursive(s_radio, portMAX_DELAY); }
static inline void unlock_radio(void) { xSemaphoreGiveRecursive(s_radio); }

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static inline void radio_try(const char *what, esp_err_t err)
{
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
        ESP_LOGW(TAG, "%s: %s", what, esp_err_to_name(err));
}

static inline wifi_mode_t wifi_mode_of(ot_prov_mode_t mode)
{
    switch (mode) {
    case OT_PROV_MODE_ACCESS_POINT: return WIFI_MODE_AP;
    case OT_PROV_MODE_STATION:      return WIFI_MODE_STA;
    case OT_PROV_MODE_AP_STA:       return WIFI_MODE_APSTA;
    case OT_PROV_MODE_OFF:          break;
    }
    return WIFI_MODE_NULL;
}

// --- exported from one source to another --------------------------------------------------------

// ot_net.c: names each repaired field in the log, never its value. ot_net_start() and
// ot_net_config_apply() both report through it.
void otnet_report_repairs(ot_config_repairs_t repairs);

// ot_net_radio.c: what step() does with the machine's answer every tick, on the provisioning task,
// after it has released the machine lock.
void otnet_apply_mode(ot_prov_mode_t mode);
void otnet_captive_dns_follow_ap(bool up);

// ot_net_prov.c: the task and the event handler ot_net_start() registers. The handler runs on the
// system event task and only enqueues.
void otnet_net_task(void *arg);
void otnet_on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data);
