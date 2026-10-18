# ot_mqtt — what an MQTT message means

## Purpose

The pure, broker-free half of the firmware's MQTT surface: the topic tree, the decoder that says
what an arriving message means, the command entry point (`ot_mqtt_handle()`), and the outbound state
payloads. It contains no networking code — the esp-mqtt client, its owner task and its receive
buffers live in the separate `ot_mqtt_link` component, which calls the functions here from its one
publisher task.

Everything in this component is a pure function: no FreeRTOS, no esp-mqtt, no `esp_timer`, no
`esp_log`, no ESP-IDF header of any kind. That is what lets the host test suites include `ot_mqtt.h`
and exercise the real decision logic on a workstation with no device attached.

## Responsibility

Owns:
- The topic-tree grammar: build an outbound topic (`ot_mqtt_topic`), and parse an inbound
  topic+payload into a meaning (`ot_mqtt_decide`).
- The single command hand-off to the executor (`ot_mqtt_handle`), carried out exactly the way the
  web POST route carries out a command, with the three impure effects injected.
- The spelling of outbound payloads: entity state (`ot_mqtt_state_payload`), the controller-owner
  topic (`ot_mqtt_owner_payload`), and the `control_state` attributes (`ot_mqtt_attributes`).
- The publisher's bookkeeping: the "owed states" bitset (`ot_mqtt_owed_*`) and a rate limiter for
  log lines (`ot_mqtt_quiet`).

Does **not** do:
- No esp-mqtt, FreeRTOS, `esp_timer`, `esp_log`, or any ESP-IDF header. Because a host suite
  includes `ot_mqtt.h`, and PlatformIO compiles every source of a library whose header a suite
  includes, pulling in a device header would break the host build.
- No validation of its own on a command. Which keys exist, writability, bounds, and every refusal
  are decided by `ot_command_check()`. A check duplicated on this surface would silently diverge
  from the one in the depths.
- No Home Assistant discovery documents. Those are produced elsewhere; the discovery-status topic
  this component needs is passed in as an argument.

## The topic tree

`<prefix>` is the device's configured topic prefix; `<key>` is a registry entity key.

| Topic | Payload | Retained | QoS | Notes |
| --- | --- | --- | --- | --- |
| `<prefix>/status` | `online` \| `offline` | yes | 1 | availability; the MQTT will is `offline` |
| `<prefix>/control/owner` | `online` \| `offline` | yes | 1 | `online` only while the controller mode is HA |
| `<prefix>/<key>/state` | a bare scalar | yes | 0 | one per registry entity |
| `<prefix>/control_state/attributes` | `{"reason","cause"}` | yes | 0 | the executor's state attributes |
| `<prefix>/<key>/set` | a number | — | 1 | subscribed as `<prefix>/+/set` |
| `<prefix>/room/state` | a bare °C float | not retained | 0 | a room-temperature reading |
| `homeassistant/status` | `online` | — | 1 | Home Assistant's birth message |

State is published at QoS 0 and retained, so a subscriber gets the current value the moment it
subscribes. Availability, the owner topic, discovery and the command subscription are QoS 1: a lost
`offline` would show a dead device as alive, and duplicate commands are harmless because every
command is idempotent.

## Public API (`include/ot_mqtt.h`)

### Topics

| Function | Contract |
| --- | --- |
| `size_t ot_mqtt_topic(prefix, key, leaf, out, cap)` | Build `<prefix>/<key>/<leaf>`, or `<prefix>/<leaf>` when `key` is NULL. Returns length, or 0 if it does not fit (and NUL-terminates `out`). |

### Decode — what an arriving message means

| Function | Contract |
| --- | --- |
| `ot_mqtt_in_t ot_mqtt_decide(prefix, ha_status, topic, topic_len, payload, payload_len, retained)` | Classify one message. `topic`/`payload` are **not** NUL-terminated (they are esp-mqtt receive-buffer pointers) and are read by length only. `ha_status` is Home Assistant's birth topic (NULL matches nothing). |

`ot_mqtt_kind_t` results:
- `OT_MQTT_IGNORE` — not ours, or a meaningless Home Assistant `offline`. Silent: a shared broker's
  other traffic must not be counted as refused.
