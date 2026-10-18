// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Access to the generated entity registry.
//
// The registry is the SINGLE list: REST, MQTT topics, discovery and the frontend types
// are all generated from tools/opentherm_ids.py. Here there is only lookup into it.
//
// Ownership: every pointer leads into the header's static data. Do not free, they live
// for the lifetime of the program. All functions are pure, stateless, and safe from any
// task and from an ISR.
//
// **One Data-ID carries several entities.** For ID 5 that is six flags plus the
// manufacturer code, for ID 0 six status flags. That is why lookup by identifier gives
// an ITERATOR, not a single record: returning the first one found would mean never
// filling in the rest.

#include "registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t ot_registry_count(void);

// NULL if index is outside the registry.
const ot_entity_t *ot_registry_at(uint16_t index);

// NULL if the key does not exist or key == NULL.
const ot_entity_t *ot_registry_by_key(const char *key);

// -1 if the key does not exist. The position is stable within one firmware build and
// serves as the index into ot_state's arrays.
int ot_registry_index_of(const char *key);

// Traversal of the entities of a single Data-ID. They return a position, or -1 when
// there are no more:
//
//     for (int i = ot_registry_first_for_id(id); i >= 0;
//          i = ot_registry_next_for_id(id, i))
//         ...
//
// A synthetic row (data_id -1) is never yielded, for any data_id: its value
// comes from ot_state_set_virtual(), never from the wire.
int ot_registry_first_for_id(uint8_t data_id);
int ot_registry_next_for_id(uint8_t data_id, int after);

// Whether the entity's value is a boolean -- a flag bit, a synthetic binary or switch --
// rather than a number. By KIND, not by codec: a synthetic switch has OT_CODEC_NONE, and
// "boolean means OT_CODEC_FLAG" prints it as 1.00. Every renderer of a value asks THIS, so
// /api/state, /ws and MQTT cannot disagree about which rows are booleans. false for NULL.
bool ot_registry_is_boolean(const ot_entity_t *e);

// The options of an OT_KIND_ENUM entity, stored as one "a|b|c" string (OT_OPTION_SEP).
// 0 for every other kind and for NULL.
uint8_t ot_registry_option_count(const ot_entity_t *e);

// The index-th option, NOT NUL-terminated: its length goes to *len (which may be NULL);
// print it with "%.*s". NULL when e is not an enum or index is past the last option.
const char *ot_registry_option(const ot_entity_t *e, unsigned index, size_t *len);

// The polling round, from the table. ID 0 is not part of it: the scheduler sends that
// one on every second step.
uint16_t ot_registry_poll_count(void);
uint8_t  ot_registry_poll_at(uint16_t index);

#ifdef __cplusplus
}
#endif
