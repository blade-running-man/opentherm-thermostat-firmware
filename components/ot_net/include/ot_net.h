// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Bringing the device onto a network, and keeping it reachable when that fails.
//
// The transport is named, not assumed. Wi-Fi only ships today, but the Olimex ESP32-EVB is
// Ethernet (esp_eth), and a network layer that later learns about Ethernet has to be
// rewritten rather than extended. The board boundary exists from the
// start, and board_t already carries which transport a board has.
//
// THIS FILE DECIDES NOTHING. What the device should be doing is ot_provision's answer and
// this is the half that owns the radio, the flash and the clock: it feeds the machine facts,
// reads back a mode and an action, and does them. That split is why every case in
// test/test_provision is a line of C rather than a fifteen-minute wait in front of a device, and
// it is the reason there is no second idea of "connected" anywhere in here.
//
// THE RADIO HAS ONE OWNER, and it is the task started by ot_net_start(). Everything below
// that a request handler may call takes the same lock that task takes around every esp_wifi call
// -- ot_net_scan() is the one that visibly costs something for it, because a sweep is
// seconds long and the machine's tick waits behind it. DO NOT add a call that touches esp_wifi
// from outside this file: two tasks driving one radio is how a mode change lands between a
// set_config and a connect.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "ot_config.h"
#include "ot_provision.h"
#include "ot_wire.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OT_NET_DOWN,
    // Serving its own access point: reachable, but by anyone in radio range.
    OT_NET_ACCESS_POINT,
    OT_NET_CONNECTED,
} ot_net_state_t;

// Called whenever the state changes, from the provisioning task. Keep it short.
typedef void (*ot_net_cb_t)(ot_net_state_t state, void *ctx);

esp_err_t ot_net_start(const board_t *board, ot_net_cb_t cb, void *ctx);

ot_net_state_t ot_net_get_state(void);

// Whether a pair this radio could actually use is stored. NOT "the device is claimed" -- see
// ot_net_is_provisioned(), which is the question the access policy asks and the one that function
// answers differently.
bool ot_net_has_credentials(void);

// The single most important question this component answers.
//
// FALSE FOR AS LONG AS AN OPEN ACCESS POINT IS ON THE AIR, whatever NVS holds, because in that
// moment anyone in radio range is "the client". Storing credentials must not flip it: that gap is
// where a passer-by hands the device to their own network and then sets the first UI password on
// it, two requests apart.
//
// ASKED, never pushed. A pushed copy is a tick out of date, and the tick it is out of date by is
// exactly the window this exists to close: the access point goes off the air the instant the
// station gets an address, and a policy still holding last tick's answer would refuse a write
// that is now legitimate -- or, the way round that matters, allow one that is not.
bool ot_net_is_provisioned(void);

// --- provisioning -------------------------------------------------------------------------

// Hand the device a network. Stores the pair as ONE blob and asks the machine to start a trial.
//
// RETURNS AS SOON AS THE PAIR IS STORED, and the connection attempt happens afterwards on the
// provisioning task. That ordering is an invariant, not an optimisation: the station tunes to the
// router's channel and drags the access point with it, so the phone holding the setup page drops
// -- and if the answer had not gone out first, everything that worked looks like a failure.
// A handler that awaits the connection here breaks it.
//
// The pair is NOT validated in here. ot_config_wifi_usable() is the one predicate, and the
// caller has already had to run it to build the record.
esp_err_t ot_net_provision(const ot_wifi_t *wifi);

// The same thing straight from a POST body, decoded HERE and not by the caller.
//
// The decoding itself is ot_wire_parse_provision(), which is pure and tested on the host;
// what this adds is the one thing that function needs and a request handler must not have -- the
// STORED PAIR. `wifi_psk` has three states and the third is "keep the key already stored"
// (web/src/api/client.ts, ProvisionRequest), so resolving the body requires reading the key. A
// handler that could read it could also log it, and the log ring is served to anybody by
// GET /api/log. So the body comes in, the answer goes out, and the key stays
// on this side of the wall -- the same arrangement ot_net_check_password() uses for the
// password record, and for the same reason.
//
// `result` may be NULL. When it is not, it carries why a body was refused, in a form a handler
// can turn into a status code and a field name without quoting anything the client sent.
esp_err_t ot_net_provision_body(const char *body, ot_wire_result_t *result);

// Somebody is talking to us on the access point. This extends the setup window, because the
// window closing while the owner is in the next room reading the password off their router is the
// most annoying failure in the whole design, and it is not an attack.
//
// Cheap and safe from a request handler: it queues a fact and returns.
void ot_net_note_client(void);

// --- what the owner is told -----------------------------------------------------------------

// The row shape is ot_wire's, not one of this component's own. It is what turns a sweep
// into bytes, and a struct defined on both sides of that boundary is two struct layouts one
// memcpy away from disagreeing.
typedef ot_wire_network_t ot_net_scan_entry_t;

// One sweep is thirteen channels and a handful of records per channel. Bounded because the buffer
// is static: a scan on a device with 512 KB of RAM must not size itself from what is on the air.
#define OT_NET_SCAN_MAX 32