- `OT_MQTT_REJECT` — ours, refused before the command layer: a retained message, or a non-number
  payload. `reason` is a fixed literal (never the payload — it flows to the log).
- `OT_MQTT_COMMAND` — a `<prefix>/<key>/set` write; `key` (a copy) and `value` are set.
- `OT_MQTT_HA_ONLINE` — `homeassistant/status = online`; the caller re-publishes discovery. It feeds
  nothing that heats: Home Assistant being alive is not proof its controller is producing outputs.
- `OT_MQTT_ROOM` — `<prefix>/room/state`, a room-temperature reading; `value` is set, no key. The
  caller routes it to the room-source mailbox, never through the command/executor path, so a
  measurement can be neither a command nor a signal that keeps the controller alive.

`ot_mqtt_in_t` fields: `kind`, `reason` (REJECT only), `key[OT_MQTT_KEY_MAX]` (COMMAND), `value`
(COMMAND and ROOM).

### Handle — what an accepted command makes happen

| Function | Contract |
| --- | --- |
| `ot_mqtt_verdict_t ot_mqtt_handle(in, fx)` | Carry out an `OT_MQTT_COMMAND` exactly as the web POST route does. Any other kind returns `{false, NULL}`. |

- `ot_mqtt_effects_t` injects the three impure effects (so the meeting with the executor is
  host-tested): `cfg` (fill in the configuration snapshot the executor last stepped with), `apply`
  (the executor's final answer for a Home-Assistant-origin command; returns 0 when accepted, else an
  error code), and `write` (emit a bus frame). On the device these are backed by the thermostat
  task's config accessor, its apply wrapper, and the bus writer.
- `ot_mqtt_verdict_t` = `{ bool accepted; const char *reason; }` (`reason` is NULL when accepted).

### Outbound payloads

| Function | Contract |
| --- | --- |
| `size_t ot_mqtt_state_payload(index, out, cap)` | The bare-scalar payload for registry entity `index`, in the same spelling the JSON state projection uses. A null value becomes `"None"` (never empty — an empty retained message would delete the stored value); a quoted option becomes its inner text. Returns 0 for an out-of-range index. |
| `const char *ot_mqtt_owner_payload(mode)` | `online` while `mode` is HA, else `offline`. Pass the mode the executor last **stepped** with, not a fresh snapshot. |
| `size_t ot_mqtt_attributes(reason, cause, out, cap)` | `{"reason":"…","cause":"…"}` for `control_state`. Returns 0 if it does not fit. |

### Publisher bookkeeping

| Function | Contract |
| --- | --- |
| `ot_mqtt_owed_all(o)` | Mark every entity state owed (a new connection). |
| `ot_mqtt_owed_set(o, index)` / `ot_mqtt_owed_clear(o, index)` | Set/clear one bit; out-of-range is ignored. |
| `int ot_mqtt_owed_next(o, after)` | Lowest owed index `> after`, or -1. |
| `bool ot_mqtt_quiet(q, now_ms, period_ms, held)` | Rate limit: true when a log line is allowed now; `*held` (may be NULL) is how many were held back since the last one. |

Constants: `OT_MQTT_TOPIC_MAX` 128, `OT_MQTT_PAYLOAD_MAX` 32, `OT_MQTT_KEY_MAX` 48,
`OT_MQTT_STATE_MAX` 64; QoS `OT_MQTT_QOS_STATE` 0 / `OT_MQTT_QOS_META` 1; availability payloads
`OT_MQTT_ONLINE` / `OT_MQTT_OFFLINE`.

## Implementation

Three sources. The build requires `ot_command`, `ot_control` and `ot_registry` publicly (their types
appear in `ot_mqtt.h`) and `ot_api` privately (the state-payload spelling).

- **`ot_mqtt_in.c`** — `ot_mqtt_decide()` plus a private `number()` parser. All matching is bounded
  by length, never by a terminator, because esp-mqtt hands over a raw buffer. Order of checks:
  1. Home Assistant birth topic — exact match; only `online` yields `OT_MQTT_HA_ONLINE`. `offline`
     is dropped, because Home Assistant's will also fires on a clean shutdown, which would otherwise
     light the burner at the failsafe setpoint.
  2. `<prefix>/room/state` — matched **exactly** (a longer level falls through to IGNORE), checked
     **before** the `/set` branch so a key literally named `room` could never shadow it; a retained
     reading is refused.
  3. `<prefix>/<key>/set` — the key is one topic level of at least one byte; a retained payload is
     refused, then the payload is parsed as a number.

