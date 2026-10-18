# ot_mqtt_link — the impure MQTT glue

## Purpose

The broker connection: the esp-mqtt client, the single FreeRTOS task that owns it, and the wiring
between the broker and the rest of the firmware. This component is **impure and thin**. It decides
nothing about protocol meaning: *what an arriving message means*, *whether a command is allowed*, and
*what any state payload or Home Assistant discovery document contains* belong to the pure, host-tested
components `ot_mqtt` and `ot_ha`. This glue only carries their decisions out on real hardware.

It **follows** the stored broker settings rather than being pushed them: once a second the task reads
the current broker configuration and restarts the client whenever anything in it changed, so a
configuration write over HTTP needs no callback and the HTTP server task never enters esp-mqtt. It
fills the `mqtt` block of the device's `GET /api/status` document.

## Responsibility

**Owns:**

- The one task (`"ot_mqtt"`, priority 3, 6144-byte stack) that creates, starts, publishes,
  subscribes, stops and destroys the esp-mqtt client. No other task ever touches esp-mqtt.
- The esp-mqtt event handler. It runs on esp-mqtt's own task inside esp-mqtt's API lock, so it only
  **sets flags and copies bytes** into a queue — no log, no lock, no publish, no wait.
- Following the broker settings by a 32-bit fingerprint and restarting the client on any change.
- Draining the inbound queue and dispatching each message to the pure deciders.
- The publishing pass: availability, the subscriptions, the owner topic, the `control_state`
  attributes, the owed entity states and the owed Home Assistant discovery documents.
- The `GET /api/status` mqtt block, via `ot_mqtt_link_status()`.
- Bounded connection logging — the story of the link in at most a line a minute or a line an hour —
  and silencing esp-mqtt's own per-attempt ERROR spam.

**Does NOT do:**

- Decide what a message means, whether a command is allowed, or what any payload or document
  contains — those belong to `ot_mqtt` and `ot_ha`.
- Stop, block or reboot because the broker is absent, flapping or refusing: a down broker costs a
  retry every 10 s and a bounded log line, nothing more. Falling silent toward the boiler is the
  dangerous state, not toward the broker.
- Hold any lock of the bus or the state model across a publish.

### Why it is a separate component, not part of `ot_net`

A broker lifecycle living inside `ot_net` would have to call `ot_thermostat` (to read the executor
snapshot and apply commands), while `ot_thermostat` already requires `ot_net` — a dependency cycle
that will not link. Keeping the glue in its own component breaks that cycle: `ot_mqtt_link` only
*reads* `ot_net`'s public header, and nothing in `ot_net` requires `ot_mqtt_link`.

## Public API

`include/ot_mqtt_link.h`. `ot_wire` is the one public dependency (the header hands out
`ot_wire_mqtt_t`); everything else is private.

| Function | Contract |
| --- | --- |
| `esp_err_t ot_mqtt_link_start(void)` | Creates the inbound queue and the owner task; the task starts the client on its first pass if a broker host is stored. Call **after** `ot_net_start()` and `ot_thermostat_start()`, **before** `ot_http_start()`. Idempotent. Returns `ESP_ERR_NO_MEM` if the queue or task could not be created — **not fatal**: the caller logs it and continues, and the device simply has no MQTT. |
| `void ot_mqtt_link_status(ot_wire_mqtt_t *out)` | Fills the `GET /api/status` mqtt block: `configured`, `connected`, `published`, `commands`, `rejected`, `reconnects`, `last_connack`. Callable from any task with no lock — the counters are atomics and the flags are read once each. All fields are zero before `ot_mqtt_link_start()`. |

## Implementation

Three source files behind one private header (`ot_mqtt_link_internal.h`). Naming convention: `g_`
symbols are shared across files; `s_` symbols stay within one file. The task's own helpers
(`otml_client_*`, `otml_publish_pass`, `otml_rediscover`) must **NEVER** be called from any other
task.

### `ot_mqtt_link.c` — the spine (task, settings-follow, dispatch, logging, public face)

- **`link_task`** loops on `ulTaskNotifyTake` with a 1000 ms timeout, dropping to a 100 ms timeout
  while states or documents are still owed (eight publishes per pass, so up to ten passes a second).
  Each pass, in order: drain the inbox (commands first, whether or not the link is up — a command
  that arrived just before a drop is still a command, judged against the context it arrived under),
  follow the settings, re-discover if the device address changed, run one publishing pass, then
  report the connection story, then emit a bounded stack high-water-mark line.
