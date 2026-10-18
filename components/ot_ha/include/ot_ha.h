// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ha_generated.h"   // ot_ha_doc_t, OT_HA_DOCS, OT_HA_DOC_COUNT -- generated, DO NOT EDIT
#include "ot_state.h"       // ot_availability_t

// Home Assistant discovery: which documents HA is owed, and each one's bytes.
//
// The documents are GENERATED from the single list (tools/render_discovery.py): their shape, every
// key, which entities are gated on the owner topic, which take runtime bounds. This component
// fills six tokens and decides when a document is sent; it never decides what an entity IS in HA.
// render_discovery.py carries the five rules that kill an entity silently, and the tests of both.
//
// PURE: no FreeRTOS, no ESP-IDF, no allocation. Reads ot_state (under ot_lock, inside its own
// calls) and ot_registry; writes only the caller's buffers and plan. Host suites test_ot_ha and
// test_ot_ha_plan. The esp-mqtt glue (ot_mqtt_link) calls it on its own task.
//
// Failure: a render or a topic that does not fit returns 0 and is a DEFECT -- the glue logs the
// key and publishes nothing, never a truncated document. test_ot_ha builds the worst case against
// OT_HA_DOC_MAX.

#ifdef __cplusplus
extern "C" {
#endif

// The publish buffer the glue renders into. test_ot_ha renders every document with the longest
// context the configuration store allows and fails if one does not fit.
#define OT_HA_DOC_MAX   1536
#define OT_HA_TOPIC_MAX 128

// Everything a document needs that the registry does not carry. BORROWED: every pointer must
// outlive the call. "" means "we do not know it", and the key is then OMITTED (rule 5).
typedef struct {
    const char *prefix;      // the topic prefix, vetted by ot_config_check_prefix()
    const char *device_id;   // twelve lower-case hex characters: the node segment, the unique ids
    const char *mac;         // "aa:bb:cc:dd:ee:ff" for `connections`, or ""
    const char *name;        // the device's display name, any UTF-8 -- escaped here
    const char *model;       // or ""
    const char *sw_version;  // or ""
    const char *ip;          // a dotted quad for configuration_url, or "" (rule 4)
} ot_ha_ctx_t;

uint16_t           ot_ha_doc_count(void);
const ot_ha_doc_t *ot_ha_doc_at(uint16_t index);   // NULL past the end

// homeassistant/<component>/<device_id>/<object_id>/config. 0 when the context is unusable (a
// device id that is not twelve lower-case hex characters would break rule 3) or it does not fit.
size_t ot_ha_topic(const ot_ha_doc_t *doc, const ot_ha_ctx_t *ctx, char *out, size_t cap);

// The document with its tokens filled. lo_dc and hi_dc are read only by a bounded document
// (doc->bounds != OT_HA_BOUNDS_NONE), in tenths of a degree; lo_dc > hi_dc refuses it, because HA
// refuses a number whose min exceeds its max -- silently. 0 on refusal or when it does not fit.
size_t ot_ha_render(const ot_ha_doc_t *doc, const ot_ha_ctx_t *ctx, int16_t lo_dc, int16_t hi_dc,
                    char *out, size_t cap);

// --- when a document is owed --------------------------------------------------------------------

typedef enum {
    OT_HA_WAIT = 0,   // not yet: the boiler has not answered this row's Data-ID
    OT_HA_WANT,       // HA should have it
    OT_HA_DROP,       // HA should not: discovery is off, or the boiler does not support it
} ot_ha_want_t;

// A document read from the boiler (doc->wire) waits for its Data-ID's first answer and is dropped
// once the boiler has answered UNKNOWN-DATAID twice (ot_state's rule): an entity that appears on
// every boot and vanishes two minutes later is worse than one that appears once. Every other
// document is wanted whenever discovery is on.
ot_ha_want_t ot_ha_want(const ot_ha_doc_t *doc, bool discovery_on, ot_availability_t a);

// ot_ha_want() for every document, the availability read from ot_state.
void ot_ha_wants(bool discovery_on, ot_ha_want_t want[OT_HA_DOC_COUNT]);

// The runtime bounds of every document, in tenths: FLOW from the flow band the executor last
// stepped with, STATE from ot_state_bounds() -- ID 48 once read, the table
// before. 0 for an unbounded document.
void ot_ha_bounds(int16_t flow_min_dc, int16_t flow_max_dc, int16_t lo_dc[OT_HA_DOC_COUNT],
                  int16_t hi_dc[OT_HA_DOC_COUNT]);

// What this connection has told HA. A zeroed plan (or ot_ha_plan_reset()) has told it nothing.
typedef struct {
    uint8_t sent[OT_HA_DOC_COUNT];     // OT_HA_SENT_*
    int16_t lo_dc[OT_HA_DOC_COUNT];    // the bounds the sent config carried
    int16_t hi_dc[OT_HA_DOC_COUNT];
} ot_ha_plan_t;

enum { OT_HA_SENT_NOTHING = 0, OT_HA_SENT_CONFIG, OT_HA_SENT_EMPTY };

// Forget everything sent: a new connection, HA's `online`, a changed context (a new
// name, prefix or address). Every wanted document is then offered again, and every dropped one
// is cleared again -- an empty retained payload deletes the entity in HA.
void ot_ha_plan_reset(ot_ha_plan_t *plan);

// The next document owed, lowest index first, or -1. *empty: publish an EMPTY retained payload
// (drop) rather than the config. A wanted document is owed until settled with its current bounds,
// so a changed flow band re-offers ch_setpoint alone; a waiting document is never offered.
int ot_ha_plan_next(const ot_ha_plan_t *plan, const ot_ha_want_t want[OT_HA_DOC_COUNT],
                    const int16_t lo_dc[OT_HA_DOC_COUNT], const int16_t hi_dc[OT_HA_DOC_COUNT],
                    bool *empty);

// Document `index` is settled for these inputs: published -- or refused by ot_ha_render(), which
// is retried only when its inputs change, not on every pass. Out of range is ignored.
void ot_ha_plan_done(ot_ha_plan_t *plan, int index, bool empty, int16_t lo_dc, int16_t hi_dc);

#ifdef __cplusplus
}
#endif
