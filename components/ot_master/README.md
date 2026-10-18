# ot_master — the OpenTherm master: one conversation on the wire, with correct timing

The lowest transmit/receive layer of the OpenTherm bus. It sends one Manchester-encoded
frame, listens for the slave's response inside the protocol's timing window, and reports
the outcome. It knows about bits and edges; it does not know *what* to ask or *when* —
that is `ot_bus`'s business. The split keeps the polling schedule a pure, host-tested
function and leaves here only the glue that a real boiler must verify anyway.

## Responsibility

Owns:
- The two OpenTherm GPIOs (`ot_in`, `ot_out`) and their logical↔physical inversion. The
  OpenTherm line is idle-LOW and an adapter may invert either direction; the polarity flip
  that reconciles the physical level with the logical one lives here and only here. Above
  this layer, `true` means "line active".
- The single hardware timer (`gptimer`) that clocks both transmission and reception.
- The transmit/receive state machine that drives one frame out and pulls one frame back in.
- Per-conversation and lifetime statistics (`ot_master_stats_t`).
- Two hardware-diagnostic probes: input duty and a manual line-drive.

Does NOT do:
- Decide which Data-IDs to poll, in what order, or how often (that is `ot_bus` / `ot_bus_sched`).
- Provide any locking, reentrancy guard, or multi-master arbitration. Safety comes from
  the fact that a *single* caller (the `ot_bus` task) ever calls `ot_master_exchange()`.
- Interpret frame contents beyond parity/decode (that is `ot_frame` / `ot_registry` / `ot_state`).

## Public API

All declared in `include/ot_master.h`.

| Symbol | Contract |
| --- | --- |
| `esp_err_t ot_master_init(const board_t *board)` | Configure pins and the timer, undo the line inversion, run the one-off pins-shorted check. Call once, before `ot_bus_start()`. Returns `ESP_OK` or a driver error. |
| `ot_exchange_result_t ot_master_exchange(const ot_frame_t *request, ot_frame_t *response)` | One full conversation. **Blocks the caller up to ~1.2 s** (wait ceiling 1.5 s). Call **only** from the `ot_bus` task — no lock protects it. `response` is written only on `OT_EXCHANGE_OK`; left untouched otherwise. |
| `uint8_t ot_master_input_duty(void)` | Fraction (%) of samples where the input was active over ~2 ms. Diagnostic. Idle line reads ~0; steady 100 means input polarity is inverted (flip `ot_in_inverted`). Safe from any task, but only meaningful while idle. |
| `void ot_master_drive_line(bool active)` | Hold the line in one logical state for multimeter checks. Transmits no frame. `ot_bus` task only, never while a conversation is live. **DO NOT call from HTTP handlers.** |
| `void ot_master_stats(ot_master_stats_t *out)` | Copy out the statistics snapshot. |
| `const char *ot_exchange_result_str(ot_exchange_result_t r)` | Short label ("ok"/"timeout"/"frame"/"parity") for a result. |

Types:
- `ot_exchange_result_t` — `OT_EXCHANGE_OK`, `OT_EXCHANGE_TIMEOUT` (slave silent past the
  reception budget), `OT_EXCHANGE_FRAME_ERROR` (Manchester/timing/stop bit),
  `OT_EXCHANGE_PARITY_ERROR` (assembled but parity failed).
- `ot_master_stats_t` — `rx_edges` (input transitions counted across every reception window;
  zero over hundreds of conversations means a dead wire), `pins_shorted` (init-time check:
  input follows output ⇒ shorted/swapped pins), and the counters `sent`, `ok`, `timeout`,
  `frame_error`, `parity_error`.

## Implementation

Single source file `ot_master.c` (+ header + `CMakeLists.txt`). Requires `board`,
`ot_frame`, `ot_decode`, `ot_encode`, `esp_driver_gptimer`, `esp_driver_gpio`.

**One timer, one period, deliberately.** `TICK_US` = 100 µs (`OT_DECODE_SAMPLE_US`); a
transmit half-bit is 500 µs (`OT_ENCODE_HALFBIT_US`), i.e. `TICKS_PER_HALF` = 5 ticks. The
timer is configured once and the ISR never re-touches the driver config: on transmit it just
changes the level every fifth tick. Choosing a single period over switching it inside the
handler removes one race.

**State machine** (`phase_t`: `PH_IDLE` → `PH_TX` → `PH_RX` → `PH_IDLE`), driven entirely by
the `on_tick` timer callback:
- `ot_master_exchange` encodes the request to `s_tx[OT_ENCODE_HALFBITS]`, resets the indices,
  captures the calling task handle, drains any stale notification, sets `PH_TX`, and starts
  the timer.
- `PH_TX`: emit one half-bit every 5 ticks. When all half-bits are out, drive the line idle,
  reset the decoder, and switch to `PH_RX` immediately — the slave is allowed to answer as
  early as 20 ms later.
- `PH_RX`: every 100 µs sample the line, count edges into `rx_edges`, and feed `ot_decode_push`.
  On `OT_DECODE_DONE`/`OT_DECODE_ERROR`, or when `RX_BUDGET_TICKS` (9000 = 900 ms) elapses
  (sets `s_rx_expired`), go `PH_IDLE` and `vTaskNotifyGiveFromISR` the waiter.
- The task wakes via `ulTaskNotifyTake` (1500 ms ceiling — larger than the RX budget because
  it only fires if the handler never ran, e.g. the timer failed to start), stops the timer,
  forces the line idle, and maps the outcome: no notification ⇒ timeout; `s_rx_expired` ⇒
  timeout; decoder not `DONE` ⇒ frame error; `ot_frame_decode` fails ⇒ parity error; else OK.

**Reception budget rationale:** the OpenTherm slave is allowed up to 800 ms to begin its
response, and the response frame itself takes about 34 ms; 900 ms covers both with margin and
stays under the ~1150 ms deadline past which the master itself would count as silent (a boiler
that hears nothing from the master for that long treats the thermostat as shorted and demands
heat).

**Init-time integrity checks:** the input pin is pulled DOWN (the idle line is LOW, so a
disconnected adapter reads as idle rather than as frame-start noise); a pins-shorted probe
drives the line active then idle and checks whether the input follows (a healthy adapter has
no loop between transmit and receive because with no boiler no current flows) — logs an error
and sets `pins_shorted` if it does.

**Invariants / DO NOT:**
- Exactly one caller of `ot_master_exchange`; there is intentionally no lock. Two overlapping
  conversations lose both frames and surface as rare, unexplained comms errors.
- `on_tick` is `IRAM_ATTR` so it runs while the flash cache is disabled (e.g. an NVS write
  mid-conversation would otherwise eat a frame). Note in-comment: full IRAM coverage also needs
  `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM`; the remainder is an open question for over-the-air
  updates.
- Inversion is applied only in `drive()`/`sense()`; everything above the pin is logical.

## Tests

No dedicated host suite lives inside this component. The `ot_master` host suite and its
fake ESP-IDF live at `test/test_ot_master/idf/`. That fake must **NOT** be moved into a
shared directory: once two suites share it, "it grew a capability for suite B" becomes a way
to quietly weaken suite A.

## Notes

- `rx_edges` is the honest "is anything on the wire?" signal — it watches the whole response
  window every conversation, unlike `ot_master_input_duty` which samples ~2 ms at an arbitrary
  moment.
- Both `ot_master_input_duty` and `ot_master_drive_line` exist purely to make an otherwise
  invisible hardware fault (34 ms of activity once a second is unmeasurable with a probe)
  observable at idle; they must never wedge into a live conversation.
</content>
</invoke>
