// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The half of the configuration store that has no flash in it.
//
// Everything here is a decision -- what a value has to look like to be storable, what a
// document means when it mentions half of itself, what a reset destroys, what a client is
// allowed to see. The ot_config_nvs*.c files are the other half and deliberately dull: they
// open a namespace, move bytes, and ask the questions below. Nothing in this file includes
// an ESP-IDF header, which is what lets the test_config_* suites cover every rule that matters.
//
// Read ot_config.h first; the reasoning lives there, next to each declaration. What is
// repeated here is only what a reader of the implementation would otherwise be tempted to
// "simplify".
//
// This half is six files, cut along the seam of one responsibility per file and
// one step past it, because the named cut alone left this file over the 350-line ceiling:
//   ot_config.c           the table, the namespaces, the reset tiers, the projection, the schema
//   ot_config_check.c     what a value has to look like to be storable
//   ot_config_strerror.c  what the owner is told when it does not
//   ot_config_defaults.c  what a fresh device holds, and what damage is repaired to
//   ot_config_record.c    the UI password record: its format, its hash, its check
//   ot_config_apply.c     what a submitted document means when it mentions half of itself
// ot_config_internal.h holds what they share. A helper that has to leave its file goes there,
// never into include/: the public header is the contract, and the cut did not change it.
#include "ot_config.h"

#include <string.h>

#include "ot_config_internal.h"

// --- the table -------------------------------------------------------------------------------

// `name` is what the API and the owner see; `key` is what NVS is asked for. They are the same
// string wherever the stored thing IS the named thing, and the two rows where they differ are
// the two rows worth reading: the Wi-Fi pair shares ONE key because it is one record, and the
// UI password's key says `hash` because what is kept is not the password.
static const ot_config_field_info_t FIELDS[OT_CONFIG_F_COUNT] = {
    [OT_CONFIG_F_WIFI_SSID] = {"wifi_ssid", OT_CONFIG_KEY_STA, OT_CONFIG_NS_WIFI,
                                     false},
    [OT_CONFIG_F_WIFI_PSK] = {"wifi_psk", OT_CONFIG_KEY_STA, OT_CONFIG_NS_WIFI,
                                    true},
    [OT_CONFIG_F_MQTT_HOST]     = {"mqtt_host", "mqtt_host", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_MQTT_PORT]     = {"mqtt_port", "mqtt_port", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_MQTT_USER]     = {"mqtt_user", "mqtt_user", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_MQTT_PASSWORD] = {"mqtt_password", "mqtt_password", OT_CONFIG_NS_APP,
                                         true},
    [OT_CONFIG_F_TOPIC_PREFIX]  = {"topic_prefix", "topic_prefix", OT_CONFIG_NS_APP,
                                         false},
    [OT_CONFIG_F_HA_DISCOVERY]  = {"ha_discovery", "ha_discovery", OT_CONFIG_NS_APP,
                                         false},
    [OT_CONFIG_F_DEVICE_NAME]   = {"device_name", "device_name", OT_CONFIG_NS_APP,
                                         false},
    // NS_OWNER, alone, because this one record is what says the device is claimed -- and the
    // soft-reset tier puts it on the soft gesture's side of the line while the broker stays on
    // the other.
    [OT_CONFIG_F_UI_PASSWORD] = {"ui_password", "ui_pw_hash", OT_CONFIG_NS_OWNER, true},
    // NS_APP: the zone and the time server survive a soft reset, like every other setting the
    // owner typed. Neither is a secret -- an NTP server address is not worth redacting, and a
    // sentinel in its place would break the settings page's round trip.
    [OT_CONFIG_F_TZ]         = {"tz", "tz", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_NTP_SERVER] = {"ntp_server", "ntp", OT_CONFIG_NS_APP, false},
    // `dhw_en` in schema 2. It was `cl_dhw_en` under schema 1, and the migration carries it over.
    [OT_CONFIG_F_DHW_ENABLE]       = {"dhw_enable", "dhw_en", OT_CONFIG_NS_APP, false},
    // The executor's settings. NS_APP, so a soft reset keeps them with the
    // broker. Keys abbreviated to fit OT_CONFIG_NVS_NAME_MAX; names spelled out, `_dc` for tenths.
    [OT_CONFIG_F_CONTROL_MODE]  = {"control_mode", "ctl_mode", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_HEATING_SEASON] = {"heating_season", "season", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_WATCHDOG_S]    = {"watchdog_s", "wd_s", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_FAILSAFE_SETPOINT] = {"failsafe_setpoint_dc", "fs_sp", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_FAILSAFE_ROOM_TARGET] = {"failsafe_room_target_dc", "fs_room", OT_CONFIG_NS_APP,
                                          false},
    [OT_CONFIG_F_FAILSAFE_HEAT_DAYS] = {"failsafe_heat_days", "fs_days", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_FAILSAFE_MIN_CYCLE] = {"failsafe_min_cycle_s", "fs_cycle", OT_CONFIG_NS_APP,
                                        false},
    [OT_CONFIG_F_FLOW_MIN]      = {"flow_min_dc", "flow_min", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_FLOW_MAX]      = {"flow_max_dc", "flow_max", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_LOCAL_CH_ENABLE] = {"local_ch_enable", "loc_ch", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_LOCAL_CH_SETPOINT] = {"local_ch_setpoint_dc", "loc_ch_sp", OT_CONFIG_NS_APP,
                                       false},
    [OT_CONFIG_F_DHW_SETPOINT]  = {"dhw_setpoint_dc", "dhw_sp", OT_CONFIG_NS_APP, false},
    // The MQTT room-source slot. NS_APP, like the row above.
    [OT_CONFIG_F_ROOM_MQTT_ENABLE] = {"room_mqtt_enable", "room_en", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_ROOM_MQTT_ROLE] = {"room_mqtt_role", "room_role", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_ROOM_MQTT_STALE_S] = {"room_mqtt_stale_s", "room_stale", OT_CONFIG_NS_APP, false},
    [OT_CONFIG_F_ROOM_MQTT_HA_FORWARDED] = {"room_mqtt_ha_forwarded", "room_ha_fwd",
                                            OT_CONFIG_NS_APP, false},
};

