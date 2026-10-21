# MQTT

MQTT is how Home Assistant drives the device. The firmware publishes every entity's state to a
broker, listens for commands on its own topic tree and, by default, announces itself to Home
Assistant through discovery (see [Home Assistant discovery](/interfaces/home-assistant-discovery)).
Home Assistant is the controller; the device carries out what it is told, inside the bounds it
enforces itself, and falls back to its own failsafe when the commands stop (see
[Executor](/architecture/executor)).

The work is split along the pure/impure seam. What a message **means** — the topics, the decoder,
the one command entry point and the outbound payloads — is the pure component `components/ot_mqtt`,
host-tested. Everything that needs a socket — the esp-mqtt client, the one task that owns it and the
event handler — is `components/ot_mqtt_link`, which decides nothing of its own.

## Connection

| Setting | Value |
| --- | --- |
| Broker URI | `mqtt://host:port` — plain TCP, no TLS |
| Port | `mqtt_port`, default 1883 |
| Credentials | `mqtt_user` / `mqtt_password`, sent only when a user is set |
| Client id | `device_id` — twelve lower-case hex digits of the MAC, so two devices never evict each other |
| Last will | `<prefix>/status` = `offline`, QoS 1, retained |
| Keepalive | 60 s — the will reaches Home Assistant within about 90 s of a death |
| Auto-reconnect | on, every 10 s, for ever |
| Buffers | 1024 bytes in (commands are short); out sized for a whole discovery document |
| Outbox | 16 KB; a QoS 1 publish beyond it fails and stays owed |
| Tasks | `ot_mqtt` (priority 3, 6144-byte stack) and esp-mqtt's own (priority 3) |

The client is built in `otml_client_start()` (`ot_mqtt_link_client.c`). Everything is configured
at runtime through `POST /api/config` (`mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_password`,
`topic_prefix`, `device_name`, `ha_discovery`) and takes effect within a second, without a reboot.
An empty `mqtt_host` means "no broker": nothing is dialled and nothing is reported as a fault. See
[Configuration](/guide/configuration).

The topic prefix defaults to `opentherm/<device_id>`, e.g. `opentherm/aabbccddeeff`. It is keyed on
the MAC, never the device name: a prefix keyed on the name would re-key every topic on a rename and
leave the old retained documents in the broker for ever.

**Nothing reboots because the broker is absent.** A broker that is down, flapping or refusing costs
a retry every 10 s and a bounded log line. Both MQTT tasks run at priority 3, below the thermostat
(4), the network (5) and the bus (10), so the conversation with the boiler is never starved.

### Why there is no TLS

Every value that arrives over MQTT is checked by the firmware against its own bounds before it
reaches the boiler, and what matters most is out of MQTT's reach entirely: the control mode, turning
the heating season on, the flow bounds and the watchdog period cannot be set over MQTT at all. A
forged message can do no more than a legitimate Home Assistant could.

## Topics

`<prefix>` is the configured topic prefix; `<key>` is a registry entity key (see
[Entities](/reference/entities)).

| Topic | Direction | Payload | QoS | Retained |
| --- | --- | --- | --- | --- |
| `<prefix>/status` | out | `online` / `offline` | 1 | yes — the will is `offline` |
| `<prefix>/control/owner` | out | `online` while `control_mode` is HA, else `offline` | 1 | yes |
| `<prefix>/<key>/state` | out | a bare scalar, one topic per entity | 0 | yes |
| `<prefix>/control_state/attributes` | out | `{"reason":"…","cause":"…"}` | 0 | yes |
| `homeassistant/<component>/<device_id>/<object_id>/config` | out | a discovery document, or empty to delete | 1 | yes |
| `<prefix>/+/set` | in | a number | 1 | refused if retained |
| `<prefix>/room/state` | in | a room temperature in °C | 0 | refused if retained |
| `homeassistant/status` | in | Home Assistant's birth message | 1 | — |

