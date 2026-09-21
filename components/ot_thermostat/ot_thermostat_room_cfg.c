// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#include "ot_thermostat_room_cfg.h"

// Kept PURE on purpose (ot_thermostat_room_cfg.h says why): no FreeRTOS, no ESP-IDF, no logging --
// so it host-builds and host-tests on its own (test/test_ot_thermostat_room/).
void ot_thermostat_room_build_cfg(const ot_config_room_mqtt_t *m, ot_room_cfg_t *out)
{
    // Slot 0 = the DS18B20 shield, AMBIENT and not ha_forwarded (nothing forwards it): present
    // whatever the MQTT slot's configuration is: slot 0 is always the DS18B20 shield. 60000 ms matches the stale window
    // ds18b20_task used to run its own ot_sensor with before ot_room existed.
    out->cfg[0].role           = OT_ROOM_AMBIENT;
    out->cfg[0].ha_forwarded   = false;
    out->cfg[0].stale_after_ms = 60000;
    out->count                 = 1;

    if (!m->enable)
        return;   // count stays 1: the MQTT slot is absent, not merely unconfigured

    // Slot 1 = the MQTT room source, entirely from the live config snapshot: role/ha_forwarded
    // verbatim, stale_s converted seconds -> ms (ot_room_slot_cfg_t's unit, ot_sensor_init()'s).
    out->cfg[1].role           = m->role != 0 ? OT_ROOM_ROOM : OT_ROOM_AMBIENT;
    out->cfg[1].ha_forwarded   = m->ha_forwarded;
    out->cfg[1].stale_after_ms = (uint32_t)m->stale_s * 1000u;
    out->count                 = 2;
}
