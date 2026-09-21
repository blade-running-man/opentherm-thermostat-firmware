// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// What an arriving MQTT message means (ot_mqtt.h, ot_mqtt_decide). Runs on a message from
// anything that can reach the broker, before anything has decided it is ours: every read is
// bounded by a length, never by a terminator.
#include "ot_mqtt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static ot_mqtt_in_t reject(const char *why)
{
    ot_mqtt_in_t r = {OT_MQTT_REJECT, why, {0}, 0.0f};
    return r;
}

// The strict grammar of ot_mqtt.h: -?[0-9]+(\.[0-9]+)? and nothing else. Checked BEFORE strtof, so
// strtof's own leniency -- leading spaces, "inf", hex, an exponent, a partial parse -- never runs.
static bool number(const char *s, size_t n, float *out)
{
    if (n == 0 || n >= OT_MQTT_PAYLOAD_MAX)
        return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    size_t whole = 0;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++)
        whole++;
    if (whole == 0)
        return false;
    if (i < n && s[i] == '.') {
        size_t frac = 0;
        for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++)
            frac++;
        if (frac == 0)
            return false;
    }
    if (i != n)
        return false;
    char buf[OT_MQTT_PAYLOAD_MAX];
    memcpy(buf, s, n);
    buf[n] = '\0';
    *out = strtof(buf, NULL);
    return isfinite(*out);
}

ot_mqtt_in_t ot_mqtt_decide(const char *prefix, const char *ha_status, const char *topic,
                            size_t topic_len, const char *payload, size_t payload_len,
                            bool retained)
{
    ot_mqtt_in_t r = {OT_MQTT_IGNORE, NULL, {0}, 0.0f};
    if (prefix == NULL || topic == NULL || (payload == NULL && payload_len != 0))
        return r;

    // HA's birth. `offline` is NOT used: HA's will also fires on a clean shutdown, so
    // every HA update would otherwise light the burner at the failsafe setpoint.
    const size_t hs = ha_status != NULL ? strlen(ha_status) : 0;
    if (hs != 0 && topic_len == hs && memcmp(topic, ha_status, hs) == 0) {
        if (payload_len == strlen(OT_MQTT_ONLINE) &&
            memcmp(payload, OT_MQTT_ONLINE, payload_len) == 0)
            r.kind = OT_MQTT_HA_ONLINE;
        return r;
    }

    // <prefix>/room/state: matched EXACTLY, byte-length-bounded like the /set builder below
    // -- NOT NUL-assumed, since topic is esp-mqtt's own buffer, read by length only. Ordered
    // before /set so a (nonexistent) key named "room" could never shadow it. A retained reading
    // is refused for the same reason a retained command is: replayed, it would look as fresh as
    // one just published, and this source is about to steer the failsafe -- a stale value
    // must not masquerade as fresh and hold a heating target nobody chose.
    const size_t plen = strlen(prefix);
    {
        static const char room[] = "/room/state";
        const size_t      rlen   = plen + (sizeof room - 1);
        if (plen != 0 && topic_len == rlen && memcmp(topic, prefix, plen) == 0 &&
            memcmp(topic + plen, room, sizeof room - 1) == 0) {
            if (retained)
                return reject("a retained room reading is refused");
            float value = 0.0f;
            if (!number(payload, payload_len, &value))
                return reject("the payload is not a number");
            r.kind  = OT_MQTT_ROOM;
            r.value = value;
            return r;
        }
    }

    // <prefix>/<key>/set, with a key of at least one byte and no level inside it.
    const size_t tail = strlen("/set");
    if (plen == 0 || topic_len < plen + 1 + 1 + tail || memcmp(topic, prefix, plen) != 0 ||
        topic[plen] != '/' || memcmp(topic + topic_len - tail, "/set", tail) != 0)
        return r;
    const char  *key  = topic + plen + 1;
    const size_t klen = topic_len - plen - 1 - tail;
    // '/' would be a topic level this component does not serve; '\0' would be read as the end of
    // the key by everything downstream, so "<prefix>/ch_enable\0x/set" would become ch_enable.
    if (klen >= OT_MQTT_KEY_MAX || memchr(key, '/', klen) != NULL ||
        memchr(key, '\0', klen) != NULL)
        return r;

    if (retained)
        return reject("a retained command is ignored");
    float value = 0.0f;
    if (!number(payload, payload_len, &value))
        return reject("the payload is not a number");

    r.kind = OT_MQTT_COMMAND;
    memcpy(r.key, key, klen);
    r.key[klen] = '\0';
    r.value     = value;
    return r;
}
