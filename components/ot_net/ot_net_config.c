// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_net_config.c: what request handlers and the other components ask of ot_net -- whether the
// device is provisioned, the provisioning status, the configuration document, the UI password,
// the broker settings, the address.
//
// The seam is the provisioning state machine: nothing here drives it. Each accessor takes the
// machine lock for a snapshot or one committed change and lets go, so the document is loaded and
// lives in ot_net.c, is changed only here or by the task, and leaves only projected (ot_net.h).

#include "ot_net.h"
#include "ot_net_internal.h"

#include <stdio.h>
#include <string.h>

#include "ot_config_nvs.h"
#include "esp_log.h"
#include "esp_wifi.h"

bool ot_net_has_credentials(void)
{
    if (s_machine == NULL)
        return false;
    lock_machine();
    const bool has = ot_prov_has_credentials(&s_prov);
    unlock_machine();
    return has;
}

bool ot_net_is_provisioned(void)
{
    // Before the task exists there is no answer but "no". That is the safe end: an unclaimed
    // device refuses every write but the one that hands it a network.
    if (s_machine == NULL)
        return false;
    lock_machine();
    const bool provisioned = ot_prov_is_provisioned(&s_prov);
    unlock_machine();
    return provisioned;
}

// --- what the owner is told -----------------------------------------------------------------

void ot_net_status(ot_net_status_t *out)
{
    if (out == NULL)
        return;
    const ot_net_status_t empty = {0};
    *out                              = empty;

    snprintf(out->ap_ssid, sizeof out->ap_ssid, "%s", s_ap_ssid);

    if (s_machine == NULL) {
        // Asked before the task exists. ot_prov_state() would answer WINDOW_CLOSED here,
        // which is the right answer for the radio and the wrong one for a status page -- it
        // announces a transition that never happened (ot_provision.h, the DO NOT on
        // ot_prov_state). UNCONFIGURED is what "starting up" looks like from outside.
        out->state = OT_PROV_UNCONFIGURED;
        out->mode  = OT_PROV_MODE_OFF;
        return;
    }

    const uint32_t t = now_ms();
    lock_machine();
    out->state               = ot_prov_state(&s_prov);
    out->mode                = ot_prov_mode(&s_prov);
    out->failure             = ot_prov_failure(&s_prov);
    out->failure_reason      = ot_prov_failure_reason(&s_prov);
    out->provisioned         = ot_prov_is_provisioned(&s_prov);
    out->has_credentials     = ot_prov_has_credentials(&s_prov);
    out->has_known_good      = ot_prov_has_known_good(&s_prov);
    out->window_open         = ot_prov_window_is_open(&s_prov);
    out->window_remaining_ms = ot_prov_window_remaining_ms(&s_prov, t);
    out->fallback_in_ms      = ot_prov_fallback_in_ms(&s_prov, t);
    // The SSID is in every beacon this network sends; it is not a secret, and it is the one fact
    // that makes "it will not connect" diagnosable. The key is not in ot_prov_t at all (its
    // static_assert says so) and is not copied here.
    const uint8_t n = s_config.wifi.ssid_len < sizeof out->ssid - 1 ? s_config.wifi.ssid_len
                                                                    : (uint8_t)(sizeof out->ssid - 1);
    memcpy(out->ssid, s_config.wifi.ssid, n);
    out->ssid[n] = '\0';
    unlock_machine();

    out->connected = s_have_address;
    out->ap_on_air = (wifi_mode_of(out->mode) & WIFI_MODE_AP) != 0;
    snprintf(out->ip, sizeof out->ip, "%s", s_have_address ? s_ip : "");
}

// --- the configuration document ------------------------------------------------------------

void ot_net_config_snapshot(ot_config_public_t *out, bool *known_good)
{
    if (out == NULL)
        return;
    // ot_net_start() can return before it creates the lock -- a board with no Wi-Fi transport, no
    // memory -- and app_main starts the thermostat and HTTP whatever it returned. Taking a NULL
    // mutex asserts inside FreeRTOS, so a device without a network would crash-loop in the
    // thermostat task instead of polling the boiler. The DEFAULTS are the answer, which
    // ot_net_start() put in s_config before its first way out; read unlocked, because without the
    // lock nothing writes s_config. LOCAL, heating_season false -- and DHW on: a zeroed document
    // dropped the DHW bit, no hot water on a combi boiler. ot_net_config_apply(),
    // ot_net_status() and ot_net_broker() guard too. ot_net_password_set(),
    // ot_net_check_password() and ot_net_provision_body() do not, and need not: with no lock there
    // is no network interface, so no request reaches the HTTP handlers that call them.
    if (s_machine == NULL) {
        ot_config_project(&s_config, out);
        if (known_good != NULL)
            *known_good = false;
        return;
    }
    lock_machine();
    ot_config_project(&s_config, out);
    // Under the SAME lock as the document. It comes from the machine and not from the store --
    // "a stored pair has actually produced an address" is not a thing the flash knows -- and the
    // two have to describe one moment or the page shows a network whose rollback promise belongs
    // to a different one.
    if (known_good != NULL)
        *known_good = ot_prov_has_known_good(&s_prov);
    unlock_machine();
}

