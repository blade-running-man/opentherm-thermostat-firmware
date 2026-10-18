// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_ticket.h"

#include <string.h>

void ot_ticket_reset(ot_ticket_table_t *t)
{
    memset(t, 0, sizeof *t);
}

static bool expired(const ot_ticket_slot_t *s, uint32_t now_ms)
{
    // Unsigned subtraction survives the millisecond counter overflow after 49 days:
    // the difference stays correct and the comparison against the TTL stays meaningful.
    return (uint32_t)(now_ms - s->issued_ms) > OT_TICKET_TTL_MS;
}

static bool well_formed(const char *value)
{
    return value != NULL && strlen(value) == OT_TICKET_LEN;
}

bool ot_ticket_issue(ot_ticket_table_t *t, const char *value, uint32_t now_ms)
{
    if (!well_formed(value))
        return false;

    int chosen = -1;
    for (int i = 0; i < OT_TICKET_SLOTS; i++) {
        if (!t->s[i].live || expired(&t->s[i], now_ms)) { chosen = i; break; }
    }
    if (chosen < 0) {
        // All slots are live -- evict the oldest by issue time.
        chosen = 0;
        for (int i = 1; i < OT_TICKET_SLOTS; i++)
            if ((uint32_t)(now_ms - t->s[i].issued_ms) >
                (uint32_t)(now_ms - t->s[chosen].issued_ms))
                chosen = i;
    }

    memcpy(t->s[chosen].value, value, OT_TICKET_LEN + 1);
    t->s[chosen].issued_ms = now_ms;
    t->s[chosen].live      = true;
    return true;
}

bool ot_ticket_redeem(ot_ticket_table_t *t, const char *value, uint32_t now_ms)
{
    if (!well_formed(value))
        return false;

    for (int i = 0; i < OT_TICKET_SLOTS; i++) {
        if (!t->s[i].live || expired(&t->s[i], now_ms))
            continue;
        // A full comparison with no early exit. A ticket lives thirty seconds and is
        // spent once, so there is practically nothing with which to measure it
        // character by character in time -- but constant time costs nothing here, and
        // the reasoning "one could go faster here" one day moves somewhere it costs
        // dearly.
        unsigned diff = 0;
        for (int k = 0; k < OT_TICKET_LEN; k++)
            diff |= (unsigned)(t->s[i].value[k] ^ value[k]);
        if (diff == 0) {
            t->s[i].live = false;   // one-shot
            return true;
        }
    }
    return false;
}
