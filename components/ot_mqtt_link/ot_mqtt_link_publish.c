// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What goes out, on the link task only (ot_mqtt_link_internal.h): the birth and the
// subscriptions, owed until confirmed and retried each pass rather than fired once on
// connect, then every pass the owner topic, control_state's attributes, the owed states and the
// owed discovery documents -- eight messages at most, so a burst never keeps this task from its
// inbox for long.
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "ot_mqtt_link_internal.h"
#include "ot_registry.h"
#include "ot_state.h"
#include "ot_thermostat.h"

static const char *TAG = "mqtt";

#define PER_PASS 8

// The task's own; static for its stack's sake (OT_HA_DOC_MAX alone is 1.5 KB).
static char            s_topic[OT_MQTT_TOPIC_MAX];
static char            s_payload[OT_HA_DOC_MAX];
static ot_mqtt_owed_t  s_owed;
static ot_ha_plan_t    s_plan;
static ot_ha_want_t    s_want[OT_HA_DOC_COUNT];
static int16_t         s_lo[OT_HA_DOC_COUNT], s_hi[OT_HA_DOC_COUNT];
static const char     *s_owner_sent;   // the owner payload this connection has published; NULL: none
static bool            s_status_sent;   // `online` availability confirmed sent this connection
static bool            s_subscribed;    // both SUBSCRIBEs accepted by esp-mqtt this connection
static char            s_attr_sent[OT_MQTT_STATE_MAX];
static ot_mqtt_quiet_t s_q_doc;

// Retained, every one: a subscriber -- HA after its own restart -- gets the current value,
// availability and document the moment it subscribes. esp-mqtt answers -1 on a dead socket and -2
// on a full outbox; either way the message stays owed. NO LOCK of ours is held here: the state
// model's lock is taken and released inside every ot_* call above, never across this one.
static bool publish(const char *topic, const char *payload, int qos)
{
    if (!g_connected || g_client == NULL)
        return false;
    if (esp_mqtt_client_publish(g_client, topic, payload, 0, qos, 1) < 0)
        return false;
    bump(&g_published);
    return true;
}

void otml_rediscover(void) { ot_ha_plan_reset(&s_plan); }

// A broker that has just accepted us holds nothing of ours we can trust: owe it `online`, the
// subscriptions, every state, the owner topic, the attributes and every document. This ONLY resets
// bookkeeping -- it sends nothing: the `online` publish and the SUBSCRIBEs are fire-and-forget no
// longer. A -1 dead socket / -2 full outbox on either would otherwise leave HA's
// entities stuck `unavailable`, or the device deaf to commands, until the next full reconnect --
// which on a stable link may never come. So they are owed until confirmed, like every other publish
// here, and driven from otml_publish_pass() below.
static void fresh(void)
{
    ot_mqtt_owed_all(&s_owed);
    ot_ha_plan_reset(&s_plan);
    s_owner_sent   = NULL;
    s_status_sent  = false;
    s_subscribed   = false;
    s_attr_sent[0] = '\0';
}