ot_config_err_t ot_net_config_apply(const ot_config_patch_t *patch)
{
    if (patch == NULL)
        return OT_CONFIG_ERR_SSID;
    // No lock, no loaded document (the case at ot_net_config_snapshot()): refused, never a NULL
    // mutex taken. NO_DOCUMENT is the nearest code: there is no document to apply the patch to.
    if (s_machine == NULL)
        return OT_CONFIG_ERR_NO_DOCUMENT;

    // The salt lives HERE and not inside the context, because the context only borrows it and
    // has to outlive the apply() call. Fresh per stored password -- never per device, never per
    // build -- or one recovered flash image answers for every device whose owner chose the same
    // word (ot_config_nvs.h:138-141).
    uint8_t                    salt[OT_CONFIG_SALT_LEN];
    ot_config_hash_ctx_t ctx;

    lock_machine();
    ot_config_device_hash_ctx(&ctx, salt);

    // _ex, for what the commit MOVED to stay legal: a flow band that no longer contains the stored
    // LOCAL setpoint carries it to the nearest edge (ot_config.h). The owner is told below, in the
    // vocabulary a repaired document is already reported in at boot.
    ot_config_repairs_t   moved = 0;
    const ot_config_err_t err   = ot_config_apply_ex(&s_config, patch, &ctx, &moved);
    if (err != OT_CONFIG_OK) {
        // Nothing was written -- ot_config_apply is all-or-nothing -- so there is nothing
        // to roll back and the owner retries against the document they were looking at.
        unlock_machine();
        return err;
    }

    // Persisting can fail on a full or damaged NVS. The in-memory document is already the new
    // one, and that is the honest outcome to report: the change took effect and may not survive
    // a reboot. Saying "rejected" would be a lie the owner could not act on.
    const esp_err_t saved = ot_config_nvs_save(&s_config);
    unlock_machine();

    // After the lock: a log line is not something to hold the provisioning machine for.
    otnet_report_repairs(moved);
    // NOT "not persisted": a failed erase of a RETIRED config key (one dropped by a schema
    // migration, which may be missing from flash) is answered too, and
    // the settings are committed beside it (ot_config_nvs.c) -- so name the error, claim no loss.
    if (saved != ESP_OK)
        ESP_LOGE(TAG, "settings applied; the save reported %s, so a setting or only the erase of a "
                      "retired key may be missing from flash", esp_err_to_name(saved));
    return OT_CONFIG_OK;
}

bool ot_net_check_password(const char *candidate)
{
    if (candidate == NULL)
        return false;
    lock_machine();
    const bool ok = ot_config_check_ui_password_against(
        s_config.ui_pw_hash, candidate, ot_config_device_kdf());
    unlock_machine();
    return ok;
}

void ot_net_broker(ot_net_broker_t *out)
{
    if (out == NULL)
        return;
    const ot_net_broker_t empty = {0};
    *out                              = empty;
    if (s_machine == NULL)
        return;
    lock_machine();
    // Copied, not projected: the projection is what redacts, and MQTT needs the plaintext to
    // authenticate. See the header -- this struct must never reach a log line.
    snprintf(out->host, sizeof out->host, "%s", s_config.mqtt_host);
    out->port = s_config.mqtt_port;
    snprintf(out->user, sizeof out->user, "%s", s_config.mqtt_user);
    snprintf(out->password, sizeof out->password, "%s", s_config.mqtt_password);
    snprintf(out->topic_prefix, sizeof out->topic_prefix, "%s", s_config.topic_prefix);
    snprintf(out->device_name, sizeof out->device_name, "%s", s_config.device_name);
    out->ha_discovery = s_config.ha_discovery;
    unlock_machine();
}

// ot_config_room_mqtt(), declared in ot_config.h (not ot_net.h): the field group belongs to
// ot_config, but ot_config keeps no live document of its own to copy from -- see the long comment
// on the declaration for why this is the one function of its shape implemented outside its own
// component. Otherwise identical to ot_net_broker() just above: a copy under the machine lock, so
// a caller polling once a second (ot_thermostat_room's own cadence) never holds it.
void ot_config_room_mqtt(ot_config_room_mqtt_t *out)
{
    if (out == NULL)
        return;
    const ot_config_room_mqtt_t empty = {0};
    *out                              = empty;
    if (s_machine == NULL)
        return;
    lock_machine();
    out->enable       = s_config.room_mqtt_enable;
    // Narrowed here, not stored this way: ot_config.h keeps every number as uint16_t so a
    // damaged value cannot look like a valid enumerator before ot_config_sanitize() runs. By the
    // time it reaches this copy it has already passed that gate and is 0 or 1.
    out->role         = (uint8_t)s_config.room_mqtt_role;
    out->stale_s      = s_config.room_mqtt_stale_s;
    out->ha_forwarded = s_config.room_mqtt_ha_forwarded;
    unlock_machine();
}

void ot_net_ip_string(char out[16])
{
    if (out == NULL)
        return;
    snprintf(out, 16, "%s", s_have_address ? s_ip : "");
}

bool ot_net_password_set(void)
{
    lock_machine();
    const bool set = ot_config_password_set(&s_config);
    unlock_machine();
    return set;
}

esp_err_t ot_net_provision_body(const char *body, ot_wire_result_t *result)
{
    ot_wifi_t wifi;

    // Decoded UNDER THE LOCK, because the third state of `wifi_psk` is "keep the key already
    // stored" and resolving it means reading that key. ot_wire_parse_provision() is where
    // the three states are decided -- through ot_secret_decide(), the same function the
    // settings document uses, so there is one place that knows what the sentinel means and one
    // set of host tests over it.
    lock_machine();
    const ot_wire_result_t decoded = ot_wire_parse_provision(body, &s_config.wifi, &wifi);
    unlock_machine();

    if (result != NULL)
        *result = decoded;
    if (decoded.status != OT_WIRE_OK)
        return ESP_ERR_INVALID_ARG;

    return ot_net_provision(&wifi);
}
