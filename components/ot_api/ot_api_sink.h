// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>

// The snprintf accumulator every renderer of this component writes through. PRIVATE: it sits
// beside the sources, not in include/, because it is how ot_api writes and not something any other
// component has business calling -- the same arrangement as ot_http_internal.h.
//
// It writes into the buffer with snprintf semantics and accumulates the REQUIRED size even once
// the room has run out. Without that the caller would learn about a shortfall only from the
// truncated document, that is, never.

typedef struct {
    char  *out;
    size_t cap;
    size_t need;
} ot_api_sink_t;

void ot_api_emit(ot_api_sink_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Leaves the buffer a string in any outcome, including when there was not enough room: a
// truncated document must at least not be read past its own end.
void ot_api_terminate(const ot_api_sink_t *s);
