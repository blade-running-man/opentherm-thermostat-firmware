# ot_lock — the one recursive lock guarding the state model

## Responsibility

**Owns:** the single process-wide mutex that serializes access to the state model, so a
reader (an HTTP handler rendering `/api/state`) and the bus task (applying an incoming
frame) cannot race.

**Does NOT do:** it holds no state of its own, knows nothing about what it protects, and
takes no arguments — it is a bare lock/unlock pair. It does not lock per-object; there is
exactly one lock for the whole state model. It is not an init'd resource: there is no
`ot_lock_init()` and no teardown.

## Public API

`include/ot_lock.h`, C linkage (`extern "C"`):

| Function | Contract |
| --- | --- |
| `void ot_lock(void)` | Acquire the lock, blocking (`portMAX_DELAY`) until held. Recursive: safe to call again while already held by the same task. Safe to call before any initialization — the lock is created lazily on first acquisition. |
| `void ot_unlock(void)` | Release one level of the lock. Must be paired with a prior `ot_lock()` (recursive give). |

## Implementation

Two source files: `ot_lock.c` and its header. Registered as a plain ESP-IDF component
(`CMakeLists.txt`: `SRCS "ot_lock.c"`, `INCLUDE_DIRS "include"`), which makes it a
PlatformIO library at the same time, so the host builds compile the identical source.

Two build variants, selected by `#ifdef ESP_PLATFORM`:

- **On device (`ESP_PLATFORM`):** a file-static `SemaphoreHandle_t s_mutex`, a FreeRTOS
  **recursive** mutex (`xSemaphoreCreateRecursiveMutex`). `ot_lock` calls `ensure()` then
  `xSemaphoreTakeRecursive(..., portMAX_DELAY)`; `ot_unlock` calls `xSemaphoreGiveRecursive`.
  Both guard on `s_mutex != NULL`.
- **On host (everything else):** both functions are empty no-ops.

**Recursive is load-bearing.** The `/api/state` renderer takes the lock for the whole
document to get a consistent snapshot, and inside that it calls `ot_state_get()`, which
takes the lock again. A plain (non-recursive) mutex would deadlock on the second
acquisition.

**Lazy creation.** `ensure()` creates the mutex on first use rather than in an init
function, so no caller can forget to initialise it and no inter-component ordering
constraint appears. The check-then-create in `ensure()` is not a real race: the first take
happens from `app_main` long before any task competes for it. Making it a race would
require two tasks calling in before `app_main` finishes, which cannot happen.

**DO NOT** add a threading dependency to the host build to "properly" implement the
no-op. Host tests are single-threaded; there is nothing to serialize, and pulling in a
threading library to prove that would be worse than the no-op.

## Tests

No dedicated host suite. On the host the lock compiles to no-ops; its purpose is to keep the
state-model sources identical between device and host so the state-model host suites exercise
the same code the device runs.

## Notes

- The component exists because the state model gained a second accessor: originally only
  the bus task touched state, so the absence of a lock was safe by accident, not by
  design. An HTTP handler reading `/api/state` at the moment a frame arrives is a genuine
  race, and this lock closes it.
- Both properties are deliberate: RECURSIVE (for the nested renderer/`ot_state_get()`
  acquisition) and ON THE HOST — A NO-OP (to preserve the "same code the device executes"
  test guarantee).
- Naming: this is the lock over the registry-backed `ot_state` model, distinct from other
  per-component locks in the codebase (e.g. the bus's `s_mux`).
