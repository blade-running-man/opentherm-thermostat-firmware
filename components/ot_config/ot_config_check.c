// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What a value has to look like to be storable: every ot_config_check_*() and the one predicate
// built from two of them, ot_config_wifi_usable(). Pure, like the whole half described at the top
// of ot_config.c; the reason for each rule sits beside the rule.
#include "ot_config.h"

#include <string.h>

#include "ot_config_internal.h"

// --- small things ----------------------------------------------------------------------------

// Control characters and DEL. Everything at 0x80 and above is left alone: those bytes are UTF-8
// and a device called "Термостат" is a device with a name, not an error. The renderer escapes
// what JSON needs escaped (ot_api_escape_json), so this is not the place to police it.
static bool is_control(unsigned char c) { return c < 0x20 || c == 0x7f; }

// NULL is treated as the empty string throughout, one rule for every checker: a value that is
// not there is not a malformed value. Whether empty is ACCEPTABLE is then each field's own
// answer -- it is for a broker host, it is not for a topic prefix.
static const char *or_empty(const char *s) { return s != NULL ? s : ""; }

// --- what is acceptable ------------------------------------------------------------------------

ot_config_err_t ot_config_check_ssid(const uint8_t *ssid, size_t len)
{
    if (ssid == NULL || len == 0 || len > OT_CONFIG_SSID_MAX)
        return OT_CONFIG_ERR_SSID;

    // wifi_sta_config_t carries an ssid[32] and no length beside it
    // (esp_wifi_types_generic.h:559-593), so a name shorter than the array is terminated and an
    // embedded NUL cannot be expressed at all: the driver would associate with a shorter name
    // than the one stored, and nothing in the symptom would say so.
    for (size_t i = 0; i < len; i++)
        if (ssid[i] == 0)
            return OT_CONFIG_ERR_SSID;
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_psk(const uint8_t *psk, size_t len)
{
    // An open network is a network. Refusing this would make the device impossible to put on
    // one, and a guest network with no key is the normal case in half the houses that have one.
    if (len == 0)
        return OT_CONFIG_OK;

    if (psk == NULL || len < OT_CONFIG_PSK_MIN || len > OT_CONFIG_PSK_MAX)
        return OT_CONFIG_ERR_PSK;

    // wpa_supplicant measures the key with strlen() (rsn_supp/wpa.c:2517), so a NUL inside it
    // does not truncate the value -- it changes which BRANCH runs, and a 64-byte key with a NUL
    // at byte ten is run through pbkdf2_sha1 as a ten-character passphrase.
    for (size_t i = 0; i < len; i++)
        if (psk[i] == 0)
            return OT_CONFIG_ERR_PSK;

    if (len == OT_CONFIG_PSK_MAX) {
        // 64 characters is not a long passphrase, it is the 256-bit key written as hex, and
        // wpa_supplicant decides that by LENGTH ALONE: 64 goes to hexstr2bin, anything else to
        // pbkdf2_sha1 (rsn_supp/wpa.c:2517-2524). When hexstr2bin fails there the function
        // simply returns -- no PMK is computed, no error is raised, and the device silently
        // never associates. Refused here, because on the way out it is not an error, it is
        // silence.
        for (size_t i = 0; i < len; i++)
            if (!is_hex((char)psk[i]))
                return OT_CONFIG_ERR_PSK;
    }
    return OT_CONFIG_OK;
}

// A hostname, an IPv4 literal or an IPv6 literal, and nothing else. Underscore is in the set
// although RFC 952 does not allow it: real LAN names contain one, resolvers answer them, and a
// store that refuses a broker the owner can ping is a bug they cannot work around. `/` is out,
// which is what makes "mqtt://broker" a refusal the owner can read rather than a device that
// resolves nothing for ever -- the port is its own field, so a URL here can never be honoured.
static bool host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '-' || c == '_' || c == ':' || c == '[' || c == ']';
}

