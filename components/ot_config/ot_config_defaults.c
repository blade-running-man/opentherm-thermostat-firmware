// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What a fresh device holds, and what a value that came back damaged from flash is repaired to.
// One file for both because they are one answer asked twice: every value ot_config_sanitize()
// puts back is one ot_config_defaults() would have written. DO NOT move sanitize next to the
// checkers it calls -- it would take default_prefix(), default_name() and DEFAULT_MQTT_PORT with
// it, or leave them written down twice.
#include "ot_config.h"

#include <stdio.h>
#include <string.h>

#include "ot_config_internal.h"

// IANA has 1883 for MQTT and 8883 for MQTT over TLS. TLS is deferred,
// so the second number is deliberately absent: a default the transport cannot honour is worse
// than no default at all.
#define DEFAULT_MQTT_PORT 1883

// The executor's defaults. The two flow bounds and the failsafe setpoint are one
// triple: ot_config_sanitize() puts all three back together when the flash hands back a band that
// does not contain its setpoint.
#define DEFAULT_WATCHDOG_S           900
#define DEFAULT_FAILSAFE_SETPOINT_DC 450
#define DEFAULT_ROOM_TARGET_DC       180
#define DEFAULT_HEAT_DAYS            3
#define DEFAULT_MIN_CYCLE_S          600
#define DEFAULT_FLOW_MIN_DC          400
#define DEFAULT_FLOW_MAX_DC          700
#define DEFAULT_LOCAL_CH_SETPOINT_DC 450
// The MQTT room-source slot. "room", not "ambient": a slot the owner turns on is assumed to
// be the thing they meant to steer with, not a second outdoor-style reading.
#define DEFAULT_ROOM_MQTT_ROLE       1u
#define DEFAULT_ROOM_MQTT_STALE_S    900u

// --- defaults ----------------------------------------------------------------------------------

// Exactly OT_CONFIG_DEVICE_ID_LEN hex digits. Case is accepted either way and never
// rewritten: a caller that rendered its MAC with %02X gets its own string back rather than a
// silent second spelling of the same device.
static bool device_id_ok(const char *id)
{
    if (id == NULL)
        return false;
    size_t n = 0;
    for (; id[n] != '\0'; n++) {
        if (n >= OT_CONFIG_DEVICE_ID_LEN || !is_hex(id[n]))
            return false;
    }
    return n == OT_CONFIG_DEVICE_ID_LEN;
}

static void default_prefix(char *out, size_t cap, const char *device_id)
{
    // The identity is the full MAC, never the
    // name. A prefix keyed on the name means renaming re-keys every topic, the old retained
    // discovery messages stay in the broker for ever, and Home Assistant shows two devices of
    // which one is dead and cannot be removed.
    if (device_id_ok(device_id))
        snprintf(out, cap, "opentherm/%s", device_id);
    else
        // A WORSE default -- two units in one house would share a topic tree -- and still much
        // better than refusing to boot over it. ot_config_sanitize() reports the field so
        // the owner is not left guessing.
        snprintf(out, cap, "opentherm");
}

static void default_name(char *out, size_t cap, const char *device_id)
{
    // ot_net.c names the access point after the last two MAC bytes, so a device the owner
    // joined as `opentherm-e5f6` announces itself with the same four digits. Display only:
    // this is about recognising one of two identical units, never about identity.
    if (device_id_ok(device_id))
        snprintf(out, cap, "Термостат OpenTherm %s",
                 device_id + OT_CONFIG_DEVICE_ID_LEN - 4);
    else
        snprintf(out, cap, "Термостат OpenTherm");
}

