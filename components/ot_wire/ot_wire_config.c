// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The settings document, both directions: POST /api/config decoded into an ot_config_patch_t, and
// the projection rendered for GET /api/config. The two rules at the top of ot_wire.c bind both --
// no secret is rendered, and no submitted value is quoted in an error.
#include "ot_wire.h"

#include <string.h>

#include "ot_json.h"
#include "ot_wire_writer.h"

// Only this route has a field that belongs to another one, so the constructor stays here.
static ot_wire_result_t wrong_route(const char *field)
{
    const ot_wire_result_t r = {OT_WIRE_WRONG_ROUTE, OT_CONFIG_OK, field};
    return r;
}

// --- POST /api/config --------------------------------------------------------------------

// One string field of the patch. Returns false and names the field when the submission is not
// something this device could store; MISSING leaves the pointer NULL, which is what "the key was
// absent" means all the way down to ot_config_apply().
static bool read_field(const char *body, const char *name, char *store, size_t cap,
                       const char **target, ot_wire_result_t *fail)
{
    const ot_json_read_t read = ot_json_string(body, name, store, cap);
    if (read == OT_JSON_MISSING) {
        *target = NULL;
        return true;
    }
    if (read != OT_JSON_FOUND) {
        *fail = bad_field(name);
        return false;
    }
    *target = store;
    return true;
}

// One of the executor's numbers, with read_field()'s contract: absent leaves `*has` false, and
// anything but a whole non-negative number names the field. Whether it is a legal WATCHDOG is
// ot_config_check_range()'s answer; this layer says "a number", and carries it as uint32_t so
// that 70000 reaches that answer whole.
static bool read_number(const char *body, const char *name, bool *has, uint32_t *value,
                        ot_wire_result_t *fail)
{
    const ot_json_read_t read = ot_json_u32(body, name, value);
    if (read == OT_JSON_MISSING)
        return true;
    if (read != OT_JSON_FOUND) {
        *fail = bad_field(name);
        return false;
    }
    *has = true;
    return true;
}

