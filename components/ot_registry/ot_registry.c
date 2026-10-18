// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_registry.h"

#include <string.h>

uint16_t ot_registry_count(void)
{
    return (uint16_t)OT_ENTITY_COUNT;
}

const ot_entity_t *ot_registry_at(uint16_t index)
{
    return index < OT_ENTITY_COUNT ? &OT_ENTITIES[index] : NULL;
}

int ot_registry_index_of(const char *key)
{
    // Linear search deliberately: the registry is under a hundred records, and lookup
    // by key happens on an HTTP request, not in the conversation loop. A hash table here
    // would be a third representation of the same list.
    if (!key)
        return -1;
    for (uint16_t i = 0; i < OT_ENTITY_COUNT; i++)
        if (strcmp(OT_ENTITIES[i].key, key) == 0)
            return (int)i;
    return -1;
}

const ot_entity_t *ot_registry_by_key(const char *key)
{
    const int i = ot_registry_index_of(key);
    return i < 0 ? NULL : &OT_ENTITIES[i];
}

// A synthetic row never matches. The `>= 0` looks dead -- an int16_t -1 never
// equals a uint8_t promoted to int -- and it is not: it is the one thing that keeps a synthetic
// row out the day the comparison truncates to a byte, where -1 is 255, a legal frame ID. DO NOT
// drop it. test_a_synthetic_row_is_never_yielded_for_any_data_id kills the pair (the guard
// dropped AND a (uint8_t) cast); either change alone passes it.
static bool reads_id(const ot_entity_t *e, uint8_t data_id)
{
    return e->data_id >= 0 && e->data_id == (int16_t)data_id;
}

int ot_registry_first_for_id(uint8_t data_id)
{
    for (uint16_t i = 0; i < OT_ENTITY_COUNT; i++)
        if (reads_id(&OT_ENTITIES[i], data_id))
            return (int)i;
    return -1;
}

int ot_registry_next_for_id(uint8_t data_id, int after)
{
    if (after < 0)
        return -1;
    for (uint16_t i = (uint16_t)(after + 1); i < OT_ENTITY_COUNT; i++)
        if (reads_id(&OT_ENTITIES[i], data_id))
            return (int)i;
    return -1;
}

bool ot_registry_is_boolean(const ot_entity_t *e)
{
    return e && (e->kind == OT_KIND_BINARY || e->kind == OT_KIND_SWITCH);
}

uint8_t ot_registry_option_count(const ot_entity_t *e)
{
    if (!e || e->kind != OT_KIND_ENUM || !e->options)
        return 0;
    uint8_t n = 1;
    for (const char *p = e->options; *p; p++)
        if (*p == OT_OPTION_SEP)
            n++;
    return n;
}

// Walked on every render rather than split once into a table: an enum has a handful of short
// options, and a split table would be a second representation of the generated string.
const char *ot_registry_option(const ot_entity_t *e, unsigned index, size_t *len)
{
    if (!e || e->kind != OT_KIND_ENUM || !e->options)
        return NULL;
    const char *p = e->options;
    for (unsigned k = 0; k < index; k++) {
        p = strchr(p, OT_OPTION_SEP);
        if (!p)
            return NULL;
        p++;
    }
    const char *end = strchr(p, OT_OPTION_SEP);
    if (len)
        *len = end ? (size_t)(end - p) : strlen(p);
    return p;
}

uint16_t ot_registry_poll_count(void)
{
    return (uint16_t)OT_POLL_ID_COUNT;
}

uint8_t ot_registry_poll_at(uint16_t index)
{
    return index < OT_POLL_ID_COUNT ? OT_POLL_IDS[index] : 0;
}
