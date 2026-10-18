// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The spine: the task that owns the client, what an arriving message makes happen, the broker
// settings it follows, the lines it logs, and the public face (ot_mqtt_link.h).
#include "ot_mqtt_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "ot_bus.h"
#include "ot_mqtt_link_internal.h"
#include "ot_thermostat.h"

static const char *TAG = "mqtt";

#define TICK_MS     1000   // the settings, the owner topic and the log are looked at once a second
#define BUSY_MS     100    // while states or documents are owed: eight per pass, ten passes a second
#define INBOX_LEN   8      // more than a person or an automation produces; a burst beyond is dropped
#define STACK_BYTES 6144   // it runs ot_thermostat_control_apply(), whose persist writes NVS
#define PRIORITY    3      // BELOW ot_thermostat (4), ot_net (5) and ot_bus (10): the bus must never be starved
#define HOUR_MS     3600000u

TaskHandle_t     g_task;
QueueHandle_t    g_inbox;
_Atomic uint32_t g_published, g_rejected, g_reconnects, g_last_connack, g_attempts, g_sub_refused;
static _Atomic uint32_t s_commands;
static volatile bool    s_configured;

// Static, not on the stack: 600 bytes of settings with the password IN THE CLEAR (ot_net.h). Read
// once a pass and wiped at once; the fingerprint is all that is kept.
static ot_net_broker_t s_broker;
static uint32_t        s_fp;
static bool            s_fp_known;

static ot_mqtt_quiet_t s_q_refused, s_q_down, s_q_conn;
static bool            s_was_up;
static uint32_t        s_connack_said, s_sub_said;
static uint32_t        s_hwm_said = UINT32_MAX;

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }

// --- the effects ot_mqtt_handle() carries a command out through ----------------------------------

static void fx_cfg(ot_control_cfg_t *out) { ot_thermostat_control_cfg(out); }

// `origin` is ot_mqtt_handle()'s -- OT_ORIGIN_HA, in pure code where a test pins it. DO NOT name
// an origin here: tools/tests/test_source_guards_mqtt.py fails on either one appearing in this
// component. The store's refusal reason is the thermostat's to log (ot_thermostat_control_apply).
static int fx_apply(ot_origin_t origin, ot_control_cmd_t cmd, int16_t value)
{
    ot_config_err_t store_err = OT_CONFIG_OK;
    return (int)ot_thermostat_control_apply(origin, cmd, value, &store_err);
}

static void fx_write(uint8_t data_id, uint16_t raw) { ot_bus_write(data_id, raw); }

static const ot_mqtt_effects_t FX = {fx_cfg, fx_apply, fx_write};

// why: a room reading is a MEASUREMENT, not a command -- it must never cross ot_mqtt_handle(),
// which exists to run a command through ot_command_check()/the executor and feed the watchdog
// (the command entry point). Routed through the command entry point, a republished sensor value
// would count as HA activity and could be misjudged as an unknown/bad key; dispatched here
// instead, straight to the mailbox ot_room_select_steer() reads, it can be neither. Slot 1 is the
// MQTT room slot. DO NOT route a measurement through ot_mqtt_handle/ot_command.
static void fx_room(float celsius) { ot_thermostat_room_submit(1, celsius); }

// The reason, NEVER the payload: a command carries whatever anybody on the LAN published, and the
// log ring is served by GET /api/log. At most a line a minute, with the count held back.
static void refuse(const char *why)
{
    bump(&g_rejected);
    uint32_t held = 0;
    if (ot_mqtt_quiet(&s_q_refused, now_ms(), 60000, &held))
        ESP_LOGW(TAG, "command refused: %s (%u more since the last line)", why, (unsigned)held);
}

static void handle_inbound(const otml_inbound_t *m)
{
    const ot_mqtt_in_t in = ot_mqtt_decide(g_ctx.prefix, OT_HA_DISCOVERY_PREFIX "/status", m->topic,
                                           m->topic_len, m->payload, m->payload_len, m->retained);
    switch (in.kind) {
    case OT_MQTT_IGNORE:
        return;
    case OT_MQTT_HA_ONLINE:
        otml_rediscover();
        return;
    case OT_MQTT_REJECT:
        refuse(in.reason);
        return;
    case OT_MQTT_COMMAND: {
        const ot_mqtt_verdict_t v = ot_mqtt_handle(&in, &FX);
        if (v.accepted)
            bump(&s_commands);
        else
            refuse(v.reason != NULL ? v.reason : "refused");
        return;
    }
    case OT_MQTT_ROOM:
        fx_room(in.value);
        return;
    }
}

static void drain(void)
{
    otml_inbound_t m;
    while (g_inbox != NULL && xQueueReceive(g_inbox, &m, 0) == pdTRUE)
        handle_inbound(&m);
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}

