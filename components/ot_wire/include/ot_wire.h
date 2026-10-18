// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The documents this component puts on the wire, in both directions.
//
// GET  /api/config     what the settings page loads
// POST /api/config     what it saves
// POST /api/provision  the network the owner picked
// GET  /api/provision  why it is not connecting
// GET  /api/wifi/scan  what the radio can hear
//
// SEPARATE FROM ot_http ON PURPOSE, and the reason is the same one ot_policy exists
// for: a handler that decides what a body means can only be exercised by flashing a board, and
// the bodies this reads are written by whoever is in radio range of an OPEN access point.
// Everything that can be decided without a socket is decided here, where test/test_wire reaches
// it -- the handler next door is left with "read the body, call this, send what it says".
//
// SEPARATE FROM ot_api TOO, although both render JSON. ot_api projects the STATE
// MODEL, which is generated from pdo_table.py and must never grow a second list (CLAUDE.md).
// These five documents are the configuration surface: different source of truth, different
// lifetime, and the field names here are ot_config's `name` column rather than an entity
// key. They share one thing, and it is deliberately shared rather than duplicated -- the escaper
// in ot_json.
//
// THE ONE RULE THAT OUTRANKS EVERYTHING IN THIS FILE: no value for which ot_secret_key() is
// true is ever rendered, and no error message ever quotes a submitted value. Both come from the
// same fact -- everything this device logs is served by GET /api/log, and on the setup access
// point that is world-readable. A message names a FIELD, never its contents.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_config.h"
#include "ot_provision.h"
#include "ot_secrets.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- how a submission can fail ----------------------------------------------------------------

typedef enum {
    OT_WIRE_OK,
    // The body is not a flat JSON object at all. DISTINCT from an empty one: every field of a
    // config patch is optional, so `{}` is a legitimate save that changes nothing, and reporting
    // an unreadable body as that would tell the owner Saved over a request that stored nothing.
    OT_WIRE_BAD_BODY,
    // A field is present and is not what it has to be -- a number where a string belongs, a
    // string longer than the device could ever store. `field` names which.
    OT_WIRE_BAD_FIELD,
    // A validator in ot_config refused it. `err` is its answer and
    // ot_config_strerror() is the sentence.
    OT_WIRE_REFUSED,
    // The flash holds a schema this build does not understand. Nothing is written.
    OT_WIRE_READ_ONLY,
    // A field that belongs to a different route. Today that is exactly the Wi-Fi pair arriving at
    // POST /api/config, and it has its own status rather than sharing BAD_FIELD because the two
    // need different sentences: BAD_FIELD says "you typed something impossible", and this says
    // "you typed it into the right form and sent it to the wrong door". A caller reaches this by
    // echoing back the whole GET /api/config document -- which carries wifi_ssid, because the
    // page has to show which network is configured -- so it is a mistake somebody makes without
    // doing anything unreasonable, and the answer has to name the route that works.
    OT_WIRE_WRONG_ROUTE,
} ot_wire_status_t;

typedef struct {
    ot_wire_status_t status;
    ot_config_err_t  err;    // meaningful when status is OT_WIRE_REFUSED
    // The key at fault, as a pointer to a literal. NEVER the value: this string ends up in an
    // HTTP body and in the log ring, and the log ring is public.
    const char *field;
} ot_wire_result_t;

// One sentence for the owner, with no value of any kind in it. Safe to log and safe to send.
const char *ot_wire_strerror(ot_wire_result_t result);

// --- POST /api/provision ------------------------------------------------------------------

