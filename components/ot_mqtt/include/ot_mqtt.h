// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_command.h"    // ot_command_check(), the one entry point of every refusal
#include "ot_control.h"    // the executor's vocabulary: origin, command, mode, reason
#include "ot_registry.h"   // OT_ENTITY_COUNT

// MQTT's pure half that needs no broker: the topic tree, what an arriving
// message MEANS, what an accepted command makes happen, and what goes out as a state payload.
//
// PURE: no FreeRTOS, no esp-mqtt, no ESP-IDF header -- host suites test_ot_mqtt, test_ot_mqtt_out
// and test_ot_mqtt_control. The client, its task and its buffers are ot_mqtt_link's, which calls
// these on its one publisher task. DO NOT include mqtt_client.h, esp_log.h or freertos/ here or
// in any source of this component: a suite includes this header, and PlatformIO compiles every
// source of a library whose header a suite includes.
//
// THE TOPIC TREE (the heater firmware's flat layout, keyed by the registry key):
//
//   <prefix>/status                    online | offline   retained, QoS 1; the will is offline
//   <prefix>/control/owner             online | offline   retained, QoS 1; online iff mode is HA
//   <prefix>/<key>/state               a bare scalar      retained, QoS 0
//   <prefix>/control_state/attributes  {"reason","cause"} retained, QoS 0
//   <prefix>/<key>/set                 a number           subscribed as <prefix>/+/set, QoS 1
//   <prefix>/room/state                a bare °C float    subscribed, QoS 0, NOT retained
//   homeassistant/status               online             subscribed, QoS 1
//
// Ownership: every pointer is the caller's and nothing is kept. No function fails loudly: a topic
// or a payload that does not fit returns 0, which the caller must treat as "publish nothing".

