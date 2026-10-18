// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The provisioning documents, both directions: POST /api/provision decoded into the Wi-Fi record
// the radio is handed, and the status document GET /api/provision renders. The two rules at the
// top of ot_wire.c bind both -- no key is rendered, and no submitted value is quoted in an error.
#include "ot_wire.h"

#include <string.h>

#include "ot_json.h"
#include "ot_wire_writer.h"

// --- POST /api/provision ---------------------------------------------------------------------

ot_wire_result_t ot_wire_parse_provision(const char *body,
                                                     const ot_wifi_t *stored,
                                                     ot_wifi_t *out)
{
    if (out == NULL)
        return bad_body();
    if (ot_json_check(body) != OT_JSON_DOC_OK)
        return bad_body();

    char ssid[OT_CONFIG_SSID_MAX + 1] = {0};
    if (ot_json_string(body, "wifi_ssid", ssid, sizeof ssid) != OT_JSON_FOUND)
        return bad_field("wifi_ssid");

    // The three states of wifi_psk, resolved by ot_secret_decide() rather than by a rule
    // of this route's own: absent is KEEP, "" is CLEAR, anything else is STORE
    // (web/src/api/client.ts, ProvisionRequest). One vocabulary for both routes means the
    // firmware decodes it in one place instead of two that drift.
    char       psk[OT_CONFIG_PSK_MAX + 1] = {0};
    const bool has_stored = stored != NULL && stored->psk_len > 0;

    const ot_json_read_t read = ot_json_string(body, "wifi_psk", psk, sizeof psk);
    const char                *submitted = NULL;
    if (read == OT_JSON_FOUND)
        submitted = psk;
    else if (read != OT_JSON_MISSING)
        return bad_field("wifi_psk");

    ot_wifi_t record = {0};
    record.ssid_len        = (uint8_t)strlen(ssid);
    memcpy(record.ssid, ssid, record.ssid_len);

    switch (ot_secret_decide(has_stored, submitted)) {
    case OT_SECRET_KEEP:
        // The owner changed only some OTHER field, or the page handed back the sentinel for an
        // untouched box. KEEP of nothing is an open-network attempt, which is the correct reading
        // and needs no special case.
        if (has_stored) {
            // But a KEPT password belongs to the network it was stored FOR. Pairing it with a
            // DIFFERENT SSID is the exact atomicity failure ot_config_apply() refuses on
            // /api/config (its wifi_ssid/psk block, OT_CONFIG_ERR_WIFI_PAIR): a new network
            // carrying the previous network's key, which strands the device -- the likeliest
            // catastrophe in this project (ot_provision.c). Refuse it here too, so the two routes
            // that write Wi-Fi credentials agree instead of one keeping what the other forbids.
            if (record.ssid_len != stored->ssid_len ||
                memcmp(record.ssid, stored->ssid, record.ssid_len) != 0)
                return refused(OT_CONFIG_ERR_WIFI_PAIR, "wifi_psk");
            record.psk_len = stored->psk_len;
            memcpy(record.psk, stored->psk, stored->psk_len);
        }
        break;
    case OT_SECRET_CLEAR:
        break;  // an open network: the record keeps its zero length
    case OT_SECRET_STORE:
        record.psk_len = (uint8_t)strlen(psk);
        memcpy(record.psk, psk, record.psk_len);
        break;
    case OT_SECRET_REJECT:
        // The sentinel submitted as a value on a device with nothing stored. It is thirteen
        // printable bytes and passes ot_config_check_psk whole, so nothing downstream would
        // catch it -- and a device holding credentials it cannot associate with raises no access
        // point, which is the disappearance ot_provision.c calls the likeliest catastrophe
        // in this project, reached with nobody having made a typo.
        return refused(OT_CONFIG_ERR_PSK, "wifi_psk");
    }

    // Validated on the way IN, never at the point of use: a stored value that is only discovered
    // to be impossible at the next boot is one the surface that could have fixed it is no longer
    // up to fix (ot_config.h, the first of its three rules).
    const ot_config_err_t ssid_err = ot_config_check_ssid(record.ssid, record.ssid_len);
    if (ssid_err != OT_CONFIG_OK)
        return refused(ssid_err, "wifi_ssid");
    const ot_config_err_t psk_err = ot_config_check_psk(record.psk, record.psk_len);
    if (psk_err != OT_CONFIG_OK)
        return refused(psk_err, "wifi_psk");

    // The SAME predicate ot_config_nvs_has_credentials() asks, over the same record. It
    // cannot fail after the two checks above -- it is those two checks -- and it is asked anyway
    // because the pair being usable is what decides whether the device raises an access point,
    // and a second answer to that question is the state the invariants forbid outright.
    if (!ot_config_wifi_usable(&record))
        return refused(OT_CONFIG_ERR_WIFI_PAIR, NULL);

    // Written only now. A caller that ignores the status must not find half a credential here.
    *out = record;
    return ok();
}

// --- GET /api/provision (the spellings it uses are ot_wire_state_name() and friends, in ot_wire.c)

// OT_PROV_NEVER is not a duration and must not be rendered as one. "No time left" and "no
// end" are opposite answers: a window with 0 ms left is closed, a window that never closes is
// a device sealed behind the front panel of a running unit, and a page that showed 0 for both
// would count that device down to a deadline it does not have.
static void put_duration(writer_t *w, uint32_t ms)
{
    if (ms == OT_PROV_NEVER)
        put(w, "null");
    else
        put_u32(w, ms);
}

