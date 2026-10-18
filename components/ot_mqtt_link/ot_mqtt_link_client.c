// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The client's birth, death and event handler (ot_mqtt_link_internal.h). Everything here but the
// handler runs on the link task; the handler runs on esp-mqtt's task, inside esp-mqtt's API lock.
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "ot_mqtt_link_internal.h"

static const char *TAG = "mqtt";

esp_mqtt_client_handle_t g_client;
volatile bool            g_connected;
volatile bool            g_republish;
ot_ha_ctx_t              g_ctx;
bool                     g_discovery;

// The strings g_ctx points into, written by otml_client_start() and otml_client_address_changed()
// only -- both on the task.
static char s_prefix[OT_CONFIG_PREFIX_MAX + 1];
static char s_name[OT_CONFIG_NAME_MAX + 1];
static char s_id[OT_CONFIG_DEVICE_ID_LEN + 1];
static char s_mac[18];
static char s_ip[16];
static char s_sw[32];
static char s_uri[OT_CONFIG_HOST_MAX + 16];
static char s_will[OT_MQTT_TOPIC_MAX];

// SETS FLAGS AND COPIES BYTES. Nothing else, ever: no log, no lock, no publish, no wait -- see rule
// one in ot_mqtt_link.h. The retain flag is handed over untouched; the decision is pure.
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        bump(&g_attempts);
        break;
    case MQTT_EVENT_CONNECTED:
        g_connected = true;
        g_republish = true;
        bump(&g_reconnects);
        atomic_store(&g_last_connack, 0);
        atomic_store(&g_attempts, 0);
        if (g_task != NULL)
            xTaskNotifyGive(g_task);
        break;
    case MQTT_EVENT_DISCONNECTED:
        g_connected = false;
        break;
    case MQTT_EVENT_DATA: {
        // A fragmented message is refused whole (its first fragment counted): a truncated command
        // is a different command. So is one that would fill a buffer.
        if (ev->current_data_offset != 0)
            break;
        if (g_inbox == NULL || ev->total_data_len != ev->data_len || ev->data_len < 0 ||
            (size_t)ev->data_len >= OT_MQTT_PAYLOAD_MAX || ev->topic_len <= 0 ||
            (size_t)ev->topic_len >= OT_MQTT_TOPIC_MAX) {
            bump(&g_rejected);
            break;
        }
        otml_inbound_t in;
        memset(&in, 0, sizeof in);
        memcpy(in.topic, ev->topic, (size_t)ev->topic_len);
        in.topic_len = (size_t)ev->topic_len;
        memcpy(in.payload, ev->data, (size_t)ev->data_len);
        in.payload_len = (size_t)ev->data_len;
        in.retained = ev->retain;
        // NEVER waits: a full inbox drops the command -- applying it late is worse.
        if (xQueueSend(g_inbox, &in, 0) != pdTRUE)
            bump(&g_rejected);
        else if (g_task != NULL)
            xTaskNotifyGive(g_task);
        break;
    }
    case MQTT_EVENT_SUBSCRIBED:
        // WHERE ESP-MQTT PUTS IT, and it is not where one would look: a SUBACK return code with
        // bit 7 set -- the broker's ACL refusing <prefix>/+/set -- sets error_type
        // SUBSCRIBE_FAILED and then dispatches MQTT_EVENT_SUBSCRIBED (mqtt_client.c, the SUBACK
        // branch of mqtt_process_receive). MQTT_EVENT_ERROR never carries it, so a handler that
        // reads error_handle only there counts nothing and the device is silently deaf to every
        // command. DO NOT move this into MQTT_EVENT_ERROR.
        if (ev->error_handle != NULL &&
            ev->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED)
            bump(&g_sub_refused);
        break;
    case MQTT_EVENT_ERROR:
        // A refused CONNACK is not an unreachable broker: the owner must read a different line.
        if (ev->error_handle != NULL &&
            ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
            atomic_store(&g_last_connack, (uint32_t)ev->error_handle->connect_return_code);
        break;
    default:
        break;
    }
}