Availability, the owner topic, discovery and the command subscription are QoS 1: a lost `offline`
would show a dead device as alive, and a duplicated command is harmless because every command is
idempotent. State is QoS 0 and retained, so a subscriber gets the current value the moment it
subscribes, and a lost update is replaced by the next. The room reading is subscribed at QoS 0 for
the same reason: a missed reading is simply replaced by the next one, unlike a missed command.

### State payloads

`ot_mqtt_state_payload()` takes the value from `ot_api`'s one renderer — the same spelling as
`GET /api/state` and `/ws` — and strips it to a bare scalar:

| Value | Payload |
| --- | --- |
| a number | `45.50` (two decimals) |
| a binary or switch entity | `true` / `false` |
| an enum | the option text, unquoted: `ha` |
| no data (`null`) | `None` — Home Assistant's "unknown" |

`None`, never an empty payload: an empty retained message deletes the stored value on the broker.

### Availability and the owner topic

`<prefix>/status` is the device's availability: `online` is the first thing published after every
connect, and until it is retained Home Assistant holds every entity `unavailable`. On a clean stop —
a cleared broker, a changed prefix — the device publishes `offline` itself before it disconnects,
because a broker does not send the will on a clean DISCONNECT and `online` would otherwise stay
retained for ever.

`<prefix>/control/owner` is the second availability topic of the control entities. It reads
`online` only while `control_mode` is HA, taken from the mode the executor last **stepped** with,
not from a fresh read of the store. The discovery documents of the switches, the CH and DHW numbers
and the season button list both topics with `availability_mode: all`, so in LOCAL mode they are
unavailable in Home Assistant and its service layer never even publishes their commands — better
than refusing and re-publishing, which misbehaves in Home Assistant's UI (a switch snaps back; a
number whose echo equals its state never redraws). See
[Home Assistant discovery](/interfaces/home-assistant-discovery#availability).

`<prefix>/control_state/attributes` carries the executor's `reason` and `cause` beside the
`control_state` sensor; it is published only when it changes.

## Inbound messages

The esp-mqtt event handler runs on esp-mqtt's own task inside its API lock, so it only copies bytes
into an inbox of 8 messages and wakes the `ot_mqtt` task — no parsing, no log, no lock. A fragmented
message is refused whole (a truncated command is a different command), as is anything longer than
the topic (128) or payload (32) buffers. A full inbox drops the message: applied late is worse than
not applied. Each refusal counts in `rejected`.

The task then hands each message to the pure `ot_mqtt_decide()`. Topics and payloads are read by
length only — they are esp-mqtt's raw buffers, not C strings — and checked in this order:

| Order | Topic | Outcome |
| --- | --- | --- |
| 1 | `homeassistant/status` | `online` → **re-publish discovery**. `offline` is ignored |
| 2 | `<prefix>/room/state`, exactly | A **room reading** → the room-source mailbox |
| 3 | `<prefix>/<key>/set` | A **command** → `ot_mqtt_handle()` |
| — | anything else | Ignored silently — a shared broker's other traffic is not "refused" |

- **Home Assistant's `offline` is ignored** because its will also fires on a clean shutdown, and
  hurrying the failsafe on it would light the burner at the failsafe setpoint during every Home
  Assistant restart. Its `online` feeds **nothing** that heats: Home Assistant being alive is not
  proof that its controller is producing outputs.
- **The room topic is matched exactly and before `/set`**, so a key literally named `room` could
  never shadow it. A reading is a measurement, not a command: it goes straight to slot 1 of the
  room-source registry (`ot_thermostat_room_submit()`) and never through the command layer, so it can
  neither feed the watchdog nor be judged as a bad command key. See
  [Room sources](/device/room-sources).
- **The key** is exactly one topic level of at least one byte; a key containing `/` or an embedded
  NUL is ignored, so `<prefix>/ch_enable\0x/set` cannot become a write to `ch_enable`.
- **The payload** must match `-?[0-9]+(\.[0-9]+)?` and be finite. The grammar is checked before
  `strtof`, whose leniency (spaces, `inf`, hex, exponents, partial parses) never runs — `strtof`
  reads `hello` as 0, and 0 is a valid command (turning CH off).

### The retained-command refusal

**A retained command or reading is refused**, after "is it ours" and before the payload is even
looked at. A broker sets the RETAIN flag only when it replays a stored message to a new subscription,
so a retained command is by definition old. Accepted, it would feed the watchdog on every reconnect
and make a dead Home Assistant look alive; a retained room reading would masquerade as fresh and
hold a heating target nobody chose. No discovery document asks Home Assistant to retain a command.

### The command entry point: `ot_mqtt_handle()`

An accepted command is carried out exactly the way the web's `POST /api/entities/{key}` carries one
out, with the origin fixed to `OT_ORIGIN_HA` in pure code:

1. Read the executor's configuration snapshot (`cfg`).
2. **`ot_command_check(key, value, OT_ORIGIN_HA, …)`** — the early answer and the one place every
   refusal is decided: the key and writability, the boiler's support, the bounds, and ownership (see
   [Commands](/protocol/commands)).
