// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// See ot_wire.h for what these five documents are and why they are not in ot_http.
//
// TWO RULES GOVERN EVERY FUNCTION BELOW, and both are the same fact seen from two sides: this
// runs on a device whose setup access point is OPEN, and everything it logs is served to
// whoever can reach it by GET /api/log.
//
//  * NO SECRET IS RENDERED. The projection is what turns a stored password into a sentinel, and
//    ot_config_public_t is deliberately the only thing the config renderer accepts -- a
//    renderer handed the raw document would be one decision away from publishing one.
//  * NO SUBMITTED VALUE IS QUOTED IN AN ERROR. A message names a FIELD, never its contents, and
//    ot_wire_result_t carries a pointer to a literal for exactly that reason.
//
// The component is four sources behind the one public header, cut because this
// file had reached 665 lines against the 350-line ceiling (CLAUDE.md) with the executor settings
// about to land in it. One responsibility per file, and the two rules above bind every one of them:
//   ot_wire.c            the sentences and spellings the other files answer with, and the scan
//   ot_wire_config.c     GET and POST /api/config
//   ot_wire_provision.c  POST and GET /api/provision
//   ot_wire_command.c    the command-layer bodies
// ot_wire_writer.h is what they share. A helper that has to leave its file goes there, never into
// include/: the public header is the contract, and the cut did not change it.
#include "ot_wire.h"

#include "ot_wire_writer.h"

const char *ot_wire_strerror(ot_wire_result_t result)
{
    switch (result.status) {
    case OT_WIRE_OK:
        return "Accepted.";
    case OT_WIRE_BAD_BODY:
        return "The request body is not a JSON object this device can read.";
    case OT_WIRE_BAD_FIELD:
        // The FIELD is carried beside this sentence rather than formatted into it: this string is
        // a literal precisely so that nothing submitted can end up inside it, and a handler that
        // wants to name the field renders result.field as its own JSON member.
        return "A field is missing, of the wrong type, or longer than this device can store.";
    case OT_WIRE_REFUSED:
        return ot_config_strerror(result.err);
    case OT_WIRE_READ_ONLY:
        return ot_config_strerror(OT_CONFIG_ERR_READ_ONLY);
    case OT_WIRE_WRONG_ROUTE:
        // Names the route that works. The whole GET /api/config document carries wifi_ssid --
        // the page needs it to show which network is configured -- so a client that echoes the
        // document back arrives here without having done anything unreasonable, and a message
        // saying only "bad field" would send it looking at the value.
        return "The network is set through POST /api/provision, not here.";
    }
    return "The request could not be applied.";
}

// --- GET /api/wifi/scan --------------------------------------------------------------------

size_t ot_wire_render_scan(const ot_wire_network_t *list, size_t count, char *out,
                                 size_t cap)
{
    writer_t w = {out, cap, 0, false};

    put(&w, "[");
    for (size_t i = 0; i < count && list != NULL; i++) {
        if (i > 0)
            put(&w, ",");
        put(&w, "{");
        put_key(&w, "ssid");
        // Through the escaper, and this is the one row where that is not a formality: an SSID is
        // chosen by whoever set up the router next door, so `"},{"ssid":"` is a name somebody can
        // broadcast at this device.
        put_string(&w, list[i].ssid);
        put(&w, ",");
        put_key(&w, "rssi");
        put_i32(&w, list[i].rssi);
        put(&w, ",");
        put_key(&w, "secure");
        put_bool(&w, list[i].secure);
        put(&w, "}");
    }
    put(&w, "]");

    return finish(&w);
}

// --- GET /api/provision --------------------------------------------------------------------

const char *ot_wire_state_name(ot_prov_state_t state)
{
    switch (state) {
    case OT_PROV_UNCONFIGURED:  return "unconfigured";
    case OT_PROV_CONNECTING:    return "connecting";
    case OT_PROV_CONNECTED:     return "connected";
    case OT_PROV_TRIAL:         return "trial";
    case OT_PROV_RETRYING:      return "retrying";
    case OT_PROV_FALLBACK_AP:   return "fallback-ap";
    case OT_PROV_WINDOW_CLOSED: return "window-closed";
    }
    // Never NULL. A renderer that printed (null) here would put a bare word in the document and
    // the page would fail to parse a perfectly healthy device.
    return "unknown";
}

const char *ot_wire_mode_name(ot_prov_mode_t mode)
{
    switch (mode) {
    case OT_PROV_MODE_OFF:          return "off";
    case OT_PROV_MODE_ACCESS_POINT: return "access-point";
    case OT_PROV_MODE_STATION:      return "station";
    case OT_PROV_MODE_AP_STA:       return "ap-station";
    }
    return "unknown";
}

// A uint32_t and not the esp-mqtt enum on purpose: this file builds on the host, where
// mqtt_client.h does not exist, and the caller has the code as a number anyway.
const char *ot_wire_connack_name(uint32_t code)
{
    switch (code) {
    // "no refusal", NOT "accepted": the field means only that no refusal was recorded, which is
    // also the state of a device that has never contacted a broker at all. Rendering "accepted"
    // there would tell the owner a broker approved us when nothing ever happened.
    case 0: return "no refusal";
    case 1: return "unacceptable protocol version";
    case 2: return "client identifier rejected";
    case 3: return "server unavailable";
    case 4: return "bad username or password";
    case 5: return "not authorized";
    }
    // Same rule as the two above: never NULL, and never a fabricated meaning. A broker sending a
    // code outside the specification tells us nothing, and saying so is the honest answer.
    return "unknown refusal code";
}
