// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_net_radio.c: the radio -- every esp_wifi call the provisioning task makes, the captive DNS
// responder that follows the access point, and the scan a request handler asks for.
//
// The seam is the radio lock (s_radio): this file is what runs under it, and the task and the
// scan meet here and nowhere else, which is why ot_net.h forbids an esp_wifi call from anywhere
// but this component. Nothing here decides a mode; it applies the one ot_net_prov.c read.

#include "ot_net.h"
#include "ot_net_internal.h"

#include <string.h>

#include "ot_captive_dns_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

// What the radio was last told, so a mode is applied on change rather than every quarter second.
static ot_prov_mode_t s_radio_mode = OT_PROV_MODE_OFF;
static bool                 s_wifi_started;
static bool s_dns_running;

// The pair, out of the document and into the radio's own struct.
//
// memcpy with a measured length, NOT strncpy(..., sizeof - 1). Both fields are fixed-size byte
// arrays that esp_wifi does not require to be NUL-terminated (esp_wifi_types_generic.h:559-560),
// so reserving a byte for a terminator silently truncates a legal 32-character SSID to 31 and a
// 64-character hex PSK to 63 -- and the device then fails to associate for ever, with a reason
// nobody can deduce from the outside. ot_wifi_t carries the two lengths for this reason and
// this one alone.
static void wifi_config_from(const ot_wifi_t *wifi, wifi_config_t *out)
{
    const wifi_config_t empty = {0};
    *out = empty;
    memcpy(out->sta.ssid, wifi->ssid, wifi->ssid_len);
    memcpy(out->sta.password, wifi->psk, wifi->psk_len);
    // An open network is a legal answer and ot_config_check_psk() says so. The threshold
    // has to come down with it, or esp_wifi refuses to associate with a network it can see.
    out->sta.threshold.authmode = wifi->psk_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
}

// --- the radio -----------------------------------------------------------------------------

// Every call in here is logged and returned on, never ESP_ERROR_CHECK'd. A radio that will not
// come up must leave a device that still drives the boiler and still says why over USB;
// aborting turns that into a reboot loop, which is the one failure nobody can reach the device to
// fix (CLAUDE.md).

static void apply_ap_config(void)
{
    wifi_config_t ap = {0};
    const size_t  n  = strlen(s_ap_ssid);
    memcpy(ap.ap.ssid, s_ap_ssid, n);
    ap.ap.ssid_len       = (uint8_t)n;
    ap.ap.max_connection = 4;
    // OPEN, and it is deliberate rather than an oversight: a WPA2 key nobody has been told
    // is a brick, and the old firmware's answer -- generate one and publish it through a sensor
    // -- put the key exactly where it cannot be read, because reading a sensor needs the network
    // the key is for. What bounds the exposure is the access policy
    // and the setup window's clock, not the radio.
    ap.ap.authmode       = WIFI_AUTH_OPEN;
    radio_try("ap config", esp_wifi_set_config(WIFI_IF_AP, &ap));
}

static void apply_sta_config(void)
{
    wifi_config_t sta;
    lock_machine();
    wifi_config_from(&s_config.wifi, &sta);
    unlock_machine();
    radio_try("sta config", esp_wifi_set_config(WIFI_IF_STA, &sta));
}

// The DNS responder lives and dies with the access point, and with nothing else. An answer for
// every name in existence is correct only on a network that has nothing else in it -- the socket
// binds to the access point's own address for the same reason (ot_captive_dns_server.h).
void otnet_captive_dns_follow_ap(bool up)
{
    if (up == s_dns_running)
        return;
    if (!up) {
        ot_captive_dns_stop();
        s_dns_running = false;
        return;
    }
    esp_netif_ip_info_t info = {0};
    if (s_ap_netif == NULL || esp_netif_get_ip_info(s_ap_netif, &info) != ESP_OK) {
        ESP_LOGW(TAG, "no address on the access point; names will not resolve on it");
        return;
    }
    uint8_t octets[4];
    // lwip already stores ip4_addr_t.addr in network order, so this is the whole bridge.
    memcpy(octets, &info.ip.addr, sizeof octets);
    const esp_err_t err = ot_captive_dns_start(octets);
    if (err != ESP_OK) {
        // Not fatal and deliberately not retried into a loop: a captive portal that will not
        // start is a device the owner reaches by typing an address, and a device that reboots is
        // a device the owner does not reach at all.
        ESP_LOGW(TAG, "captive DNS did not start (%s); the setup page is still at " IPSTR,
                 esp_err_to_name(err), IP2STR(&info.ip));
        return;
    }
    s_dns_running = true;
    ESP_LOGI(TAG, "captive portal up: every name resolves to " IPSTR, IP2STR(&info.ip));
}

