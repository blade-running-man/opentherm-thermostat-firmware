// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What survives a power cut: the network, the broker, the name, and the two passwords.
//
// This half is PURE -- no ESP-IDF, no NVS, no clock. Everything that decides whether a value
// is acceptable, what a document means when it only mentions half of itself, and which keys a
// reset destroys lives here, where a host test can reach it. ot_config_nvs.h is the
// other half: it opens namespaces, reads bytes, and asks the questions below. That split is
// not tidiness. Every rule in this file exists because a bad value stored once outlives every
// reboot, and a device that panics on boot over its own configuration has no way back over the
// air -- one of the invariants that are not choices: "Not one ESP_ERROR_CHECK on data from NVS".
//
// Three rules run through the whole file, and each is here because it has already cost
// somebody a device:
//
//  * VALIDATE ON THE WAY IN. A value is refused at the point it is accepted, not when it is
//    used. The alternative is a stored value that is only discovered to be impossible on the
//    next boot, by which time the only surface that could have fixed it is gone.
//  * SSID AND PSK ARE ONE RECORD. NVS gives no atomicity between two keys; a power cut between
//    two writes leaves a new SSID with an old password, which is indistinguishable from a typo
//    and strands the device just as thoroughly. ot_wifi_t is that
//    record, it is written under ONE key, and ot_config_apply() refuses a document that
//    mentions one half of it without the other.
//  * NOTHING RESERVES A BYTE FOR A TERMINATOR. wifi_sta_config_t.ssid is 32 bytes and
//    .password is 64, neither NUL-terminated (esp_wifi_types_generic.h:559-560). The obvious
//    strncpy(dst, src, sizeof dst - 1) silently truncates a legal 32-character SSID to 31 and
//    a 64-character hex PSK to 63, and the device then never associates -- with a symptom
//    nobody can deduce from outside. Lengths are carried explicitly instead.
//
// The two passwords are stored under DIFFERENT policies and ONE disclosure policy. The broker
// password is stored recoverably because MQTT needs the plaintext to authenticate -- a hash is
// meaningless there, which is why storing a hash was rejected: MQTT authenticates with the
// plaintext, so a hash of the broker password would be useless. The UI password is stored as a
// one-way record. Neither is ever returned by any projection: write-only secret fields are
// the property that unblocks a single universal binary.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_secrets.h"  // the sentinel and the constant-time compare; both are reused here