void ot_config_defaults(ot_config_t *cfg, const char *device_id)
{
    if (cfg == NULL)
        return;
    memset(cfg, 0, sizeof *cfg);
    cfg->mqtt_port = DEFAULT_MQTT_PORT;
    // On by default, and it costs nothing until a broker is configured -- there is nowhere to
    // publish discovery to before then. MQTT with HA discovery is the control path, and
    // "Home Assistant sees the device" is an acceptance
    // criterion; an owner who typed a broker address and then had to find a checkbox would be
    // paying for a default that protects nobody.
    cfg->ha_discovery = true;
    default_prefix(cfg->topic_prefix, sizeof cfg->topic_prefix, device_id);
    default_name(cfg->device_name, sizeof cfg->device_name, device_id);
    // MSK-3 is UTC+3. The sign in a POSIX TZ is inverted relative to the one everybody says out
    // loud, and this is the single most common way a schedule ends up firing at the wrong hour.
    copy_str(cfg->tz, sizeof cfg->tz, "MSK-3");
    copy_str(cfg->ntp_server, sizeof cfg->ntp_server, "pool.ntp.org");
    // True: hot water is what a thermostat permits by default, and the boiler manages it
    // itself. This is the one bit of ID 0 that becomes non-zero on a fresh flash, and
    // the owner must confirm on hardware that hot water still behaves.
    cfg->dhw_enable = true;
    // The executor's settings. heating_season and local_ch_enable are false and memset already
    // made them so; written out because a reader looking for the answer to "does a freshly
    // flashed device ask the boiler for heat" must find it here, in words, and not infer it from
    // the absence of a line. It does not: LOCAL mode, out of season, with LOCAL's own CH switch
    // off.
    cfg->control_mode            = OT_CONFIG_MODE_LOCAL;
    cfg->heating_season          = false;
    cfg->watchdog_s              = DEFAULT_WATCHDOG_S;
    cfg->failsafe_setpoint_dc    = DEFAULT_FAILSAFE_SETPOINT_DC;
    cfg->failsafe_room_target_dc = DEFAULT_ROOM_TARGET_DC;
    cfg->failsafe_heat_days      = DEFAULT_HEAT_DAYS;
    cfg->failsafe_min_cycle_s    = DEFAULT_MIN_CYCLE_S;
    cfg->flow_min_dc             = DEFAULT_FLOW_MIN_DC;
    cfg->flow_max_dc             = DEFAULT_FLOW_MAX_DC;
    cfg->local_ch_enable         = false;
    cfg->local_ch_setpoint_dc    = DEFAULT_LOCAL_CH_SETPOINT_DC;
    cfg->dhw_setpoint_dc         = 0;  // unset: no ID 56 goes out until somebody writes one
    // The MQTT room-source slot, off until the owner turns it on: a freshly flashed device
    // must not start trusting a topic nobody has wired up in Home Assistant yet.
    cfg->room_mqtt_enable       = false;
    cfg->room_mqtt_role         = DEFAULT_ROOM_MQTT_ROLE;
    cfg->room_mqtt_stale_s      = DEFAULT_ROOM_MQTT_STALE_S;
    cfg->room_mqtt_ha_forwarded = false;
}

// --- what came back out of the flash ---------------------------------------------------------

// A number from flash, judged by the checker ot_config_apply() refuses by and replaced by its
// default when that checker says no. ot_config_io_load_u16() takes whatever sixteen bits the key
// holds, so this is the only place a stored number is ever looked at.
static void repair_number(ot_config_field_t field, uint16_t *value, uint16_t fallback,
                          ot_config_repairs_t *repairs)
{
    if (ot_config_check_range(field, *value) != OT_CONFIG_OK) {
        *value = fallback;
        *repairs |= OT_CONFIG_REPAIRED(field);
    }
}

