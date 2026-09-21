// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_net_prov.c: the provisioning task -- the half of ot_net that turns radio events into facts for
// ot_provision, ticks it, and carries out the action and the flags it hands back.
//
// The seam is the machine lock: everything here either feeds the machine or reads its answer and
// releases it before acting, which is the second arrangement at the top of ot_net.c. What the
// answer does to the radio is ot_net_radio.c's; what a request handler reads is ot_net_config.c's.

#include "ot_net.h"
#include "ot_net_internal.h"

#include <stdio.h>

#include "ot_config_nvs.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// How often the machine is asked to decide. Every deadline it holds is seconds or minutes long,
// so this is not a resolution requirement -- it is how long a fact can sit in the queue before it
// is acted on, and how long after a window expires the access point actually goes down.
#define TICK_MS 250

static ot_net_state_t s_state = OT_NET_DOWN;

// --- events ------------------------------------------------------------------------------------

static uint32_t s_events_dropped;

static void post_event(ev_kind_t kind, uint8_t reason)
{
    if (s_events == NULL)
        return;
    const ev_t ev = {.kind = kind, .reason = reason};
    // NEVER BLOCKS. Waiting here would stall the system event task, and the event queued behind
    // the wait is the one this whole component exists to receive.
    if (xQueueSend(s_events, &ev, 0) != pdTRUE)
        s_events_dropped++;
}

static void set_state(ot_net_state_t state)
{
    if (s_state == state)
        return;
    s_state = state;
    if (s_cb != NULL)
        s_cb(state, s_ctx);
}

// --- actions -------------------------------------------------------------------------------

static void apply_action(ot_prov_action_t action)
{
    switch (action) {
    case OT_PROV_ACTION_NONE:
        return;

    case OT_PROV_ACTION_CONNECT:
        s_connect_wanted = true;
        return;

    case OT_PROV_ACTION_COMMIT_CREDENTIALS: {
        // The pair in NVS just produced an address. One write, on the transition only -- a
        // flapping router must not spend a flash cycle every few minutes rewriting two strings.
        const esp_err_t err = ot_config_nvs_commit_known_good();
        if (err != ESP_OK)
            ESP_LOGW(TAG, "could not record the network as known-good: %s", esp_err_to_name(err));
        return;
    }

    case OT_PROV_ACTION_RESTORE_CREDENTIALS: {
        // The likeliest catastrophe in this project is not an attack, it is a typo: the owner
        // changes networks, mistypes one character, the old pair is already overwritten, no
        // access point comes up because credentials exist -- and the device disappears.
        // This is the way back.
        ot_wifi_t restored = {0};
        const esp_err_t err      = ot_config_nvs_restore_known_good(&restored);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "could not put the last working network back: %s", esp_err_to_name(err));
            return;
        }
        lock_machine();
        s_config.wifi = restored;
        unlock_machine();
        ESP_LOGW(TAG, "the network just saved did not work; '%.*s' put back",
                 (int)restored.ssid_len, (const char *)restored.ssid);
        s_connect_wanted = true;
        return;
    }
    }
}

// The two facts ot_prov_boot_t wants back at the next boot. Written on transition only.
static void persist_flags(bool window_closed, bool no_address)
{
    if (window_closed == s_stored_window_closed && no_address == s_stored_no_address)
        return;
    const esp_err_t err = ot_config_nvs_save_prov_flags(window_closed, no_address);
    if (err != ESP_OK) {
        // Survivable and worth seeing: the cost is a device that offers the first-run window
        // again at the next boot, or one that does not offer the no-address escape. Neither is
        // worth a panic on a device nobody can reach.
        ESP_LOGW(TAG, "could not record the provisioning flags: %s", esp_err_to_name(err));
        return;
    }
    s_stored_window_closed = window_closed;
    s_stored_no_address    = no_address;
}

// --- the loop ------------------------------------------------------------------------------

static void feed(const ev_t *ev)
{
    const uint32_t t = now_ms();
    lock_machine();
    switch (ev->kind) {
    case EV_STA_READY:
        s_sta_ready = true;
        break;
    case EV_STA_STOPPED:
        s_sta_ready  = false;
        s_associated = false;
        break;
    case EV_AP_STARTED:
        ot_prov_on_ap_started(&s_prov, t);
        break;
    case EV_AP_STOPPED:
        ot_prov_on_ap_stopped(&s_prov, t);
        break;
    case EV_ASSOCIATED:
        s_associated = true;
        ot_prov_on_associated(&s_prov, t);
        break;
    case EV_GOT_IP:
        s_have_address = true;
        ot_prov_on_connected(&s_prov, t);
        break;
    case EV_DISCONNECTED:
        s_associated   = false;
        s_have_address = false;
        s_ip[0]        = '\0';
        ot_prov_on_disconnected(&s_prov, t, ev->reason);
        break;
    case EV_CREDENTIALS_SAVED:
        ot_prov_on_credentials_saved(&s_prov, t);
        break;
    case EV_CLIENT_SEEN:
        ot_prov_on_client_seen(&s_prov, t);
        break;
    }
    unlock_machine();
}

