// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// When a discovery document is owed (ot_ha.h): what HA should have, and what this connection has
// told it. Pure; the glue asks once a pass and publishes what ot_ha_plan_next() names.
#include "ot_ha.h"

#include <math.h>
#include <string.h>

#include "ot_registry.h"

ot_ha_want_t ot_ha_want(const ot_ha_doc_t *doc, bool discovery_on, ot_availability_t a)
{
    // Discovery off clears what an earlier boot published: a device the owner has told to keep
    // out of HA must not leave its entities retained under homeassistant/ for ever.
    if (doc == NULL || !discovery_on)
        return OT_HA_DROP;
    if (!doc->wire)
        return OT_HA_WANT;
    switch (a) {
    case OT_AVAIL_UNKNOWN:     return OT_HA_WAIT;
    case OT_AVAIL_UNSUPPORTED: return OT_HA_DROP;
    case OT_AVAIL_OK:
    case OT_AVAIL_INVALID:     return OT_HA_WANT;   // supported, no data right now
    }
    return OT_HA_WAIT;
}

void ot_ha_wants(bool discovery_on, ot_ha_want_t want[OT_HA_DOC_COUNT])
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++) {
        const ot_entity_t *e = ot_registry_at(OT_HA_DOCS[i].entity);
        ot_value_t         v = {.availability = OT_AVAIL_UNKNOWN};
        if (e != NULL)
            (void)ot_state_get(e->key, &v);
        want[i] = ot_ha_want(&OT_HA_DOCS[i], discovery_on, v.availability);
    }
}

static int16_t to_dc(float celsius)
{
    return (int16_t)lroundf(celsius * 10.0f);
}

void ot_ha_bounds(int16_t flow_min_dc, int16_t flow_max_dc, int16_t lo_dc[OT_HA_DOC_COUNT],
                  int16_t hi_dc[OT_HA_DOC_COUNT])
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++) {
        lo_dc[i] = 0;
        hi_dc[i] = 0;
        if (OT_HA_DOCS[i].bounds == OT_HA_BOUNDS_FLOW) {
            lo_dc[i] = flow_min_dc;
            hi_dc[i] = flow_max_dc;
        } else if (OT_HA_DOCS[i].bounds == OT_HA_BOUNDS_STATE) {
            const ot_entity_t *e  = ot_registry_at(OT_HA_DOCS[i].entity);
            float              lo = 0.0f, hi = 0.0f;
            if (e != NULL && ot_state_bounds(e->key, &lo, &hi)) {
                lo_dc[i] = to_dc(lo);
                hi_dc[i] = to_dc(hi);
            }
        }
    }
}

void ot_ha_plan_reset(ot_ha_plan_t *plan)
{
    memset(plan, 0, sizeof *plan);
}

static bool bounded(uint16_t i) { return OT_HA_DOCS[i].bounds != OT_HA_BOUNDS_NONE; }

int ot_ha_plan_next(const ot_ha_plan_t *plan, const ot_ha_want_t want[OT_HA_DOC_COUNT],
                    const int16_t lo_dc[OT_HA_DOC_COUNT], const int16_t hi_dc[OT_HA_DOC_COUNT],
                    bool *empty)
{
    for (uint16_t i = 0; i < OT_HA_DOC_COUNT; i++) {
        if (want[i] == OT_HA_WANT) {
            const bool stale = plan->sent[i] != OT_HA_SENT_CONFIG ||
                               (bounded(i) && (plan->lo_dc[i] != lo_dc[i] ||
                                               plan->hi_dc[i] != hi_dc[i]));
            if (stale) {
                *empty = false;
                return i;
            }
        } else if (want[i] == OT_HA_DROP && plan->sent[i] != OT_HA_SENT_EMPTY) {
            *empty = true;
            return i;
        }
    }
    return -1;
}

void ot_ha_plan_done(ot_ha_plan_t *plan, int index, bool empty, int16_t lo_dc, int16_t hi_dc)
{
    if (index < 0 || index >= OT_HA_DOC_COUNT)
        return;
    plan->sent[index]  = empty ? OT_HA_SENT_EMPTY : OT_HA_SENT_CONFIG;
    plan->lo_dc[index] = lo_dc;
    plan->hi_dc[index] = hi_dc;
}