// APPEND ONLY. A name leaves this list only when no device anywhere can still hold it, which is
// never: the price of a key is one entry here for ever.
static const char *const RETIRED_KEYS[] = {
    OT_CONFIG_KEY_V1_DHW_EN,  // schema 1's DHW bit, carried into dhw_en by the migration
    // Retired with their last consumers, and WITHOUT a schema bump: every boot of a writable
    // store erases the whole list whatever its number, and so does every save (ot_config_nvs.c).
    "cl_auto",                // the old climate_auto: the deleted PI loop's switch, retired with the loop
    "cl_man_ch",              // the old manual_ch_enable: LOCAL mode's local_ch_enable replaced it
};

const char *ot_config_retired_key(size_t index)
{
    return index < sizeof RETIRED_KEYS / sizeof RETIRED_KEYS[0] ? RETIRED_KEYS[index] : NULL;
}

static const char *const NS_NAMES[OT_CONFIG_NS_COUNT] = {
    [OT_CONFIG_NS_WIFI]   = OT_CONFIG_NS_WIFI_NAME,
    [OT_CONFIG_NS_OWNER]  = OT_CONFIG_NS_OWNER_NAME,
    [OT_CONFIG_NS_APP]    = OT_CONFIG_NS_APP_NAME,
};

const ot_config_field_info_t *ot_config_field(ot_config_field_t field)
{
    // Compared as unsigned so that a negative id -- which is what an unparsed number from a URL
    // looks like -- is out of range rather than an index behind the table.
    if ((unsigned)field >= (unsigned)OT_CONFIG_F_COUNT)
        return NULL;
    // A row nobody filled in reads back as all zeros, and the first renderer to touch its NULL
    // name crashes the HTTP task. A missing row answers "no such field" instead, which the
    // test notices loudly and a device survives.
    return FIELDS[field].name != NULL ? &FIELDS[field] : NULL;
}

const char *ot_config_ns_name(ot_config_ns_t ns)
{
    if ((unsigned)ns >= (unsigned)OT_CONFIG_NS_COUNT)
        return NULL;
    return NS_NAMES[ns];
}

bool ot_config_reset_erases(ot_config_reset_t tier, ot_config_ns_t ns)
{
    if ((unsigned)ns >= (unsigned)OT_CONFIG_NS_COUNT)
        return false;

    // The hard tier answers for the WHOLE enum rather than from a list, and that is the point
    // of asking here at all: a namespace added next year is erased by a factory reset without
    // anyone remembering to extend anything. The failure this shape prevents is a device passed
    // on to someone else with the previous owner's broker password still in its flash.
    if (tier == OT_CONFIG_RESET_HARD)
        return true;

    // Five seconds after boot takes the network and the UI password and
    // leaves the broker, the prefix and the name. The gesture means "forget this network and let
    // me back in", not "throw away everything the owner configured".
    return ns == OT_CONFIG_NS_WIFI || ns == OT_CONFIG_NS_OWNER;
}

// --- the projection ------------------------------------------------------------------------------

bool ot_config_password_set(const ot_config_t *cfg)
{
    // DERIVED, and never its own key. A flag in a second key can be written while the record is
    // not -- a power cut lands between two NVS writes -- and the device then demands a password
    // that does not exist: locked, by nobody, for ever, on a wall thermostat with no keypad and
    // no way in. ot_config_sanitize() is what guarantees a non-empty record here is
    // also a READABLE one.
    return cfg != NULL && cfg->ui_pw_hash[0] != '\0';
}

