// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_config, the half that touches flash: LOADING the document.
//
// THE GUARD BELOW IS NOT DECORATION. This component is an ESP-IDF component AND a PlatformIO
// library at once (platformio.ini, lib_extra_dirs), so the host test build compiles every .c in
// this directory the moment a test_config_* suite includes ot_config.h -- and this one cannot
// compile without nvs.h. Faking NVS to get it to would mean those suites were testing the
// fake, which is exactly the split this component was designed around: what can be decided
// without flash is decided in the pure half (ot_config.c), where the suites reach it for real.
//
// The device half is four files -- ot_config_nvs_io.c the field-by-field helpers,
// ot_config_nvs_recover.c the ways back, ot_config_nvs_kdf.c the one-way function, and
// ot_config_nvs.c the document's load and save together. The room-slot fields pushed that
// last file's 335 lines over the 350-line ceiling, and it was cut along the seam its own two
// section comments already named: LOADING here, WRITING in ot_config_nvs_save.c. erase_retired()
// moved to ot_config_nvs_io.c (as ot_config_io_erase_retired()) because both halves called it and
// neither owns it more than the other.
#ifdef ESP_PLATFORM

#include "ot_config_nvs.h"

#include "esp_log.h"
#include "nvs.h"

#include "ot_config_nvs_internal.h"

// What every boot of a writable store writes, once the document is read. The retired keys are
// erased on EVERY such boot, not only while the schema number is behind: cl_auto and
// cl_man_ch were retired without a schema bump, from a store already at 2, and a store at 2 never migrates
// again. Gated on the number, the erase waits for a settings save -- and a device that boots this
// build, is never saved, and is rolled back to an older build reads cl_man_ch = true there: manual
// heat the owner left behind comes back. An absent key is NOT_FOUND, which erase_key()
// answers as success without writing, so a clean store pays lookups and no flash. DO NOT gate
// the erase on the schema again "to save the lookups"; that is the defect this replaced.
//
// Schema 1 -> 2 is the same pass with two more steps, in the order a power cut cannot hurt: the
// carried DHW bit under its new key FIRST, the retired keys after it -- cl_dhw_en among them, read
// by the loader before this runs -- and the schema number LAST. A cut after the first step reads
// cl_dhw_en and then dhw_en at the next boot and gets the same answer; a cut after the second
// finds dhw_en alone; only a finished migration is marked as one. DO NOT drop the first step as
// "the save will write it": nothing saves at boot, so the carried value would live in RAM only and
// the second boot -- schema 2, no carry -- would put an owner's "hot water off" back to default.
// A failed carry erases nothing; a failed erase withholds only the number, like the save's.
static void boot_write(nvs_handle_t app, const ot_config_t *cfg, bool migrate)
{
    esp_err_t e = ESP_OK;
    if (migrate)
        ot_config_io_set_bool_field(app, OT_CONFIG_F_DHW_ENABLE, cfg->dhw_enable, &e);
    if (e == ESP_OK)
        e = ot_config_io_erase_retired(app);
    if (e == ESP_OK && migrate) {
        e = nvs_set_u32(app, OT_CONFIG_KEY_SCHEMA, OT_CONFIG_SCHEMA_VERSION);
        ot_config_io_warn_store(OT_CONFIG_KEY_SCHEMA, e);
    }
    // Each failure above has already been warned about under the key that failed; this one is the
    // namespace's, and it is named as such rather than blamed on the schema key.
    const esp_err_t c = nvs_commit(app);
    if (c != ESP_OK)
        ESP_LOGW(TAG, "could not commit %s: %s", OT_CONFIG_NS_APP_NAME, esp_err_to_name(c));
}

