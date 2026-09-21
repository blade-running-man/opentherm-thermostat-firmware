// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The one lock guarding the state model.
//
// It exists because the state model has a reader besides the bus task. Without the lock, only
// the bus task touched state, and the absence of a lock was safe by accident, not by design: an
// HTTP handler reading /api/state at the moment a frame arrives is a genuine race.
//
// Two properties are load-bearing:
//
//  * RECURSIVE. The renderer takes the lock for the whole document so the snapshot is
//    consistent, and inside it calls ot_state_get(), which takes it again. An ordinary
//    mutex would deadlock on the second acquisition.
//  * ON THE HOST -- A NO-OP. The point of host tests of the state model is that this is
//    the same code the device executes (CLAUDE.md, "Tests"), and a dependency on
//    FreeRTOS would put an end to that. Host tests are single-threaded, there is
//    nothing to serialize there.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Safe to call before any initialization: the lock is created on first acquisition, and
// on the host both calls do nothing at all.
void ot_lock(void);
void ot_unlock(void);

#ifdef __cplusplus
}
#endif