- **`follow_settings`** reads the broker settings into a **static** ~600-byte buffer (which holds the
  password in the clear), computes the fingerprint, and on any change stops the client (which
  publishes `offline` on the old status topic), drains once more against the old context, then starts
  fresh. The buffer is `explicit_bzero`'d at the end of every pass; only the fingerprint is kept.
- **`fingerprint`** hashes the broker settings **field by field** (host, port, user, password, topic
  prefix, device name, discovery flag) with FNV-1a — never the whole struct, whose padding bytes are
  not ours to hash and would make the fingerprint drift on its own, restarting the client every
  second.
- **Dispatch** (`handle_inbound`): the pure `ot_mqtt_decide()` classifies each message by topic.
  `IGNORE` is dropped; a Home Assistant `online` announcement triggers `otml_rediscover()`; a
  `REJECT` is logged as a refusal; a `COMMAND` is handed to `ot_mqtt_handle()`; a `ROOM` reading goes
  to `fx_room()`.
- **The effects table `FX`** (`ot_mqtt_effects_t`) is how `ot_mqtt_handle()` reaches the impure
  world: `fx_cfg` reads the executor's configuration snapshot, `fx_apply` runs the thermostat's
  command-apply path, `fx_write` sends a raw bus frame. **DO NOT** name a command origin in this
  file — the origin belongs to `ot_mqtt_handle()` in pure code, and the source-guard test fails if an
  origin literal appears here.
- **`fx_room`** submits a room-temperature reading straight into the thermostat's room mailbox (slot
  1, the MQTT room slot). A reading is a **measurement, not a command**: **DO NOT** route it through
  `ot_mqtt_handle`/`ot_command`. Doing so would feed the command watchdog as if Home Assistant had
  commanded, and a republished sensor value could be misjudged as an unknown or bad command key.
- **`report`** tells the connection's story in bounded lines: each transition (connected / lost)
  once; while the broker is down, one line an hour; a subscription-ACL refusal when its code changes;
  and a connection refusal (a CONNACK return code) distinguished from a simply unreachable broker.
  Rate limiting uses `ot_mqtt_quiet()` timers.
- **`refuse`** logs the *reason* only, **never the payload** — a command carries whatever anyone on
  the LAN published, and the raw payload is already in the log ring served by `GET /api/log`. At most
  a line a minute, with the suppressed count folded into the next line.
- **Status counters** (`g_published`, `g_rejected`, `g_reconnects`, `g_last_connack`, `g_attempts`,
  `g_sub_refused`, `s_commands`) are **atomics, not lock-guarded**: the handler runs inside esp-mqtt's
  API lock, so any lock of ours taken there could deadlock against it.

### `ot_mqtt_link_client.c` — the client's birth, death and event handler

- **`on_event`** runs on esp-mqtt's own task inside its API lock, so it only sets flags and copies
  bytes. `MQTT_EVENT_DATA` refuses a fragmented message whole (a truncated command is a different
  command) and anything that would overflow the topic or payload buffers (bumping the rejected
  counter), copies a clean message into the inbound queue with a non-blocking `xQueueSend` (never
  waits — a full inbox drops the command, because applying it late is worse), and notifies the task.
  The retain flag is handed over untouched; the pure `ot_mqtt_decide()` is what refuses a retained
  command.
- **`MQTT_EVENT_SUBSCRIBED`**: a SUBACK whose return code has bit 7 set — the broker's ACL refusing
  the `<prefix>/+/set` command subscription — arrives here with `error_type ==
  MQTT_ERROR_TYPE_SUBSCRIBE_FAILED`, **not** on `MQTT_EVENT_ERROR`. **DO NOT** move this branch into
  `MQTT_EVENT_ERROR`, or the device is silently deaf to every command.
- **`otml_client_start`** builds the esp-mqtt client config from the broker settings: URI
  `mqtt://host:port`; `client_id` set to the full-MAC-based device id, so two devices never evict
  each other from the broker; a retained `offline` last-will on `<prefix>/status`; 60 s keepalive
  (the will reaches Home Assistant within about 90 s of a death); 10 s reconnect. **DO NOT** set
  `disable_auto_reconnect` — reconnecting forever without a reboot is exactly the required behavior. It
  also rebuilds the `g_ctx` string set that every topic and document is filled from. esp-mqtt copies
  the URI, credentials, client id and will topic on init, so the caller wipes its password copy
  immediately after.