// Decodes the network the owner picked into a record the radio can be handed.
//
// `stored` is what is in NVS now -- it may be NULL on a device that has never been configured --
// and it is needed because `wifi_psk` has THREE states, not two (web/src/api/client.ts,
// ProvisionRequest):
//
//   {"wifi_ssid":"Kitchen","wifi_psk":"hunter2"}   store this key
//   {"wifi_ssid":"Guest","wifi_psk":""}            an OPEN network: no key at all
//   {"wifi_ssid":"Kitchen"}                        keep the key already stored
//
// The third is the same rule ot_config_patch_t states for every other string, so this route
// needs no vocabulary of its own: the field goes through ot_secret_decide() and absent is
// KEEP, "" is CLEAR, anything else is STORE.
//
// THE SENTINEL IS REFUSED, and that refusal is the reason this function exists rather than the
// handler doing it inline. "__UNCHANGED__" is thirteen printable bytes and passes
// ot_config_check_psk whole -- at least eight characters, no NUL, not sixty-four so no hex
// test -- so a handler that wrote it through would leave the device holding credentials that
// cannot associate. Credentials existing is what stops the access point coming back up, so the
// device would simply disappear: ot_provision.c calls that the likeliest catastrophe in the
// project, and this path reaches it with nobody having made a typo.
//
// `out` is written only on OT_WIRE_OK, and the pair it holds has already been through
// ot_config_wifi_usable().
ot_wire_result_t ot_wire_parse_provision(const char *body,
                                                     const ot_wifi_t *stored,
                                                     ot_wifi_t *out);

// --- POST /api/config ---------------------------------------------------------------------

// Where the patch's strings live. ot_config_patch_t holds POINTERS, and NULL in it means
// "the key was absent" -- so the strings need somewhere to sit that outlives the parse and is not
// the request body, which the server may reuse.
//
// DO NOT put one of these on a request handler's stack: it is the better part of a kilobyte and
// httpd runs its handlers on one task with a fixed stack. A static in the handler is the intended
// home, and it holds the broker password and the owner's new UI password in the clear until the
// next parse -- so it is also a struct that must never reach ESP_LOG*.
typedef struct {
    char mqtt_host[OT_CONFIG_HOST_MAX + 1];
    char mqtt_user[OT_CONFIG_USER_MAX + 1];
    char mqtt_password[OT_CONFIG_BROKER_PASS_MAX + 1];
    char topic_prefix[OT_CONFIG_PREFIX_MAX + 1];
    char device_name[OT_CONFIG_NAME_MAX + 1];
    char tz[OT_CONFIG_TZ_MAX + 1];
    char ntp_server[OT_CONFIG_NTP_MAX + 1];
    char ui_password[OT_CONFIG_UI_PASS_MAX + 1];
} ot_wire_patch_storage_t;

// Decodes a settings submission. Nothing is validated here beyond types and lengths -- what a
// value may BE is ot_config_apply()'s answer, and asking it twice in two places is how the
// two come to disagree.
//
// AN UNKNOWN KEY IS IGNORED, so a newer page can talk to an older firmware without every save
// failing. THE TWO WI-FI KEYS ARE NOT: they are refused by name, because /api/config is not the
// route the network travels on (web/src/api/config.ts, ConfigPatch) and silently dropping a
// credential the owner typed into a form is worse than telling them where it belongs.
//
// Absent is not empty, everywhere and for every field. Absent leaves the stored value alone;
// empty clears it. The whole sentinel scheme exists because those two were once the same thing
// and one save of an unrelated setting wiped the broker credentials.
//
// THE FIVE VALUES THE EXECUTOR OWNS ARE REFUSED TOO: local_ch_enable, local_ch_setpoint_dc,
// dhw_enable, dhw_setpoint_dc and heating_season go through POST /api/entities/<key>,
// and a body carrying any of them, whatever its type, is OT_WIRE_REFUSED with
// OT_CONFIG_ERR_READ_ONLY_FIELD and `field` naming it. GET /api/config still renders all five.
ot_wire_result_t ot_wire_parse_config(const char *body, ot_config_patch_t *out,
                                                  ot_wire_patch_storage_t *storage);

// --- GET /api/config ---------------------------------------------------------------------

