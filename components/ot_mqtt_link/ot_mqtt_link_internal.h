// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The contract between the three files of ot_mqtt_link. Private: beside the sources, not in
// include/. ot_mqtt_link.c is the task and its public face, ot_mqtt_link_client.c the client's
// birth, death and event handler, ot_mqtt_link_publish.c what goes out. `g_` is shared by at least
// two of them; `s_` stays in one file.
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "ot_ha.h"
#include "ot_mqtt.h"
#include "ot_net.h"

// One arriving message, copied out of esp-mqtt's receive buffer by the handler.
typedef struct {
    char   topic[OT_MQTT_TOPIC_MAX];
    size_t topic_len;
    char   payload[OT_MQTT_PAYLOAD_MAX];
    size_t payload_len;
    bool   retained;
} otml_inbound_t;

// The task's, created in ot_mqtt_link.c; the handler only notifies and enqueues, never waits.
extern TaskHandle_t  g_task;
extern QueueHandle_t g_inbox;

// The client: created, used and destroyed on the task only (ot_mqtt_link.h, rule one).
extern esp_mqtt_client_handle_t g_client;
// Written by the handler and by otml_client_stop() only.
extern volatile bool g_connected;
// Set by the handler on connect; cleared by the task when it serves it, and by otml_client_stop().
extern volatile bool g_republish;
// What every topic and document is built from. Written by otml_client_start() only, on the task.
extern ot_ha_ctx_t g_ctx;
extern bool        g_discovery;

// The status block's counters. Atomics, not a lock: the handler runs inside esp-mqtt's API lock,
// and a lock of ours taken there is one half of the heater firmware's ABBA deadlock.
extern _Atomic uint32_t g_published, g_rejected, g_reconnects, g_last_connack, g_attempts;
extern _Atomic uint32_t g_sub_refused;

static inline void bump(_Atomic uint32_t *c) { atomic_fetch_add_explicit(c, 1, memory_order_relaxed); }

// --- the task's own calls: NEVER from any other task --------------------------------------------

// Builds and starts the client from `b` and rewrites g_ctx. ESP_ERR_NOT_FOUND with no host --
// the state every device ships in, not a fault; ESP_ERR_INVALID_STATE when one exists already.
esp_err_t otml_client_start(const ot_net_broker_t *b);
// Publishes `offline`, stops and destroys the client, clears the connection flags. Safe with none.
void      otml_client_stop(void);
// The device's address moved (DHCP): configuration_url in every document is now stale.
bool      otml_client_address_changed(void);

// One publishing pass: the owner topic, control_state's attributes, owed states, owed discovery
// documents -- at most eight messages. true: more is owed, come back soon.
bool otml_publish_pass(void);
// Every discovery document owed again: HA's `online`, a changed address.
void otml_rediscover(void);
