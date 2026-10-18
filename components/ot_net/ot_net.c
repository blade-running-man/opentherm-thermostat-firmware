// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_net: the half of provisioning that owns the radio, the flash and the clock.
//
// Read ot_net.h first, and ot_provision.h after it. Nothing here decides anything --
// the machine next door answers "what should this device be doing", and this file does it and
// reports back what actually happened. The two facts "we asked for an access point" and "an
// access point is on the air" are deliberately different events, because the setup window is
// anchored on the second one.
//
// THREE ARRANGEMENTS IN HERE ARE LOAD-BEARING AND WILL LOOK LIKE CEREMONY:
//
//  * THE EVENT HANDLER ONLY ENQUEUES. It runs on the system event task, whose stack is 2304 bytes
//    (CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE), and an NVS write or a JSON parse in there either
//    overflows it or blocks the queue behind it -- and the event that then arrives late, or not at
//    all, is IP_EVENT_STA_GOT_IP, which is the single event the whole design turns on.
//  * ONE LOCK ROUND THE MACHINE, A DIFFERENT ONE ROUND THE RADIO. A request handler asking
//    ot_net_is_provisioned() must not wait out an esp_wifi_scan_start(), and the task must
//    not hold the machine while it talks to the radio. Where both are held the order is radio,
//    then machine -- apply_sta_config() takes the machine inside apply_mode() (ot_net_radio.c),
//    for the length of a copy -- and no path takes them the other way round. DO NOT take the
//    radio lock while holding the machine: from a request handler that deadlocks against the task
//    inside apply_mode(), and from the task it makes every handler wait out a scan.
//  * NO ESP_ERROR_CHECK ON ANYTHING DOWNSTREAM OF STORED OR RECEIVED DATA. A configuration
//    survives a reboot, so a panic over one is a loop with no way out over the air, on a
//    thermostat screwed to a wall with the boiler depending on it. Radio calls
//    are the same shape of hazard for a different reason: nothing may reboot because a peer is absent
//    (CLAUDE.md).

// THE CUT. This was one file of a thousand lines; it is four, cut where the
// responsibilities already met, with every function body moved byte for byte. "This file" above
// means the four of them:
//
//  * ot_net.c        -- the state more than one source touches, the identity the MAC gives, and
//                       ot_net_start(), which builds all of it in the order it is written here.
//  * ot_net_prov.c   -- the provisioning task: the handler that only enqueues, the loop that feeds
//                       ot_provision and carries out its actions, and the calls that post a fact.
//  * ot_net_radio.c  -- every esp_wifi call the task makes, under the radio lock, and the scan.
//  * ot_net_config.c -- what request handlers and other components ask for: the status, the
//                       configuration document, the password, the broker settings, the address.
//
// A new responsibility -- the broker's lifecycle is the next -- is a new sibling beside these, not
// a paragraph grown into one of them. What they share is in ot_net_internal.h, beside them.
#include "ot_net.h"
#include "ot_net_internal.h"

#include <stdio.h>

#include "ot_config_nvs.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// Facts, not work. Sixteen is far more than the radio produces in one tick; the counter beside
// post_event() (ot_net_prov.c) is what makes an overflow visible rather than a lost address.
#define EVENT_QUEUE_LEN 16

// --- state -------------------------------------------------------------------------------------

ot_net_cb_t    s_cb;
void                *s_ctx;

// The whole stored document, loaded once at start.
//
// STATIC, not a local: ot_config_t is the better part of a kilobyte. DO NOT hand it to
// ESP_LOG* in any form -- it holds the broker password in plaintext by design and the UI
// password record beside it, and the log ring is served to anyone who can reach the device.
// Only `wifi` is read in here, and only to fill the radio's own struct.
ot_config_t s_config;

ot_prov_t   s_prov;
SemaphoreHandle_t s_machine;  // guards s_prov and s_config.wifi
SemaphoreHandle_t s_radio;    // guards every esp_wifi call in this file
QueueHandle_t     s_events;
TaskHandle_t      s_task;

esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;