// The broker's counters, as the last member of the status document. LAST ON PURPOSE and not for
// tidiness: it is the only nested object the document has, and ot_json_check() stops at the
// first one it meets (ot_json.cpp:377-381), so every flat field ahead of it stays reachable
// to the reader that walks the document from the front.
//
// DO NOT give this an "omitted means zero" form. The caller passing NULL is saying nothing is known
// about a broker, which is a different fact from every counter being zero -- see the note on
// ot_wire_render_provision in the header.
static void put_mqtt(writer_t *w, const ot_wire_mqtt_t *mqtt)
{
    put(w, ",");
    put_key(w, "mqtt");
    put(w, "{");
    // Stored, NOT reached. An owner whose broker has never answered sees configured true beside
    // connected false, and that pair is the diagnosis; one flag covering both would erase it.
    put_key(w, "configured");
    put_bool(w, mqtt->configured);
    put(w, ",");
    put_key(w, "connected");
    put_bool(w, mqtt->connected);
    put(w, ",");
    put_key(w, "published");
    put_u32(w, mqtt->published);
    put(w, ",");
    put_key(w, "commands");
    put_u32(w, mqtt->commands);
    put(w, ",");
    put_key(w, "rejected");
    put_u32(w, mqtt->rejected);
    put(w, ",");
    put_key(w, "reconnects");
    put_u32(w, mqtt->reconnects);
    put(w, ",");
    // The number and the sentence together, and the sentence comes from the same function the MQTT
    // log line prints (ot_wire_connack_name(), which ot_mqtt_link.c's report() also calls). That is
    // the whole point of the pair: an owner
    // who mistyped a broker password was told "broker unreachable" every ten seconds, and the
    // refusal that says otherwise was reachable only with a serial cable until this member existed.
    put_key(w, "last_connack");
    put_u32(w, mqtt->last_connack);
    put(w, ",");
    put_key(w, "last_connack_reason");
    put_string(w, ot_wire_connack_name(mqtt->last_connack));
    put(w, "}");
}

size_t ot_wire_render_provision(const ot_wire_provision_t *status,
                                      const ot_wire_mqtt_t *mqtt,
                                      const ot_wire_device_t *device, char *out, size_t cap)
{
    if (status == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};

    put(&w, "{");
    put_key(&w, "state");
    put_string(&w, ot_wire_state_name(status->state));
    put(&w, ",");
    put_key(&w, "mode");
    put_string(&w, ot_wire_mode_name(status->mode));
    put(&w, ",");
    // The answer the access policy acts on. Published so that "why will it not let
    // me save anything" has an answer that does not need a serial cable.
    put_key(&w, "provisioned");
    put_bool(&w, status->provisioned);
    put(&w, ",");
    put_key(&w, "has_credentials");
    put_bool(&w, status->has_credentials);
    put(&w, ",");
    put_key(&w, "wifi_known_good");
    put_bool(&w, status->has_known_good);
    put(&w, ",");
    put_key(&w, "connected");
    put_bool(&w, status->connected);
    put(&w, ",");
    put_key(&w, "wifi_ssid");
    put_string(&w, status->ssid);
    put(&w, ",");
    put_key(&w, "ip");
    put_string(&w, status->ip);
    put(&w, ",");
    put_key(&w, "ap_ssid");
    put_string(&w, status->ap_ssid);
    put(&w, ",");
    put_key(&w, "ap_on_air");
    put_bool(&w, status->ap_on_air);
    put(&w, ",");
    // A window closing has to be OBSERVABLE, or a network that silently
    // vanished is indistinguishable from a device that broke.
    put_key(&w, "setup_window_open");
    put_bool(&w, status->window_open);
    put(&w, ",");
    put_key(&w, "setup_window_remaining_ms");
    put_duration(&w, status->window_remaining_ms);
    put(&w, ",");
    put_key(&w, "fallback_ap_in_ms");
    put_duration(&w, status->fallback_in_ms);
    put(&w, ",");
    // The invariant, in the API where the owner can see it: four different answers instead of one
    // "it will not connect". The word is ot_provision's, so the REST
    // projection and the MQTT one cannot invent a second spelling.
    put_key(&w, "failure");
    put_string(&w, ot_prov_failure_name(status->failure));
    put(&w, ",");
    // The raw wifi_err_reason_t, for an owner looking one up. 0 when there is none -- the
    // no-address answer is the ABSENCE of an event rather than an event.
    put_key(&w, "failure_reason");
    put_u32(&w, status->failure_reason);
    // Who answered. Absent rather than empty when the caller has nothing to say, because a
    // consumer keys its record of this device on device_id and an empty one is not an identity.
    if (device != NULL) {
        put(&w, ",");
        put_key(&w, "device_id");
        put_string(&w, device->device_id);
        put(&w, ",");
        put_key(&w, "mac");
        put_string(&w, device->mac);
        put(&w, ",");
        put_key(&w, "sw_version");
        put_string(&w, device->sw_version);
    }
    // Absent rather than zeroed when the caller has nothing to say about a broker.
    if (mqtt != NULL)
        put_mqtt(&w, mqtt);
    put(&w, "}");

    return finish(&w);
}
