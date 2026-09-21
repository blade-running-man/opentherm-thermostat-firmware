// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ot_control.h"

// GET /api/control: what the executor is doing and why. It absorbs the mode byte and boost
// block of the former GET /api/climate.
//
// PURE, and fed a struct rather than reaching for the thermostat: ot_api is compiled whole into
// the host suite test_api, and one include of ot_thermostat.h would drag FreeRTOS into it -- the
// rake recorded for ot_bus_sched and ot_sensor. The task layer gathers
// (ot_thermostat_control_get()); this renders. ot_control.h is pure, so including it drags nothing.
//
// snprintf semantics, exactly as ot_api.h: the REQUIRED size without the terminating zero is
// returned, the buffer is never overrun and is always left a string. A caller that ran out of
// room answers 500 rather than sending a truncated document.
//
// Every string printed is a literal of this component or of ot_control_names.c; nothing from a
// client or from the configuration document is formatted in, so the no-escaping rule of ot_api.h
// holds by construction. Temperatures are integer tenths (_dc), as in /api/config: no float is
// formatted, so there is no "nan" to guard against and no second rounding between the executor
// and the page.

#ifdef __cplusplus
extern "C" {
#endif

// The version of THIS document's shape, not OT_SCHEMA_VERSION (the registry table's).
#define OT_API_CONTROL_SCHEMA 1

typedef struct {
    ot_control_mode_t   mode;
    ot_control_state_t  state;
    ot_control_reason_t reason;           // what the CH bit is doing
    ot_control_reason_t cause;            // why the state is failsafe; NONE in every other state
    bool     heating_season;
    uint8_t  status_high;                 // what the task last handed the bus: ASKED, not confirmed
    int16_t  held_setpoint_dc;            // the held ID 1, always valid
    bool     dhw_enable;
    bool     dhw_setpoint_set;
    int16_t  dhw_setpoint_dc;             // read only while dhw_setpoint_set
    bool     boost_active;
    int16_t  boost_setpoint_dc;           // read only while boost_active
    uint32_t boost_remaining_s;           // read only while boost_active
    uint32_t failsafe_count;              // since this boot
    uint32_t last_failsafe_duration_s;
    uint32_t watchdog_overdue_s;
    bool     stack_known;                 // false until the task has measured itself
    uint32_t stack_hwm;                   // bytes of the thermostat task's stack never used
} ot_api_control_t;

// {"schema":1,"mode":"local","state":"boost","reason":"none","cause":"none",
//  "heating_season":true,"status_high":3,"held_setpoint_dc":500,
//  "dhw":{"enable":true,"setpoint_dc":505},
//  "boost":{"active":true,"setpoint_dc":500,"remaining_s":3540},
//  "failsafe":{"count":2,"last_duration_s":61},"watchdog_overdue_s":7,"stack_hwm":1184}
//
// An absent value is null, never 0: no boost is not a boost whose time is up, an unset DHW
// setpoint is not 0 °C, an unmeasured stack is not an untouched one.
size_t ot_api_render_control(const ot_api_control_t *c, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
