// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// One-shot, short-lived tickets with which a WebSocket authorizes.
//
// **Why they exist at all.** All the rest of the API is closed off by HTTP Basic. The
// browser does NOT send an Authorization header on a WebSocket handshake -- neither
// `new WebSocket()` nor the `ws://user:pass@host` form does so reliably. So without a
// separate mechanism the socket would be either open to everyone who can reach the
// device, or would not work from a browser at all. The ticket is issued by an ORDINARY
// endpoint under the same password, and the socket presents it in the request.
//
// **The component is pure.** Randomness arrives as an argument, time arrives as an
// argument: on the device the value comes from esp_random(), in tests it is given. That
// also makes "generate a ticket from the table" impossible -- it cannot invent them.
//
// Ownership: the table belongs to the caller. Nothing is allocated.
// Failures: a boolean result. Not one function can fail in a way that would require
// examining the reason: "no" here always means "do not open the socket".
//
// It is kept in RAM and does not survive a reboot -- and MUST NOT: a saved ticket would
// outlive a change of the interface password.

#ifdef __cplusplus
extern "C" {
#endif

// TWICE AS MANY as the server has client sessions (seven -- OT_HTTP_MAX_CLIENTS, and
// the same place explains why seven and not four).
//
// Seven is how many tickets can be IN USE at once: the server will not accept more
// sockets, so an eighth ticket would lead nowhere. The second seven are for abandoned
// ones: the tab was closed between the REST response and the handshake, and the slot is
// held until the TTL runs out, needed by nobody any more.
//
// **The size of the table IS the answer to overflow**; the eviction policy never was.
// Someone asking for tickets faster than they are spent will evict someone else's
// ticket in the gap between the REST response and the handshake under ANY policy, and
// if issuance were refused instead -- would shut the door for thirty seconds. What
// protects the endpoint is not this table but the access policy: POST /api/ws-ticket
// requires the same password as everything else (ot_http_policy.c). The table merely
// has to be large enough that ORDINARY use -- several tabs, a page reload, a
// reconnection after the laptop sleeps -- fits into it, and four slots were not enough
// for that.
#define OT_TICKET_SLOTS 14

// 128 bits of randomness in hexadecimal. Cannot be guessed in thirty seconds.
#define OT_TICKET_LEN 32

#define OT_TICKET_TTL_MS 30000u

typedef struct {
    char     value[OT_TICKET_LEN + 1];
    uint32_t issued_ms;
    bool     live;
} ot_ticket_slot_t;

typedef struct {
    ot_ticket_slot_t s[OT_TICKET_SLOTS];
} ot_ticket_table_t;

void ot_ticket_reset(ot_ticket_table_t *t);

// Puts a ready value into the table. `value` must be a string of exactly OT_TICKET_LEN
// characters; otherwise false and the table is unchanged.
//
// A slot counts as free if it is empty OR expired. If there are no free ones, the
// oldest is evicted.
//
// Eviction protects against NOTHING -- see the note on table size above. It merely
// chooses what to do when the table is full after all, and "the oldest" is better here
// than a refusal: the oldest LIVE ticket is the one that has gone unspent the longest,
// that is, most likely the abandoned one. A refusal instead would hold an abandoned
// ticket for all thirty seconds of its life and for exactly that long keep the owner
// off the page.
bool ot_ticket_issue(ot_ticket_table_t *t, const char *value, uint32_t now_ms);

// Spends a ticket. true exactly once per issued value and only within OT_TICKET_TTL_MS
// of issuance.
bool ot_ticket_redeem(ot_ticket_table_t *t, const char *value, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