// Renders the projection. Returns bytes written, or 0 if it did not fit -- and 0 is a DEFECT to
// report, never a short document to serve: a truncated JSON body with a 200 in front of it is
// what the entity list once came twenty-three bytes away from serving.
//
// `known_good` comes from the provisioning machine (ot_prov_has_known_good), not from the
// stored document, and the page cannot tell the truth without it: it is the ONLY condition under
// which a failed provisioning rolls back, and `wifi_ssid != ""` is not a substitute -- an SSID
// stored beside a mistyped key has never produced an address.
//
// ot_config_public_t is what goes in, never ot_config_t. The projection is where the
// two passwords become a sentinel or an empty string, and taking the raw document here would put
// the decision to redact inside a renderer instead of in front of it.
size_t ot_wire_render_config(const ot_config_public_t *pub, bool known_good, char *out,
                             size_t cap);

// --- GET /api/wifi/scan ----------------------------------------------------------------

// One network the radio heard. The row shape lives HERE rather than in ot_net because this
// is what turns it into bytes, and ot_net cannot be built on the host.
typedef struct {
    // NUL-terminated. "" is a hidden network, which is a true thing to report -- the page drops
    // the row, but "the scan saw seven things and can name five" is worth being able to say.
    char   ssid[OT_CONFIG_SSID_MAX + 1];
    int8_t rssi;    // dBm: negative, larger is better
    bool   secure;  // false ONLY for a genuinely open network
} ot_wire_network_t;

// A JSON ARRAY at the top level, possibly empty. Not an object with a `networks` key -- the
// handler renders one thing and this is it (web/src/api/client.ts, scanNetworks).
//
// Duplicates are NOT filtered: one sweep returns a record per BSSID, so a mesh answers several
// times under one name, and which to show is a presentation question the page has more
// information about than this does.
size_t ot_wire_render_scan(const ot_wire_network_t *list, size_t count, char *out,
                                 size_t cap);

// --- GET /api/provision ----------------------------------------------------------------

// Everything the status document is built from. Filled by ot_net under its lock so the
// fields agree with one another: a state saying CONNECTED beside a failure of "wrong-password" is
// a page nobody can act on.
//
// NO PSK, EVER, and no SSID but the configured one. An SSID is in every beacon that network
// sends, so it is not a secret, and it is the one fact that makes "it will not connect"
// diagnosable. The key is not in ot_prov_t either -- that struct has a static_assert saying
// so -- and it must not arrive here by another road.
typedef struct {
    ot_prov_state_t   state;
    ot_prov_mode_t    mode;
    ot_prov_failure_t failure;
    // The raw wifi_err_reason_t behind `failure`, for an owner looking something up. 0 when there
    // is none: the no-address answer is the ABSENCE of an event, not an event.
    uint8_t  failure_reason;
    bool     provisioned;
    bool     has_credentials;
    bool     has_known_good;
    bool     connected;
    bool     ap_on_air;
    bool     window_open;
    // OT_PROV_NEVER for a window that never closes and for a fallback that is not
    // scheduled. Rendered as null, never as 0: "no time left" and "no end" are opposite answers
    // and a page that shows 0 for both counts a device down to a deadline it does not have.
    uint32_t window_remaining_ms;
    uint32_t fallback_in_ms;
    char     ssid[OT_CONFIG_SSID_MAX + 1];
    char     ap_ssid[33];
    char     ip[16];
} ot_wire_provision_t;

// One spelling of each state and mode, decided here. Four different answers to the owner is an
// invariant, and the REST projection and the MQTT one inventing two spellings of them is
// exactly how that invariant gets lost.
const char *ot_wire_state_name(ot_prov_state_t state);
const char *ot_wire_mode_name(ot_prov_mode_t mode);

// And one spelling of a broker's refusal, decided here for the same reason: the MQTT log line
// needs the words now and GET /api/status needs the same words later, and the two inventing their
// own would be that invariant lost again. Codes are MQTT 3.1.1 section 3.2.2.3 -- 1
// unacceptable protocol version, 2 identifier rejected, 3 server unavailable, 4 bad username or
// password, 5 not authorized -- and 0 means no refusal happened. Only 4 and 5 are about
// credentials, so ONE message covering all five would be a lie about the cause for three of them.
// Never NULL, and an unrecognised code gets the "unknown" spelling rather than an invented one.
const char *ot_wire_connack_name(uint32_t code);

