# ot_provision — pure Wi-Fi provisioning state machine

Decides what the device should be doing given what has happened to it: which radio mode to run,
whether the setup window is open, and whether credentials just handed in must be rolled back. It is
pure logic — no ESP-IDF — and **time comes in as a parameter on every call**, which is the only way
the multi-minute cases this exists for can be written as one-line tests instead of fifteen-minute
waits in front of a real device.

## Responsibility

Owns:

- The provisioning **state** (`ot_prov_state_t`) and the **radio mode** (`ot_prov_mode_t`) the
  caller should drive from it.
- The **setup window** lifecycle: length selection (a long first window vs. a short reopen window),
  the activity ceiling, and closing.
- The **trial / rollback** logic: a newly saved credential pair is on probation until it produces
  an address; if it does not, the known-good pair is restored (`OT_PROV_TRIAL`). "Credentials are
  stored" and "the device is on the owner's network" are deliberately kept as separate facts, so a
  pair with a mistyped password can never masquerade as a working connection.
- **Failure classification**: the raw Wi-Fi disconnect reason byte mapped to one of four
  owner-facing answers (`ot_prov_failure_t`), and the backoff/retry schedule.
- The **self-healing fallback access point** and the **no-address escape** that survives a reboot.
- The one-shot **actions** (`ot_prov_action_t`) the caller must carry out (connect / commit / restore).

Does NOT do:

- Touch the radio, NVS, or any ESP-IDF API. The caller owns esp_wifi and storage; this component
  only knows *whether* something is stored, never *what* (no SSID/PSK ever enters `ot_prov_t` —
  enforced by a `static_assert`).
- Read a clock. Every deadline is evaluated against a `now_ms` the caller passes in. **DO NOT** call
  `esp_timer_get_time()` in here.
- Anchor anything on boot. Every clock starts on the event it is about (the AP appearing, a save, an
  association), not on init.

## Public API

Header: `include/ot_provision.h`. All functions take `ot_prov_t *` (opaque by convention; the caller
allocates it). NULL-safe throughout.

### Setup

| Function | Contract |
| --- | --- |
| `ot_prov_config_defaults(cfg)` | Fills `cfg` with the default durations (see Implementation). |
| `ot_prov_init(s, cfg, boot)` | Initialise machine. `cfg` NULL → defaults; `boot` NULL → nothing stored. Takes no timestamp on purpose. |

### Events (drive from radio/UI callbacks)

| Function | Meaning |
| --- | --- |
| `ot_prov_tick(s, now_ms)` | The clock. All deadlines are decided here; a caller that stops ticking stops the machine (deliberate). |
| `ot_prov_on_ap_started(s, now_ms)` | The AP is **on the air**. The setup-window clock is anchored here, and "unclaimed" is keyed here. Repeat events ignored. |
| `ot_prov_on_ap_stopped(s, now_ms)` | AP left the air (not the same as the window running out). |
| `ot_prov_on_associated(s, now_ms)` | Joined the AP, no address yet. **Not** a success. |
| `ot_prov_on_connected(s, now_ms)` | Got an address — the only proof of a credential pair. |
| `ot_prov_on_disconnected(s, now_ms, reason)` | Disconnect; `reason` is the raw `wifi_err_reason_t` (uint8). |
| `ot_prov_on_credentials_saved(s, now_ms)` | A pair was written to NVS. Starts the trial and rollback timer; **not** "connected". |
| `ot_prov_on_client_seen(s, now_ms)` | Someone is talking to us on the AP; extends the window. |

### Questions (projection / status)

| Function | Answer |
| --- | --- |
| `ot_prov_state(s)` | Current `ot_prov_state_t`. NULL → `WINDOW_CLOSED`. **DO NOT** project before init (see header). |
| `ot_prov_mode(s)` | The `ot_prov_mode_t` the radio should run (OFF / AP / STA / AP_STA). |
| `ot_prov_is_provisioned(s)` | The most important rule: false whenever an open AP is on the air, whatever NVS holds. NOT YET WIRED (see Notes). |
| `ot_prov_has_credentials(s)` / `ot_prov_has_known_good(s)` | Storage facts. |
| `ot_prov_window_is_open(s)` | Reports the last tick's decision (not re-decided). Not for driving the radio — use `mode()`. |
| `ot_prov_window_remaining_ms(s, now_ms)` | ms left, or `OT_PROV_NEVER` for a never-closing window. |
| `ot_prov_window_has_closed(s)` | A window has run out at some point (caller persists this across reboot). |
| `ot_prov_address_never_arrived(s)` | Station joined but was never given an address, for a sustained period (caller persists). |
| `ot_prov_should_roll_back(s)` | Whether the NVS pair must be undone; latched with the RESTORE action. |
| `ot_prov_failure(s)` / `ot_prov_failure_reason(s)` | The standing failure verdict and its raw reason byte. |
| `ot_prov_failure_name(failure)` | One canonical string per verdict (single spelling — see Notes). |
| `ot_prov_fallback_in_ms(s, now_ms)` | ms until the AP self-returns, or `OT_PROV_NEVER` (always NEVER for a no-address failure). |
| `ot_prov_take_action(s)` | Returns and clears the pending one-shot action. Call after every event and tick. |
| `ot_prov_classify(reason)` | The number→verdict table, exposed so the mapping can be pinned by reason code. |