#ifdef __cplusplus
extern "C" {
#endif

// --- the names, which are a contract ---------------------------------------------------------
//
// An OTA that renames one of these sends the owner back to the setup page with a device that
// has forgotten its network. test_config_names pins every literal below for that reason:
// changing one has to be a deliberate act with a migration behind it, not a tidy-looking rename.

// Bumped when the meaning or the layout of a stored value changes -- NOT when a field is added
// with a default that a fresh read already produces. ot_config_schema_check() says what a
// given stored number means for this build.
//
// 2 as of schema-v2: the DHW bit moved from `cl_dhw_en` to `dhw_en`. Retiring a key
// is NOT a reason to bump: retired keys are erased at every boot, not by the migration alone. See
// ot_config_retired_key().
#define OT_CONFIG_SCHEMA_VERSION 2u

// NVS_KEY_NAME_MAX_SIZE is 16 INCLUDING the terminator (nvs.h:61), and namespaces share that
// limit (nvs.h:62). A 16-character key is not truncated, it is refused with
// ESP_ERR_NVS_KEY_TOO_LONG -- and since nothing here may ESP_ERROR_CHECK a store result, an
// over-long key would fail silently and the field would simply never persist. Pinned by a test
// over the whole table rather than checked at each call site.
#define OT_CONFIG_NVS_NAME_MAX 15

// Three namespaces, and which one a field lives in is what decides whether the soft reset
// destroys it. They are spelled out here rather than only inside ot_config_ns_name()
// because ot_config_nvs.c opens them by name and a test pins them by name, and a string
// that appears twice is a string that gets changed once.
#define OT_CONFIG_NS_WIFI_NAME  "cfg_wifi"
#define OT_CONFIG_NS_OWNER_NAME "cfg_owner"
#define OT_CONFIG_NS_APP_NAME   "cfg_app"

// Where the schema number lives. In the namespace the soft reset KEEPS, so that clearing the
// network does not also throw away the device's knowledge of its own layout.
#define OT_CONFIG_KEY_SCHEMA "schema"

// The Wi-Fi pair, and the last pair that was known to produce an address. Both are blobs, both
// are one key, and that is the atomicity invariant expressed as storage rather than as a
// comment. The second one is what ot_prov's COMMIT and RESTORE actions move
// (ot_provision.h, OT_PROV_ACTION_COMMIT_CREDENTIALS): without a slot for it the
// rollback that protects against a typo has nowhere to roll back to.
#define OT_CONFIG_KEY_STA      "sta"
#define OT_CONFIG_KEY_STA_GOOD "sta_good"

// The two facts ot_prov_boot_t needs handed back after a power cut, and neither is a
// setting. They are here because every NVS name in this firmware is written down in one place,
// and they are in the WI-FI namespace because that is the tier the soft reset erases: the
// gesture means "forget this network", and a device that has forgotten its network is on its
// first run again -- it should get the fifteen-minute first-run window, not the five `win_closed`
// would still be asking for.
//
// What each one buys is a WAY BACK, which is why losing one is worse than losing a setting:
//  * win_closed -- "the access point comes back for five minutes at each boot", which only
//    means anything if the device remembers that a window ran out at all.
//  * no_addr -- a station that joined the owner's network and was never given an address. The
//    thirty-minute return excludes that failure BY NAME, so nothing raises an access
//    point for it; this flag is what makes the NEXT boot offer one instead
//    (ot_provision.h, ot_prov_address_never_arrived).
#define OT_CONFIG_KEY_WINDOW_CLOSED "win_closed"
#define OT_CONFIG_KEY_NO_ADDRESS    "no_addr"

// Where schema 1 kept the DHW bit. Read once, by the migration to schema 2, which
// carries it into dhw_enable's own key and then erases it.
#define OT_CONFIG_KEY_V1_DHW_EN "cl_dhw_en"

// The keys a previous schema wrote and this one never reads, in cfg_app, `index` from 0; NULL past
// the end. NVS saves per key and erases only by namespace, so a retired key stays in the flash
// until somebody erases it -- an OTA rollback to the build that wrote it reads it back as live, and
// a later field reusing the name inherits a stale value. Every boot of a writable
// store erases every one of them, whatever its schema number -- a key can be retired without a
// bump -- and so does every save, before it writes the number; test_config_names pins the list
// and forbids FIELDS[] from reusing a name on it. Pure: a table lookup.
const char *ot_config_retired_key(size_t index);


// --- sizes -----------------------------------------------------------------------------------

// 802.11 caps an SSID at 32 octets and WPA2 caps a passphrase at 63 characters, with 64 meaning
// the 256-bit PSK written as hex (IEEE 802.11i Annex H). Both extremes are LEGAL and both must
// survive this store byte for byte.
#define OT_CONFIG_SSID_MAX 32
#define OT_CONFIG_PSK_MAX  64
// Below this a WPA2 passphrase is not a passphrase. Refused on the way in rather than handed to
// esp_wifi, which would take it and never associate.
#define OT_CONFIG_PSK_MIN  8

// RFC 1035 §2.3.4: the longest legal domain name. Deliberately not a rounder, smaller number --
// a cap that refuses a name the owner's DNS resolves is a bug report nobody can act on, and the
// bytes cost nothing next to the flash page they live in.
#define OT_CONFIG_HOST_MAX   253
#define OT_CONFIG_USER_MAX   128
#define OT_CONFIG_BROKER_PASS_MAX 128
#define OT_CONFIG_PREFIX_MAX 64
// BYTES, not characters, and the default name is Cyrillic: "Термостат OpenTherm e5f6" is 24
// characters and 33 bytes in UTF-8. At 32 this component's own default did not fit its own
// validator -- snprintf truncated the last hex digit, and the name the owner saw in Home
// Assistant no longer matched the access point they had joined. DO NOT lower it back to a
// round 32 "because a name that long is silly": in UTF-8 that is sixteen Cyrillic letters.
#define OT_CONFIG_NAME_MAX   48
// The plaintext the owner types, not what is stored. Long enough for a passphrase, bounded so a
// POST body cannot make the hash step take arbitrarily long.
// A POSIX TZ string, which is what setenv("TZ", ...) takes. The long form with a DST rule --
// "CET-1CEST,M3.5.0,M10.5.0/3" -- is 26 bytes, and the ones with explicit offsets are longer
// still; 63 leaves room for every zone in the tzdata POSIX column.
#define OT_CONFIG_TZ_MAX 63
// The NTP server is a host name and is checked by ot_config_check_host(), so it shares the
// broker's cap rather than growing one of its own.
#define OT_CONFIG_NTP_MAX OT_CONFIG_HOST_MAX

#define OT_CONFIG_UI_PASS_MAX 128
// There is no lockout and there cannot be one: locking out attempts on a device nobody can
// reach is a way to lose the device, not to defend it. So the only cost an attacker on the
// LAN pays per guess is one derivation, and the search space has to carry the rest.
// DO NOT lower this to "let people use a PIN" -- four digits at 100 ms is a quarter of an hour.
#define OT_CONFIG_UI_PASS_MIN 8

// The full MAC rendered as lower-case hex. Identity is the MAC, never the name -- renaming
// a device must not re-key anything, or Home Assistant ends up with two devices and the dead one
// cannot be removed.
#define OT_CONFIG_DEVICE_ID_LEN 12

// --- the field table ---------------------------------------------------------------------------

typedef enum {
    OT_CONFIG_F_WIFI_SSID,
    OT_CONFIG_F_WIFI_PSK,
    OT_CONFIG_F_MQTT_HOST,
    OT_CONFIG_F_MQTT_PORT,
    OT_CONFIG_F_MQTT_USER,
    OT_CONFIG_F_MQTT_PASSWORD,
    OT_CONFIG_F_TOPIC_PREFIX,
    OT_CONFIG_F_HA_DISCOVERY,
    OT_CONFIG_F_DEVICE_NAME,
    OT_CONFIG_F_UI_PASSWORD,
    OT_CONFIG_F_TZ,
    OT_CONFIG_F_NTP_SERVER,
    OT_CONFIG_F_DHW_ENABLE,
    // The executor's settings. An id is a bit of ot_config_repairs_t, and those
    // bits are RAM-only: the same build turns them into names (ot_net.c, report_repairs) and none
    // is ever stored -- the flash holds each field under its key -- so ids may be renumbered. The
    // only limit is 32 (test_every_field_has_a_bit_of_its_own_in_the_repair_set).
    OT_CONFIG_F_CONTROL_MODE,
    OT_CONFIG_F_HEATING_SEASON,
    OT_CONFIG_F_WATCHDOG_S,
    OT_CONFIG_F_FAILSAFE_SETPOINT,
    OT_CONFIG_F_FAILSAFE_ROOM_TARGET,
    OT_CONFIG_F_FAILSAFE_HEAT_DAYS,
    OT_CONFIG_F_FAILSAFE_MIN_CYCLE,
    OT_CONFIG_F_FLOW_MIN,
    OT_CONFIG_F_FLOW_MAX,
    OT_CONFIG_F_LOCAL_CH_ENABLE,
    OT_CONFIG_F_LOCAL_CH_SETPOINT,
    OT_CONFIG_F_DHW_SETPOINT,
    // The MQTT room-source slot's own settings. NS_APP, like
    // the executor's numbers above: a soft reset forgets the network, not a slot the owner
    // wired up in Home Assistant.
    OT_CONFIG_F_ROOM_MQTT_ENABLE,
    OT_CONFIG_F_ROOM_MQTT_ROLE,
    OT_CONFIG_F_ROOM_MQTT_STALE_S,
    OT_CONFIG_F_ROOM_MQTT_HA_FORWARDED,
    OT_CONFIG_F_COUNT,
} ot_config_field_t;

// Erasure is by NAMESPACE, never by a list of keys, and that is the whole reason namespaces
// exist in this component. A hand-written key list goes stale the first time a field is added --
// and worse, it cannot name a key that an OLDER firmware wrote and this build has never heard
// of. nvs_erase_all() over a namespace takes both. The named failure is a "factory reset" that
// leaves the previous owner's broker password in the flash.
typedef enum {
    OT_CONFIG_NS_WIFI,    // the pair, and the known-good pair
    OT_CONFIG_NS_OWNER,   // the UI password record: what says this device is claimed
    OT_CONFIG_NS_APP,     // broker, prefix, discovery, name -- and whatever setting a later
                                // phase starts storing. New fields land here without anyone
                                // touching the reset code, which is the point of a namespace
                                // carrying the tier instead of each field carrying it.
    OT_CONFIG_NS_COUNT,
} ot_config_ns_t;

// The reset tiers, and the two durations are the gesture's, not this component's: 5 s after boot is soft,
// 15 s is hard. The button is GPIO9, which is also the download strap -- holding it AT POWER-ON
// enters the bootloader instead, so neither tier can be triggered that way.
typedef enum {
    // Wi-Fi and the UI password. The UI password goes deliberately: a forgotten password on a
    // device that is visible on the network and completely unusable is otherwise unfixable
    // without USB.
    OT_CONFIG_RESET_SOFT,
    OT_CONFIG_RESET_HARD,
} ot_config_reset_t;

typedef struct {
    // The name the owner and the API see, and -- where the field is its own NVS entry -- the
    // key it is stored under. One name, not two, except where what is stored is not what is
    // named: `ui_password` is stored as `ui_pw_hash` because the record is not the password.
    const char *name;
    const char *key;
    ot_config_ns_t ns;
    // True when ot_secret_key(name) must agree. The redaction in ot_secrets matches
    // by word, not by list, so a field named `mqtt_pw` would be published in full while looking
    // perfectly sensible here. The test asserts the two agree for every row.
    bool secret;
} ot_config_field_info_t;

// NULL for an id outside the table -- callers get the entity list wrong, and a crash on the
// HTTP task is a worse answer than a 404.
const ot_config_field_info_t *ot_config_field(ot_config_field_t field);

const char *ot_config_ns_name(ot_config_ns_t ns);

// Which namespaces a tier destroys. Ask this rather than listing namespaces at the call site:
// the reset code should not be somewhere a new namespace can be forgotten.
bool ot_config_reset_erases(ot_config_reset_t tier, ot_config_ns_t ns);

// --- the document ------------------------------------------------------------------------------

// One atomic record. DO NOT split it into two fields "for symmetry with the API" -- the whole
// point is that no power cut can land between the SSID and the PSK.
//
// Neither array is NUL-terminated and neither has room to be. Read exactly `*_len` bytes; a
// strlen() over either is a bug that only shows up on a 32-character SSID, which is to say in
// somebody's house and not on the bench.
typedef struct {
    uint8_t ssid[OT_CONFIG_SSID_MAX];
    uint8_t psk[OT_CONFIG_PSK_MAX];
    uint8_t ssid_len;
    uint8_t psk_len;
} ot_wifi_t;

// The record format is `1$<iterations>$<salt hex>$<digest hex>`, which at the current sizes is
// 105 characters. The margin is for a larger iteration count, not for a larger digest.
#define OT_CONFIG_HASH_MAX 127

// The whole stored document, in memory.
//
// DO NOT hand this struct to a renderer, a logger or an event payload. It holds the broker
// password in plaintext by design and the UI password record beside it; the absolute rule is
// that nothing for which ot_secret_key() is true reaches
// ESP_LOG*, because logging is captured whole and served to anyone by /api/log.
// ot_config_project() is the only thing that leaves this struct.
typedef struct {
    ot_wifi_t wifi;
    char     mqtt_host[OT_CONFIG_HOST_MAX + 1];
    uint16_t mqtt_port;
    char     mqtt_user[OT_CONFIG_USER_MAX + 1];
    char     mqtt_password[OT_CONFIG_BROKER_PASS_MAX + 1];
    char     topic_prefix[OT_CONFIG_PREFIX_MAX + 1];
    bool     ha_discovery;
    char     device_name[OT_CONFIG_NAME_MAX + 1];
    // Wall-clock features (logs, time-of-day reasoning) need a wall clock, and a wall clock needs a zone. Both live here
    // rather than in a build flag because one universal binary is the whole point: a device
    // reflashed in another country must be correctable through the settings page.
    char     tz[OT_CONFIG_TZ_MAX + 1];
    char     ntp_server[OT_CONFIG_NTP_MAX + 1];
    // Bit 1 of ID 0, in every mode and state: ot_control builds it from this flag alone. Domestic
    // hot water has no loop of its own: the boiler holds the DHW temperature itself and decides
    // DHW priority over heating, and we neither imitate that nor try to guess it.
    bool     dhw_enable;
    // THE EXECUTOR'S SETTINGS. Temperatures are tenths of a degree (`_dc`),
    // because this store has no floating-point type and a float that crosses it twice stops
    // comparing equal. Every number is uint16_t -- control_mode and failsafe_heat_days included --
    // because that is how each is STORED (ot_config_nvs_io.c has u16 helpers and no u8 ones): a
    // uint8_t here would truncate a damaged 0x0101 to a valid-looking 1 before
    // ot_config_sanitize() could see it. The meaning of each value is ot_control.h's.
    uint16_t control_mode;              // OT_CONFIG_MODE_LOCAL or OT_CONFIG_MODE_HA
    // FALSE BY DEFAULT: a freshly flashed device never asks for heat.
    // An entity, written through its route like local_ch_enable, local_ch_setpoint_dc and
    // dhw_setpoint_dc below; ot_wire refuses it by name.
    bool     heating_season;
    uint16_t watchdog_s;
    uint16_t failsafe_setpoint_dc;
    uint16_t failsafe_room_target_dc;
    uint16_t failsafe_heat_days;
    uint16_t failsafe_min_cycle_s;
    uint16_t flow_min_dc;
    uint16_t flow_max_dc;
    // LOCAL mode's command, and dhw_setpoint_dc. Stored here, WRITTEN by the executor through the
    // entity path and never by a settings body -- ot_wire refuses them by name.
    bool     local_ch_enable;
    uint16_t local_ch_setpoint_dc;
    uint16_t dhw_setpoint_dc;           // 0 means nobody has written it yet
    // THE MQTT ROOM-SOURCE SLOT. Off by default: a freshly
    // flashed device must not start trusting a topic nobody has wired up in Home Assistant yet.
    // `room_mqtt_role` is uint16_t although only 0 (ambient) and 1 (room) are legal, for the same
    // reason control_mode above is: every number in this store is STORED as uint16_t
    // (ot_config_nvs_io.c has u16 helpers and no u8 ones), and a uint8_t field here would let a
    // damaged 0x0101 read back as a valid-looking 1 before ot_config_sanitize() ever saw it.
    // ot_config_room_mqtt() below narrows it to uint8_t only in the copy it hands out.
    bool     room_mqtt_enable;
    uint16_t room_mqtt_role;            // 0 ambient, 1 room -- OT_CONFIG_ROOM_MQTT_ROLE_MAX
    uint16_t room_mqtt_stale_s;
    bool     room_mqtt_ha_forwarded;
    // The one-way record, never the password. Empty means no password is set -- see
    // ot_config_password_set(), which is why this is not accompanied by a flag.
    char     ui_pw_hash[OT_CONFIG_HASH_MAX + 1];
    // Set by the loader when the flash holds a schema this build does not understand, which is
    // what an OTA rollback looks like from here. The flag lives in the document rather than
    // in the store so that every write path passes through one check instead of each caller
    // remembering; ot_config_apply() refuses while it is set.
    bool     read_only;
} ot_config_t;

// --- what a client is allowed to see -------------------------------------------------------------

// Built by ot_config_project(). Every pointer except the three secrets borrows from the
// ot_config_t it was built from and dies with it -- except where the document came back
// from flash without a terminator in a field, and then it borrows a literal "" instead. A
// renderer may strlen() every one of these; that is the whole contract of this struct.
typedef struct {
    // Terminated here even though the record is not, because this is what gets rendered. 33
    // bytes, so a 32-character SSID arrives whole.
    char        ssid[OT_CONFIG_SSID_MAX + 1];
    const char *psk;             // the sentinel or ""; never the value
    const char *mqtt_host;
    uint16_t    mqtt_port;
    const char *mqtt_user;
    const char *mqtt_password;   // the sentinel or ""; never the value
    const char *topic_prefix;
    bool        ha_discovery;
    const char *device_name;
    const char *tz;
    const char *ntp_server;
    bool        dhw_enable;
    // The executor's settings, copied as they are: none is a secret, and a client needs all of
    // them to show what the device will do when Home Assistant goes quiet.
    uint16_t    control_mode;
    bool        heating_season;
    uint16_t    watchdog_s;
    uint16_t    failsafe_setpoint_dc;
    uint16_t    failsafe_room_target_dc;
    uint16_t    failsafe_heat_days;
    uint16_t    failsafe_min_cycle_s;
    uint16_t    flow_min_dc;
    uint16_t    flow_max_dc;
    bool        local_ch_enable;
    uint16_t    local_ch_setpoint_dc;
    uint16_t    dhw_setpoint_dc;
    // The MQTT room-source slot: none of the four is a secret, and each is copied as it is
    // stored, like the executor's settings above.
    bool        room_mqtt_enable;
    uint16_t    room_mqtt_role;
    uint16_t    room_mqtt_stale_s;
    bool        room_mqtt_ha_forwarded;
    const char *ui_password;     // the sentinel or ""; never the value
    // DERIVED from the record being non-empty, and never stored. A flag of its own would be a
    // second key, and a half-finished write that stored the flag without the record would leave
    // the device demanding a password that does not exist -- locked, by nobody, for ever.
    bool        ui_password_set;
    bool        read_only;
} ot_config_public_t;

void ot_config_project(const ot_config_t *cfg, ot_config_public_t *out);

// The derivation itself, exposed because the access policy needs it too
// (ot_http_policy.h, ctx.password_set) and must not grow its own idea of what "set" means.
bool ot_config_password_set(const ot_config_t *cfg);

// --- what is acceptable --------------------------------------------------------------------------

typedef enum {
    OT_CONFIG_OK,
    OT_CONFIG_ERR_SSID,
    OT_CONFIG_ERR_PSK,
    // One half of the Wi-Fi pair without the other. Its own code because the owner has to be
    // told something better than "bad request": the form always submits both, so a document
    // with one is either a hand-made request or a broken client, and guessing which password
    // was meant is how a device leaves the network for good.
    OT_CONFIG_ERR_WIFI_PAIR,
    OT_CONFIG_ERR_HOST,
    OT_CONFIG_ERR_PORT,
    OT_CONFIG_ERR_USER,
    OT_CONFIG_ERR_BROKER_PASSWORD,
    OT_CONFIG_ERR_PREFIX,
    OT_CONFIG_ERR_NAME,
    OT_CONFIG_ERR_UI_PASSWORD,
    // The submission was fine and there is no way to store it: no hash function was supplied.
    // Separate from ERR_UI_PASSWORD because the owner did nothing wrong and retyping will not
    // help.
    OT_CONFIG_ERR_NO_HASH,
    // The flash holds a newer schema than this build understands. See ot_config_t.read_only.
    OT_CONFIG_ERR_READ_ONLY,
    // There was no document to apply. Its own code rather than OK, because OK is what a handler
    // turns into 200 and what ot_config_strerror() renders as the word "accepted": the
    // owner would read Saved over a request that stored nothing and close the page. A patch with
    // every field ABSENT is a different thing and still succeeds -- the settings page submits the
    // whole document, and "nothing changed" is a legitimate save.
    OT_CONFIG_ERR_NO_DOCUMENT,
    // APPENDED, not inserted next to its neighbours. These values are compared in stored tests
    // and mapped to HTTP codes; renumbering the middle of the enum would move every code after
    // it in a diff that reads as one added line.
    OT_CONFIG_ERR_TZ,
    OT_CONFIG_ERR_NTP,
    // The executor settings, appended for the same reason. ERR_FLOW and ERR_FAILSAFE are the two
    // cross-field rules and are decided on the MERGED document -- the patch's value where it has
    // one, the stored one where it does not -- because each field alone can be legal while the
    // pair is a device with no legal setpoint.
    OT_CONFIG_ERR_FLOW,               // flow_min_dc is not below flow_max_dc
    OT_CONFIG_ERR_FAILSAFE,           // failsafe_setpoint_dc is outside [flow_min_dc, flow_max_dc]
    OT_CONFIG_ERR_MODE_NEEDS_BROKER,  // Home Assistant mode with no broker address
    OT_CONFIG_ERR_RANGE,              // a number outside its own bounds (ot_config_check_range)
    // A field the executor owns, sent in a settings body. Returned by ot_wire, never by
    // ot_config_apply(): the executor persists these through apply, so apply must take them.
    OT_CONFIG_ERR_READ_ONLY_FIELD,
    // The broker host and its password are a pair, exactly as the Wi-Fi SSID and PSK are
    // (OT_CONFIG_ERR_WIFI_PAIR). Appended, not inserted, for the reason the TZ block above gives.
    // A mqtt_host that changes while the stored broker password is KEPT (the page renders it as
    // the sentinel) would send that password in cleartext to the new, possibly attacker-chosen,
    // host -- so the change is refused until the password is re-entered for the new broker.
    OT_CONFIG_ERR_BROKER_PAIR,
} ot_config_err_t;

// A sentence for the owner. Never contains a value -- these strings end up in an HTTP body and
// in the log ring, and the log ring is served by /api/log to whoever can reach it.
const char *ot_config_strerror(ot_config_err_t err);

// A short name per refusal -- "mqtt-host", "flow", "read-only-field" -- which POST /api/config
// sends as `field` beside the sentence, so the page can point at the box it means. Pinned literal
// by literal: the page matches on them. "rejected" for a code this build does not know; never
// NULL. Here rather than in ot_http_config.c, where it fell through to "rejected" for every code
// added after it: ot_http is never host-built, and here the host build (-Werror=switch) and
// test_every_refusal_has_a_name_of_its_own are the guard against a code without a name.
const char *ot_config_err_name(ot_config_err_t err);

// Length is passed, not inferred: `ssid` is not a C string in the record it comes from.
ot_config_err_t ot_config_check_ssid(const uint8_t *ssid, size_t len);
ot_config_err_t ot_config_check_psk(const uint8_t *psk, size_t len);
ot_config_err_t ot_config_check_host(const char *host);
// uint32_t, not uint16_t. A port arrives as a JSON number and 70000 has to be REFUSED; taking it
// as uint16_t wraps it to 4464 and stores a broker address the owner never typed.
ot_config_err_t ot_config_check_port(uint32_t port);
ot_config_err_t ot_config_check_user(const char *user);
ot_config_err_t ot_config_check_broker_password(const char *password);
ot_config_err_t ot_config_check_prefix(const char *prefix);
ot_config_err_t ot_config_check_name(const char *name);
ot_config_err_t ot_config_check_ui_password(const char *password);
// A POSIX TZ string, handed verbatim to setenv() and tzset(). Checked for shape only: what a
// zone MEANS is tzdata's answer, and this device carries no table to check it against -- an
// unrecognised zone silently behaves as UTC, which is a wrong schedule rather than a refused
// one. DO NOT try to validate it harder here; validate it by showing the owner the clock.
ot_config_err_t ot_config_check_tz(const char *tz);
// The NTP server. Its SHAPE is a host name and is checked by the same rules as the broker's --
// two checks that mean the same thing are two checks that drift -- but EMPTY is refused here and
// accepted there. An empty broker address is a legitimate "no broker"; an empty NTP server is a
// device that never learns what day it is, which strands every wall-clock feature.
ot_config_err_t ot_config_check_ntp(const char *server);

// The executor's numbers. Tenths of a degree for every `_DC`.
// The flow bounds are the registry's limits for ID 1 (10.0 to 90.0 degrees, ch_setpoint in
// tools/opentherm_ids.py): a setting the command layer would refuse to put on the bus is not a
// setting worth storing. The DHW setpoint is deliberately NOT held to the registry's ID 56 row:
// once the boiler has answered ID 48 its own bounds replace the table's (ot_state.c:200-204), and a
// store stricter than the command layer refuses a value ot_command_check() and ot_control_apply()
// have already granted. So it is 0 (unset) to the ceiling of ID 1, and the boiler's own bounds stay
// ot_command's business.
#define OT_CONFIG_MODE_LOCAL          0u   // equal to OT_CONTROL_MODE_LOCAL (ot_control.h)
#define OT_CONFIG_MODE_HA             1u   // equal to OT_CONTROL_MODE_HA
#define OT_CONFIG_WATCHDOG_S_MIN      60u
#define OT_CONFIG_WATCHDOG_S_MAX      7200u
#define OT_CONFIG_ROOM_TARGET_DC_MIN  50u
#define OT_CONFIG_ROOM_TARGET_DC_MAX  300u
#define OT_CONFIG_HEAT_DAYS_MIN       1u
#define OT_CONFIG_HEAT_DAYS_MAX       30u
#define OT_CONFIG_MIN_CYCLE_S_MIN     60u
#define OT_CONFIG_MIN_CYCLE_S_MAX     3600u
#define OT_CONFIG_FLOW_DC_MIN         100u
#define OT_CONFIG_FLOW_DC_MAX         900u
#define OT_CONFIG_DHW_DC_MAX          900u
// The MQTT room-source slot. role is ambient(0)/room(1) --
// OT_ROOM_AMBIENT/OT_ROOM_ROOM in ot_room.h, kept as a plain number here because this component
// does not depend on ot_room. stale_s is the window ot_sensor waits before calling the reading
// STALE; 10 s floors it against a slot that could never register as fresh.
#define OT_CONFIG_ROOM_MQTT_ROLE_MAX    1u
#define OT_CONFIG_ROOM_MQTT_STALE_S_MIN 10u
#define OT_CONFIG_ROOM_MQTT_STALE_S_MAX 65535u
// The half-degree grid every CH setpoint sits on. ot_control quantises the held ID 1 value to it
// and THEN keeps it inside [flow_min_dc, flow_max_dc]: with a bound of 403 the
// two rules fight and the bound wins over the rounding, so the four values that end up as ID 1
// must be on the grid themselves. Both flow limits above are multiples of it (a static assert in
// ot_config_check.c), so rounding onto the grid can never leave the band.
#define OT_CONFIG_HALF_DEGREE_DC      5u

// One number against its own bounds: OT_CONFIG_OK or OT_CONFIG_ERR_RANGE. uint32_t for the reason
// check_port() gives -- a JSON 70000 must be REFUSED, not wrapped into a legal-looking 4464.
// failsafe_setpoint_dc, flow_min_dc, flow_max_dc and local_ch_setpoint_dc share the flow bounds AND
// must be multiples of OT_CONFIG_HALF_DEGREE_DC; dhw_setpoint_dc is 0 ("unset", not a temperature)
// to OT_CONFIG_DHW_DC_MAX, with no floor of its own (see above). A field that is not a number here
// -- a string, a bool -- is ERR_RANGE: a caller asking has made a mistake, and a refusal is the
// answer that stores nothing.
ot_config_err_t ot_config_check_range(ot_config_field_t field, uint32_t value);
// The two flow rules, asked of values that each passed check_range(): flow_min_dc strictly below
// flow_max_dc (ERR_FLOW), then failsafe_setpoint_dc inside them, both ends included (ERR_FAILSAFE).
ot_config_err_t ot_config_check_flow(uint32_t flow_min_dc, uint32_t flow_max_dc,
                                     uint32_t failsafe_setpoint_dc);
// Home Assistant mode needs a broker address to hear Home Assistant through; a NULL host
// is the empty one. ERR_MODE_NEEDS_BROKER, or OK. Only the emptiness of `mqtt_host` is read.
ot_config_err_t ot_config_check_mode(uint32_t control_mode, const char *mqtt_host);

// The ONE answer to "is there a network in this record", asked by both of the functions that
// could otherwise disagree about it -- and the disagreement builds the one state that must never
// occur: no network, no access point and no way in.
//
// ot_config_nvs_has_credentials() fills ot_prov_boot_t.has_credentials, which is what
// decides whether the device raises its own access point. ot_config_sanitize() decides what
// the station is handed. When the first says present and the second clears the record, the device
// puts no access point on air and hands the station nothing: no network, no way in, and permanent,
// because nothing rewrites the blob. One flipped bit in the PSK length byte was enough.
//
// "An SSID worth trying is stored", never "the key exists": an earlier design loaded its
// preference with no emptiness test (wifi_component.cpp:655-663) and has_sta() was true for a
// saved-but-zeroed blob (wifi_component.h:491), so after `Forget Wi-Fi` it spent a boot
// associating with "" -- ap_guard.cpp:143-153 is the fix, not the bug. Length alone is not enough
// either, which is the other half of what this exists to stop: a 64-byte key that is not hex, a
// seven-character passphrase and an SSID with a NUL in it are all stored records no radio can use.
bool ot_config_wifi_usable(const ot_wifi_t *wifi);

// --- defaults ----------------------------------------------------------------------------------

// `device_id` is OT_CONFIG_DEVICE_ID_LEN hex characters from the full MAC. It is a
// parameter and not a call into esp_mac.h for the same reason time is a parameter in
// ot_provision: a component that reads its own identity cannot be tested for what it does
// with a different one.
//
// NULL or malformed is tolerated and produces a prefix with no id in it. That is a WORSE default
// -- two devices in one house would then share a topic tree -- but refusing to boot over it is
// worse still, and ot_config_sanitize() reports it.
void ot_config_defaults(ot_config_t *cfg, const char *device_id);

// --- applying a submitted document ------------------------------------------------------------

// A POST body, decoded. A NULL string means the key was ABSENT, which is not the same as an
// empty string: absent leaves the stored value alone, empty clears it. The whole sentinel scheme
// in ot_secrets exists because those two were once the same thing and one save of an
// unrelated setting wiped the broker credentials.
typedef struct {
    const char *wifi_ssid;
    const char *wifi_psk;
    const char *mqtt_host;
    const char *mqtt_user;
    const char *mqtt_password;
    const char *topic_prefix;
    const char *device_name;
    const char *tz;
    const char *ntp_server;
    const char *ui_password;
    bool        has_mqtt_port;
    uint32_t    mqtt_port;
    bool        has_ha_discovery;
    bool        ha_discovery;
    bool        has_dhw_enable;
    bool        dhw_enable;
    // The executor settings. Numbers are uint32_t, like mqtt_port, so that a value too big for its field
    // reaches ot_config_check_range() whole and is refused instead of wrapped.
    bool        has_control_mode;
    uint32_t    control_mode;
    bool        has_heating_season;
    bool        heating_season;
    bool        has_watchdog_s;
    uint32_t    watchdog_s;
    bool        has_failsafe_setpoint_dc;
    uint32_t    failsafe_setpoint_dc;
    bool        has_failsafe_room_target_dc;
    uint32_t    failsafe_room_target_dc;
    bool        has_failsafe_heat_days;
    uint32_t    failsafe_heat_days;
    bool        has_failsafe_min_cycle_s;
    uint32_t    failsafe_min_cycle_s;
    bool        has_flow_min_dc;
    uint32_t    flow_min_dc;
    bool        has_flow_max_dc;
    uint32_t    flow_max_dc;
    // Written by the EXECUTOR, which builds this patch in C to persist a command it accepted.
    // ot_wire never sets these: a settings body that carries one is refused by name.
    // dhw_enable and heating_season above are the fourth and the fifth.
    bool        has_local_ch_enable;
    bool        local_ch_enable;
    bool        has_local_ch_setpoint_dc;
    uint32_t    local_ch_setpoint_dc;
    bool        has_dhw_setpoint_dc;
    uint32_t    dhw_setpoint_dc;
    // The MQTT room-source slot. Numbers are uint32_t like every other patch number above,
    // so a role or a stale window too big for its stored field reaches ot_config_check_range()
    // whole and is refused instead of wrapped.
    bool        has_room_mqtt_enable;
    bool        room_mqtt_enable;
    bool        has_room_mqtt_role;
    uint32_t    room_mqtt_role;
    bool        has_room_mqtt_stale_s;
    uint32_t    room_mqtt_stale_s;
    bool        has_room_mqtt_ha_forwarded;
    bool        room_mqtt_ha_forwarded;
} ot_config_patch_t;

#define OT_CONFIG_SALT_LEN       16
#define OT_CONFIG_DIGEST_LEN     32
// PBKDF2-HMAC-SHA256 at this count costs order 100 ms on a C6 with the SHA accelerator. It is
// carried IN the record, so raising it later does not invalidate what is already stored.
// DO NOT run the check once per request: verify at login and carry a session, or every page of
// the UI pays for it.
#define OT_CONFIG_KDF_ITERATIONS 10000u
// The largest count this build will WRITE, and the largest it will READ BACK. Both halves, and
// they have to be the same number: a parser stricter than the writer stores a password and then
// refuses it, with nobody to ask.
//
// A hundred times the compiled-in count is room to raise it and no room for a bit flip. The
// number that has to be excluded is what a corrupt digit string reaches: at the 100 ms per
// 10,000 iterations above, 100,000,000 -- which a uint32_t holds comfortably -- is about
// seventeen minutes of PBKDF2 on the HTTP task, paid on EVERY login attempt, on a device whose
// owner is standing in front of it. There is no lockout to bound how often it is asked for (see
// OT_CONFIG_UI_PASS_MIN), so nothing else bounds the cost either.
#define OT_CONFIG_KDF_ITERATIONS_MAX (100u * OT_CONFIG_KDF_ITERATIONS)

// The one-way function, supplied by the caller. Injected because it is the only thing in this
// component that needs a crypto library: mbedtls is on the device and not on the host, and a
// component that cannot build on the host cannot have its record format, its salt handling or
// its answer to a wrong password tested at all. Returns false if the derivation failed.
typedef bool (*ot_config_kdf_t)(const char *password, const uint8_t *salt, size_t salt_len,
                                      uint32_t iterations, uint8_t *out, size_t out_len);

typedef struct {
    ot_config_kdf_t kdf;
    // OT_CONFIG_SALT_LEN bytes of fresh randomness, per stored password. Fresh because two
    // devices with the same password must not produce the same record: one recovered flash image
    // would otherwise answer for every device whose owner chose the same word.
    const uint8_t *salt;
    // 0 means OT_CONFIG_KDF_ITERATIONS.
    uint32_t iterations;
} ot_config_hash_ctx_t;

// Renders the stored record for `password`. Writes nothing on failure.
bool ot_config_hash_password(const char *password, const ot_config_hash_ctx_t *ctx,
                                   char *out, size_t cap);

// Constant-time in the digest, via ot_secret_equals. False for a record that does not
// parse -- and note that ot_config_sanitize() will already have thrown such a record away;
// see the comment there for why an unreadable password unlocks the device rather than sealing it.
bool ot_config_check_ui_password_against(const char *record, const char *candidate,
                                               ot_config_kdf_t kdf);

// ALL OR NOTHING. Everything in `patch` is validated before anything is written, so a document
// with a good host and a bad port changes neither. A half-applied configuration is the failure
// this is built to avoid: the owner sees the error, retries, and the second attempt is applied
// on top of a document that is no longer the one they were looking at.
//
// `hash` may be NULL when the patch does not set a UI password. When it does, a NULL hash
// context is OT_CONFIG_ERR_NO_HASH -- never a password stored in the clear.
//
// An accepted document can change a field it did NOT carry. A flow band that no longer contains
// the stored local_ch_setpoint_dc moves that setpoint to the band's nearest edge in the same commit
// (the reason is at ot_config_apply_ex() below), so a patch of flow_max_dc alone may return OK with
// local_ch_setpoint_dc different too. This form does not say whether that happened. A caller that
// must tell the owner, or that keeps its own copy of the local setpoint, calls
// ot_config_apply_ex() and reads *moved. DO NOT assume that "OK" means "exactly the patch landed".
ot_config_err_t ot_config_apply(ot_config_t *cfg,
                                            const ot_config_patch_t *patch,
                                            const ot_config_hash_ctx_t *hash);

// --- what came back out of the flash -------------------------------------------------------------

// One bit per field, using the ot_config_field_t ids.
typedef uint32_t ot_config_repairs_t;
#define OT_CONFIG_REPAIRED(field) ((ot_config_repairs_t)1u << (field))

// ot_config_apply(), and what it MOVED to keep the document legal, as OT_CONFIG_REPAIRED() bits in
// *moved: 0 on a refusal and when nothing moved; NULL does not ask. One field can move today,
// local_ch_setpoint_dc. It is read-only on the wire, so a flow band that no longer contains it
// cannot be refused over it -- the owner has no way to resolve that conflict -- and it cannot be
// left stranded either, because ot_control would then clamp it silently and ID 1 on the bus would
// disagree with GET /api/config. It moves to the nearest edge of the MERGED band, which is on the
// half-degree grid, inside the same all-or-nothing commit. The bits are the ones
// ot_config_sanitize() reports the same move in, so a caller tells the owner in the vocabulary it
// already prints (ot_net.c, report_repairs) instead of a second one for one field.
ot_config_err_t ot_config_apply_ex(ot_config_t *cfg, const ot_config_patch_t *patch,
                                   const ot_config_hash_ctx_t *hash, ot_config_repairs_t *moved);

// Runs the same validators over a document that was just READ, and replaces anything unusable
// with its default. This is the second half of "validate on the way in": the way in was checked,
// and the flash can still hand back something else -- a partial write, a bit flip, a document
// written by a firmware that is not this one.
//
// Returns which fields were replaced so the owner can be told. Nothing here logs: a rejected
// value may itself be a secret, and the log ring is public.
//
// A wrecked Wi-Fi record is cleared, which sends the device to its own access point. That is the
// right answer and not a loss: an access point is a way back, and the invariant that outranks
// everything else here is that no state exists with no network, no access point and no way in.
ot_config_repairs_t ot_config_sanitize(ot_config_t *cfg, const char *device_id);

// --- the live document, polled -----------------------------------------------------------------

// What ot_thermostat needs to init and refresh the MQTT room-source slot: a COPY, not a
// pointer, because a caller that polls once a second (ot_mqtt_link's own pattern) must not hold
// this component's lock between calls. `role` is narrowed to uint8_t here -- see the comment on
// ot_config_t.room_mqtt_role for why the STORED field is wider.
//
// Mirrors ot_net_broker() (ot_net.h) on purpose, and the two are declared in different files for
// the same reason: this one is DECLARED here because the four fields are ot_config's own NS_APP
// rows, but it cannot be IMPLEMENTED here, because ot_config keeps no live document of its own --
// only the shape of one (ot_config_t) and the rules a document must pass on the way in, which is
// this whole file. It is implemented in ot_net_config.c, beside ot_net_broker(), because that is
// where the live, mutex-guarded document already lives (ot_net_internal.h's s_config) and is
// already read the same way by ot_net_config_snapshot() and ot_net_broker(). A second live copy
// kept here would answer "is MQTT room enabled" from a second mutex that could disagree with the
// first -- ot_net's is the one the rest of the device already trusts.
typedef struct {
    bool     enable;
    uint8_t  role;
    uint16_t stale_s;
    bool     ha_forwarded;
} ot_config_room_mqtt_t;

void ot_config_room_mqtt(ot_config_room_mqtt_t *out);

// --- schema ---------------------------------------------------------------------------------------

typedef enum {
    OT_CONFIG_SCHEMA_CURRENT,
    OT_CONFIG_SCHEMA_MIGRATE,
    // Newer than this build. An OTA rollback lands exactly here, and both obvious answers
    // are wrong: erasing destroys a working owner's configuration to fix a problem they do not
    // have, and refusing to boot is a brick on a device behind a front panel. So the document is
    // read with whatever this build understands and the store goes READ-ONLY -- the device keeps
    // ventilating, keeps its network and keeps its broker, and refuses to write settings until
    // someone does the 15-second reset, which is a way back.
    OT_CONFIG_SCHEMA_FUTURE,
} ot_config_schema_action_t;

ot_config_schema_action_t ot_config_schema_check(uint32_t stored);

#ifdef __cplusplus
}
#endif