// Field by field, never the struct: its padding is not ours to hash, and a fingerprint that moves
// by itself would restart the client every second.
static uint32_t fingerprint(const ot_net_broker_t *b)
{
    uint32_t h = 2166136261u;
    h = fnv(h, b->host, strlen(b->host) + 1);
    h = fnv(h, &b->port, sizeof b->port);
    h = fnv(h, b->user, strlen(b->user) + 1);
    h = fnv(h, b->password, strlen(b->password) + 1);
    h = fnv(h, b->topic_prefix, strlen(b->topic_prefix) + 1);
    h = fnv(h, b->device_name, strlen(b->device_name) + 1);
    return fnv(h, &b->ha_discovery, sizeof b->ha_discovery);
}

// A changed setting restarts the client: stop (which says `offline` on the old status topic), a
// second drain against the context those messages arrived under, start. A cleared host leaves it
// off -- not a fault.
static void follow_settings(void)
{
    ot_net_broker(&s_broker);
    const uint32_t fp = fingerprint(&s_broker);
    if (!s_fp_known || fp != s_fp) {
        s_fp       = fp;
        s_fp_known = true;
        otml_client_stop();
        drain();
        s_configured        = s_broker.host[0] != '\0';
        const esp_err_t err = otml_client_start(&s_broker);
        if (err == ESP_ERR_NOT_FOUND)
            ESP_LOGI(TAG, "no broker configured; MQTT stays off");
        else if (err != ESP_OK)
            ESP_LOGE(TAG, "client did not start: %s", esp_err_to_name(err));
    }
    explicit_bzero(&s_broker, sizeof s_broker);
}

// The connection's story, told in bounded lines: each transition once; while it is down, one line
// an hour; a refusal when its code changes, then hourly. esp-mqtt's own per-attempt lines are
// silenced in ot_mqtt_link_start().
static void report(void)
{
    const bool up = g_connected;
    if (up && !s_was_up)
        ESP_LOGI(TAG, "broker connected");
    if (!up && s_was_up)
        ESP_LOGW(TAG, "broker lost; retrying every 10 s, nothing else changes");
    s_was_up = up;
    const uint32_t sub = atomic_load(&g_sub_refused);
    if (sub != s_sub_said) {
        ESP_LOGE(TAG, "the broker refused the command subscription: check its ACL");
        s_sub_said = sub;
    }
    if (up || !s_configured) {
        s_connack_said = 0;
        return;
    }
    const uint32_t code  = atomic_load(&g_last_connack);
    const bool     timer = ot_mqtt_quiet(code != 0 ? &s_q_conn : &s_q_down, now_ms(), HOUR_MS, NULL);
    if (code != 0 && (code != s_connack_said || timer)) {
        ESP_LOGE(TAG, "broker refused the connection: %s (code %u)", ot_wire_connack_name(code),
                 (unsigned)code);
        s_connack_said = code;
    } else if (code == 0 && timer && atomic_load(&g_attempts) > 0) {
        ESP_LOGW(TAG, "broker unreachable; %u attempts so far, retrying every 10 s",
                 (unsigned)atomic_load(&g_attempts));
    }
}

static void link_task(void *arg)
{
    (void)arg;
    bool busy = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(busy ? BUSY_MS : TICK_MS));
        // Commands first, whether or not the link is up: one that arrived just before a drop is
        // still a command, and it is judged against the context it arrived under.
        drain();
        follow_settings();
        if (otml_client_address_changed())
            otml_rediscover();
        busy = g_connected && otml_publish_pass();
        report();
        const uint32_t hwm = (uint32_t)uxTaskGetStackHighWaterMark(NULL);   // bytes in ESP-IDF
        if (hwm + 256 <= s_hwm_said) {
            ESP_LOGI(TAG, "stack high-water mark %u of %u bytes", (unsigned)hwm, STACK_BYTES);
            s_hwm_said = hwm;
        }
    }
}

esp_err_t ot_mqtt_link_start(void)
{
    if (g_task != NULL)
        return ESP_OK;
    // esp-mqtt, tcp_transport and esp-tls log every failed attempt at ERROR -- three lines every
    // ten seconds for as long as a broker is off, which would push everything else out of the ring
    // GET /api/log serves. report() says what matters, in bounded lines. DO NOT raise these back.
    static const char *const noisy[] = {"mqtt_client", "transport_base", "transport", "esp-tls",
                                        "outbox"};
    for (size_t i = 0; i < sizeof noisy / sizeof noisy[0]; i++)
        esp_log_level_set(noisy[i], ESP_LOG_NONE);
    if (g_inbox == NULL)
        g_inbox = xQueueCreate(INBOX_LEN, sizeof(otml_inbound_t));
    if (g_inbox == NULL)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(link_task, "ot_mqtt", STACK_BYTES, NULL, PRIORITY, &g_task) != pdPASS) {
        g_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ot_mqtt_link_status(ot_wire_mqtt_t *out)
{
    out->configured   = s_configured;
    out->connected    = g_connected;
    out->published    = atomic_load(&g_published);
    out->commands     = atomic_load(&s_commands);
    out->rejected     = atomic_load(&g_rejected);
    out->reconnects   = atomic_load(&g_reconnects);
    out->last_connack = atomic_load(&g_last_connack);
}