- **`otml_client_stop`** publishes `offline` itself (best-effort QoS 1, without waiting for the
  PUBACK): a broker does not send the last-will on a clean DISCONNECT, so a cleared broker or a
  changed prefix would otherwise leave `online` retained forever. It then stops and destroys the
  client and clears the connection flags **after** the stop has returned (until then esp-mqtt's task
  could still dispatch an event for a client being destroyed).
- **`otml_client_address_changed`** returns true when the device IP moved (for example after a DHCP
  lease change), so the `configuration_url` in every discovery document is re-published.

### `ot_mqtt_link_publish.c` — what goes out (link task only)

- **`otml_publish_pass`** publishes at most eight messages per pass, in strict order: availability
  (`online`) first — until it is retained, Home Assistant holds every entity `unavailable` and no
  value would even be shown; then the three subscriptions together (`<prefix>/+/set` and Home
  Assistant's `status` at the meta QoS, and `<prefix>/room/state` at the state QoS — a missed room
  reading is simply replaced by the next one, unlike a missed command); then the owner topic first
  (the gated documents name it); then the `control_state` attributes; then the owed entity states;
  then the owed discovery documents. It returns `true` when the eight-message budget was exhausted,
  so the task comes back sooner.
- **`fresh()`** (triggered by the handler's republish flag on each connect) re-owes everything: a
  just-connected broker holds nothing of ours we can trust. The `online` publish, the subscriptions,
  the states and the documents are all **owed until confirmed** rather than fired once on connect — a
  dead socket or a full outbox retries on the next pass instead of leaving Home Assistant stuck
  `unavailable`, or the device deaf, until a reconnect that on a stable link may never come. The
  three subscriptions are owed together: the `subscribed` flag is set only when all three succeed,
  and esp-mqtt's SUBSCRIBE is idempotent, so a repeat after a partial success is harmless.
- **`publish()`** always sends **retained**, so a subscriber that appears later immediately gets the
  current value, availability and document. esp-mqtt returns a negative value on a dead socket or a
  full outbox, and either way the message stays owed. **No lock of ours** is held across a publish —
  the state model's lock is taken and released inside each `ot_*` call, never across the publish
  itself.
- **Discovery documents**: an **empty retained payload** on a discovery topic deletes the entity in
  Home Assistant, which is exactly what a *drop* is. A document that will not render is a settled
  defect or an out-of-bounds condition; it is logged (rate-limited) and retried only when its inputs
  change.

## Invariants

1. **One task owns the client.** Do not call esp-mqtt from any other task; a lock shared with the
   handler is one half of a lock-ordering deadlock.
2. **Nothing reboots or waits because the broker is absent.** Task priorities: bus 10, net 5,
   thermostat 4, this 3, esp-mqtt's own task 3 — this glue sits below everything that matters.
3. **A retained command is ignored** — `ot_mqtt_decide()` refuses it.

## Tests

**No host suite.** The component is impure and is not host-built — no test compiles
`ot_mqtt_link.h`. What it carries out is decided by `ot_mqtt` and `ot_ha`, both pure and covered by
their own host suites. The calls that tie this glue to them (and the rule that no command-origin
literal appears in this component) are pinned by `tools/tests/test_source_guards_mqtt.py`.

## Notes

- **Settings are followed, not pushed.** The task polls the broker settings once a second and
  restarts the client only when a 32-bit FNV-1a fingerprint of the settings changes, so a
  configuration write needs no callback and the HTTP server task never enters esp-mqtt.
- **Owed-until-confirmed.** `online`, the subscriptions, the states and the documents are retried
  each pass until esp-mqtt accepts them, not fired once on connect.
- **Device identity.** The MQTT `client_id` is the full-MAC-based device id, so two devices never
  evict each other from the broker.
- **Password hygiene.** The password is read into a static buffer, copied into the esp-mqtt config
  (which copies it again internally), then `explicit_bzero`'d each pass; only the fingerprint
  persists. Logs print the host and prefix, **never** the user or password.
- **esp-mqtt log spam** from the `mqtt_client`, `transport_base`, `transport`, `esp-tls` and `outbox`
  tags is set to `ESP_LOG_NONE` in `ot_mqtt_link_start()`; `report()` says what matters in bounded
  lines. Do not raise these back.
- **Subscription-ACL refusal** surfaces on `MQTT_EVENT_SUBSCRIBED` (SUBACK bit 7), not
  `MQTT_EVENT_ERROR` — the reason for the explicit DO-NOT comment in the handler.
- **Task stack is 6144 bytes** because applying a command runs the thermostat's command-apply path,
  whose persist step writes NVS; each pass logs a bounded stack high-water-mark line.