ot_config_err_t ot_config_check_host(const char *host)
{
    const char *h = or_empty(host);
    // No broker at all is a supported configuration: the unit ventilates the house whether or
    // not anyone runs one, and CLAUDE.md's rule that nothing reboots because a peer is absent
    // starts with being able to say "I do not use MQTT".
    if (h[0] == '\0')
        return OT_CONFIG_OK;

    const size_t len = strlen(h);
    // RFC 1035 2.3.4, and deliberately not a rounder smaller number: a cap that refuses a name
    // the owner's resolver answers is a bug report nobody can act on.
    if (len > OT_CONFIG_HOST_MAX)
        return OT_CONFIG_ERR_HOST;
    for (size_t i = 0; i < len; i++)
        if (!host_char(h[i]))
            return OT_CONFIG_ERR_HOST;
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_port(uint32_t port)
{
    // uint32_t, not uint16_t, and this is the whole reason: 70000 arrives from a JSON body and
    // has to be REFUSED. Taken as a uint16_t it becomes 4464, and the device then talks to a
    // port the owner never typed -- "validated" quietly turned into "accepted something else".
    if (port == 0 || port > 65535)
        return OT_CONFIG_ERR_PORT;
    return OT_CONFIG_OK;
}

// Length, and nothing else, for both broker credentials. MQTT 3.1.1 3.1.3 makes the user name a
// UTF-8 string and the password binary data; a store that decides which byte sequences a broker
// is allowed to accept refuses somebody's real credential, and there is nothing on the other
// side to protect -- a credential the broker rejects is a connection error the owner can read.
ot_config_err_t ot_config_check_user(const char *user)
{
    return strlen(or_empty(user)) <= OT_CONFIG_USER_MAX ? OT_CONFIG_OK
                                                             : OT_CONFIG_ERR_USER;
}

ot_config_err_t ot_config_check_broker_password(const char *password)
{
    return strlen(or_empty(password)) <= OT_CONFIG_BROKER_PASS_MAX
               ? OT_CONFIG_OK
               : OT_CONFIG_ERR_BROKER_PASSWORD;
}

ot_config_err_t ot_config_check_ntp(const char *server)
{
    const char *p = or_empty(server);
    if (p[0] == '\0')
        return OT_CONFIG_ERR_NTP;
    // Shape delegated on purpose: a second opinion about what a host name looks like is a second
    // opinion that drifts from the first. Only the emptiness rule differs, and it differs for a
    // reason worth one line rather than a whole copy of the parser.
    return (ot_config_check_host(p) == OT_CONFIG_OK) ? OT_CONFIG_OK : OT_CONFIG_ERR_NTP;
}

ot_config_err_t ot_config_check_tz(const char *tz)
{
    const char *p = or_empty(tz);
    // Empty is refused rather than defaulted: an empty TZ makes tzset() fall back to UTC, and a
    // schedule silently three hours out is worse than a save that failed loudly.
    if (p[0] == '\0')
        return OT_CONFIG_ERR_TZ;
    const size_t len = strlen(p);
    if (len > OT_CONFIG_TZ_MAX)
        return OT_CONFIG_ERR_TZ;
    // Printable ASCII, no space. This string goes into the process environment verbatim, and a
    // POSIX TZ has no legal use for a space or a control byte; refusing them here is what keeps
    // setenv() from carrying one.
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)p[i];
        if (c <= 0x20u || c >= 0x7fu)
            return OT_CONFIG_ERR_TZ;
    }
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_prefix(const char *prefix)
{
    const char *p = or_empty(prefix);
    // Empty is refused rather than defaulted because this function does not know the device id
    // and must not invent one; ot_config_sanitize() does know it and puts the default
    // back. An empty prefix would publish to topics beginning with a slash.
    if (p[0] == '\0')
        return OT_CONFIG_ERR_PREFIX;

    const size_t len = strlen(p);
    if (len > OT_CONFIG_PREFIX_MAX)
        return OT_CONFIG_ERR_PREFIX;
    // $SYS and its neighbours are the broker's own tree. Publishing into it is refused by every
    // broker worth using and accepted silently by the rest, which is worse.
    if (p[0] == '$')
        return OT_CONFIG_ERR_PREFIX;
    // Topics are built as prefix + "/" + entity, so a leading or trailing slash makes an empty
    // level -- `opentherm//state`. That is legal MQTT and the worst kind of wrong: it works, it
    // is subscribable, and it does not match the topic anybody types by hand.
    if (p[0] == '/' || p[len - 1] == '/')
        return OT_CONFIG_ERR_PREFIX;

    for (size_t i = 0; i < len; i++) {
        // MQTT 3.1.1 4.7.1: `+` and `#` are subscription syntax and are not legal in a topic
        // being published to. A broker's answer to one is to drop the connection, which reads
        // from here as a broker that keeps disconnecting for no reason anybody can see.
        if (p[i] == '+' || p[i] == '#')
            return OT_CONFIG_ERR_PREFIX;
        if (p[i] == ' ' || is_control((unsigned char)p[i]))
            return OT_CONFIG_ERR_PREFIX;
        if (p[i] == '/' && p[i + 1] == '/')
            return OT_CONFIG_ERR_PREFIX;
    }
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_name(const char *name)
{
    const char *n = or_empty(name);
    // Same reason as the prefix: a nameless device in Home Assistant is not a configuration
    // anyone wants, and the default that replaces it needs a device id this function has not
    // got.
    if (n[0] == '\0')
        return OT_CONFIG_ERR_NAME;

    const size_t len = strlen(n);
    if (len > OT_CONFIG_NAME_MAX)
        return OT_CONFIG_ERR_NAME;
    // A pasted newline is the realistic case and it ends up in a log line and an MQTT payload.
    for (size_t i = 0; i < len; i++)
        if (is_control((unsigned char)n[i]))
            return OT_CONFIG_ERR_NAME;
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_ui_password(const char *password)
{
    const char *p = or_empty(password);
    const size_t len = strlen(p);
    if (len < OT_CONFIG_UI_PASS_MIN || len > OT_CONFIG_UI_PASS_MAX)
        return OT_CONFIG_ERR_UI_PASSWORD;
    for (size_t i = 0; i < len; i++)
        if (is_control((unsigned char)p[i]))
            return OT_CONFIG_ERR_UI_PASSWORD;
    return OT_CONFIG_OK;
}

bool ot_config_wifi_usable(const ot_wifi_t *wifi)
{
    if (wifi == NULL)
        return false;
    // BOTH checkers, and the same two ot_config_sanitize() runs. DO NOT reduce this to a
    // length test on the SSID "because that is what asking whether a network is stored means":
    // that is what it did, and it made the access point decision disagree with the document. One
    // flipped bit in the PSK length byte -- 0x08 to 0x88 -- was a credential the provisioning
    // state machine believed in and a record sanitize() cleared, which is a device with no
    // network, no access point and no way back. check_ssid() already refuses
    // a zero length, so the emptiness test is inside this and not beside it.
    return ot_config_check_ssid(wifi->ssid, wifi->ssid_len) == OT_CONFIG_OK &&
           ot_config_check_psk(wifi->psk, wifi->psk_len) == OT_CONFIG_OK;
}

// --- the executor's numbers ---------------------------------------------------

// One table, one rule -- inside both ends and on the step, or refused -- and the one place a bound
// is written down: ot_config_apply() refuses by it and ot_config_sanitize() repairs by it, so the
// two cannot come to disagree about what a legal watchdog is. The four flow-bounded fields share
// the registry's ID 1 limits and the half-degree grid because each of them is, sooner or later, a
// value of ID 1 on the bus. The room target is a temperature too, and is NOT on the grid: nothing
// quantises it, it is only compared with a room reading.
#define HALF OT_CONFIG_HALF_DEGREE_DC
_Static_assert(OT_CONFIG_FLOW_DC_MIN % HALF == 0 && OT_CONFIG_FLOW_DC_MAX % HALF == 0,
               "rounding a flow value onto the grid must never leave the band");
static const struct {
    ot_config_field_t field;
    uint32_t          lo;
    uint32_t          hi;
    uint32_t          step;
} RANGES[] = {
    {OT_CONFIG_F_CONTROL_MODE, OT_CONFIG_MODE_LOCAL, OT_CONFIG_MODE_HA, 1},
    {OT_CONFIG_F_WATCHDOG_S, OT_CONFIG_WATCHDOG_S_MIN, OT_CONFIG_WATCHDOG_S_MAX, 1},
    {OT_CONFIG_F_FAILSAFE_SETPOINT, OT_CONFIG_FLOW_DC_MIN, OT_CONFIG_FLOW_DC_MAX, HALF},
    {OT_CONFIG_F_FAILSAFE_ROOM_TARGET, OT_CONFIG_ROOM_TARGET_DC_MIN, OT_CONFIG_ROOM_TARGET_DC_MAX,
     1},
    {OT_CONFIG_F_FAILSAFE_HEAT_DAYS, OT_CONFIG_HEAT_DAYS_MIN, OT_CONFIG_HEAT_DAYS_MAX, 1},
    {OT_CONFIG_F_FAILSAFE_MIN_CYCLE, OT_CONFIG_MIN_CYCLE_S_MIN, OT_CONFIG_MIN_CYCLE_S_MAX, 1},
    {OT_CONFIG_F_FLOW_MIN, OT_CONFIG_FLOW_DC_MIN, OT_CONFIG_FLOW_DC_MAX, HALF},
    {OT_CONFIG_F_FLOW_MAX, OT_CONFIG_FLOW_DC_MIN, OT_CONFIG_FLOW_DC_MAX, HALF},
    {OT_CONFIG_F_LOCAL_CH_SETPOINT, OT_CONFIG_FLOW_DC_MIN, OT_CONFIG_FLOW_DC_MAX, HALF},
    // 0 is "nobody has written it", and there is no floor above it: the boiler's ID 48
    // bounds replace the registry's once read (ot_state.c:200-204) and are ot_command's to
    // enforce. A store stricter than the command layer refuses what that layer has granted.
    {OT_CONFIG_F_DHW_SETPOINT, 0, OT_CONFIG_DHW_DC_MAX, 1},
    // The MQTT room-source slot. role is not on the half-degree grid -- it is not a
    // temperature, it is ambient(0)/room(1).
    {OT_CONFIG_F_ROOM_MQTT_ROLE, 0, OT_CONFIG_ROOM_MQTT_ROLE_MAX, 1},
    {OT_CONFIG_F_ROOM_MQTT_STALE_S, OT_CONFIG_ROOM_MQTT_STALE_S_MIN, OT_CONFIG_ROOM_MQTT_STALE_S_MAX,
     1},
};

ot_config_err_t ot_config_check_range(ot_config_field_t field, uint32_t value)
{
    for (size_t i = 0; i < sizeof RANGES / sizeof RANGES[0]; i++)
        if (RANGES[i].field == field)
            return value >= RANGES[i].lo && value <= RANGES[i].hi && value % RANGES[i].step == 0
                       ? OT_CONFIG_OK
                       : OT_CONFIG_ERR_RANGE;
    return OT_CONFIG_ERR_RANGE;
}

ot_config_err_t ot_config_check_flow(uint32_t flow_min_dc, uint32_t flow_max_dc,
                                     uint32_t failsafe_setpoint_dc)
{
    // Strictly below: equal bounds refuse every CH setpoint but one (the refusal does not
    // clamp), which is a boiler that can be told one temperature. The pair is asked before the
    // setpoint so that an inverted band is reported as the band, the thing actually wrong.
    if (flow_min_dc >= flow_max_dc)
        return OT_CONFIG_ERR_FLOW;
    if (failsafe_setpoint_dc < flow_min_dc || failsafe_setpoint_dc > flow_max_dc)
        return OT_CONFIG_ERR_FAILSAFE;
    return OT_CONFIG_OK;
}

ot_config_err_t ot_config_check_mode(uint32_t control_mode, const char *mqtt_host)
{
    // Only the first byte is read, which keeps this safe on a stored host that has not been
    // through terminated(): apply() hands it cfg->mqtt_host straight out of the document.
    if (control_mode == OT_CONFIG_MODE_HA && or_empty(mqtt_host)[0] == '\0')
        return OT_CONFIG_ERR_MODE_NEEDS_BROKER;
    return OT_CONFIG_OK;
}
