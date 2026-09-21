// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_err.h"
#include "ot_wire.h"   // ot_wire_mqtt_t, GET /api/status's mqtt block

// The broker connection: the esp-mqtt client, the one task that owns it, and what
// that task publishes and hears. Impure and thin -- what a message means, what a command does and
// what a document says are ot_mqtt's and ot_ha's, host-tested; this carries them out.
//
// THREE RULES, each a failure somebody already shipped (heater-firmware MQTT hardening design):
//
//  * ONE TASK OWNS THE CLIENT. It creates, publishes, subscribes, stops and destroys it; the
//    esp-mqtt event handler only sets flags and copies bytes into a queue. esp-mqtt runs its
//    handler synchronously inside its own API lock, so anything else -- a publish from the
//    handler, a stop from another task, a lock shared with the handler -- is a deadlock or a
//    buffer published to the wrong topic. DO NOT call into esp-mqtt from any other task.
//  * NOTHING REBOOTS BECAUSE THE BROKER IS ABSENT, and nothing waits for it. The bus task is
//    priority 10, the thermostat 4 and this 3; no lock of the bus or the state model is held
//    across a publish. A broker that is down, flapping or refusing costs a retry every 10 s
//    and a bounded log line.
//  * A RETAINED COMMAND IS IGNORED: ot_mqtt_decide() refuses it; the handler hands the
//    retain flag over untouched.
//
// The broker settings are FOLLOWED, not pushed: once a second the task reads ot_net_broker() and
// restarts the client when anything in it changed, so POST /api/config needs no hook and the
// httpd task never enters esp-mqtt. The password is read into a static buffer and wiped at once.

#ifdef __cplusplus
extern "C" {
#endif

// Creates the task, which starts the client on its first pass if a broker host is stored. Call
// AFTER ot_net_start() and ot_thermostat_start(), BEFORE ot_http_start(). Idempotent.
// ESP_ERR_NO_MEM if the queue or the task could not be created: the caller logs it and goes on --
// NOT fatal; the device then simply has no MQTT.
esp_err_t ot_mqtt_link_start(void);

// GET /api/status's mqtt block, every field filled. Any task, no lock: counters are atomics and
// the flags are read once each. All zero before ot_mqtt_link_start().
void ot_mqtt_link_status(ot_wire_mqtt_t *out);

#ifdef __cplusplus
}
#endif