### Types

- `ot_prov_state_t` — `UNCONFIGURED, CONNECTING, CONNECTED, TRIAL, RETRYING, FALLBACK_AP, WINDOW_CLOSED`.
- `ot_prov_mode_t` — `OFF, ACCESS_POINT, STATION, AP_STA`.
- `ot_prov_failure_t` — `NONE, NETWORK_ABSENT, WRONG_PASSWORD, REFUSED, NO_ADDRESS` (the "four answers to the owner").
- `ot_prov_action_t` — `NONE, CONNECT, COMMIT_CREDENTIALS, RESTORE_CREDENTIALS` (one-shot, only one held).
- `ot_prov_config_t` — eight `uint32_t` durations in ms; each field documents whether `0` is a legal setting or a hazard (nothing clamps).
- `ot_prov_boot_t` — four bools read out of NVS before startup: `has_credentials`, `has_known_good`, `window_closed_before`, `address_never_arrived`.
- `ot_prov_t` — the machine; `static_assert(sizeof <= 96)` is the tripwire that keeps an SSID/PSK out of it.
- `OT_PROV_NEVER` (`UINT32_MAX`) — "this has no end" from the duration queries (distinct from `0` = "nothing running").

## Implementation

Files:

- `include/ot_provision.h` — the full contract; the header comment records every rule and the
  invariant that outranks them all.
- `ot_provision.c` — the whole implementation (~730 lines).
- `CMakeLists.txt` — registers the component with sources `ot_provision.c` and `include/`; kept a
  separate component precisely because it needs no radio, so its whole logic is host-testable.

Default durations (`ot_prov_config_defaults`):

| Field | Default | Meaning |
| --- | --- | --- |
| `setup_window_ms` | 15 min | The long first window. `0` = a never-closing window (a supported configuration, not an escape hatch). |
| `setup_window_ceiling_ms` | 60 min | The bound on how far client activity can extend a window, measured from the AP coming up. `0` = no ceiling. |
| `reopen_window_ms` | 5 min | The short window every time the AP comes back (after one closed, after a reboot, or for a never-addressed station). `0` = those windows never close. |
| `fallback_after_ms` | 30 min | Sustained failure before the device raises its own AP again. `0` = the AP never self-returns, and also disables the no-address escape that shares this clock — i.e. a device with no way back at all. |
| `trial_timeout_ms` | 90 s | How long a newly saved pair gets to produce an address before the known-good pair is restored. **DO NOT** set to `0`, or every saved pair is rolled back on the next tick. |
| `address_timeout_ms` | 30 s | How long an associated station gets an address before that counts as the fourth kind of failure. **DO NOT** set to `0`, or a station is declared address-less the instant it associates. |
| `retry_backoff_min_ms` | 1 s | Retry backoff lower bound. **DO NOT** set to `0`, or the retry becomes a station that reconnects on every tick. |
| `retry_backoff_max_ms` | 30 s | Retry backoff upper bound. |

Time arithmetic — the core invariant:

- `elapsed(now, since, limit)` / `remaining(now, since, limit)` use **unsigned subtraction**
  ("has `limit` passed since `since`"), never an absolute deadline comparison. A 32-bit ms counter
  wraps every 49.7 days; the subtraction is correct across exactly one wrap. **DO NOT** rewrite these
  as `now >= deadline` — `test_the_clock_wrapping_does_not_close_the_window_early` guards it.

Window length: the first window of a device's life is the long `setup_window_ms`; every window that
comes back is the short `reopen_window_ms`. Which one an AP gets is **latched when it comes up**
(`on_ap_started` sets `window_is_the_short_one`), never asked live — `has_credentials` goes true the
instant the form is submitted, so a live test would cut the owner's own window. A never-closing
`setup_window_ms == 0` is checked first and outranks everything, including the ceiling.

Backoff (`backoff_ms`/`arm_retry`): doubles from min to max with a saturating attempt count (capped
at 32) and an overflow guard (`delay > UINT32_MAX/2`) so a near-`UINT32_MAX` ceiling can never wrap
the delay back to a tiny value. There is **no attempt limit** — the alternative to retrying is
rebooting, and nothing here reboots because a peer is absent.

The `tick()` ladder (six ordered steps):