esp_err_t ot_config_nvs_load(ot_config_t *cfg, const char *device_id,
                                   ot_config_repairs_t *repairs)
{
    if (repairs != NULL)
        *repairs = 0;
    if (cfg == NULL)
        return ESP_ERR_INVALID_ARG;

    // Defaults FIRST, so that every early return below hands back a document the device can boot
    // on. There is no path out of this function that leaves `cfg` half filled.
    ot_config_defaults(cfg, device_id);

    esp_err_t    result = ESP_OK;
    nvs_handle_t app;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_APP_NAME, NVS_READONLY, &app);
    uint32_t     stored_schema = 0;
    if (err == ESP_OK) {
        if (nvs_get_u32(app, OT_CONFIG_KEY_SCHEMA, &stored_schema) != ESP_OK)
            stored_schema = 0;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        // Not "no settings yet" but "the store did not answer". Reported, never fatal.
        ESP_LOGW(TAG, "settings unreadable (%s); starting from defaults", esp_err_to_name(err));
        result = err;
    }

    const ot_config_schema_action_t action = ot_config_schema_check(stored_schema);
    // An OTA rollback lands on a store written by a build that knew more than this one.
    // Erasing it would destroy a working owner's settings to fix a problem they do not have, and
    // refusing to boot is a brick behind a front panel -- so the document is read with whatever
    // this build understands and every SETTINGS write is refused: ot_config_apply() and
    // ot_config_nvs_save() both check this flag.
    //
    // DO NOT extend that refusal to ot_config_nvs_save_wifi(), _commit_known_good() or
    // _restore_known_good(), which take no document and are deliberately not gated. Those three
    // are the way back. A rolled-back device whose network no longer works raises its access
    // point, and a provisioning POST that stores nothing leaves no network, a useless access
    // point and no way in -- the one state that must never occur. The price of the
    // exemption is one pair a roll-forward may have to be re-entered, from a device the owner can
    // reach; the fifteen-second gesture is not an answer here, because the device is a thermostat
    // screwed to a wall, often in a hallway nobody thinks to look at.
    cfg->read_only = (action == OT_CONFIG_SCHEMA_FUTURE);

    // What was in the flash and could not be taken. It is NOT what ot_config_sanitize()
    // finds: a dropped value leaves a valid default behind, so the sanitizer sees a document with
    // nothing wrong in it and reports nothing. The loss is shown to the owner either way.
    ot_config_repairs_t lost = 0;

    if (err == ESP_OK) {
        ot_config_io_load_str_field(app, OT_CONFIG_F_MQTT_HOST, cfg->mqtt_host,
                                    sizeof cfg->mqtt_host, &lost);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_MQTT_PORT), &cfg->mqtt_port);
        ot_config_io_load_str_field(app, OT_CONFIG_F_MQTT_USER, cfg->mqtt_user,
                                    sizeof cfg->mqtt_user, &lost);
        // Plaintext, on purpose: MQTT authenticates with it and a hash here is not hardening, it
        // is a broker that never connects. It is never RETURNED -- that
        // is ot_config_project()'s job, and it is the same answer for both passwords.
        ot_config_io_load_str_field(app, OT_CONFIG_F_MQTT_PASSWORD, cfg->mqtt_password,
                                    sizeof cfg->mqtt_password, &lost);
        ot_config_io_load_str_field(app, OT_CONFIG_F_TOPIC_PREFIX, cfg->topic_prefix,
                                    sizeof cfg->topic_prefix, &lost);
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_HA_DISCOVERY),
                               &cfg->ha_discovery);
        ot_config_io_load_str_field(app, OT_CONFIG_F_DEVICE_NAME, cfg->device_name,
                                    sizeof cfg->device_name, &lost);
        ot_config_io_load_str_field(app, OT_CONFIG_F_TZ, cfg->tz, sizeof cfg->tz, &lost);
        ot_config_io_load_str_field(app, OT_CONFIG_F_NTP_SERVER, cfg->ntp_server,
                                    sizeof cfg->ntp_server, &lost);
        // Schema 1 kept the DHW bit under cl_dhw_en. The old key is read FIRST and
        // the new one after it, so a store that holds both -- a power cut inside the migration --
        // answers with the new one. Only below schema 2: a current store has no business with a
        // retired key, whatever is left of it.
        if (stored_schema < 2)
            ot_config_io_load_bool(app, OT_CONFIG_KEY_V1_DHW_EN, &cfg->dhw_enable);
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_DHW_ENABLE), &cfg->dhw_enable);
        // The executor's settings. Every number is u16 on flash, and
        // ot_config_sanitize() below is what judges it: load_u16 takes whatever the key holds.
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_CONTROL_MODE),
                              &cfg->control_mode);
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_HEATING_SEASON),
                               &cfg->heating_season);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_WATCHDOG_S), &cfg->watchdog_s);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FAILSAFE_SETPOINT),
                              &cfg->failsafe_setpoint_dc);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FAILSAFE_ROOM_TARGET),
                              &cfg->failsafe_room_target_dc);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FAILSAFE_HEAT_DAYS),
                              &cfg->failsafe_heat_days);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FAILSAFE_MIN_CYCLE),
                              &cfg->failsafe_min_cycle_s);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FLOW_MIN), &cfg->flow_min_dc);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_FLOW_MAX), &cfg->flow_max_dc);
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_LOCAL_CH_ENABLE),
                               &cfg->local_ch_enable);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_LOCAL_CH_SETPOINT),
                              &cfg->local_ch_setpoint_dc);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_DHW_SETPOINT),
                              &cfg->dhw_setpoint_dc);
        // The MQTT room-source slot. Absent on every store older than this build, and
        // load_bool()/load_u16() leave the default ot_config_defaults() already wrote untouched
        // when a key is not found -- so these four read back as that default on a fresh OR a
        // pre-room-slot NVS store. No schema bump: a MISSING key's meaning has not changed, only a new key was
        // added (see OT_CONFIG_SCHEMA_VERSION's own comment, ot_config.h).
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_ROOM_MQTT_ENABLE),
                               &cfg->room_mqtt_enable);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_ROOM_MQTT_ROLE),
                              &cfg->room_mqtt_role);
        ot_config_io_load_u16(app, ot_config_io_key_of(OT_CONFIG_F_ROOM_MQTT_STALE_S),
                              &cfg->room_mqtt_stale_s);
        ot_config_io_load_bool(app, ot_config_io_key_of(OT_CONFIG_F_ROOM_MQTT_HA_FORWARDED),
                               &cfg->room_mqtt_ha_forwarded);
        nvs_close(app);
    }

    nvs_handle_t owner;
    if (nvs_open(OT_CONFIG_NS_OWNER_NAME, NVS_READONLY, &owner) == ESP_OK) {
        // A record too long for this field is a password the owner still believes in and this
        // build cannot check. Dropping it leaves the device OPEN rather than sealed -- the same
        // trade ot_config_sanitize() makes for a record that will not parse -- so it has to
        // be reported, or the owner meets a device that stopped asking for its password.
        ot_config_io_load_str_field(owner, OT_CONFIG_F_UI_PASSWORD, cfg->ui_pw_hash,
                                    sizeof cfg->ui_pw_hash, &lost);
        nvs_close(owner);
    }

    nvs_handle_t wifi;
    if (nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READONLY, &wifi) == ESP_OK) {
        (void)ot_config_io_load_wifi(wifi, OT_CONFIG_KEY_STA, &cfg->wifi);
        nvs_close(wifi);
    }

    const ot_config_repairs_t repaired = ot_config_sanitize(cfg, device_id);
    if (repairs != NULL)
        *repairs = repaired | lost;

    // Written only once the document has been READ and repaired: earlier would mark a store as
    // migrated that a power cut then left half moved. `result` is the other half of "read": an open
    // that failed with something other than "never written" got not one field out -- marking that
    // store current retires the migration on a device where it never ran, and erasing there would
    // take cl_dhw_en before it was ever carried. A read-only store is not written at all.
    if (!cfg->read_only && result == ESP_OK) {
        nvs_handle_t write;
        const esp_err_t e = nvs_open(OT_CONFIG_NS_APP_NAME, NVS_READWRITE, &write);
        if (e == ESP_OK) {
            boot_write(write, cfg, action == OT_CONFIG_SCHEMA_MIGRATE);
            nvs_close(write);
        } else {
            ESP_LOGW(TAG, "could not open %s: %s", OT_CONFIG_NS_APP_NAME, esp_err_to_name(e));
        }
    }

    return result;
}

#endif  // ESP_PLATFORM