ot_wire_result_t ot_wire_parse_config(const char *body, ot_config_patch_t *out,
                                                  ot_wire_patch_storage_t *storage)
{
    if (out == NULL || storage == NULL)
        return bad_body();
    if (ot_json_check(body) != OT_JSON_DOC_OK)
        return bad_body();

    // Built locally and copied out at the very end. ot_config_apply()'s contract is ALL OR
    // NOTHING and it can only keep it if what it is handed is all or nothing too -- a patch
    // carrying the good half of a refused document would be applied by a handler that checked the
    // status after passing it on.
    ot_config_patch_t patch = {0};
    ot_wire_result_t  fail  = ok();

    // The network does not travel on this route (web/src/api/config.ts, ConfigPatch). Refused BY
    // NAME rather than ignored: silently dropping a credential somebody typed into a form is
    // worse than telling them where it belongs, because the form then reports success over a
    // network that was never stored.
    char probe[OT_CONFIG_PSK_MAX + 1];
    if (ot_json_string(body, "wifi_ssid", probe, sizeof probe) != OT_JSON_MISSING)
        return wrong_route("wifi_ssid");
    if (ot_json_string(body, "wifi_psk", probe, sizeof probe) != OT_JSON_MISSING)
        return wrong_route("wifi_psk");

    // The five values the executor owns are written through the entity
    // path, so that one value has one validator and one ownership check. Refused BY NAME for the
    // reason the Wi-Fi pair is -- a page that reported Saved over a hot-water switch it silently
    // dropped would be lying -- and by PRESENCE, whatever the type. ot_config_patch_t keeps
    // members for them because the executor persists through ot_config_apply(); only a request
    // body may not carry them. heating_season is an entity Home Assistant may only turn OFF:
    // a settings page loaded in season and saved after HA turned it off would send `true` back and
    // heat behind HA's back. DO NOT return it to the settings because GET still renders it.
    static const char *const EXECUTOR_OWNED[] = {"local_ch_enable", "local_ch_setpoint_dc",
                                                 "dhw_enable", "dhw_setpoint_dc",
                                                 "heating_season"};
    for (size_t i = 0; i < sizeof EXECUTOR_OWNED / sizeof EXECUTOR_OWNED[0]; i++)
        if (ot_json_string(body, EXECUTOR_OWNED[i], probe, sizeof probe) != OT_JSON_MISSING)
            return refused(OT_CONFIG_ERR_READ_ONLY_FIELD, EXECUTOR_OWNED[i]);

    // The names are ot_config's `name` column, which that table defines as what the API and
    // the owner see. A name that disagrees with it is a field that silently never saves.
    if (!read_field(body, "mqtt_host", storage->mqtt_host, sizeof storage->mqtt_host,
                    &patch.mqtt_host, &fail) ||
        !read_field(body, "mqtt_user", storage->mqtt_user, sizeof storage->mqtt_user,
                    &patch.mqtt_user, &fail) ||
        !read_field(body, "mqtt_password", storage->mqtt_password, sizeof storage->mqtt_password,
                    &patch.mqtt_password, &fail) ||
        !read_field(body, "topic_prefix", storage->topic_prefix, sizeof storage->topic_prefix,
                    &patch.topic_prefix, &fail) ||
        !read_field(body, "device_name", storage->device_name, sizeof storage->device_name,
                    &patch.device_name, &fail) ||
        !read_field(body, "tz", storage->tz, sizeof storage->tz, &patch.tz, &fail) ||
        !read_field(body, "ntp_server", storage->ntp_server, sizeof storage->ntp_server,
                    &patch.ntp_server, &fail) ||
        !read_field(body, "ui_password", storage->ui_password, sizeof storage->ui_password,
                    &patch.ui_password, &fail))
        return fail;

    uint32_t                   port = 0;
    const ot_json_read_t got  = ot_json_u32(body, "mqtt_port", &port);
    if (got == OT_JSON_FOUND) {
        patch.has_mqtt_port = true;
        // Carried as a uint32_t, and whether it is a PORT is ot_config_check_port()'s
        // answer, not this one. This layer says "a number"; asking the same question twice in two
        // places is how the two come to disagree.
        patch.mqtt_port = port;
    } else if (got != OT_JSON_MISSING) {
        return bad_field("mqtt_port");
    }

    bool                       discovery = false;
    const ot_json_read_t flag      = ot_json_bool(body, "ha_discovery", &discovery);
    if (flag == OT_JSON_FOUND) {
        patch.has_ha_discovery = true;
        patch.ha_discovery     = discovery;
    } else if (flag != OT_JSON_MISSING) {
        return bad_field("ha_discovery");
    }

    // The room-MQTT source's four settings: plain NS_APP fields, not
    // EXECUTOR_OWNED, so they arrive here rather than through the entity path. Without this
    // decode they were plumbed all the way to ot_config_apply() but unreachable from the wire,
    // so room_mqtt_enable could only ever be its default (false).
    bool                  room_enable = false;
    const ot_json_read_t room_enable_r = ot_json_bool(body, "room_mqtt_enable", &room_enable);
    if (room_enable_r == OT_JSON_FOUND) {
        patch.has_room_mqtt_enable = true;
        patch.room_mqtt_enable     = room_enable;
    } else if (room_enable_r != OT_JSON_MISSING) {
        return bad_field("room_mqtt_enable");
    }

    bool                  room_fwd = false;
    const ot_json_read_t room_fwd_r = ot_json_bool(body, "room_mqtt_ha_forwarded", &room_fwd);
    if (room_fwd_r == OT_JSON_FOUND) {
        patch.has_room_mqtt_ha_forwarded = true;
        patch.room_mqtt_ha_forwarded     = room_fwd;
    } else if (room_fwd_r != OT_JSON_MISSING) {
        return bad_field("room_mqtt_ha_forwarded");
    }

    // The executor's settings: eight numbers. read_number() keeps what read_field() keeps for the
    // strings -- every name appears once, next to the patch member it fills, which keeps the name
    // and the member side by side, so a review sees both.
    if (!read_number(body, "control_mode", &patch.has_control_mode, &patch.control_mode, &fail) ||
        !read_number(body, "watchdog_s", &patch.has_watchdog_s, &patch.watchdog_s, &fail) ||
        !read_number(body, "failsafe_setpoint_dc", &patch.has_failsafe_setpoint_dc,
                     &patch.failsafe_setpoint_dc, &fail) ||
        !read_number(body, "failsafe_room_target_dc", &patch.has_failsafe_room_target_dc,
                     &patch.failsafe_room_target_dc, &fail) ||
        !read_number(body, "failsafe_heat_days", &patch.has_failsafe_heat_days,
                     &patch.failsafe_heat_days, &fail) ||
        !read_number(body, "failsafe_min_cycle_s", &patch.has_failsafe_min_cycle_s,
                     &patch.failsafe_min_cycle_s, &fail) ||
        !read_number(body, "flow_min_dc", &patch.has_flow_min_dc, &patch.flow_min_dc, &fail) ||
        !read_number(body, "flow_max_dc", &patch.has_flow_max_dc, &patch.flow_max_dc, &fail) ||
        // The room-MQTT source's two numbers, carried the same way: as a
        // uint32_t here, narrowed to uint16_t by ot_config_apply() -- this layer says "a
        // number", not "a role" or "a staleness".
        !read_number(body, "room_mqtt_role", &patch.has_room_mqtt_role, &patch.room_mqtt_role,
                    &fail) ||
        !read_number(body, "room_mqtt_stale_s", &patch.has_room_mqtt_stale_s,
                    &patch.room_mqtt_stale_s, &fail))
        return fail;

    // Everything else in the body is IGNORED, and that includes ui_password_set, read_only and
    // wifi_known_good. They are derived by the device and appear in the projection only; the page
    // submits the whole document, so echoing them back has to be harmless. Ignoring unknown keys
    // in general is what lets a newer page talk to an older firmware without every save failing.
    *out = patch;
    return ok();
}

// --- GET /api/config ------------------------------------------------------------------------

