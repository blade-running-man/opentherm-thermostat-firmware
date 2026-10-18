// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What goes out (ot_mqtt.h): topics, state payloads, the owner topic, control_state's attributes,
// and the two pieces of bookkeeping the publisher keeps -- the owed states and the log limiter.
#include "ot_mqtt.h"

#include <stdio.h>
#include <string.h>

#include "ot_api.h"

size_t ot_mqtt_topic(const char *prefix, const char *key, const char *leaf, char *out, size_t cap)
{
    if (prefix == NULL || leaf == NULL || out == NULL || cap == 0)
        return 0;
    const int n = key != NULL ? snprintf(out, cap, "%s/%s/%s", prefix, key, leaf)
                              : snprintf(out, cap, "%s/%s", prefix, leaf);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

size_t ot_mqtt_state_payload(uint16_t index, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return 0;
    char value[OT_MQTT_STATE_MAX];
    const size_t n = ot_api_render_value(index, value, sizeof value);
    if (n == 0 || n >= sizeof value)
        return 0;
    const char *text = value;
    size_t      len  = n;
    if (strcmp(value, "null") == 0) {
        text = "None";   // HA's PAYLOAD_NONE: "unknown" -- never "", which deletes the retained value
        len  = 4;
    } else if (value[0] == '"' && n >= 2 && value[n - 1] == '"') {
        text = value + 1;   // an option: registry text, nothing in it needs escaping (ot_api.h)
        len  = n - 2;
    }
    if (len + 1 > cap) {
        out[0] = '\0';
        return 0;
    }
    memcpy(out, text, len);
    out[len] = '\0';
    return len;
}

const char *ot_mqtt_owner_payload(ot_control_mode_t mode)
{
    return mode == OT_CONTROL_MODE_HA ? OT_MQTT_ONLINE : OT_MQTT_OFFLINE;
}

size_t ot_mqtt_attributes(ot_control_reason_t reason, ot_control_reason_t cause, char *out,
                          size_t cap)
{
    if (out == NULL || cap == 0)
        return 0;
    const int n = snprintf(out, cap, "{\"reason\":\"%s\",\"cause\":\"%s\"}",
                           ot_control_reason_name(reason), ot_control_reason_name(cause));
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

void ot_mqtt_owed_all(ot_mqtt_owed_t *o)
{
    memset(o, 0, sizeof *o);
    for (int i = 0; i < OT_ENTITY_COUNT; i++)
        o->w[i / 32] |= (uint32_t)1u << (i % 32);
}

void ot_mqtt_owed_set(ot_mqtt_owed_t *o, int index)
{
    if (index >= 0 && index < OT_ENTITY_COUNT)
        o->w[index / 32] |= (uint32_t)1u << (index % 32);
}

void ot_mqtt_owed_clear(ot_mqtt_owed_t *o, int index)
{
    if (index >= 0 && index < OT_ENTITY_COUNT)
        o->w[index / 32] &= ~((uint32_t)1u << (index % 32));
}

int ot_mqtt_owed_next(const ot_mqtt_owed_t *o, int after)
{
    for (int i = after < 0 ? 0 : after + 1; i < OT_ENTITY_COUNT; i++)
        if (o->w[i / 32] & ((uint32_t)1u << (i % 32)))
            return i;
    return -1;
}

bool ot_mqtt_quiet(ot_mqtt_quiet_t *q, uint64_t now_ms, uint32_t period_ms, uint32_t *held)
{
    if (q->spoke && now_ms - q->last_ms < period_ms) {
        q->held++;
        return false;
    }
    if (held != NULL)
        *held = q->held;
    q->held    = 0;
    q->spoke   = true;
    q->last_ms = now_ms;
    return true;
}