esp_err_t otml_client_start(const ot_net_broker_t *b)
{
    if (g_client != NULL)
        return ESP_ERR_INVALID_STATE;
    if (b->host[0] == '\0')
        return ESP_ERR_NOT_FOUND;

    ot_net_device_id(s_id);
    ot_net_mac_string(s_mac);
    ot_net_ip_string(s_ip);
    snprintf(s_prefix, sizeof s_prefix, "%s", b->topic_prefix);
    snprintf(s_name, sizeof s_name, "%s", b->device_name);
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(s_sw, sizeof s_sw, "%s", app != NULL ? app->version : "");
    g_ctx = (ot_ha_ctx_t){s_prefix, s_id, s_mac, s_name, app != NULL ? app->project_name : "",
                          s_sw, s_ip};
    g_discovery = b->ha_discovery;
    if (ot_mqtt_topic(s_prefix, NULL, "status", s_will, sizeof s_will) == 0)
        return ESP_ERR_INVALID_ARG;
    snprintf(s_uri, sizeof s_uri, "mqtt://%s:%u", b->host, (unsigned)b->port);

    esp_mqtt_client_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.broker.address.uri = s_uri;
    cfg.credentials.client_id = s_id;   // the full MAC, so two devices never evict each other
    if (b->user[0] != '\0') {
        cfg.credentials.username                = b->user;
        cfg.credentials.authentication.password = b->password;
    }
    cfg.session.last_will.topic  = s_will;
    cfg.session.last_will.msg    = OT_MQTT_OFFLINE;
    cfg.session.last_will.qos    = OT_MQTT_QOS_META;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive        = 60;      // the will reaches HA within about 90 s of a death
    cfg.network.reconnect_timeout_ms = 10000;
    // DO NOT set disable_auto_reconnect: reconnecting for ever, without a reboot, is the required
    // behaviour (a broker outage must not reboot the device).
    cfg.task.priority   = 3;   // esp-mqtt's own task, beside ours: below everything that matters
    cfg.task.stack_size = 6144;
    cfg.buffer.size     = 1024;                                   // inbound: commands are short
    cfg.buffer.out_size = OT_HA_DOC_MAX + OT_MQTT_TOPIC_MAX + 16;  // a whole discovery document
    cfg.outbox.limit    = 16384;   // bounded memory: a QoS 1 publish beyond it fails, and is owed

    // esp_mqtt_client_init() copies the uri, the credentials, the client id and the will topic
    // (esp_mqtt_set_config, mqtt_client.c): the caller wipes its copy of the password next.
    g_client = esp_mqtt_client_init(&cfg);
    if (g_client == NULL)
        return ESP_ERR_NO_MEM;
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, on_event, NULL);
    const esp_err_t err = esp_mqtt_client_start(g_client);
    if (err != ESP_OK) {
        otml_client_stop();
        return err;
    }
    // The host and the prefix, NEVER the user or the password.
    ESP_LOGI(TAG, "broker %s, prefix %s, discovery %s", s_uri, s_prefix, g_discovery ? "on" : "off");
    return ESP_OK;
}

void otml_client_stop(void)
{
    if (g_client == NULL)
        return;
    // A broker does not publish the will for a clean DISCONNECT, so say `offline` ourselves, or a
    // cleared broker or a changed prefix leaves `online` retained for ever. Best effort: QoS 1
    // without waiting for the PUBACK.
    if (g_connected)
        esp_mqtt_client_publish(g_client, s_will, OT_MQTT_OFFLINE, 0, OT_MQTT_QOS_META, 1);
    esp_mqtt_client_stop(g_client);
    esp_mqtt_client_destroy(g_client);
    g_client = NULL;
    // AFTER the stop returned: until then the esp-mqtt task may still dispatch an event that sets
    // these for a client already being destroyed.
    atomic_store(&g_last_connack, 0);
    g_connected = false;
    g_republish = false;
}

bool otml_client_address_changed(void)
{
    if (g_client == NULL)
        return false;
    char ip[16];
    ot_net_ip_string(ip);
    if (strcmp(ip, s_ip) == 0)
        return false;
    memcpy(s_ip, ip, sizeof s_ip);
    return true;
}
