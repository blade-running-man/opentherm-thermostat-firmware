# ot_log — in-memory log ring exposed via `GET /api/log`

A fixed-size RAM ring buffer of the most recent device log lines, rendered on demand as a
JSON array of strings for the web UI. Optionally intercepts ESP-IDF's own logging so console
output is captured here too.

## Responsibility

**Owns:**

- A fixed ring of the last `OT_LOG_LINES` (80) log lines, each at most `OT_LOG_LINE_LEN` (160)
  bytes including the terminator.
- Turning that buffer into one well-formed JSON array of strings, oldest line first
  (`ot_log_render`).
- Its own JSON string escaping, kept local rather than shared with `ot_api` so the log stays
  independent of the entity model and keeps working "when nothing else does".
- On device, routing ESP-IDF's `esp_log_*` output through the ring as well as the console
  (`ot_log_install`).

**Does NOT do:**

- Allocate or block. `ot_log_write` never mallocs and never blocks; it is safe from any task,
  including at boot.
- Serve HTTP. It only renders into a caller-supplied buffer; the HTTP layer owns `GET /api/log`.
- Persist. The ring is RAM only; a reset or power cycle clears it.
- Provide any locking. There is no internal mutex (see Invariants).

**`ot_log_render()` escapes every non-ASCII byte as `\u00xx`** (any byte `< 0x20` or `> 0x7e`
is emitted as `\uXXXX`). That is *why log strings on this device are English*: a non-ASCII log
line comes back through `GET /api/log` as unreadable, ungreppable escape sequences.

## Public API

Declared in `include/ot_log.h` (C linkage; header is C++-safe via `extern "C"`).

| Symbol | Contract |
| --- | --- |
| `#define OT_LOG_LINES 80` | Number of ring slots. Raise only against a measured RAM figure. |
| `#define OT_LOG_LINE_LEN 160` | Per-slot byte capacity including the NUL terminator. |
| `void ot_log_reset(void)` | Empties the ring (resets write cursor and used count). |
| `void ot_log_write(const char *line)` | Records one line. NULL or empty input is ignored. Longer input is truncated to a slot; trailing `\n`/`\r` are stripped. Never allocates, never blocks. |
| `size_t ot_log_render(char *out, size_t cap)` | Writes a JSON array of the lines (oldest first) into `out`, NUL-terminated, and returns the length written. Returns `0` if it does not fit — an all-or-nothing contract, so a caller never serves half a document with `200 OK`. |
| `void ot_log_install(void)` | Device only: installs a `vprintf` hook so ESP-IDF logging also lands in the ring. No-op on the host build. |

## Implementation

Key files:

- `include/ot_log.h` — the contract and the two size constants.
- `ot_log.c` — the ring, the escaper, the renderer, and the (device-only) log hook.
- `CMakeLists.txt` — registers the component; `REQUIRES log` (ESP-IDF `esp_log`).

**Ring buffer.** Backed by a static 2-D array `s_lines[OT_LOG_LINES][OT_LOG_LINE_LEN]` plus
`s_next` (next write slot) and `s_count` (slots in use, capped at `OT_LOG_LINES`). Writing
advances `s_next` modulo the ring size; once full, the **oldest** line is overwritten — during
an incident the most recent lines are the interesting ones, and a log that stops recording when
full is worse than none. `ot_log_write` copies with `strncpy` into the slot, force-terminates
at `OT_LOG_LINE_LEN - 1`, then trims trailing `\n`/`\r` (framing, not content, which would
otherwise be escaped into every rendered line).

**Rendering.** `ot_log_render` emits `[`, then each in-use line as a quoted, escaped string
separated by commas, then `]` and a NUL. It walks lines oldest-first starting at
`first = (s_count == OT_LOG_LINES) ? s_next : 0`. `append_escaped` handles `"`, `\`, `\n`,
`\r`, `\t` explicitly and `\u00xx`-escapes anything outside printable ASCII (`< 0x20` or
`> 0x7e`). Every write checks remaining capacity first; on any overflow the function returns
`0` (the all-or-nothing contract). Guards: `out == NULL` or `cap < 3` returns `0` immediately.

**Device log hook.** Under `#ifdef ESP_PLATFORM`, `ot_log_install` calls
`esp_log_set_vprintf(log_hook)` and saves the previous handler in `s_previous`. `log_hook`
formats into a stack `line[OT_LOG_LINE_LEN]` using a `va_copy` of the argument list, writes it
to the ring, then forwards the **original** list to `s_previous` so the console keeps working
(USB is the only channel when the network is what is broken; the ring is an addition, never a
replacement). Off-device (host build), `ot_log_install` is an empty stub.

**DO NOT:**

- Reuse a `va_list` after it has been walked — `log_hook` deliberately `va_copy`s before
  `vsnprintf` and forwards the untouched original.
- Assume `ot_log_render` succeeds — check the return value; `0` means it did not fit and
  nothing usable was produced.
- Add non-ASCII text to log strings — it renders as `\u00xx` and defeats reading/grepping the
  log.

## Tests

No dedicated host suite in this component directory. The renderer's JSON output feeds the
`GET /api/log` surface; behavior is exercised through that path rather than a `test_ot_log`
suite here.

## Notes

- **No internal synchronization.** The ring uses plain static state with no mutex. `s_next`,
  `s_count`, and slot contents can be touched concurrently by any logging task and by a reader
  calling `ot_log_render`; the component relies on being driven from a context where that is
  acceptable rather than guarding itself.
- **Sizes are deliberate round numbers, not measurements.** 80 × 160 is roughly 13 KB, about
  the last minute of a busy boot. The header instructs raising them only against a measured RAM
  figure, not a feeling.
- **Escaping is intentionally duplicated** rather than shared with `ot_api`: independence from
  the entity model is the point — the log must render even when the rest of the firmware is in
  trouble.
- **Empty/NULL lines are silently dropped** by `ot_log_write`, so they never occupy a slot.