// The networks the DEVICE can see, which is not the list the phone can see -- a router publishes
// one name on both bands and the C6 has only the 2.4 GHz radio. That difference is the single most
// common support request for any device of this kind, which is why this list is treated as an
// invariant rather than a feature.
//
// SYNCHRONOUS AND SECONDS LONG. It takes the radio lock, so the provisioning tick waits behind it,
// and on a device serving the setup page from its own access point the radio leaves that channel
// while it sweeps -- the phone may lose the page mid-request. Nothing here retries, restarts or
// reboots for that: the caller reports "no list this time" and manual entry stays available
// (web/src/api/client.ts, scanNetworks).
//
// Duplicate SSIDs are NOT filtered. One sweep returns a record per BSSID, so a mesh answers
// several times under one name; which of them to show has more information available to it in the
// page than here (mergeScan, web/src/pages/settings/wifi.ts).
esp_err_t ot_net_scan(ot_net_scan_entry_t *out, size_t cap, size_t *count);

// Everything the provisioning status document is built from. A snapshot, taken under the lock, so
// the fields agree with one another -- a state that says CONNECTED beside a failure of
// "wrong-password" is a status page nobody can act on.
//
// NO SSID OF ANY NETWORK BUT THE CONFIGURED ONE, and NO PSK EVER. The SSID is not a secret -- it
// is in every beacon -- and it is the one fact that makes "it will not connect" diagnosable. The
// key is not here, is not in ot_prov_t (its static_assert says so), and must not be added.
//
// Defined in ot_wire, where the renderer that reads it lives, for the same reason the scan
// row is: the struct and the document it becomes are one decision.
typedef ot_wire_provision_t ot_net_status_t;

void ot_net_status(ot_net_status_t *out);

// --- the configuration document ------------------------------------------------------------
//
// It is loaded here, it lives here, and it leaves here only projected. The HTTP layer gets these
// three and nothing else: it never sees ot_config_t, never opens NVS, and therefore cannot
// grow a second idea of what a valid document is or of what "password set" means.

// The document as a client may see it: every secret already replaced by the sentinel. Copies
// under the same lock the provisioning machine runs under, so a save landing mid-render cannot
// produce a document describing a moment that never existed.
//
// `known_good` may be NULL. It is NOT part of the stored document and cannot be -- it is the
// provisioning machine's answer to "has a stored pair ever actually produced an address", which
// is the ONLY condition under which a failed provisioning rolls back. The settings page cannot
// tell the truth without it, and `wifi_ssid != ""` is not a substitute: an SSID stored beside a
// mistyped key has never produced an address, so a page that promised a rollback there would
// tell the owner to wait for a return that is not coming. It is taken under the same lock as the
// document so the two describe one moment.
void ot_net_config_snapshot(ot_config_public_t *out, bool *known_good);

// Validates the patch, applies it all-or-nothing, and persists it. A rejected patch changes
// nothing -- neither in memory nor in flash -- which is what lets the owner retry against the
// document they were actually looking at. Before ot_net_start() has created its lock (it can
// return without one) there is no document to apply to: OT_CONFIG_ERR_NO_DOCUMENT, nothing taken.
ot_config_err_t ot_net_config_apply(const ot_config_patch_t *patch);

// Whether a candidate matches the stored web UI password. Constant time, and the hash never
// leaves this component: a caller that could read the record could also log it, and the log is
// served to anyone by GET /api/log.
bool ot_net_check_password(const char *candidate);

// The broker settings, WITH THE PASSWORD IN THE CLEAR.
//
// This is the one projection of the configuration that is not redacted, and it exists because
// MQTT authenticates with the plaintext -- a hash there is not hardening, it is a broker that
// never connects. Everything else in this firmware gets
// ot_config_public_t, where the same field is already a sentinel.
//
// SO THIS STRUCT MUST NEVER REACH ESP_LOG*, and neither must any field of it. The log ring is
// served to whoever can reach GET /api/log. It is filled under the lock and the caller owns it;
// keep it on a stack that dies quickly.
typedef struct {
    char     host[OT_CONFIG_HOST_MAX + 1];
    uint16_t port;
    char     user[OT_CONFIG_USER_MAX + 1];
    char     password[OT_CONFIG_BROKER_PASS_MAX + 1];
    char     topic_prefix[OT_CONFIG_PREFIX_MAX + 1];
    char     device_name[OT_CONFIG_NAME_MAX + 1];
    bool     ha_discovery;
} ot_net_broker_t;

void ot_net_broker(ot_net_broker_t *out);

// The full MAC as twelve lower-case hex characters, and as a colon-separated address. Identity
// is the MAC and never the name.
void ot_net_device_id(char out[OT_CONFIG_DEVICE_ID_LEN + 1]);
void ot_net_mac_string(char out[18]);

// The address the device currently holds, or "" when it has none. For Home Assistant's
// configuration_url, which its validator refuses without a scheme.
void ot_net_ip_string(char out[16]);

// Derived from the stored record being non-empty -- never its own key. The access policy asks
// this rather than keeping its own flag, so the two cannot disagree about whether the device
// has an owner.
bool ot_net_password_set(void);

#ifdef __cplusplus
}
#endif