// The broker connection as GET /api/status reports it. THIS STRUCT LIVES HERE, NOT IN THE LINK
// COMPONENT: ot_mqtt_link drags in esp-mqtt and FreeRTOS, so an ot_wire that included its header
// could no longer be built or exercised on the host -- which is the whole reason this document is
// rendered here rather than in the handler (see this component's CMakeLists.txt, and the same
// argument ot_policy is built on). ot_mqtt_link_status() fills this struct DIRECTLY, with no
// second struct and no mapping; ot_http's config handler is the caller that owns it.
//
// COUNTERS ONLY. No host, no user, no password: which broker is configured is GET /api/config's
// answer, where the projection has already turned every secret into a sentinel, and a diagnostic
// struct is exactly the shape somebody later adds "the host, to help with debugging" to.
//
// OWNED BY THE CALLER and filled by ot_mqtt_link_status() immediately before rendering.
// TASK CONTEXT: whichever task renders -- in the firmware that is httpd's. Every field is a
// SNAPSHOT of counters the publisher task keeps moving, so two members may disagree by one publish
// and no reader may treat them as simultaneous. The renderer copies nothing and keeps no pointer,
// so the struct may live on the caller's stack.
typedef struct {
    bool     configured;   // a broker host is stored -- NOT that one was ever reached
    bool     connected;
    uint32_t published;
    uint32_t commands;     // accepted from the broker
    uint32_t rejected;     // arrived and were refused: bad topic, bad payload, or retained
    uint32_t reconnects;
    // The CONNACK refusal code, 0 when no refusal was recorded. Rendered beside
    // ot_wire_connack_name()'s spelling of it, so the number an owner can look up and the
    // sentence they can read cannot come to disagree. MQTT 3.1.1 section 3.2.2.3.
    uint32_t last_connack;
} ot_wire_mqtt_t;

// Renders the status document. Returns bytes written, or 0 if it did not fit -- and 0 is a DEFECT
// the caller must turn into an error, never a short document to serve: the handler answers it with
// 500, because a truncated JSON body behind a 200 is what the entity list once came
// twenty-three bytes away from serving.
//
// `mqtt` MAY BE NULL, AND NULL OMITS THE WHOLE `mqtt` MEMBER RATHER THAN RENDERING ZEROS. The two
// are opposite answers and not a formatting choice. A block of zeros is a device that looked at its
// broker connection and found nothing had happened yet; no block at all is a document rendered
// where nobody was in a position to look. POST /api/provision's reply is the second case -- it is
// answered before the radio has even moved to the owner's network -- and a page that read
// "published": 0 there would be shown a configured broker gone silent, which is the fault this
// member exists to make visible. A caller that passes a pointer MUST have filled every field of it;
// there is no partial form of the block.
// Who this device is, as GET /api/status reports it. BORROWED, like ot_wire_mqtt_t and for
// the same reason: the values come from esp_read_mac and esp_app_get_description, which
// ot_wire must never call -- a component that cannot build on the host cannot have its
// renderer tested against the document it renders.
//
// device_id is the twelve lower-case hex characters of the STA MAC, the same string
// ot_net_device_id() gives the MQTT client, and the same identity ot_ha uses for
// discovery. Two spellings of "who this device is" is that invariant lost again -- and the
// consumer that reads this keys a Home Assistant config entry on it, so a second spelling would
// duplicate every entity the first time the two disagreed.
typedef struct {
    const char *device_id;
    const char *mac;         // colon-separated, for Home Assistant's device `connections`
    const char *sw_version;  // esp_app_get_description()->version
} ot_wire_device_t;

