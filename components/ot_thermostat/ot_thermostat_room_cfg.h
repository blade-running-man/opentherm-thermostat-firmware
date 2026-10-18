// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

// PRIVATE to components/ot_thermostat, and deliberately its OWN header (not folded into
// ot_thermostat_internal.h): that header pulls in ot_bus.h for otth_publish(), and ot_bus.h chains
// into ot_master/ot_net/ot_api -- none of them host-buildable. This header includes ONLY
// ot_config.h and ot_room.h, both pure, so test/test_ot_thermostat_room/ can reach the ONE
// function below without dragging in the rest of the executor's impure glue.
//
// ot_thermostat_room_cfg.c is native-buildable on its own; ot_thermostat.c, ot_thermostat_room.c,
// ot_thermostat_persist.c and ot_thermostat_report.c are not (FreeRTOS, esp_timer, NVS, the bus) --
// library.json's srcFilter keeps the native (env:native) build of this component to this one file,
// so a native test that reaches into components/ot_thermostat for this header does not also try to
// compile the FreeRTOS-dependent sources. DO NOT fold this declaration back into
// ot_thermostat_internal.h: that reintroduces the ot_bus.h chain into every host include of it.

#include "ot_config.h"   // ot_config_room_mqtt_t, the input
#include "ot_room.h"      // ot_room_cfg_t, the output

#ifdef __cplusplus
extern "C" {
#endif

// PURE: the config->ot_room mapping,
// host-tested on its own in test/test_ot_thermostat_room/ without the task, the mailbox or a
// spinlock. Slot 0 is always the DS18B20 shield (AMBIENT, not ha_forwarded, 60000 ms); slot 1 is
// the MQTT room source when `m->enable`, with its configured role/ha_forwarded/stale window
// (seconds -> ms), and is absent (count == 1) otherwise. DO NOT read config here for real -- `m`
// is a snapshot the caller already took (ot_config_room_mqtt()); a live read would make this
// impure and untestable, exactly what the cut was for.
void ot_thermostat_room_build_cfg(const ot_config_room_mqtt_t *m, ot_room_cfg_t *out);

#ifdef __cplusplus
}
#endif
