// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_config, the half that touches flash: WRITING the document back.
//
// THE GUARD BELOW IS NOT DECORATION. See ot_config_nvs_load.c for the reason: this component is
// an ESP-IDF component AND a PlatformIO library at once, and this file cannot compile without
// nvs.h, so it stays out of the host build entirely (platformio.ini, lib_extra_dirs).
//
// Cut from ot_config_nvs.c along that file's own "loading" / "writing" section
// comments, when the room-slot fields pushed it over the 350-line ceiling. See
// ot_config_nvs_load.c's header for the rest of that story.
#ifdef ESP_PLATFORM

#include "ot_config_nvs.h"

#include "esp_log.h"
#include "nvs.h"

#include "ot_config_nvs_internal.h"

esp_err_t ot_config_nvs_save_wifi(const ot_wifi_t *wifi)
{
    if (wifi == NULL)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    // ONE key. Not two, not "ssid then psk", and DO NOT split this for symmetry with the API:
    // the whole reason ot_wifi_t exists is that no power cut can land between the two
    // halves of a credential.
    err = nvs_set_blob(handle, OT_CONFIG_KEY_STA, wifi, sizeof *wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    ot_config_io_warn_store(OT_CONFIG_KEY_STA, err);
    nvs_close(handle);
    return err;
}

esp_err_t ot_config_nvs_save(const ot_config_t *cfg)
{
    if (cfg == NULL)
        return ESP_ERR_INVALID_ARG;
    // The same gate ot_config_apply() has, because this function can be reached without
    // going through it. A store written by a newer build is not written into by this one.
    if (cfg->read_only)
        return ESP_ERR_INVALID_STATE;

    esp_err_t first = ot_config_nvs_save_wifi(&cfg->wifi);

    nvs_handle_t app;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_APP_NAME, NVS_READWRITE, &app);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s: %s", OT_CONFIG_NS_APP_NAME, esp_err_to_name(err));
        if (first == ESP_OK)
            first = err;
    } else {
        // Every field is attempted even after one fails. Stopping early would leave a document
        // that is neither the old one nor the new one, and nothing would say which fields made it.
        ot_config_io_set_str_field(app, OT_CONFIG_F_MQTT_HOST, cfg->mqtt_host, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_MQTT_PORT, cfg->mqtt_port, &first);
        ot_config_io_set_str_field(app, OT_CONFIG_F_MQTT_USER, cfg->mqtt_user, &first);
        // Plaintext, on purpose: MQTT authenticates with it, so a hash here is not hardening, it
        // is a broker that never connects. It is never RETURNED, which
        // is ot_config_project()'s job and the same answer for both passwords.
        ot_config_io_set_str_field(app, OT_CONFIG_F_MQTT_PASSWORD, cfg->mqtt_password, &first);
        ot_config_io_set_str_field(app, OT_CONFIG_F_TOPIC_PREFIX, cfg->topic_prefix, &first);
        ot_config_io_set_bool_field(app, OT_CONFIG_F_HA_DISCOVERY, cfg->ha_discovery, &first);
        ot_config_io_set_str_field(app, OT_CONFIG_F_DEVICE_NAME, cfg->device_name, &first);
        ot_config_io_set_str_field(app, OT_CONFIG_F_TZ, cfg->tz, &first);
        ot_config_io_set_str_field(app, OT_CONFIG_F_NTP_SERVER, cfg->ntp_server, &first);
        ot_config_io_set_bool_field(app, OT_CONFIG_F_DHW_ENABLE, cfg->dhw_enable, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_CONTROL_MODE, cfg->control_mode, &first);
        ot_config_io_set_bool_field(app, OT_CONFIG_F_HEATING_SEASON, cfg->heating_season, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_WATCHDOG_S, cfg->watchdog_s, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FAILSAFE_SETPOINT, cfg->failsafe_setpoint_dc,
                                   &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FAILSAFE_ROOM_TARGET,
                                   cfg->failsafe_room_target_dc, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FAILSAFE_HEAT_DAYS, cfg->failsafe_heat_days,
                                   &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FAILSAFE_MIN_CYCLE, cfg->failsafe_min_cycle_s,
                                   &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FLOW_MIN, cfg->flow_min_dc, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_FLOW_MAX, cfg->flow_max_dc, &first);
        ot_config_io_set_bool_field(app, OT_CONFIG_F_LOCAL_CH_ENABLE, cfg->local_ch_enable,
                                    &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_LOCAL_CH_SETPOINT, cfg->local_ch_setpoint_dc,
                                   &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_DHW_SETPOINT, cfg->dhw_setpoint_dc, &first);
        // The MQTT room-source slot.
        ot_config_io_set_bool_field(app, OT_CONFIG_F_ROOM_MQTT_ENABLE, cfg->room_mqtt_enable,
                                    &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_ROOM_MQTT_ROLE, cfg->room_mqtt_role, &first);
        ot_config_io_set_u16_field(app, OT_CONFIG_F_ROOM_MQTT_STALE_S, cfg->room_mqtt_stale_s,
                                   &first);
        ot_config_io_set_bool_field(app, OT_CONFIG_F_ROOM_MQTT_HA_FORWARDED,
                                    cfg->room_mqtt_ha_forwarded, &first);

        // The retired keys BEFORE the schema number, on every save as on every boot. A migration
        // whose erase failed leaves the store below schema 2; without this, the next save would
        // write the number, end the migration, and leave the key in the flash for an OTA rollback
        // to an older build to read back as live. A failed erase withholds only the number:
        // the settings the owner typed are committed either way, and the next save or boot tries
        // the erase again.
        const esp_err_t erased = ot_config_io_erase_retired(app);
        if (first == ESP_OK)
            first = erased;
        esp_err_t e = ESP_OK;
        if (erased == ESP_OK) {
            e = nvs_set_u32(app, OT_CONFIG_KEY_SCHEMA, OT_CONFIG_SCHEMA_VERSION);
            ot_config_io_warn_store(OT_CONFIG_KEY_SCHEMA, e);
        }
        if (e == ESP_OK)
            e = nvs_commit(app);
        nvs_close(app);
        if (first == ESP_OK)
            first = e;
    }

    nvs_handle_t owner;
    err = nvs_open(OT_CONFIG_NS_OWNER_NAME, NVS_READWRITE, &owner);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s: %s", OT_CONFIG_NS_OWNER_NAME, esp_err_to_name(err));
        if (first == ESP_OK)
            first = err;
    } else {
        // The record, never the password -- and there is NO companion "a password is set" key to
        // write, because that flag is DERIVED (ot_config_password_set). A half-finished
        // write that stored the flag and not the record would leave the device demanding a
        // password nobody has.
        ot_config_io_set_str_field(owner, OT_CONFIG_F_UI_PASSWORD, cfg->ui_pw_hash, &first);
        const esp_err_t e = nvs_commit(owner);
        if (e != ESP_OK)
            ESP_LOGW(TAG, "could not commit %s: %s", OT_CONFIG_NS_OWNER_NAME,
                     esp_err_to_name(e));
        nvs_close(owner);
        if (first == ESP_OK)
            first = e;
    }

    return first;
}

#endif  // ESP_PLATFORM