size_t ot_wire_render_config(const ot_config_public_t *pub, bool known_good, char *out,
                             size_t cap)
{
    if (pub == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};

    put(&w, "{");
    put_key(&w, "wifi_ssid");
    // Not a secret: it is in every beacon that network sends, and it is what tells the page which
    // network is configured. The KEY is next to it and is the sentinel or "" -- never the value.
    put_string(&w, pub->ssid);
    put(&w, ",");
    put_key(&w, "wifi_psk");
    put_string(&w, pub->psk);
    put(&w, ",");
    put_key(&w, "mqtt_host");
    put_string(&w, pub->mqtt_host);
    put(&w, ",");
    put_key(&w, "mqtt_port");
    put_u32(&w, pub->mqtt_port);
    put(&w, ",");
    put_key(&w, "mqtt_user");
    put_string(&w, pub->mqtt_user);
    put(&w, ",");
    put_key(&w, "mqtt_password");
    put_string(&w, pub->mqtt_password);
    put(&w, ",");
    put_key(&w, "topic_prefix");
    put_string(&w, pub->topic_prefix);
    put(&w, ",");
    put_key(&w, "ha_discovery");
    put_bool(&w, pub->ha_discovery);
    put(&w, ",");
    put_key(&w, "device_name");
    put_string(&w, pub->device_name);
    put(&w, ",");
    put_key(&w, "tz");
    put_string(&w, pub->tz);
    put(&w, ",");
    put_key(&w, "ntp_server");
    put_string(&w, pub->ntp_server);
    put(&w, ",");
    put_key(&w, "dhw_enable");
    put_bool(&w, pub->dhw_enable);
    put(&w, ",");
    // The executor's settings, the five it owns included: read-only here is a property of POST,
    // not of GET -- the page has to show what LOCAL mode is holding, and whether it is in season.
    put_key(&w, "control_mode");
    put_u32(&w, pub->control_mode);
    put(&w, ",");
    put_key(&w, "heating_season");
    put_bool(&w, pub->heating_season);
    put(&w, ",");
    put_key(&w, "watchdog_s");
    put_u32(&w, pub->watchdog_s);
    put(&w, ",");
    put_key(&w, "failsafe_setpoint_dc");
    put_u32(&w, pub->failsafe_setpoint_dc);
    put(&w, ",");
    put_key(&w, "failsafe_room_target_dc");
    put_u32(&w, pub->failsafe_room_target_dc);
    put(&w, ",");
    put_key(&w, "failsafe_heat_days");
    put_u32(&w, pub->failsafe_heat_days);
    put(&w, ",");
    put_key(&w, "failsafe_min_cycle_s");
    put_u32(&w, pub->failsafe_min_cycle_s);
    put(&w, ",");
    put_key(&w, "flow_min_dc");
    put_u32(&w, pub->flow_min_dc);
    put(&w, ",");
    put_key(&w, "flow_max_dc");
    put_u32(&w, pub->flow_max_dc);
    put(&w, ",");
    // The room-MQTT source's four settings: ordinary NS_APP fields, so the page
    // shows and saves them the same way as any other setting, unlike the five above them.
    put_key(&w, "room_mqtt_enable");
    put_bool(&w, pub->room_mqtt_enable);
    put(&w, ",");
    put_key(&w, "room_mqtt_role");
    put_u32(&w, pub->room_mqtt_role);
    put(&w, ",");
    put_key(&w, "room_mqtt_stale_s");
    put_u32(&w, pub->room_mqtt_stale_s);
    put(&w, ",");
    put_key(&w, "room_mqtt_ha_forwarded");
    put_bool(&w, pub->room_mqtt_ha_forwarded);
    put(&w, ",");
    put_key(&w, "local_ch_enable");
    put_bool(&w, pub->local_ch_enable);
    put(&w, ",");
    put_key(&w, "local_ch_setpoint_dc");
    put_u32(&w, pub->local_ch_setpoint_dc);
    put(&w, ",");
    put_key(&w, "dhw_setpoint_dc");
    put_u32(&w, pub->dhw_setpoint_dc);
    put(&w, ",");
    put_key(&w, "ui_password");
    put_string(&w, pub->ui_password);
    put(&w, ",");
    // Derived, and read-only. A stored "a password is set" flag that disagreed with the stored
    // record would be a device locked by nobody, for ever (ot_config.h, ot_config_t.ui_pw_hash
    // and ot_config_password_set()), which is why this is computed from the record's emptiness
    // every time it is asked.
    put_key(&w, "ui_password_set");
    put_bool(&w, pub->ui_password_set);
    put(&w, ",");
    put_key(&w, "read_only");
    put_bool(&w, pub->read_only);
    put(&w, ",");
    // From the provisioning machine, not from the stored document. It is the ONLY condition under
    // which a failed provisioning rolls back, and `wifi_ssid != ""` is not a substitute: an SSID
    // stored beside a mistyped key has never produced an address.
    put_key(&w, "wifi_known_good");
    put_bool(&w, known_good);
    put(&w, "}");

    return finish(&w);
}