// One turn of the crank: tick the machine, take what it decided, do it.
//
// The machine is READ AND RELEASED before anything is acted on. Holding it across an
// esp_wifi_connect() or an NVS write would put a request handler asking
// ot_net_is_provisioned() behind the flash.
static void step(void)
{
    ot_prov_mode_t   mode;
    ot_prov_action_t action;
    bool                   window_closed;
    bool                   no_address;
    bool                   ap_on_air;
    bool                   window_open;

    lock_machine();
    ot_prov_tick(&s_prov, now_ms());
    mode          = ot_prov_mode(&s_prov);
    action        = ot_prov_take_action(&s_prov);
    window_closed = ot_prov_window_has_closed(&s_prov);
    no_address    = ot_prov_address_never_arrived(&s_prov);
    window_open   = ot_prov_window_is_open(&s_prov);
    unlock_machine();

    ap_on_air = (wifi_mode_of(mode) & WIFI_MODE_AP) != 0;

    // ORDER: the action is armed before the mode is applied, so a CONNECT issued in the same turn
    // as the mode that makes the station exist is not held for another quarter second.
    apply_action(action);
    otnet_apply_mode(mode);
    otnet_captive_dns_follow_ap(ap_on_air);
    persist_flags(window_closed, no_address);

    // A window closing has to be OBSERVABLE, or a network that silently
    // vanished is indistinguishable from a device that broke.
    static bool s_was_open;
    if (s_was_open && !window_open)
        ESP_LOGW(TAG, "setup window closed");
    s_was_open = window_open;

    set_state(s_have_address        ? OT_NET_CONNECTED
              : ap_on_air           ? OT_NET_ACCESS_POINT
                                    : OT_NET_DOWN);

    if (s_events_dropped != 0) {
        ESP_LOGE(TAG, "%u radio events were dropped; provisioning may be a tick behind",
                 (unsigned)s_events_dropped);
        s_events_dropped = 0;
    }
}

void otnet_net_task(void *arg)
{
    (void)arg;
    for (;;) {
        ev_t ev;
        // Waking on a timeout as well as on an event: every deadline the machine holds is a
        // timeout, and a device whose radio has gone quiet is exactly the one with a window to
        // close and an access point to raise.
        while (xQueueReceive(s_events, &ev, pdMS_TO_TICKS(TICK_MS)) == pdTRUE)
            feed(&ev);
        step();
    }
}

// --- the event handler ---------------------------------------------------------------------

// RUNS ON THE SYSTEM EVENT TASK, 2304 bytes of stack. It queues a fact and returns. Everything
// else -- flash, the radio, the state machine -- is the provisioning task's, next door.
void otnet_on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&event->ip_info.ip));
        post_event(EV_GOT_IP, 0);
        return;
    }
    if (base != WIFI_EVENT)
        return;

    switch (id) {
    case WIFI_EVENT_STA_START:
        post_event(EV_STA_READY, 0);
        break;
    case WIFI_EVENT_STA_STOP:
        post_event(EV_STA_STOPPED, 0);
        break;
    case WIFI_EVENT_STA_CONNECTED:
        post_event(EV_ASSOCIATED, 0);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        // THE REASON IS READ. The previous version of this function began with `(void)data;` and
        // threw it away, which is how "the router is rebooting" and "you changed the password"
        // became one message saying neither -- an executor invariant in its own right.
        // It is a uint8_t on the wire (esp_wifi_types_generic.h:1167), so
        // nothing above 255 needs carrying.
        const wifi_event_sta_disconnected_t *e = (const wifi_event_sta_disconnected_t *)data;
        post_event(EV_DISCONNECTED, e != NULL ? (uint8_t)e->reason : 0);
        break;
    }
    case WIFI_EVENT_AP_START:
        post_event(EV_AP_STARTED, 0);
        break;
    case WIFI_EVENT_AP_STOP:
        post_event(EV_AP_STOPPED, 0);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        // Somebody joined the setup network. The window is extended for it: the window closing
        // while the owner is fetching their router password is the most annoying failure in the
        // design, and it is not an attack.
        post_event(EV_CLIENT_SEEN, 0);
        break;
    default:
        break;
    }
}

ot_net_state_t ot_net_get_state(void) { return s_state; }

esp_err_t ot_net_provision(const ot_wifi_t *wifi)
{
    if (wifi == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_task == NULL)
        return ESP_ERR_INVALID_STATE;

    // Stored FIRST, and the machine told afterwards. The other order gives a machine in TRIAL
    // over a pair that is not in the flash: a power cut in between would leave the trial's
    // rollback timer running against the OLD credentials and roll back to them, which is the
    // right answer for the wrong reason -- and if the write then failed, the owner would be
    // watching a device try a network it was never given.
    const esp_err_t err = ot_config_nvs_save_wifi(wifi);
    if (err != ESP_OK)
        return err;

    lock_machine();
    s_config.wifi = *wifi;
    unlock_machine();

    // A fact on the queue, not a connection. The connection happens on the provisioning task,
    // after this function has returned and after the handler that called it has answered --
    // which is the invariant, not an optimisation.
    post_event(EV_CREDENTIALS_SAVED, 0);
    if (s_task != NULL)
        xTaskNotifyGive(s_task);
    return ESP_OK;
}

void ot_net_note_client(void) { post_event(EV_CLIENT_SEEN, 0); }