1. Setup window ran out → `WINDOW_CLOSED` (no credentials) or `RETRYING` + restart the self-return clock (from `FALLBACK_AP`).
2. Trial timed out → RESTORE known-good pair (`CONNECTING`) or, on a first provisioning, `RETRYING`.
3. Associated but no address after `address_timeout_ms` → note `NO_ADDRESS`, re-nudge CONNECT.
4. Backoff expired → CONNECT, `RETRYING`→`CONNECTING`.
5. Sustained failure *of the right kind* + no AP → raise `FALLBACK_AP`.
6. The no-address escape: after sustained `NO_ADDRESS`, set `address_never_arrived` for the caller to persist (which reopens the window at the next boot, **not** the self-return AP).

Key invariants / DO NOTs:

- **There is never a state with no network, no AP and no way back.** Every failure has an exit: a
  closed window has the next boot, a typo has the rollback, an unlisted disconnect has the
  self-return AP, and a never-addressed station has `address_never_arrived` → the reopen window at
  the next boot.
- Actions: RESTORE/COMMIT are durable NVS writes and are never displaced by a CONNECT nudge
  (`set_action`); only a fresh credential save cancels a pending write. Drain with `take_action()`
  after every event/tick or a dropped COMMIT silently disarms the rollback.
- `note_failure` re-anchors the "sustained" clock only when crossing the earns-the-AP / no-address
  line, never on repeated same-kind failures.
- `on_connected` commits the known-good pair **once per pair**, not per reconnection, and clears
  `address_never_arrived`.
- `init` deliberately sets `credentials_committed = false` (never `= has_known_good`): a power cut
  mid-trial leaves an untried pair in NVS and nothing records which pair is good; assume the worst,
  costing one redundant write on the first success.
- `on_disconnected` returns early if `!has_credentials` (a stored-network-less disconnect carries no
  verdict and must not start the self-return clock).

## Tests

No test suite lives inside this directory. The component is exercised by the host suite the source
and header repeatedly cite as **`test/test_provision`** (named cases include
`test_the_clock_wrapping_does_not_close_the_window_early`,
`test_credentials_saved_mid_window_do_not_shorten_the_window_around_them`,
`test_an_address_ends_the_run_of_failures_that_preceded_it`, and
`test_the_machine_holds_no_credentials_only_the_fact_of_them`). Because time is a parameter, every
multi-minute scenario is a single synchronous test rather than a real-clock wait — which is the whole
reason this logic is a radio-free component.

## Notes

- **The rules this machine enforces**, all reflected in the default durations above: an open access
  point means the device is unclaimed (whatever NVS holds); the setup window is 15 minutes,
  activity-extended, with a 60-minute ceiling, anchored on the AP coming up, and reopened for 5
  minutes at each boot; the AP self-returns after 30 minutes of the *right kind* of failure (not on
  a no-address failure); and a never-closing window is a supported configuration. Every access point
  this firmware raises is open.
- **`ot_prov_is_provisioned()` is NOT YET WIRED.** The intent is that `ot_http_check()`'s
  `ctx.provisioned` be fed from it, so an open provisioning AP forces the device to count as
  unclaimed. At present `ot_net_has_credentials()` feeds it instead — which flips to "provisioned"
  the moment credentials are stored, leaving the gap where a passer-by sets the first UI password on
  an open AP and takes the device. Nothing in the tree calls any `ot_prov_*` function yet.
- **Reason 8 (`WIFI_REASON_ASSOC_LEAVE`) is special-cased to `FAIL_NONE`:** `esp_wifi_disconnect()`
  raises it, and the CONNECT action's contract makes the caller disconnect an associated station
  before reconnecting — so this is the machine's own nudge coming back. Classifying it would answer
  "router refusal" for a dead DHCP server and start the self-return clock for the one failure that
  clock excludes. **DO NOT** add other locally-flavoured codes (3 AUTH_LEAVE, 47 AP_INITIATED) on
  the same reasoning — they can equally be a router kicking us for good, and a code that stops
  counting toward the self-return clock is a device that never raises its AP again.
- **`classify()`'s default matters more than its list:** ~60 reason codes exist and only a dozen are
  named; any unlisted one must fall into a class that counts toward the self-return clock
  (`FAIL_REFUSED`), or a device failing for an unenumerated reason would never raise its AP again.
- **`WRONG_PASSWORD` has no dedicated reason code:** a wrong PSK fails the four-way-handshake MIC
  check and the AP simply stops replying, so the station reports a timeout (14/15/202/204).
- Reason byte facts are pinned to ESP-IDF 5.5.5
  `components/esp_wifi/include/esp_wifi_types_generic.h` (reason enum :113-178; `reason` is a uint8 at
  :1167 — hence nothing above 255 is handled).
- `ot_prov_failure_name()` gives one canonical spelling per verdict so the REST and MQTT projections
  cannot invent two.