// The station interface has reported STA_START. esp_wifi_connect() before that is
// ESP_ERR_WIFI_NOT_STARTED, so a CONNECT that arrives first is held here rather than dropped -- a
// dropped nudge is a station that never tries again, which on a device with no access point is
// exactly the stranding the executor invariants forbid.
bool s_sta_ready;
bool s_connect_wanted;
bool s_associated;
bool s_have_address;

// What was last written to flash, so the two durable facts cost one flash cycle per transition
// rather than one per tick.
bool s_stored_window_closed;
bool s_stored_no_address;

char s_ap_ssid[33];
char s_ip[16];

// The access point is named after the MAC so two devices in one house are distinguishable
// before either has been configured. Sixteen bits of it, which is enough for a person to tell
// two units apart and NOT enough to be an identifier -- the broker's client id is kept on the
// full MAC for exactly that reason.
// ESP_MAC_WIFI_STA, and NOT ESP_MAC_WIFI_SOFTAP, and that matters.
//
// The MAC of the access point interface is one greater than the station MAC. The first
// revision read SOFTAP here, while device_id() below reads STA, and on live hardware
// that produced a mismatch: the network was called opentherm-c38d, while the device name
// and the topic prefix were built from c38c. Found on the board; it does not reproduce
// on the host, because there the MAC is a single one. The test further down the tree
// asserts outright that the device name matches the access point the owner saw on their
// phone -- and that assertion was false on exactly the hardware it was written for.
//
// DO NOT go back to SOFTAP "because that is the MAC of that network": what is on the air
// is the BSSID, while a person reads the SSID string, and the string must match the rest
// of the identity.
static void build_ap_ssid(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof s_ap_ssid, "opentherm-%02x%02x", mac[4], mac[5]);
}

// Identity is the full MAC, never the device's name. It seeds the topic prefix and the
// broker client id, so renaming a device must not re-key anything -- otherwise Home Assistant
// ends up holding two devices, one of them dead and unremovable.
static void device_id(char out[OT_CONFIG_DEVICE_ID_LEN + 1])
{
    uint8_t mac[6] = {0};
    // The base MAC, read out of efuse; it needs no radio and is available before esp_wifi_init().
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, OT_CONFIG_DEVICE_ID_LEN + 1, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
}

// Settings that vanish silently are indistinguishable from a firmware that reset itself, so
// what the flash could not answer for is named out loud.
//
// The field NAME, and nothing else. A name is not a value, and a value is what may never reach
// the log ring that /api/log serves to whoever can hear the access point.
void otnet_report_repairs(ot_config_repairs_t repairs)
{
    if (repairs == 0)
        return;
    for (int i = 0; i < OT_CONFIG_F_COUNT; i++) {
        if ((repairs & OT_CONFIG_REPAIRED(i)) == 0)
            continue;
        const ot_config_field_info_t *f = ot_config_field((ot_config_field_t)i);
        // One sentence for both callers: the boot-time sanitize resets a field to its default,
        // and a settings commit moves the LOCAL setpoint into a narrowed flow band (_apply_ex).
        ESP_LOGW(TAG, "stored setting '%s' was not usable as it was and has been repaired "
                      "(reset to its default, or moved into its bounds)",
                 f != NULL ? f->name : "?");
    }
}

// --- lifecycle -------------------------------------------------------------------------------

