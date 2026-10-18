// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_config_apply(): a submitted document, decided whole before anything is written. Alone in
// its file because it is the largest function of the store and the one every new field grows:
// each adds a check above the line where nothing can fail and a commit below it.
#include "ot_config.h"

#include <string.h>

#include "ot_config_internal.h"

// --- applying a submitted document ---------------------------------------------------------------

typedef enum { WIFI_KEEP, WIFI_SET, WIFI_CLEAR } wifi_action_t;

ot_config_err_t ot_config_apply_ex(ot_config_t *cfg, const ot_config_patch_t *patch,
                                   const ot_config_hash_ctx_t *hash, ot_config_repairs_t *moved)
{
    // Zero first, so that every refusal below -- including the two before any field is read --
    // reports that nothing moved.
    if (moved != NULL)
        *moved = 0;
    // A body that failed to parse arrives as nothing at all, on the HTTP task. It must not crash
    // and it must not answer OK: OK is what the handler turns into 200 and what
    // ot_config_strerror() renders as the word "accepted", so the owner would read Saved
    // over a request that stored nothing. An absent FIELD still means "leave it alone" -- that is
    // the whole of the sentinel scheme -- but an absent DOCUMENT is not a save.
    if (cfg == NULL || patch == NULL)
        return OT_CONFIG_ERR_NO_DOCUMENT;

    // One check, before every write path, rather than one per caller. An OTA rollback leaves
    // a store written by a build that knew more than this one; writing into it destroys what the
    // newer build stored and the owner who rolls forward again finds their settings gone.
    if (cfg->read_only)
        return OT_CONFIG_ERR_READ_ONLY;

    // ---- everything is decided before anything is written -----------------------------------
    //
    // A document with a good host and a bad port must change NEITHER. Otherwise the owner fixes
    // the port, saves again, and the second attempt lands on a document that is no longer the one
    // they were looking at.

    wifi_action_t wifi     = WIFI_KEEP;
    size_t        ssid_len = 0;
    size_t        psk_len  = 0;

    const bool has_ssid = patch->wifi_ssid != NULL;
    const bool has_psk  = patch->wifi_psk != NULL;
    // Half a pair cannot be applied without inventing the other half, and the invented one is
    // always the stored one -- which is precisely the pairing this component exists to prevent.
    if (has_ssid != has_psk)
        return OT_CONFIG_ERR_WIFI_PAIR;

    if (has_ssid) {
        ssid_len = strlen(patch->wifi_ssid);
        if (strcmp(patch->wifi_psk, OT_SECRET_SENTINEL) == 0) {
            // The settings page renders a stored key as the sentinel, so a form where only the
            // SSID was edited submits a NEW network carrying the PREVIOUS network's password.
            // Storing that pair is the atomicity failure arriving through the front door instead
            // of through a power cut, and it strands the device just as completely.
            if (ssid_len != cfg->wifi.ssid_len ||
                memcmp(patch->wifi_ssid, cfg->wifi.ssid, ssid_len) != 0)
                return OT_CONFIG_ERR_WIFI_PAIR;
            // Same network, untouched key: the document says nothing new about the pair. This
            // branch is what makes saving any OTHER setting possible once a network is stored --
            // the page submits the whole document every time.
            wifi = WIFI_KEEP;
        } else if (ssid_len == 0 && patch->wifi_psk[0] == '\0') {
            // Both halves emptied deliberately: forget this network. The only way the page can
            // say it, and it clears both halves in one act for the same reason they are one key.
            wifi = WIFI_CLEAR;
        } else {
            psk_len = strlen(patch->wifi_psk);
            const ot_config_err_t e_ssid =
                ot_config_check_ssid((const uint8_t *)patch->wifi_ssid, ssid_len);
            if (e_ssid != OT_CONFIG_OK)
                return e_ssid;
            const ot_config_err_t e_psk =
                ot_config_check_psk((const uint8_t *)patch->wifi_psk, psk_len);
            if (e_psk != OT_CONFIG_OK)
                return e_psk;
            wifi = WIFI_SET;
        }
    }

    if (patch->mqtt_host != NULL) {
        const ot_config_err_t e = ot_config_check_host(patch->mqtt_host);
        if (e != OT_CONFIG_OK)
            return e;
    }
    if (patch->has_mqtt_port) {
        const ot_config_err_t e = ot_config_check_port(patch->mqtt_port);
        if (e != OT_CONFIG_OK)
            return e;
    }
    if (patch->mqtt_user != NULL) {
        const ot_config_err_t e = ot_config_check_user(patch->mqtt_user);
        if (e != OT_CONFIG_OK)
            return e;
    }

    ot_secret_action_t broker = OT_SECRET_KEEP;
    if (patch->mqtt_password != NULL) {
        broker = ot_secret_decide(cfg->mqtt_password[0] != '\0', patch->mqtt_password);
        if (broker == OT_SECRET_REJECT)
            return OT_CONFIG_ERR_BROKER_PASSWORD;
        if (broker == OT_SECRET_STORE) {
            const ot_config_err_t e =
                ot_config_check_broker_password(patch->mqtt_password);
            if (e != OT_CONFIG_OK)
                return e;
        }
    }

    // The broker host and its password are one record, for the same reason the Wi-Fi SSID and PSK
    // are (the wifi_ssid/psk block above). The settings page renders a stored
    // broker password as the sentinel, so a form that edited only mqtt_host re-submits the sentinel
    // and KEEPs the stored password -- which the MQTT client would then send in cleartext to the
    // NEW, possibly attacker-chosen, host, exfiltrating it. Refuse a kept broker password
    // paired with a changed host; the owner re-enters it for the new broker. Still above the
    // no-fail line, so the document is refused whole.
    //
    // But an EMPTY new host is not a repointing: ot_config_check_host() treats "" as "I do not use
    // a broker" (ot_config_check.c) and the MQTT client then connects nowhere, so there is no host
    // -- attacker-chosen or otherwise -- for the kept password to leak to. Refusing it would make
    // disabling the broker impossible once a password is stored, so `!= '\0'` keeps the guard on
    // repointings only.
    if (patch->mqtt_host != NULL && patch->mqtt_host[0] != '\0' &&
        cfg->mqtt_password[0] != '\0' &&
        broker == OT_SECRET_KEEP &&
        strcmp(patch->mqtt_host, cfg->mqtt_host) != 0)
        return OT_CONFIG_ERR_BROKER_PAIR;

    if (patch->topic_prefix != NULL) {
        const ot_config_err_t e = ot_config_check_prefix(patch->topic_prefix);
        if (e != OT_CONFIG_OK)
            return e;
    }
    if (patch->device_name != NULL) {
        const ot_config_err_t e = ot_config_check_name(patch->device_name);
        if (e != OT_CONFIG_OK)
            return e;
    }
    if (patch->tz != NULL) {
        const ot_config_err_t e = ot_config_check_tz(patch->tz);
        if (e != OT_CONFIG_OK)
            return e;
    }
    if (patch->ntp_server != NULL) {
        const ot_config_err_t e = ot_config_check_ntp(patch->ntp_server);
        if (e != OT_CONFIG_OK)
            return e;
    }

    // The executor's numbers, each against its own bounds first, so that a
    // watchdog of 5 is ERR_RANGE and not whichever cross rule it also breaks. Above the UI
    // password on purpose: that one costs a key derivation, and a document refused over a number
    // should not pay for it.
    const struct {
        bool              has;
        uint32_t          value;
        ot_config_field_t field;
    } numbers[] = {
        {patch->has_control_mode, patch->control_mode, OT_CONFIG_F_CONTROL_MODE},
        {patch->has_watchdog_s, patch->watchdog_s, OT_CONFIG_F_WATCHDOG_S},
        {patch->has_failsafe_setpoint_dc, patch->failsafe_setpoint_dc,
         OT_CONFIG_F_FAILSAFE_SETPOINT},
        {patch->has_failsafe_room_target_dc, patch->failsafe_room_target_dc,
         OT_CONFIG_F_FAILSAFE_ROOM_TARGET},
        {patch->has_failsafe_heat_days, patch->failsafe_heat_days, OT_CONFIG_F_FAILSAFE_HEAT_DAYS},
        {patch->has_failsafe_min_cycle_s, patch->failsafe_min_cycle_s,
         OT_CONFIG_F_FAILSAFE_MIN_CYCLE},
        {patch->has_flow_min_dc, patch->flow_min_dc, OT_CONFIG_F_FLOW_MIN},
        {patch->has_flow_max_dc, patch->flow_max_dc, OT_CONFIG_F_FLOW_MAX},
        {patch->has_local_ch_setpoint_dc, patch->local_ch_setpoint_dc,
         OT_CONFIG_F_LOCAL_CH_SETPOINT},
        {patch->has_dhw_setpoint_dc, patch->dhw_setpoint_dc, OT_CONFIG_F_DHW_SETPOINT},
        {patch->has_room_mqtt_role, patch->room_mqtt_role, OT_CONFIG_F_ROOM_MQTT_ROLE},
        {patch->has_room_mqtt_stale_s, patch->room_mqtt_stale_s, OT_CONFIG_F_ROOM_MQTT_STALE_S},
    };
    for (size_t i = 0; i < sizeof numbers / sizeof numbers[0]; i++) {
        if (!numbers[i].has)
            continue;
        const ot_config_err_t e = ot_config_check_range(numbers[i].field, numbers[i].value);
        if (e != OT_CONFIG_OK)
            return e;
    }

    // The rules that span fields, on the MERGED document: the patch's value where it has one, the
    // stored one where it does not. Field by field, a flow_min of 700 is legal
    // and so is the stored flow_max of 700; together they are a band with nothing inside it. Still
    // above the no-fail line, so the document is refused whole or taken whole.
    const uint32_t flow_min = patch->has_flow_min_dc ? patch->flow_min_dc : cfg->flow_min_dc;
    const uint32_t flow_max = patch->has_flow_max_dc ? patch->flow_max_dc : cfg->flow_max_dc;
    const ot_config_err_t e_flow = ot_config_check_flow(
        flow_min, flow_max,
        patch->has_failsafe_setpoint_dc ? patch->failsafe_setpoint_dc : cfg->failsafe_setpoint_dc);
    if (e_flow != OT_CONFIG_OK)
        return e_flow;
    const ot_config_err_t e_mode =
        ot_config_check_mode(patch->has_control_mode ? patch->control_mode : cfg->control_mode,
                             patch->mqtt_host != NULL ? patch->mqtt_host : cfg->mqtt_host);
    if (e_mode != OT_CONFIG_OK)
        return e_mode;

    // A local setpoint the merged band does not contain is MOVED into it, not refused: it is
    // read-only on the wire, so the owner could not resolve a refusal, and left behind it would be
    // clamped silently by ot_control -- ID 1 at the band's edge while GET /api/config shows the
    // stored value. The patch's own value is treated like the stored one,
    // because the executor checks a command against a snapshot that a band save can overtake. Every
    // operand passed its range check above, so the narrowing loses nothing.
    const uint32_t local =
        patch->has_local_ch_setpoint_dc ? patch->local_ch_setpoint_dc : cfg->local_ch_setpoint_dc;
    const uint16_t local_in_band =
        into_band((uint16_t)local, (uint16_t)flow_min, (uint16_t)flow_max);
    const bool local_moved = local_in_band != local;

    ot_secret_action_t ui = OT_SECRET_KEEP;
    char                     new_record[OT_CONFIG_HASH_MAX + 1];
    if (patch->ui_password != NULL) {
        ui = ot_secret_decide(ot_config_password_set(cfg), patch->ui_password);
        if (ui == OT_SECRET_REJECT)
            return OT_CONFIG_ERR_UI_PASSWORD;
        if (ui == OT_SECRET_STORE) {
            const ot_config_err_t e = ot_config_check_ui_password(patch->ui_password);
            if (e != OT_CONFIG_OK)
                return e;
            // NEVER a fall-through to storing the plaintext. A missing hash context is the
            // owner's device being unable to honour a submission that was perfectly good, which
            // is why it has an error code of its own -- retyping will not help them.
            if (hash == NULL || hash->kdf == NULL || hash->salt == NULL)
                return OT_CONFIG_ERR_NO_HASH;
            // Derived into a local before anything is committed. A derivation that fails on the
            // device -- the RNG not started yet, mbedtls out of memory -- must leave the previous
            // password working rather than a half-written record that verifies nothing.
            if (!ot_config_hash_password(patch->ui_password, hash, new_record,
                                               sizeof new_record))
                return OT_CONFIG_ERR_NO_HASH;
        }
    }

    // ---- from here on nothing can fail --------------------------------------------------------

    if (wifi == WIFI_SET) {
        // One act, both halves, and the whole record zeroed first so no tail of the previous key
        // survives behind the new length -- that tail would be written to flash.
        memset(&cfg->wifi, 0, sizeof cfg->wifi);
        memcpy(cfg->wifi.ssid, patch->wifi_ssid, ssid_len);
        memcpy(cfg->wifi.psk, patch->wifi_psk, psk_len);
        cfg->wifi.ssid_len = (uint8_t)ssid_len;
        cfg->wifi.psk_len  = (uint8_t)psk_len;
    } else if (wifi == WIFI_CLEAR) {
        memset(&cfg->wifi, 0, sizeof cfg->wifi);
    }

    if (patch->mqtt_host != NULL)
        copy_str(cfg->mqtt_host, sizeof cfg->mqtt_host, patch->mqtt_host);
    if (patch->has_mqtt_port)
        cfg->mqtt_port = (uint16_t)patch->mqtt_port;
    if (patch->mqtt_user != NULL)
        copy_str(cfg->mqtt_user, sizeof cfg->mqtt_user, patch->mqtt_user);
    if (patch->topic_prefix != NULL)
        copy_str(cfg->topic_prefix, sizeof cfg->topic_prefix, patch->topic_prefix);
    if (patch->has_ha_discovery)
        cfg->ha_discovery = patch->ha_discovery;
    if (patch->device_name != NULL)
        copy_str(cfg->device_name, sizeof cfg->device_name, patch->device_name);
    if (patch->tz != NULL)
        copy_str(cfg->tz, sizeof cfg->tz, patch->tz);
    if (patch->ntp_server != NULL)
        copy_str(cfg->ntp_server, sizeof cfg->ntp_server, patch->ntp_server);
    // No validation pass: a bool has no invalid value.
    if (patch->has_dhw_enable)
        cfg->dhw_enable = patch->dhw_enable;
    // The executor's settings. Each number passed ot_config_check_range() above, so the narrowing
    // to the stored uint16_t cannot lose a bit.
    if (patch->has_control_mode)
        cfg->control_mode = (uint16_t)patch->control_mode;
    if (patch->has_heating_season)
        cfg->heating_season = patch->heating_season;
    if (patch->has_watchdog_s)
        cfg->watchdog_s = (uint16_t)patch->watchdog_s;
    if (patch->has_failsafe_setpoint_dc)
        cfg->failsafe_setpoint_dc = (uint16_t)patch->failsafe_setpoint_dc;
    if (patch->has_failsafe_room_target_dc)
        cfg->failsafe_room_target_dc = (uint16_t)patch->failsafe_room_target_dc;
    if (patch->has_failsafe_heat_days)
        cfg->failsafe_heat_days = (uint16_t)patch->failsafe_heat_days;
    if (patch->has_failsafe_min_cycle_s)
        cfg->failsafe_min_cycle_s = (uint16_t)patch->failsafe_min_cycle_s;
    if (patch->has_flow_min_dc)
        cfg->flow_min_dc = (uint16_t)patch->flow_min_dc;
    if (patch->has_flow_max_dc)
        cfg->flow_max_dc = (uint16_t)patch->flow_max_dc;
    if (patch->has_local_ch_enable)
        cfg->local_ch_enable = patch->local_ch_enable;
    if (patch->has_local_ch_setpoint_dc || local_moved)
        cfg->local_ch_setpoint_dc = local_in_band;
    if (patch->has_dhw_setpoint_dc)
        cfg->dhw_setpoint_dc = (uint16_t)patch->dhw_setpoint_dc;
    // The MQTT room-source slot. No validation pass on the two bools, like dhw_enable
    // above; role and stale_s passed ot_config_check_range() in the loop above.
    if (patch->has_room_mqtt_enable)
        cfg->room_mqtt_enable = patch->room_mqtt_enable;
    if (patch->has_room_mqtt_role)
        cfg->room_mqtt_role = (uint16_t)patch->room_mqtt_role;
    if (patch->has_room_mqtt_stale_s)
        cfg->room_mqtt_stale_s = (uint16_t)patch->room_mqtt_stale_s;
    if (patch->has_room_mqtt_ha_forwarded)
        cfg->room_mqtt_ha_forwarded = patch->room_mqtt_ha_forwarded;

    // Cleared with memset rather than a terminator: what is behind the terminator goes to flash
    // too, and a "cleared" password still readable in a flash dump is not cleared.
    if (broker == OT_SECRET_STORE)
        copy_str(cfg->mqtt_password, sizeof cfg->mqtt_password, patch->mqtt_password);
    else if (broker == OT_SECRET_CLEAR)
        memset(cfg->mqtt_password, 0, sizeof cfg->mqtt_password);

    if (ui == OT_SECRET_STORE)
        copy_str(cfg->ui_pw_hash, sizeof cfg->ui_pw_hash, new_record);
    else if (ui == OT_SECRET_CLEAR)
        // The password is optional and the physical button can erase it; an authenticated owner
        // removing it through the API is the same decision reached from the other side. Refusing
        // it would make a physical gesture the only way to undo a password on a device in a wall.
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);

    if (moved != NULL && local_moved)
        *moved = OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT);
    return OT_CONFIG_OK;
}

// The contract is in ot_config.h: the same answer, for a caller that does not ask what moved.
ot_config_err_t ot_config_apply(ot_config_t *cfg, const ot_config_patch_t *patch,
                                const ot_config_hash_ctx_t *hash)
{
    return ot_config_apply_ex(cfg, patch, hash, NULL);
}