// `device` MAY BE NULL, AND NULL OMITS THE THREE KEYS RATHER THAN RENDERING EMPTY STRINGS -- the
// same rule `mqtt` follows, for the same reason put_duration renders NEVER as null: "" and "we
// have nothing to say" are different answers, and a client keying a config entry on an empty
// string keys it on nothing at all.
size_t ot_wire_render_provision(const ot_wire_provision_t *status,
                                      const ot_wire_mqtt_t *mqtt,
                                      const ot_wire_device_t *device, char *out, size_t cap);

// --- command-layer request bodies ---------------------------------------
//
// Deferred along with ot_command, and returned here. What did NOT return
// with them is ot_wire_op_storage_t: it existed only as room for string parameters of an
// operation, and there are no such parameters -- a type with no caller reads as a
// supported contract, which it is not.
//
// They return ot_wire_status_t, not ot_wire_result_t: these two bodies have no field
// that it would make sense to name -- there is exactly one key and it is named in the
// contract itself, while the name of an operation parameter came from the client and
// never gets into a message (the header's rule: a message names the FIELD, not its
// content, and here the field is named by the status). The caller needs a code, not a
// struct with two empty members.

// The parsed value and the parsed parameters live HERE and not in ot_command, and that
// was decided by linking, not by taste. While they lay there, ot_wire required
// ot_command, and the test_wire_command suite -- a test of TEXT parsing -- dragged in
// ot_state, ot_registry and the whole boiler model; the build failed on ot_lock, which
// that test has nowhere to get. Spreading them across different files of one component
// would not have helped: PlatformIO compiles all of a library's sources.
//
// The cut follows responsibility: here bytes become a value, in ot_command a value
// becomes a frame. The glue is in ot_http, which knows about both.
typedef struct {
    bool  is_bool;   // the client sent `true`/`false`, not a number
    bool  boolean;
    float number;    // with is_bool it repeats the boolean as 1.0 / 0.0
} ot_wire_value_t;

// Operation parameters. Flat and numeric: the operation name is in the path, and
// everything the operations need passed to them is numbers (scan bounds, durations).
//
// There are DELIBERATELY no string parameters here. In the previous firmware a separate
// storage type existed for them, and it had not a single user; a type with no caller
// reads as a supported contract, which it is not. Should a string be needed -- it will
// come back together with the operation that needs it.
#define OT_WIRE_PARAMS_MAX 4

typedef struct {
    char    name[OT_WIRE_PARAMS_MAX][24];
    float   value[OT_WIRE_PARAMS_MAX];
    uint8_t count;
} ot_wire_params_t;

// false if the parameter does not exist. `*out` is left untouched.
bool ot_wire_param(const ot_wire_params_t *p, const char *name, float *out);

// The body of a write to an entity: `{"value": <number|boolean>}`.
//
// A boolean is kept separate from a number: `true` and `1.0` are different answers, and
// a flag entity that was sent a number is a client error, not grounds for guessing.
//
// OT_WIRE_BAD_BODY -- the body is not a flat JSON object. OT_WIRE_BAD_FIELD -- the
// `value` key is missing, or it is neither a number nor a boolean. `*out` is left
// untouched on any failure.
ot_wire_status_t ot_wire_parse_entity_write(const char *body, ot_wire_value_t *out);

// The body of an operation: a FLAT object of numeric parameters, `{}` for an operation
// without them. The operation name is in the path, so there is no nested `params` here
// and the ot_json parser, which reads only flat objects, is fit as it is.
//
// Keys are enumerated via ot_json_key_at(): the client names the parameters, and a
// second JSON parser will not be written for that.
//
// OT_WIRE_BAD_FIELD -- there are more parameters than OT_WIRE_PARAMS_MAX, a name is
// longer than fits, or a value is not a number. Surplus parameters are rejected, not
// discarded: an operation launched without half of what was passed to it is not the
// operation that was asked for, and saying so is cheaper than explaining the result
// afterwards.
ot_wire_status_t ot_wire_parse_operation(const char *body, ot_wire_params_t *out);
//
// Everything else in this header -- the parsing of configuration and provisioning --
// does not depend on the registry.

#ifdef __cplusplus
}
#endif
