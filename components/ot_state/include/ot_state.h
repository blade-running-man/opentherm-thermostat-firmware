// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ot_frame.h"
#include "ot_registry.h"

// The state model: what the boiler said about every entity in the registry.
//
// Filled by the SINGLE verb ot_state_apply_dataid(), hung on the ot_bus callback. This
// is the same seam that separated transport from model in the previous firmware.
//
// **This is not ot_observe.** The bus's raw table holds the last sixteen bits for each
// of the 128 identifiers and serves diagnostics; here there are decoded values, indexed
// by position in the registry, availability, and marks for consumers. One does not
// replace the other: the raw table has neither a codec nor a key, and the model has no
// identifiers that are absent from the registry.
//
// Task context: the bus task writes, HTTP and MQTT read. Everything is
// taken under ot_lock(); it is recursive, so the renderer can hold a whole snapshot and
// call ot_state_get() inside.
//
// Failures: not a single function returns esp_err_t. An unknown key gives false, an
// identifier outside the registry is silently ignored. A frame from a faulty slave has
// no right to bring the model down.

#ifdef __cplusplus
extern "C" {
#endif

// Availability is three-valued, plus "not asked yet".
//
// DATA-INVALID and UNKNOWN-DATAID are DIFFERENT diagnoses, and both arrive with the
// value 0x0000. The first means "the entity exists, there is no data right now": that
// is how ID 5 answers when there are no faults, and ID 27 when no outside sensor is
// connected. The second means "I do not have such an identifier". Collapsing them into
// one means showing "no faults" and "0 °C outside" where neither was said.
typedef enum {
    OT_AVAIL_UNKNOWN = 0,   // not asked yet, or there was no answer
    OT_AVAIL_OK,            // the value is valid
    OT_AVAIL_INVALID,       // the boiler answered data-invalid
    OT_AVAIL_UNSUPPORTED,   // the boiler answered unknown-dataid twice
} ot_availability_t;

typedef struct {
    ot_availability_t availability;
    float             number;     // NaN when availability != OT_AVAIL_OK
    bool              boolean;    // meaningful for kind == OT_KIND_BINARY
    uint32_t          updated_ms; // when the value arrived; zero if it never did
} ot_value_t;

// Consumers, each with its own set of "changed" marks.
//
// Marks per consumer rather than one shared set: the web interface, having collected a
// change, has no right to erase it for MQTT, which is not connected at that moment.
typedef enum {
    OT_CONSUMER_WEB = 0,
    OT_CONSUMER_MQTT,
    OT_CONSUMER_COUNT,
} ot_consumer_t;

// Zeroes the model. Needed by tests and on first start.
void ot_state_reset(void);

// Applies the boiler's answer to ALL entities that read this Data-ID. There can be
// several: ID 5 has seven, ID 0 has six.
//
// DO NOT BLOCK for long: it is called from the bus task, and while it computes, no
// conversation is happening.
void ot_state_apply_dataid(uint8_t data_id, ot_msg_type_t type, uint16_t raw,
                           uint32_t now_ms);

// The one way a value reaches a SYNTHETIC entity (data_id -1): what the executor
// decided, handed in by the task layer. Availability becomes OK; the "changed" marks rise
// only on a change, the timestamp is renewed always -- the rule of ot_state_apply_dataid().
//
// The value by kind: a binary or switch is `value != 0` (stored as boolean, and as 0/1 in
// number); an enum is its option INDEX, in the order of the table's options; anything else
// is the number as given.
//
// false, and NOTHING is written, when:
//   - the key is unknown or NULL;
//   - the key's data_id >= 0 -- the value of a boiler entity arrives only from the wire;
//   - value is NaN or infinite -- a renderer would print "nan", which is not JSON;
//   - the entity is an enum and value is not a whole index of one of its options.
//
// Takes ot_lock() like every writer of the model; any task. Not from an ISR.
bool ot_state_set_virtual(const char *key, float value, uint32_t now_ms);

// false if the key is not in the registry. *out is left untouched.
bool ot_state_get(const char *key, ot_value_t *out);

// The entity's bounds: from the bounds_from that was read, when it has arrived,
// otherwise from the table. false if the key does not exist or the entity declares no
// range.
bool ot_state_bounds(const char *key, float *min_out, float *max_out);

// Whether the Data-ID is marked unsupported. ot_bus reads it to drop the ID from the
// round.
bool ot_state_is_unsupported(uint8_t data_id);

// Takes the consumer's "changed" mark: true and clears it, or false.
bool ot_state_take_dirty(ot_consumer_t consumer, int index);

#ifdef __cplusplus
}
#endif
