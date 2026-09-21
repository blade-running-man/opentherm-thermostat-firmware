// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>

// Projection of the registry and the state into JSON. The only form in which they
// leave the device: REST, MQTT and the web UI are clients of ONE API.
//
// **The web UI has no privileged handle.** If the screen needs a
// path of its own, then the API is wrong.
//
// The functions here follow snprintf semantics: they return the REQUIRED size
// without the terminating zero, they never overflow the buffer and they always
// leave it a string. A caller who ran out of room is obliged to answer 500 rather
// than hand out a truncated document: truncated JSON looks valid right up to the
// attempt to parse it.
//
// Ownership: the buffer belongs to the caller. The snapshot is taken under
// ot_lock() as a whole, so the document is consistent.
//
// STRINGS ARE PRINTED WITHOUT ESCAPING, and that is legitimate precisely because
// all of them come from the generated registry, not from a client. The ban on the
// quote, the backslash and control characters in these strings is held not by the
// renderer but by tools/tests/test_registry.py
// (test_no_registry_string_needs_json_escaping) -- that is, the check stands at the
// entrance to the table, where the string is still a single one.
// **DO NOT** print anything here that did not come from the registry.

#ifdef __cplusplus
extern "C" {
#endif

// Registry metadata: key, name, kind, units, range, writability.
// There are no values here -- they are in ot_api_render_state().
size_t ot_api_render_entities(char *out, size_t cap);

// Values of all entities: value, availability, age_ms.
size_t ot_api_render_state(char *out, size_t cap, uint32_t now_ms);

// A single entity: metadata and value together. Zero if the key is not in the
// registry -- the caller is obliged to answer 404, not an empty document.
size_t ot_api_render_entity(const char *key, char *out, size_t cap, uint32_t now_ms);

// A /ws frame: {"type":"<type>","values":{"<key>":<value>,...}} -- every entity when `mask` is
// NULL (the snapshot a new socket is sent), otherwise only those whose bit is set: registry index
// i is bit i % 32 of mask[i / 32] (a delta). `mask`, when not NULL, holds
// (OT_ENTITY_COUNT + 31) / 32 words; a shorter array is read past its end. The frame's shape is
// the client's (web/src/api/ws.ts); each VALUE is printed by the same function
// ot_api_render_state() prints with, so /ws and /api/state cannot disagree about a switch, an
// enum or an absent value.
// `type` is printed raw: pass a literal.
size_t ot_api_render_frame(const char *type, const uint32_t *mask, char *out, size_t cap);

// ONE entity's value, bare, exactly as /api/state and /ws print it -- `null`, `true`/`false`, an
// enum's option in quotes, a number with two decimals -- for MQTT, which turns it into a
// state payload (ot_mqtt_state_payload()). The same emitter the two documents use, so the three
// transports cannot come to disagree about a switch, an enum or an absent value. `index` is the
// registry position; zero, and nothing written, when it is outside the registry.
size_t ot_api_render_value(uint16_t index, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