void otnet_apply_mode(ot_prov_mode_t mode)
{
    lock_radio();

    if (mode != s_radio_mode) {
        const wifi_mode_t want = wifi_mode_of(mode);
        radio_try("set mode", esp_wifi_set_mode(want));
        if (want & WIFI_MODE_AP)
            apply_ap_config();
        if (want & WIFI_MODE_STA)
            apply_sta_config();
        s_radio_mode = mode;

        if (want == WIFI_MODE_NULL) {
            // The terminal state on a device with nothing to connect to. The radio has nothing
            // left to do, and the way back is the short window the next boot opens -- which is
            // why this is survivable at all.
            if (s_wifi_started) {
                radio_try("stop", esp_wifi_stop());
                s_wifi_started = false;
                s_sta_ready    = false;
            }
            ESP_LOGW(TAG, "setup window closed; the access point comes back at the next boot");
        } else if (!s_wifi_started) {
            radio_try("start", esp_wifi_start());
            s_wifi_started = true;
        }
    }

    // Held until the station interface says it is up, then issued once.
    if (s_connect_wanted && s_sta_ready && (wifi_mode_of(s_radio_mode) & WIFI_MODE_STA)) {
        // The machine's contract for OT_PROV_ACTION_CONNECT: disconnect first if the
        // station is associated. That comes straight back as an event with reason 8, and
        // ot_prov_classify() knows it is ours -- which is why it is not a verdict on
        // anything and does not start the no-address fallback's clock.
        if (s_associated)
            radio_try("disconnect", esp_wifi_disconnect());
        apply_sta_config();
        radio_try("connect", esp_wifi_connect());
        s_connect_wanted = false;
    }

    unlock_radio();
}

// --- the scan --------------------------------------------------------------------------------

esp_err_t ot_net_scan(ot_net_scan_entry_t *out, size_t cap, size_t *count)
{
    if (out == NULL || count == NULL)
        return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (s_task == NULL || !s_wifi_started)
        return ESP_ERR_INVALID_STATE;

    lock_radio();

    // A sweep needs the station interface. On an unconfigured device the machine asks for an
    // access point alone, so the station is switched on for the duration and the machine's answer
    // is put back afterwards -- the radio lock is what makes that safe, and the next step() would
    // restore the mode anyway if this returned early.
    const bool  had_sta = (wifi_mode_of(s_radio_mode) & WIFI_MODE_STA) != 0;
    wifi_mode_t restore = wifi_mode_of(s_radio_mode);
    if (!had_sta) {
        const wifi_mode_t with_sta = restore == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA;
        radio_try("scan mode", esp_wifi_set_mode(with_sta));
    }

    // Blocking. Thirteen channels, and on a device serving the setup page from its own access
    // point the radio leaves that channel while it sweeps -- the phone may lose the page
    // mid-request, which the settings page is written to expect.
    const wifi_scan_config_t cfg = {0};
    esp_err_t                err = esp_wifi_scan_start(&cfg, true);
    if (err == ESP_OK) {
        uint16_t found = OT_NET_SCAN_MAX;
        static wifi_ap_record_t records[OT_NET_SCAN_MAX];
        err = esp_wifi_scan_get_ap_records(&found, records);
        if (err == ESP_OK) {
            const size_t n = found < cap ? found : cap;
            for (size_t i = 0; i < n; i++) {
                // ssid is a 33-byte field in wifi_ap_record_t and IDF terminates it, but the
                // length is bounded here anyway: this string is about to be rendered into JSON
                // and the renderer trusts a terminator.
                memcpy(out[i].ssid, records[i].ssid, sizeof out[i].ssid - 1);
                out[i].ssid[sizeof out[i].ssid - 1] = '\0';
                out[i].rssi                         = records[i].rssi;
                // Anything that is not literally WIFI_AUTH_OPEN needs a key. A firmware that
                // guessed the other way would hide the password box for a secured network and
                // leave the owner unable to join their own.
                out[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
            }
            *count = n;
        }
    }
    // Always freed, even when get_ap_records failed: the records stay in esp_wifi's heap
    // otherwise, and a device that scans on every load of the settings page would leak them.
    esp_wifi_clear_ap_list();

    if (!had_sta)
        radio_try("scan mode restore", esp_wifi_set_mode(restore));

    unlock_radio();

    if (err != ESP_OK)
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
    return err;
}