3. For a control command (`ch_enable`, `ch_setpoint`, `dhw_enable`, `dhw_setpoint`,
   `heating_season`), **`ot_thermostat_control_apply()`** — the final answer, which re-checks
   ownership against the mode the executor last observed, in case it flipped in between.

The three impure effects — read the snapshot, apply a control command, write a bus frame — are
injected as a table (`ot_mqtt_effects_t`), so the meeting between the decoder, the command layer and
the executor is host-tested (`test_ot_mqtt_control`, `test_ot_mqtt_seam`). The glue in
`ot_mqtt_link.c` may not name an origin at all; `tools/tests/test_source_guards_mqtt.py` fails if an
origin literal appears there.

What Home Assistant cannot do, by the same function:

- **Write an OpenTherm frame.** A frame row — an entity with no executor command, such as
  `max_relative_modulation`, `room_setpoint` or `room_temperature` (IDs 14, 16, 24) — is refused with
  `OT_CMD_NOT_FOR_HA`, asked **after** the request itself is judged (so an out-of-range value still
  reads as out of range). Home Assistant drives the executor, never the bus: a frame write would
  hold the bus's one write slot and defer the executor's ID 1 re-send — and since the CH bit never
  rises before the held ID 1 has gone out, the CH bit, the failsafe's included, could then never
  rise. The frame path in `ot_mqtt_handle()` is kept but unreachable; changing the policy is one line
  in `ot_command`, none here.
- **Turn the heating season on** (`OT_CMD_SEASON_ON_IS_LOCAL`); it may only turn it off.
- **Command anything in LOCAL mode** (`OT_CMD_OWNED_BY_LOCAL`).

A refusal is counted in `rejected` and logged by its reason — a fixed literal, **never the
payload**, which carries whatever anyone on the LAN published — at most one line a minute, with the
number held back folded into the next line.

### Feeding the watchdog

The watchdog is fed **only** by an accepted Home Assistant `ch_enable` or `ch_setpoint` command that
reaches the executor; the same command ends `ha_waiting`. Nothing else keeps the controller alive:
not a refused command, not a retained one, not Home Assistant's birth message, not a room reading.
If Home Assistant stops commanding for `watchdog_s`, the executor leaves HA control for the bounded
failsafe. An automation in Home Assistant therefore has to keep re-sending its CH command, and every
command is idempotent so that it can. The failsafe itself is described on
[Executor](/architecture/executor) and [Failure model](/architecture/failure-model).

## Publishing

One task, one loop. It sleeps on a task notification for up to 1000 ms, or 100 ms while anything is
still owed, and each pass:

1. **Drains the inbox** — commands first, whether or not the link is up: a command that arrived just
   before a drop is still a command, judged against the context it arrived under.
2. **Follows the broker settings** (below).
3. **Re-offers discovery** if the device's IP changed (the `configuration_url` in every document
   names it).
4. **Runs one publishing pass**, then reports the connection's story to the log.

The publishing pass works in strict order. `online` and the subscriptions are always attempted
when owed; the owner topic, the attributes, the entity states and the discovery documents share a
budget of **eight** publishes per pass (`PER_PASS`):