esp_err_t ot_net_start(const board_t *board, ot_net_cb_t cb, void *ctx)
{
    if (board == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;
    // Defaults FIRST, before any way out below: a start that returns without its lock leaves the
    // snapshot projecting s_config unlocked (ot_net_config_snapshot()), and a zeroed one drops the
    // DHW bit of a combi boiler -- no hot water. The load below overwrites them.
    char id[OT_CONFIG_DEVICE_ID_LEN + 1];
    device_id(id);
    ot_config_defaults(&s_config, id);
    if (board->net_transport != BOARD_NET_WIFI) {
        // The Olimex board is Ethernet. Refusing loudly beats pretending: this firmware ships one
        // board and this is where the second transport plugs in.
        ESP_LOGE(TAG, "board %s wants a transport this build does not have", board->name);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_cb  = cb;
    s_ctx = ctx;

    // Recursive, because apply_action() takes the machine while step() may already hold nothing
    // and apply_sta_config() takes it from inside apply_mode(). A plain mutex would deadlock the
    // moment those paths meet.
    s_machine = xSemaphoreCreateRecursiveMutex();
    s_radio   = xSemaphoreCreateRecursiveMutex();
    s_events  = xQueueCreate(EVENT_QUEUE_LEN, sizeof(ev_t));
    if (s_machine == NULL || s_radio == NULL || s_events == NULL) {
        ESP_LOGE(TAG, "out of memory bringing the network up");
        return ESP_ERR_NO_MEM;
    }

    // The whole stored document, migrated and repaired. NOT two bare NVS keys: the pair is one
    // blob under one key because NVS gives no atomicity between two of them, and a power cut
    // between an SSID write and a PSK write leaves a new SSID with an old password -- a typo
    // nobody made. ot_config_nvs_load() also brings an older schema forward,
    // so a device that has been on a network for a year does not meet the setup page after an OTA.
    //
    // The return value is REPORTED, never checked into a panic: a store that will not answer
    // still leaves `s_config` full of defaults, and a device on its own access point with default
    // settings is a device the owner can reach.
    ot_config_repairs_t repairs = 0;
    const esp_err_t           load    = ot_config_nvs_load(&s_config, id, &repairs);
    if (load != ESP_OK)
        ESP_LOGW(TAG, "settings did not read (%s); running on defaults", esp_err_to_name(load));
    otnet_report_repairs(repairs);

    // The four facts the machine wants from the last run of this device.
    //
    // has_credentials is ot_config_nvs_has_credentials(), which is ot_config_wifi_usable()
    // over the same blob ot_config_sanitize() cleaned -- ONE predicate, because two of them
    // is a device told it has a network and handed an empty one: no network, no access point, no
    // way back, permanently (ot_config_nvs.h:79-91).
    ot_prov_boot_t boot = {
        .has_credentials = ot_config_wifi_usable(&s_config.wifi),
        .has_known_good  = ot_config_nvs_has_known_good(),
    };
    ot_config_nvs_load_prov_flags(&boot.window_closed_before, &boot.address_never_arrived);
    s_stored_window_closed = boot.window_closed_before;
    s_stored_no_address    = boot.address_never_arrived;

    // Defaults: fifteen minutes, thirty, and a ninety-second
    // ap_timeout for the trial. All of them are ot_prov_config_defaults()'s, and the place
    // to change one is there, next to the reasoning for the number.
    ot_prov_init(&s_prov, NULL, &boot);

    build_ap_ssid();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "netif init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
        return err;
    }

    // BOTH netifs, once, before the radio starts. esp_netif_create_default_wifi_*() may be called
    // only once each, and the mode changes underneath them all through a device's life -- a
    // station that is provisioned, an access point that comes back for the no-address fallback, both at once for the
    // trial. Creating them on demand would mean creating one twice.
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err                     = esp_wifi_init(&init);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init: %s", esp_err_to_name(err));
        return err;
    }
    // Credentials live in NVS under this firmware's own keys and nowhere else. Letting esp_wifi
    // keep its own copy would be a second store with its own idea of what is configured -- and
    // the two disagreeing is the state the invariants forbid.
    radio_try("storage", esp_wifi_set_storage(WIFI_STORAGE_RAM));

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, otnet_on_wifi_event, NULL,
                                              NULL);
    if (err == ESP_OK)
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, otnet_on_wifi_event,
                                                  NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handlers: %s", esp_err_to_name(err));
        return err;
    }

    // 4096: the task writes NVS (nvs_set_blob down through the flash driver) and formats log
    // lines. It is not the system event task and does not have its 2304 bytes -- which is the
    // whole reason the event handler only enqueues.
    if (xTaskCreate(otnet_net_task, "ot_net", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "provisioning task did not start");
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "provisioning: %s, access point '%s'",
             boot.has_credentials ? "a network is stored" : "nothing stored", s_ap_ssid);
    return ESP_OK;
}

void ot_net_device_id(char out[OT_CONFIG_DEVICE_ID_LEN + 1])
{
    if (out != NULL)
        device_id(out);
}

void ot_net_mac_string(char out[18])
{
    if (out == NULL)
        return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
}