#ifdef __cplusplus
extern "C" {
#endif

#define OT_MQTT_TOPIC_MAX   128   // a 64-byte prefix, a key of at most 32, "/attributes"
#define OT_MQTT_PAYLOAD_MAX 32    // an inbound command: one number. Longer is refused whole
#define OT_MQTT_KEY_MAX     48
#define OT_MQTT_STATE_MAX   64    // an outbound state: a number, true/false, an option, None

// State at QoS 0 -- retained, so a subscriber gets the current value on subscribing;
// availability, discovery and the command subscription at QoS 1 -- a lost `offline` shows a dead
// device alive, and duplicates of a command are harmless because every command is idempotent.
#define OT_MQTT_QOS_STATE 0
#define OT_MQTT_QOS_META  1

#define OT_MQTT_ONLINE  "online"    // HA's default payload_available
#define OT_MQTT_OFFLINE "offline"   // and payload_not_available

// --- topics --------------------------------------------------------------------------------------

// <prefix>/<key>/<leaf>, or <prefix>/<leaf> when key is NULL. 0 when it does not fit.
size_t ot_mqtt_topic(const char *prefix, const char *key, const char *leaf, char *out, size_t cap);

// --- what an arriving message means --------------------------------------------------------------

typedef enum {
    // Not ours, or ours and meaningless (HA's `offline`). SILENTLY: a broker is shared, and
    // counting somebody else's traffic as refused would turn a healthy house into a fault report.
    OT_MQTT_IGNORE = 0,
    // Ours, and refused before the command layer: retained, or a payload that is not a number.
    OT_MQTT_REJECT,
    // A command for ot_command_check(): key and value.
    OT_MQTT_COMMAND,
    // homeassistant/status = online: re-publish discovery. Feeds NOTHING -- HA's birth
    // proves HA is alive, not that its controller produces outputs.
    OT_MQTT_HA_ONLINE,
    // <prefix>/room/state: a room-temperature reading HA is asked to publish. Dispatched
    // straight to the ot_room mailbox -- NEVER through ot_command/ot_control -- so a measurement
    // can be neither a command nor feed the watchdog. `value` carries the reading, no key.
    OT_MQTT_ROOM,
} ot_mqtt_kind_t;

typedef struct {
    ot_mqtt_kind_t kind;
    const char    *reason;               // REJECT: a literal, NEVER the payload (GET /api/log)
    char           key[OT_MQTT_KEY_MAX]; // COMMAND: a copy, so the message need not outlive this
    float          value;                // COMMAND, ROOM
} ot_mqtt_in_t;

// Decides what one message means. `topic` and `payload` are NOT NUL-terminated (esp-mqtt hands a
// pointer and a length into its receive buffer) and are read by length only. `ha_status` is HA's
// birth topic, <discovery prefix>/status -- an argument so that this component does not depend on
// ot_ha, where the discovery prefix is generated; NULL matches nothing.
//
// A RETAINED COMMAND IS REFUSED: the broker sets RETAIN only when replaying
// a stored message to a new subscription, so a retained command is by definition old -- accepted,
// it would feed the watchdog on every reconnect and a dead HA would look alive for ever. It is
// asked AFTER "is it ours" (another device's retained traffic is ignored, not refused) and BEFORE
// the payload (a retained command is refused whatever it says).
//
// A key is ONE topic level, and it ends where the length says: a '/' inside it is a level this
// component does not serve, and a NUL inside it is not a terminator -- "<prefix>/ch_enable\0x/set"
// must not become a write to ch_enable, which is what every C function downstream would make of it.
//
// A command payload is a number and nothing else: an optional minus, digits, an optional point
// with digits -- "45.5", "1", "0". No exponent, no spaces, no "nan", no "ON": strtof("hello") is 0,
// and 0 is a command (ch_enable off, the season off). Which keys exist is NOT decided here: that
// is ot_command_check()'s UNKNOWN_KEY, one place for every writer.
//
// <prefix>/room/state (the room-source topic) is matched EXACTLY -- a level longer,
// e.g. "<prefix>/room/foo/state", is a topic this component does not serve and falls through to
// IGNORE, the same reasoning as the key-with-a-slash rule above. Checked BEFORE the /set branch
// so an (impossible, but explicit) key literally named "room" can never shadow it. Retained is
// refused for the same reason a retained command is: a replayed reading would look
// exactly as fresh as one just published, and the room source steers the failsafe
// -- a stale value masquerading as fresh must not be allowed to hold a heating target.
ot_mqtt_in_t ot_mqtt_decide(const char *prefix, const char *ha_status, const char *topic,
                            size_t topic_len, const char *payload, size_t payload_len,
                            bool retained);

// --- what an accepted command makes happen -------------------------------------------------------

// The three effects, injected so this is pure and the meeting with the executor is host-tested
// (test_ot_mqtt_control drives the real ot_command and ot_control through them). On the device:
// ot_thermostat_control_cfg(), ot_thermostat_control_apply() (through a wrapper that returns its
// verdict as an int) and ot_bus_write().
typedef struct {
    void (*cfg)(ot_control_cfg_t *out);
    // The FINAL answer: 0 accepted, else an ot_control_err_t or one of the task
    // layer's own codes (ot_thermostat_err_t, >= 0x40). `origin` is always OT_ORIGIN_HA here.
    int (*apply)(ot_origin_t origin, ot_control_cmd_t cmd, int16_t value);
    void (*write)(uint8_t data_id, uint16_t raw);
} ot_mqtt_effects_t;

typedef struct {
    bool        accepted;
    const char *reason;   // refused: a literal fit for a log line; NULL when accepted
} ot_mqtt_verdict_t;

// A COMMAND, carried out exactly as the web route carries out a POST (ot_http_registry.c): the
// early answer from ot_command_check() with OT_ORIGIN_HA, then an executor command through
// fx->apply -- which re-checks ownership against the mode the executor last observed -- or a frame
// through fx->write. There is no check of its own here and there must never be one: a check
// duplicated on a surface diverges from the one in the depths, silently (ot_command.h).
//
// The watchdog is fed, or not, by ot_control_apply() alone: accepted HA ch_enable and
// ch_setpoint commands. A message that is not a COMMAND never reaches this function, which is how
// a retained command never feeds it. Any other kind answers {false, NULL}.
//
// THE FRAME PATH IS UNREACHABLE FROM HERE while ot_command_check() refuses an HA-origin frame
// write: every `fx->write` is dead code today because HA writes no OpenTherm frame. It stays, and
// is deliberately not mutated, because this transport holds no rule of its own -- it carries out
// what the one entry point decided.
ot_mqtt_verdict_t ot_mqtt_handle(const ot_mqtt_in_t *in, const ot_mqtt_effects_t *fx);

// --- what goes out -------------------------------------------------------------------------------

// The state payload of registry entity `index`: ot_api_render_value()'s spelling, the spelling of
// /api/state and /ws, as HA reads a bare MQTT payload -- a number as it is, true/false as they
// are (the discovery documents say so: payload_on "true"), an option without its quotes, and
// null as "None", HA's own "unknown" (PAYLOAD_NONE in sensor, binary_sensor and switch;
// payload_reset in number). NEVER an empty payload: an empty RETAINED message deletes the
// retained value instead of saying "no data". 0 for an index outside the registry.
size_t ot_mqtt_state_payload(uint16_t index, char *out, size_t cap);

// What <prefix>/control/owner carries: online while HA owns the controls, offline in LOCAL mode --
// the control entities are then unavailable in HA, so HA never sends what would be refused
// Pass the mode the executor last STEPPED with, never a fresh snapshot:
// owner online before the step has seen HA would invite commands ot_control still refuses.
const char *ot_mqtt_owner_payload(ot_control_mode_t mode);

// {"reason":"<reason>","cause":"<cause>"} -- control_state's attributes. 0 if short.
size_t ot_mqtt_attributes(ot_control_reason_t reason, ot_control_reason_t cause, char *out,
                          size_t cap);

// --- bookkeeping the publisher keeps ------------------------------------------------------------

// Entities whose state is owed to the broker. ot_state's MQTT mark is TAKEN (cleared) before a
// publish can fail, so the publisher moves it here and clears a bit only after the publish
// succeeded: a link that drops after three publishes must not cost the rest their values. Only
// indices below OT_ENTITY_COUNT are ever set.
#define OT_MQTT_OWED_WORDS ((OT_ENTITY_COUNT + 31) / 32)
typedef struct {
    uint32_t w[OT_MQTT_OWED_WORDS];
} ot_mqtt_owed_t;

void ot_mqtt_owed_all(ot_mqtt_owed_t *o);              // a new connection: every state owed
void ot_mqtt_owed_set(ot_mqtt_owed_t *o, int index);   // out of range ignored
void ot_mqtt_owed_clear(ot_mqtt_owed_t *o, int index);
int  ot_mqtt_owed_next(const ot_mqtt_owed_t *o, int after);   // lowest owed index > after, or -1

// Bounded log noise (a broker down for a weekend must not flood GET /api/log). At most
// one line per period; the lines held back are counted and handed to the next one allowed. The
// clock is 64-bit milliseconds -- a 32-bit one wraps after 49.7 days.
typedef struct {
    uint64_t last_ms;
    uint32_t held;
    bool     spoke;
} ot_mqtt_quiet_t;

// true: log now, and *held (may be NULL) is how many were held back since the last line.
bool ot_mqtt_quiet(ot_mqtt_quiet_t *q, uint64_t now_ms, uint32_t period_ms, uint32_t *held);

#ifdef __cplusplus
}
#endif