1. `online` on `<prefix>/status` — until it is retained, nothing would even be shown.
2. The three subscriptions together: `<prefix>/+/set` and `homeassistant/status` at QoS 1,
   `<prefix>/room/state` at QoS 0.
3. The owner topic, when it changed — before the documents that name it.
4. The `control_state` attributes, when they changed.
5. The owed entity states.
6. The owed discovery documents.

Every publish is retained. When the budget runs out the pass returns early and the task comes back
in 100 ms, so a just-connected device works through its whole registry in a second or two without
monopolising the client.

### Owed until confirmed

Nothing is fired once on connect and forgotten. Each new connection re-owes everything — a broker
that just accepted us holds nothing of ours we can trust — and `online`, the subscriptions, each
state and each document stay owed until esp-mqtt accepts them. A dead socket or a full outbox makes
the publish fail, and it is retried on the next pass instead of leaving Home Assistant stuck
`unavailable` until a reconnect that on a stable link may never come.

The state model marks a changed entity once per consumer (`OT_CONSUMER_MQTT` in `ot_state`), and
taking the mark clears it — before the publish can fail. So the publisher moves every mark into its
own **owed set** (`ot_mqtt_owed_t`, one bit per entity) and clears a bit only after its publish
succeeds; a link that drops mid-pass does not cost the remaining entities their values. The three
subscriptions are owed together, since esp-mqtt's SUBSCRIBE is idempotent. No lock of ours is held
across a publish.

### Following the broker settings

The MQTT task **follows** the stored broker settings instead of being told about them. Once a
second it reads them through `ot_net_broker()` into a static buffer, computes a 32-bit **FNV-1a
fingerprint** over host, port, user, password, topic prefix, device name and the discovery flag —
field by field, never the whole struct, whose padding bytes would make the fingerprint drift and
restart the client every second — and wipes the buffer with `explicit_bzero`. Only the fingerprint
is kept.

When the fingerprint changes, the client is stopped (publishing `offline` on the old status topic),
the inbox is drained once more against the old context, and a new client is started from the new
settings. `POST /api/config` therefore needs no callback, and the HTTP server task never enters
esp-mqtt: `esp_mqtt_client_stop()` can block for as long as a dying socket takes to time out, and on
the one task that serves every HTTP socket that would freeze the web UI.

### Logging

esp-mqtt's own per-attempt errors (the `mqtt_client`, `transport_base`, `transport`, `esp-tls` and
`outbox` tags) are silenced at start; the link tells the story in bounded lines instead: each
connect and loss once; while the broker is down, one line an hour; a CONNACK refusal (`bad username
or password`, `not authorized`, …) told apart from a broker that is simply unreachable; and a
subscription the broker's ACL refused. That last one arrives as a failed SUBACK on
`MQTT_EVENT_SUBSCRIBED`, not on `MQTT_EVENT_ERROR` — handled anywhere else, the device would be
silently deaf to every command. Logs print the host and prefix, never the user or password.

## Status counters

`ot_mqtt_link_status()` fills the `mqtt` block of [`GET /api/status`](/interfaces/rest-api#get-api-status).
The counters are atomics, not lock-guarded, because the event handler runs inside esp-mqtt's API lock
and any lock of ours taken there could deadlock against it.

| Field | Meaning |
| --- | --- |
| `configured` | A broker host is stored |
| `connected` | The client is connected now |
| `published` | Messages esp-mqtt accepted for publishing |
| `commands` | Commands accepted and carried out |
| `rejected` | Messages refused: retained, malformed, oversized, fragmented, an inbox overflow, or a command `ot_command_check()` or the executor refused |
| `reconnects` | Successful connections, the first one included |
| `last_connack` | The broker's last CONNACK refusal code, 0 when none; `last_connack_reason` is its name |

Source: [`components/ot_mqtt/README.md`](../../components/ot_mqtt/README.md),
[`components/ot_mqtt_link/README.md`](../../components/ot_mqtt_link/README.md).
