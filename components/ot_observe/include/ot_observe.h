// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_frame.h"

// What the bus has ALREADY heard from the boiler: the latest response for every Data-ID.
//
// This is neither the state model nor the entity registry. The state model is
// indexed by the registry and knows the meaning of values; here it is the raw
// identifier space and sixteen bits as they are. **This table never becomes a source of
// entities under any circumstances**: the single list is produced by the generator, and
// from here a human merely learns what is worth adding to it.
//
// The component is pure: time arrives as an argument, ESP-IDF is not needed, everything
// is verified on the host. It is kept in RAM and does not survive a reboot -- this is
// diagnostics, not configuration, and a saved copy would quickly diverge from the
// boiler.

#ifdef __cplusplus
extern "C" {
#endif

// A Data-ID is eight bits, but the spec defines 0..127; anything above is discarded.
#define OT_OBSERVE_IDS 128

typedef struct {
    bool     seen;
    uint8_t  type;      // ot_msg_type_t of the latest response
    uint16_t raw;
    uint32_t last_ms;
    uint32_t count;
} ot_observe_entry_t;

typedef struct {
    ot_observe_entry_t e[OT_OBSERVE_IDS];
} ot_observe_t;

// The document header. Filled in by the caller from ot_bus_stats: the component does
// not depend on the bus, so that it stays host-buildable.
typedef struct {
    uint32_t uptime_ms;
    uint32_t cycles;
    uint32_t ok;
    uint32_t failed;
    uint32_t overdue;
    bool     answering;
    // Fraction of samples with the input active, percent. The meaning is in
    // ot_master_input_duty().
    uint8_t  in_duty;
    // Failure breakdown. A timeout means there was NOT A SINGLE edge on the input --
    // the receive path is dead or the boiler is silent. A frame error means there were
    // edges but they did not come together: polarity, timings, interference. Adding
    // them into one number would lose the only fork there is here.
    uint32_t timeout;
    uint32_t frame_error;
    uint32_t parity_error;
    uint32_t rx_edges;      // level transitions on the input over the whole time
    bool     pins_shorted;  // input follows output -- the wires are shorted together
    uint16_t scan_done;
    uint16_t scan_total;
} ot_observe_header_t;

void ot_observe_reset(ot_observe_t *o);

// Records a response. data_id >= OT_OBSERVE_IDS is discarded silently: that is not the
// caller's error but a frame from a faulty slave, and crashing over it is not allowed.
void ot_observe_record(ot_observe_t *o, uint8_t data_id, ot_msg_type_t type,
                       uint16_t raw, uint32_t now_ms);

// NULL if data_id is outside the table. A pointer into internal storage: do not free.
const ot_observe_entry_t *ot_observe_get(const ot_observe_t *o, uint8_t data_id);

uint32_t ot_observe_seen_count(const ot_observe_t *o);

// Renders the document into out. snprintf semantics: returns the REQUIRED size without
// the terminating NUL, never overflows the buffer and always leaves it a string.
// A caller that ran out of room can allocate exactly as much as is needed, instead of a
// buffer "with margin" -- that is, instead of a guess.
size_t ot_observe_render_json(const ot_observe_t *o, const ot_observe_header_t *h,
                              uint32_t now_ms, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