- **`ot_mqtt_handle.c`** — `ot_mqtt_handle()` plus a private `apply_reason()` that maps the four
  refusal verdicts shared by the command and executor layers to a common error string (any other
  code becomes `"accepted but not carried out by the executor"`). Flow: fill the config via
  `fx->cfg`, run `ot_command_check(…, OT_ORIGIN_HA, …)` for the early answer; for a control command
  call `fx->apply` for the final answer (the mode may have flipped since the early check); for a
  frame command call `fx->write`.

- **`ot_mqtt_out.c`** — the outbound helpers and the bookkeeping. `ot_mqtt_state_payload()`
  post-processes the rendered registry value (`null` → `None`, an option's quotes stripped); the
  owed set is a `uint32_t` bitset (`OT_MQTT_OWED_WORDS` words); `ot_mqtt_quiet()` uses a 64-bit
  millisecond clock.

**Number grammar** (`number()`): `-?[0-9]+(\.[0-9]+)?` and nothing else, checked **before** `strtof`
so that `strtof`'s leniency (leading spaces, `inf`, hex, exponent, partial parse) never runs; the
result must be finite. `strtof("hello")` is 0, and 0 is a valid command (for example, turning CH
enable off), so a lenient parser would accept garbage as a real setpoint.

**Key safety** (`ot_mqtt_decide`): a key containing `/` (a level this component does not serve) or an
embedded `\0` (which downstream C would read as the key's end) is refused — `"<prefix>/ch_enable\0x/set"`
must not become a write to `ch_enable`.

**Owed-states invariant:** the state model's MQTT mark is taken (cleared) before a publish can fail,
so the publisher moves it into `ot_mqtt_owed_t` and clears a bit only after the publish succeeds — a
link that drops mid-run must not cost the remaining entities their values. Only indices
`< OT_ENTITY_COUNT` are ever set.

**Retained-message refusal:** the broker sets the RETAIN flag only when it replays a stored message
to a new subscription, so a retained command or reading is by definition old. Accepted, a retained
command would keep the controller alive on every reconnect and make a dead Home Assistant look
alive; a retained room reading would masquerade as fresh and hold a heating target nobody chose.
Both are refused **after** "is it ours" (another device's retained traffic is ignored, not refused)
and **before** the payload is even looked at.

**The frame path is currently unreachable:** `ot_command_check()` refuses a Home-Assistant-origin
frame write, so `fx->write` is never reached from here. The code is kept and deliberately not
mutated, because this transport holds no rule of its own; changing that policy is a one-line change
in `ot_command`, none here. Likewise `apply_reason()`'s owned-by-HA case is unreachable while
`apply` is always called with the HA origin; it is kept so the table can be reused from a web-origin
caller.

**Do not** include `mqtt_client.h`, `esp_log.h`, `freertos/…`, or any device header here or in any
source of this component: a host suite includes `ot_mqtt.h`, which drags every source of the library
into the host build.

## Tests

Host suites:
- `test_ot_mqtt` — the decoder (`ot_mqtt_decide`) and the number/key grammar.
- `test_ot_mqtt_out` — the outbound helpers and the publisher bookkeeping.
- `test_ot_mqtt_control` — `ot_mqtt_handle()` driving the real command and executor layers through
  the injected effects.
- `test_ot_mqtt_seam` — the seam between the pure decode/handle logic and its callers.

## Notes

- The controller's liveness is kept alive only by an accepted HA `ch_enable`/`ch_setpoint` command
  reaching the executor. A message that is not a `COMMAND` never reaches `ot_mqtt_handle()`, which is
  how a retained command can never keep a dead controller alive.
- `reason` strings are always fixed literals, never the payload, because they reach the device log,
  whose renderer escapes non-ASCII bytes into an unreadable form.
- The 64-bit millisecond clock in `ot_mqtt_quiet` is deliberate: a 32-bit millisecond clock wraps
  after 49.7 days, and a broker that is down for a weekend must not flood the log when it returns.