bool otml_publish_pass(void)
{
    if (g_republish) {
        g_republish = false;
        fresh();
    }
    int budget = PER_PASS;

    // Availability BEFORE everything: until `online` is retained, HA holds every entity of
    // ours `unavailable`, so no value we publish next would even be shown. Owed until confirmed:
    // a -1 dead socket / -2 full outbox retries next pass rather than being lost.
    if (!s_status_sent) {
        if (ot_mqtt_topic(g_ctx.prefix, NULL, "status", s_topic, sizeof s_topic) == 0 ||
            !publish(s_topic, OT_MQTT_ONLINE, OT_MQTT_QOS_META))
            return true;
        s_status_sent = true;
    }
    // Then the SUBSCRIBEs, owed together: a device subscribed to only one topic is half-deaf, so
    // s_subscribed is set only when ALL THREE succeed (>=0), else all are re-issued next pass. Exact
    // topics, QoS and order preserved from fresh(). esp-mqtt's SUBSCRIBE is idempotent, so a repeat
    // after a partial success is harmless.
    if (!s_subscribed) {
        int r1 = -1, r2 = -1, r3 = -1;
        if (ot_mqtt_topic(g_ctx.prefix, "+", "set", s_topic, sizeof s_topic) > 0)
            r1 = esp_mqtt_client_subscribe_single(g_client, s_topic, OT_MQTT_QOS_META);
        r2 = esp_mqtt_client_subscribe_single(g_client, OT_HA_DISCOVERY_PREFIX "/status",
                                              OT_MQTT_QOS_META);
        // <prefix>/room/state: a room-temperature reading HA republishes to us. QoS 0, not 1
        // like the two above -- a missed reading is simply replaced by the next one (ot_sensor's own
        // staleness rule), unlike a missed command, which would starve the watchdog unnoticed.
        // s_topic is free to reuse: r1's build was already consumed by its subscribe call above.
        if (ot_mqtt_topic(g_ctx.prefix, "room", "state", s_topic, sizeof s_topic) > 0)
            r3 = esp_mqtt_client_subscribe_single(g_client, s_topic, OT_MQTT_QOS_STATE);
        if (r1 < 0 || r2 < 0 || r3 < 0)
            return true;
        s_subscribed = true;
    }

    // The owner topic FIRST: the gated documents below name it, and HA must hold its value before
    // it holds them. From the mode the executor last STEPPED with (ot_mqtt_owner_payload()).
    ot_control_cfg_t cfg;
    ot_thermostat_control_cfg(&cfg);
    const char *owner = ot_mqtt_owner_payload(cfg.mode);
    if (owner != s_owner_sent &&
        ot_mqtt_topic(g_ctx.prefix, "control", "owner", s_topic, sizeof s_topic) > 0) {
        if (!publish(s_topic, owner, OT_MQTT_QOS_META))
            return true;
        s_owner_sent = owner;
        budget--;
    }

    ot_thermostat_control_info_t info;
    ot_thermostat_control_get(&info);
    char attr[OT_MQTT_STATE_MAX];
    if (ot_mqtt_attributes(info.reason, info.cause, attr, sizeof attr) > 0 &&
        strcmp(attr, s_attr_sent) != 0 &&
        ot_mqtt_topic(g_ctx.prefix, "control_state", "attributes", s_topic, sizeof s_topic) > 0) {
        if (!publish(s_topic, attr, OT_MQTT_QOS_STATE))
            return true;
        memcpy(s_attr_sent, attr, sizeof s_attr_sent);
        budget--;
    }

    // The state model's MQTT marks are TAKEN into s_owed, and a bit leaves s_owed only after its
    // publish succeeded (ot_mqtt.h, ot_mqtt_owed_t).
    for (uint16_t i = 0; i < ot_registry_count(); i++)
        if (ot_state_take_dirty(OT_CONSUMER_MQTT, i))
            ot_mqtt_owed_set(&s_owed, i);
    for (int i = ot_mqtt_owed_next(&s_owed, -1); i >= 0 && budget > 0;
         i = ot_mqtt_owed_next(&s_owed, i)) {
        const ot_entity_t *e = ot_registry_at((uint16_t)i);
        if (e == NULL || ot_mqtt_state_payload((uint16_t)i, s_payload, sizeof s_payload) == 0 ||
            ot_mqtt_topic(g_ctx.prefix, e->key, "state", s_topic, sizeof s_topic) == 0) {
            ot_mqtt_owed_clear(&s_owed, i);   // a defect, and retrying it would find the same
            continue;
        }
        if (!publish(s_topic, s_payload, OT_MQTT_QOS_STATE))
            return true;
        ot_mqtt_owed_clear(&s_owed, i);
        budget--;
    }

    ot_ha_wants(g_discovery, s_want);
    ot_ha_bounds(cfg.flow_min_dc, cfg.flow_max_dc, s_lo, s_hi);
    bool empty = false;
    for (int i; budget > 0 && (i = ot_ha_plan_next(&s_plan, s_want, s_lo, s_hi, &empty)) >= 0;
         budget--) {
        const ot_ha_doc_t *d = ot_ha_doc_at((uint16_t)i);
        if (ot_ha_topic(d, &g_ctx, s_topic, sizeof s_topic) == 0 ||
            (!empty && ot_ha_render(d, &g_ctx, s_lo[i], s_hi[i], s_payload, sizeof s_payload) == 0)) {
            // A document that will not render is a defect (test_ot_ha builds the worst case) or
            // bounds HA would refuse; settled, so it is retried only when its inputs change.
            if (ot_mqtt_quiet(&s_q_doc, (uint64_t)(esp_timer_get_time() / 1000), 60000, NULL))
                ESP_LOGE(TAG, "discovery for %s not published: it does not render", d->object_id);
            ot_ha_plan_done(&s_plan, i, empty, s_lo[i], s_hi[i]);
            continue;
        }
        // An EMPTY retained payload deletes the entity in HA: that is what a drop is.
        if (!publish(s_topic, empty ? "" : s_payload, OT_MQTT_QOS_META))
            return true;
        ot_ha_plan_done(&s_plan, i, empty, s_lo[i], s_hi[i]);
    }
    return budget == 0;
}