ot_config_repairs_t ot_config_sanitize(ot_config_t *cfg, const char *device_id)
{
    if (cfg == NULL)
        return 0;

    ot_config_repairs_t repairs = 0;

    // The Wi-Fi pair is repaired as ONE record because it is one, and an empty record is not a
    // repair -- a device with no network is a device on its own access point, which is a state,
    // not damage. A pair that is half readable is worse than none: it keeps the device looking
    // configured while it is unreachable, and the invariant that outranks everything else here is
    // that no state exists with no network, no access point and no way in.
    // The predicate is ot_config_wifi_usable() and not two checkers spelled out here,
    // because ot_config_nvs_has_credentials() asks the same question about the same blob
    // and the two answers decide different halves of one outcome: what the station is handed, and
    // whether an access point goes on air at all. They were two predicates once. See the DO NOT
    // on ot_config_wifi_usable().
    if (cfg->wifi.ssid_len != 0 || cfg->wifi.psk_len != 0) {
        if (!ot_config_wifi_usable(&cfg->wifi)) {
            memset(&cfg->wifi, 0, sizeof cfg->wifi);
            repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_WIFI_SSID);
            repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_WIFI_PSK);
        }
    }

    // `terminated()` is asked FIRST in each of these, and the short-circuit is load-bearing: the
    // checker takes a C string, and running one over an array with no NUL in it is the very
    // read past the end this is guarding against.
    if (!terminated(cfg->mqtt_host, sizeof cfg->mqtt_host) ||
        ot_config_check_host(cfg->mqtt_host) != OT_CONFIG_OK) {
        memset(cfg->mqtt_host, 0, sizeof cfg->mqtt_host);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_HOST);
    }
    if (ot_config_check_port(cfg->mqtt_port) != OT_CONFIG_OK) {
        cfg->mqtt_port = DEFAULT_MQTT_PORT;
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_PORT);
    }
    if (!terminated(cfg->mqtt_user, sizeof cfg->mqtt_user) ||
        ot_config_check_user(cfg->mqtt_user) != OT_CONFIG_OK) {
        memset(cfg->mqtt_user, 0, sizeof cfg->mqtt_user);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_USER);
    }
    if (!terminated(cfg->mqtt_password, sizeof cfg->mqtt_password) ||
        ot_config_check_broker_password(cfg->mqtt_password) != OT_CONFIG_OK) {
        memset(cfg->mqtt_password, 0, sizeof cfg->mqtt_password);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_MQTT_PASSWORD);
    }
    if (!terminated(cfg->topic_prefix, sizeof cfg->topic_prefix) ||
        ot_config_check_prefix(cfg->topic_prefix) != OT_CONFIG_OK) {
        default_prefix(cfg->topic_prefix, sizeof cfg->topic_prefix, device_id);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_TOPIC_PREFIX);
    }
    if (!terminated(cfg->device_name, sizeof cfg->device_name) ||
        ot_config_check_name(cfg->device_name) != OT_CONFIG_OK) {
        default_name(cfg->device_name, sizeof cfg->device_name, device_id);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_DEVICE_NAME);
    }
    // Repaired to the default rather than cleared. An empty zone is UTC and an empty NTP server
    // is no clock at all; either leaves a wall-clock feature wrong instead of absent, and
    // the result must never be "nothing".
    if (!terminated(cfg->tz, sizeof cfg->tz) ||
        ot_config_check_tz(cfg->tz) != OT_CONFIG_OK) {
        copy_str(cfg->tz, sizeof cfg->tz, "MSK-3");
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_TZ);
    }
    if (!terminated(cfg->ntp_server, sizeof cfg->ntp_server) ||
        ot_config_check_ntp(cfg->ntp_server) != OT_CONFIG_OK) {
        copy_str(cfg->ntp_server, sizeof cfg->ntp_server, "pool.ntp.org");
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_NTP_SERVER);
    }

    // The executor's numbers, each against its own bounds first. For the four that become ID 1 the
    // bounds include the half-degree grid, so a value off it is damage to that field like one out of
    // bounds, and gets the same answer. DO NOT round it onto the grid instead: every writer refuses
    // an off-grid value (ot_config_check_range) and no schema before 2 stored these keys, so its
    // nearness to a grid point is noise -- and rounding a failsafe of 453 in a 400..450 band gives
    // 455, outside the band, which then threw a legal band away over a fault in another field.
    repair_number(OT_CONFIG_F_CONTROL_MODE, &cfg->control_mode, OT_CONFIG_MODE_LOCAL, &repairs);
    repair_number(OT_CONFIG_F_WATCHDOG_S, &cfg->watchdog_s, DEFAULT_WATCHDOG_S, &repairs);
    repair_number(OT_CONFIG_F_FAILSAFE_ROOM_TARGET, &cfg->failsafe_room_target_dc,
                  DEFAULT_ROOM_TARGET_DC, &repairs);
    repair_number(OT_CONFIG_F_FAILSAFE_HEAT_DAYS, &cfg->failsafe_heat_days, DEFAULT_HEAT_DAYS,
                  &repairs);
    repair_number(OT_CONFIG_F_FAILSAFE_MIN_CYCLE, &cfg->failsafe_min_cycle_s,
                  DEFAULT_MIN_CYCLE_S, &repairs);
    repair_number(OT_CONFIG_F_FLOW_MIN, &cfg->flow_min_dc, DEFAULT_FLOW_MIN_DC, &repairs);
    repair_number(OT_CONFIG_F_FLOW_MAX, &cfg->flow_max_dc, DEFAULT_FLOW_MAX_DC, &repairs);
    // AFTER the band, and into it: a failsafe that failed its own check is known to be the damaged
    // field, so it alone is repaired -- to its default, moved to the nearest edge of the band the way
    // a stranded local setpoint is. The bare default in a 500..600 band would fail the triple rule
    // below and reset a band nothing was wrong with. An inverted band is reset whole below whatever
    // this picks, so into_band() on it is harmless.
    repair_number(OT_CONFIG_F_FAILSAFE_SETPOINT, &cfg->failsafe_setpoint_dc,
                  into_band(DEFAULT_FAILSAFE_SETPOINT_DC, cfg->flow_min_dc, cfg->flow_max_dc),
                  &repairs);
    repair_number(OT_CONFIG_F_LOCAL_CH_SETPOINT, &cfg->local_ch_setpoint_dc,
                  DEFAULT_LOCAL_CH_SETPOINT_DC, &repairs);
    repair_number(OT_CONFIG_F_DHW_SETPOINT, &cfg->dhw_setpoint_dc, 0, &repairs);
    // The MQTT room-source slot's numbers. The two bools either side of it need no repair here,
    // like ha_discovery and dhw_enable above -- a bool loaded from a u8 (ot_config_io_load_bool)
    // is already 0 or 1, and there is no third value to reject.
    repair_number(OT_CONFIG_F_ROOM_MQTT_ROLE, &cfg->room_mqtt_role, DEFAULT_ROOM_MQTT_ROLE,
                  &repairs);
    repair_number(OT_CONFIG_F_ROOM_MQTT_STALE_S, &cfg->room_mqtt_stale_s, DEFAULT_ROOM_MQTT_STALE_S,
                  &repairs);
    // ...then the rules that span them. Which of three legal-looking values the flash damaged
    // cannot be known, so the triple goes back to the defaults whole and all three are reported.
    // (A value that failed its OWN check above is known, which is why it was repaired alone.)
    // DO NOT "keep the owner's flow_min" here: the defaults are the one triple known to satisfy
    // the rule, and the repair bits are what tell the owner to check flow_min against the boiler's
    // parameter E again -- a kept half is a band nobody chose.
    if (ot_config_check_flow(cfg->flow_min_dc, cfg->flow_max_dc, cfg->failsafe_setpoint_dc) !=
        OT_CONFIG_OK) {
        cfg->flow_min_dc          = DEFAULT_FLOW_MIN_DC;
        cfg->flow_max_dc          = DEFAULT_FLOW_MAX_DC;
        cfg->failsafe_setpoint_dc = DEFAULT_FAILSAFE_SETPOINT_DC;
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MIN) |
                   OT_CONFIG_REPAIRED(OT_CONFIG_F_FLOW_MAX) |
                   OT_CONFIG_REPAIRED(OT_CONFIG_F_FAILSAFE_SETPOINT);
    }
    // A local setpoint the band -- legal now, and perhaps just reset above -- does not contain
    // moves to its nearest edge: the rule ot_config_apply() keeps on the way in. Left where it is,
    // ot_control would clamp it silently and ID 1 would disagree with the page.
    const uint16_t local = into_band(cfg->local_ch_setpoint_dc, cfg->flow_min_dc, cfg->flow_max_dc);
    if (local != cfg->local_ch_setpoint_dc) {
        cfg->local_ch_setpoint_dc = local;
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_LOCAL_CH_SETPOINT);
    }
    // AFTER the broker address above, which may just have been cleared as damaged. HA mode with no
    // broker never hears the command that ends ha_waiting, and the way in refuses it.
    if (ot_config_check_mode(cfg->control_mode, cfg->mqtt_host) != OT_CONFIG_OK) {
        cfg->control_mode = OT_CONFIG_MODE_LOCAL;
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_CONTROL_MODE);
    }

    // The uncomfortable one, and it is deliberate. A record that cannot be parsed matches no
    // password that exists, so KEEPING it locks the owner out of a device that is visible on
    // their network and completely unusable -- no way in over the air, and a boiler the house
    // depends on still being driven by it. Clearing it leaves the device open on
    // the LAN, which the owner can SEE and fix. That is the no-way-in invariant applied to the
    // password instead of to the radio. DO NOT "harden" this into keeping the record.
    if (!terminated(cfg->ui_pw_hash, sizeof cfg->ui_pw_hash)) {
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_UI_PASSWORD);
    } else if (cfg->ui_pw_hash[0] != '\0' && !ot_config_parse_record(cfg->ui_pw_hash, NULL)) {
        memset(cfg->ui_pw_hash, 0, sizeof cfg->ui_pw_hash);
        repairs |= OT_CONFIG_REPAIRED(OT_CONFIG_F_UI_PASSWORD);
    }

    // Nothing here logs. A rejected value may itself be a secret and the log ring is served to
    // whoever can reach the device, so the bitmask is how the fact travels --
    // and it has to travel, because settings that vanish silently are indistinguishable
    // from a firmware that reset itself.
    return repairs;
}
