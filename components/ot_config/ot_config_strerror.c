// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The sentence for every ot_config_err_t. One switch with no default, so an error code added to
// the enum without a sentence fails the host build (-Werror=switch, [env:native]) rather than
// reaching the owner as a fallback sentence they cannot act on. The board build has no -Wall.
#include "ot_config.h"

const char *ot_config_strerror(ot_config_err_t err)
{
    // Not one of these quotes the value it rejected. They travel in an HTTP body AND through the
    // log ring, and /api/log is readable by whoever can reach the device --
    // so a message that echoed the input would publish a password the first time somebody typed
    // one into the wrong box.
    switch (err) {
    case OT_CONFIG_OK:
        return "accepted";
    case OT_CONFIG_ERR_SSID:
        return "the network name must be 1 to 32 characters";
    case OT_CONFIG_ERR_PSK:
        return "the network password must be 8 to 63 characters, or exactly 64 hexadecimal "
               "digits, or empty for an open network";
    case OT_CONFIG_ERR_WIFI_PAIR:
        return "the network name and its password are stored as one record: send both, and "
               "re-enter the password whenever the name changes";
    case OT_CONFIG_ERR_HOST:
        return "the broker address must be a host name or an IP address, with no scheme and no "
               "port";
    case OT_CONFIG_ERR_PORT:
        return "the broker port must be between 1 and 65535";
    case OT_CONFIG_ERR_USER:
        return "the broker user name is longer than this device can store";
    case OT_CONFIG_ERR_BROKER_PASSWORD:
        return "the broker password is longer than this device can store";
    case OT_CONFIG_ERR_NTP:
        return "the time server must be a host name or an IP address, with no scheme and no "
               "port, and it may not be empty";
    case OT_CONFIG_ERR_TZ:
        // The example is the owner's own default, and it carries the trap: in a POSIX TZ the
        // sign is INVERTED -- UTC+3 is written MSK-3. Somebody who "fixes" it to MSK+3 lands six
        // hours away and blames the schedule.
        return "the time zone must be a POSIX TZ string such as MSK-3, without spaces";
    case OT_CONFIG_ERR_PREFIX:
        return "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without "
               "an empty level";
    case OT_CONFIG_ERR_NAME:
        // Bytes, because that is what OT_CONFIG_NAME_MAX counts: the Cyrillic default takes two
        // per letter. "1 to 32 characters" was the number before that default existed.
        return "the device name must be 1 to 48 bytes (a Cyrillic letter takes two) and contain "
               "no control characters";
    case OT_CONFIG_ERR_UI_PASSWORD:
        return "the web password must be 8 to 128 characters and contain no control characters";
    case OT_CONFIG_ERR_NO_HASH:
        return "this device could not secure the password just now; nothing was changed";
    case OT_CONFIG_ERR_READ_ONLY:
        return "these settings were written by a newer firmware and cannot be changed until the "
               "device is reset";
    case OT_CONFIG_ERR_NO_DOCUMENT:
        return "the request carried no settings, so nothing was saved";
    // The executor's sentences name FIELDS by their API names, because these are refusals of a number the
    // owner typed into a box labelled with that name -- and still never the number itself.
    case OT_CONFIG_ERR_FLOW:
        return "the lowest flow temperature (flow_min_dc) must be below the highest (flow_max_dc)";
    case OT_CONFIG_ERR_FAILSAFE:
        return "the failsafe setpoint (failsafe_setpoint_dc) must lie between flow_min_dc and "
               "flow_max_dc";
    case OT_CONFIG_ERR_MODE_NEEDS_BROKER:
        return "Home Assistant mode (control_mode 1) needs a broker: set mqtt_host first, and "
               "return to local mode (control_mode 0) before clearing it";
    case OT_CONFIG_ERR_RANGE:
        // Every bound in one sentence, because the refusal travels without its field --
        // ot_config_apply() returns a code, not a name -- and "out of range" alone would leave
        // the owner guessing which of ten numbers it meant.
        return "a number is out of range: control_mode 0 or 1, watchdog_s 60 to 7200, "
               "failsafe_room_target_dc 50 to 300, failsafe_heat_days 1 to 30, "
               "failsafe_min_cycle_s 60 to 3600, dhw_setpoint_dc 0 to 900, and every other "
               "_dc value 100 to 900 in steps of 5 (tenths of a degree: half a degree)";
    case OT_CONFIG_ERR_READ_ONLY_FIELD:
        return "this value belongs to the controls and is changed through "
               "POST /api/entities/<key> (heating_season, ch_enable, ch_setpoint, dhw_enable, "
               "dhw_setpoint), not through the settings";
    case OT_CONFIG_ERR_BROKER_PAIR:
        return "the broker address (mqtt_host) and its password are stored as one record: "
               "re-enter the broker password whenever the address changes";
    }
    // A code from a build that is not this one. Saying something is better than a NULL a
    // renderer will dereference.
    return "the settings were refused";
}

// One switch with no default, for the reason ot_config_strerror() has none. The guard is the host
// build and one test: a code added without a case fails -Werror=switch, and a code with a sentence
// but no name fails test_every_refusal_has_a_name_of_its_own.
const char *ot_config_err_name(ot_config_err_t err)
{
    switch (err) {
    case OT_CONFIG_OK:                    return "ok";
    case OT_CONFIG_ERR_SSID:              return "ssid";
    case OT_CONFIG_ERR_PSK:               return "psk";
    case OT_CONFIG_ERR_WIFI_PAIR:         return "wifi-pair";
    case OT_CONFIG_ERR_HOST:              return "mqtt-host";
    case OT_CONFIG_ERR_PORT:              return "mqtt-port";
    case OT_CONFIG_ERR_USER:              return "mqtt-user";
    case OT_CONFIG_ERR_BROKER_PASSWORD:   return "mqtt-password";
    case OT_CONFIG_ERR_PREFIX:            return "topic-prefix";
    case OT_CONFIG_ERR_NAME:              return "device-name";
    case OT_CONFIG_ERR_UI_PASSWORD:       return "ui-password";
    case OT_CONFIG_ERR_NO_HASH:           return "no-hash";
    case OT_CONFIG_ERR_READ_ONLY:         return "read-only";
    case OT_CONFIG_ERR_NO_DOCUMENT:       return "no-document";
    case OT_CONFIG_ERR_TZ:                return "tz";
    case OT_CONFIG_ERR_NTP:               return "ntp-server";
    case OT_CONFIG_ERR_FLOW:              return "flow";
    case OT_CONFIG_ERR_FAILSAFE:          return "failsafe";
    case OT_CONFIG_ERR_MODE_NEEDS_BROKER: return "mode-needs-broker";
    case OT_CONFIG_ERR_RANGE:             return "range";
    case OT_CONFIG_ERR_READ_ONLY_FIELD:   return "read-only-field";
    case OT_CONFIG_ERR_BROKER_PAIR:       return "broker-pair";
    }
    return "rejected";
}