void ot_config_project(const ot_config_t *cfg, ot_config_public_t *out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    // Every string a renderer will strlen() has to point somewhere, including on this path.
    out->psk           = "";
    out->mqtt_host     = "";
    out->mqtt_user     = "";
    out->mqtt_password = "";
    out->topic_prefix  = "";
    out->device_name   = "";
    out->tz            = "";
    out->ntp_server    = "";
    out->ui_password   = "";
    if (cfg == NULL)
        return;

    // Clamped rather than trusted: this can be handed a document that has not been through
    // ot_config_sanitize(), and a length byte from flash can say 200. The alternative is
    // 200 bytes written into a 33-byte buffer on the HTTP task.
    size_t ssid_len = cfg->wifi.ssid_len;
    if (ssid_len > OT_CONFIG_SSID_MAX)
        ssid_len = OT_CONFIG_SSID_MAX;
    memcpy(out->ssid, cfg->wifi.ssid, ssid_len);
    out->ssid[ssid_len] = '\0';

    // ot_secret_redact() answers for a C STRING, and the stored key is not one: no
    // terminator, and 64 legal bytes. Same rule, asked of the length instead -- something stored
    // reads back as the sentinel, nothing stored reads back empty.
    out->psk = cfg->wifi.psk_len > 0 ? OT_SECRET_SENTINEL : "";

    // The same terminated() guard ot_config_sanitize() runs, for the same reason and on the
    // same document -- the clamp above says this function can be handed one that has not been
    // sanitized, and these four leave here as `const char *` for a renderer to strlen(). Measured,
    // not reasoned about: a 0xff document with read_only set gives
    // `READ of size 743, 0 bytes after 840-byte region` under AddressSanitizer, from
    // strlen(pub.mqtt_host) on the HTTP task -- the same report the DO NOT on terminated() quotes,
    // reached through the one function that faces a client.
    //
    // "" rather than the bytes: a field the flash could not answer for is shown as nothing set,
    // which is true, while a truncated one would be a broker address the owner never typed.
    out->mqtt_host = terminated(cfg->mqtt_host, sizeof cfg->mqtt_host) ? cfg->mqtt_host : "";
    out->mqtt_port = cfg->mqtt_port;
    out->mqtt_user = terminated(cfg->mqtt_user, sizeof cfg->mqtt_user) ? cfg->mqtt_user : "";
    out->topic_prefix =
        terminated(cfg->topic_prefix, sizeof cfg->topic_prefix) ? cfg->topic_prefix : "";
    out->ha_discovery = cfg->ha_discovery;
    out->device_name =
        terminated(cfg->device_name, sizeof cfg->device_name) ? cfg->device_name : "";
    out->tz = terminated(cfg->tz, sizeof cfg->tz) ? cfg->tz : "";
    out->ntp_server =
        terminated(cfg->ntp_server, sizeof cfg->ntp_server) ? cfg->ntp_server : "";
    out->dhw_enable       = cfg->dhw_enable;
    // Copied as they are: none is a secret, and each is a number or a flag with nothing behind it
    // for a renderer to run off the end of.
    out->control_mode            = cfg->control_mode;
    out->heating_season          = cfg->heating_season;
    out->watchdog_s              = cfg->watchdog_s;
    out->failsafe_setpoint_dc    = cfg->failsafe_setpoint_dc;
    out->failsafe_room_target_dc = cfg->failsafe_room_target_dc;
    out->failsafe_heat_days      = cfg->failsafe_heat_days;
    out->failsafe_min_cycle_s    = cfg->failsafe_min_cycle_s;
    out->flow_min_dc             = cfg->flow_min_dc;
    out->flow_max_dc             = cfg->flow_max_dc;
    out->local_ch_enable         = cfg->local_ch_enable;
    out->local_ch_setpoint_dc    = cfg->local_ch_setpoint_dc;
    out->dhw_setpoint_dc         = cfg->dhw_setpoint_dc;
    out->room_mqtt_enable        = cfg->room_mqtt_enable;
    out->room_mqtt_role          = cfg->room_mqtt_role;
    out->room_mqtt_stale_s       = cfg->room_mqtt_stale_s;
    out->room_mqtt_ha_forwarded  = cfg->room_mqtt_ha_forwarded;
    out->read_only = cfg->read_only;

    // Two storage policies, ONE disclosure policy. The broker password
    // is in the struct in plaintext because MQTT authenticates with it; the UI password is a
    // one-way record. Neither leaves here. No terminated() guard on these two, and that is not an
    // omission: ot_secret_redact() reads exactly one byte and returns a literal either way
    // (ot_secrets.c:33-38), so what leaves is never the stored array.
    out->mqtt_password   = ot_secret_redact(cfg->mqtt_password);
    out->ui_password     = ot_secret_redact(cfg->ui_pw_hash);
    out->ui_password_set = ot_config_password_set(cfg);
}

// --- schema -----------------------------------------------------------------------------------

ot_config_schema_action_t ot_config_schema_check(uint32_t stored)
{
    if (stored == OT_CONFIG_SCHEMA_VERSION)
        return OT_CONFIG_SCHEMA_CURRENT;
    // A device with nothing stored reads 0 and takes the migration path with everybody else.
    // Making "fresh" a third answer would be three code paths where two do: migrating from
    // nothing finds nothing, which is exactly right -- a first boot and an upgrade from an older
    // schema are then the same path, and only one of them has ever been tested by hand.
    if (stored < OT_CONFIG_SCHEMA_VERSION)
        return OT_CONFIG_SCHEMA_MIGRATE;
    return OT_CONFIG_SCHEMA_FUTURE;
}
