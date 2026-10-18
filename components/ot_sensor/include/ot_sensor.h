// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The room-temperature source: an outlier filter and a three-state freshness machine.
//
// The measurement that matters is taken in the room, from a source the device does not control
// (MQTT, WiFi, BLE, or the shield's own DS18B20), which makes a sensor going
// stale a first-class state rather than an edge case, and makes the value itself untrusted.
// ot_room keeps one of these per room source.
//
// Pure: no timers, no tasks, no allocation. Time enters only as an argument, which is what
// lets the 24-hour and 49-day behaviour be tested on the host in milliseconds.
//
// OWNERSHIP: the caller owns the ot_sensor_t and every call takes it explicitly. There is
// no global instance and no locking -- each ot_sensor_t instance is owned by exactly one
// task and is never shared (ot_thermostat and the DS18B20 reader each own their own); a
// second caller from another task touching the same instance is the caller's problem to
// serialise.
//
// FAILURE: nothing here can fail. ot_sensor_update() returning false is not an error, it
// is the filter having done its job; the caller may count the refusals but has nothing to
// handle.

#ifdef __cplusplus
extern "C" {
#endif

#define OT_SENSOR_MIN_C          (-40.0f)   // outside this, always rejected
#define OT_SENSOR_MAX_C           (60.0f)
#define OT_SENSOR_MAX_JUMP_C       (5.0f)   // more than this from the last accepted value
#define OT_SENSOR_MAX_JUMP_REJECTS     3u   // ... at most this many times in a row

typedef enum {
    OT_SENSOR_NEVER = 0,   // no value has been accepted since boot
    OT_SENSOR_FRESH,       // the last accepted value is younger than stale_after_ms
    OT_SENSOR_STALE,       // it is not
} ot_sensor_state_t;

// NEVER is not folded into STALE although the loop gives the same behaviour for both:
// only one of the two means "this device has never had a room measurement", and the panel
// has to be able to say which (the room_temperature_actual entity is unavailable in both
// states, and the person needs to know whether to look for a broken sensor or an unfinished
// setup).

typedef struct {
    uint32_t stale_after_ms;
    float    value;            // the last accepted value; NaN until there is one
    bool     have_value;

    // Overdue time is ACCUMULATED here by ot_sensor_tick(), never computed as
    // now - last_ok. See the comment on ot_sensor_tick().
    uint32_t overdue_ms;       // saturating at UINT32_MAX
    uint32_t last_tick_ms;     // the reference moment of the accumulator
    bool     tick_seeded;      // false until the first tick or update has set it

    uint8_t  reject_streak;    // consecutive rejections by the jump filter
} ot_sensor_t;

// stale_after_ms is the FRESH deadline: state becomes STALE once the accumulated overdue
// time reaches it (the boundary belongs to STALE). Default 900 000 -- 15 minutes. The
// struct is zeroed, so an ot_sensor_t is safe to read before any
// value has arrived: NEVER, NaN, zero overdue.
void ot_sensor_init(ot_sensor_t *s, uint32_t stale_after_ms);

// Offer a measurement. Returns false when it was rejected, and a rejected value changes
// nothing except the reject counter -- in particular it does NOT refresh the deadline, or
// a sensor stuck on +85 would read as FRESH for ever.
//
// AN UPDATE IS ALSO A TICK: it carries a timestamp, so it advances the accumulator by the
// interval since the previous tick or update before deciding. The caller is therefore free
// to call only ot_sensor_update() on the arrival path and only ot_sensor_tick() on the
// periodic one; calling both at the same instant counts the interval once.
//
// The filter, all three parts:
//   * NaN and infinity are rejected;
//   * a value outside OT_SENSOR_MIN_C..OT_SENSOR_MAX_C is rejected, ALWAYS -- this alone
//     catches the +85 and -127 a real DS18B20 emitted in prior-art-diyless.md;
//   * a value more than OT_SENSOR_MAX_JUMP_C from the last accepted one is rejected, but
//     at most OT_SENSOR_MAX_JUMP_REJECTS times in a row -- the next one is accepted
//     whatever it says. DO NOT remove that escape hatch: a sensor that was moved to
//     another room, or replaced, would otherwise lock the loop out of the room
//     temperature permanently, and a thermostat that cannot be corrected is a worse
//     failure than one that briefly believes an outlier.
// The counter is reset by an accepted value, and only by one; an out-of-range rejection
// never advances it, so no run of +85 can ever reach the escape hatch.
bool ot_sensor_update(ot_sensor_t *s, float celsius, uint32_t now_ms);

// Advance time without offering a value. Called from the loop's periodic step.
//
// The overdue time is accumulated as a sum of wrap-safe deltas and NOT as now - last_ok.
// uint32 monotonic milliseconds wrap after 49.7 days while the hard STALE limit is
// 24 hours, so a subtraction spanning the wrap reads as "the sensor is fresh" -- in
// January, with nobody watching. The accumulator has no such moment: it only ever grows,
// and it saturates at UINT32_MAX (49.7 days) rather than wrapping.
//
// DO NOT "simplify" this back into storing the moment of the last accepted value and
// subtracting. That is the defect this shape exists to make unrepresentable.
//
// now_ms EARLIER THAN THE PREVIOUS CALL: indistinguishable from a wrap, because the two
// look identical in unsigned arithmetic, and it is therefore read as a forward jump of
// nearly 49 days -- the accumulator saturates and the sensor latches STALE until the next
// accepted value clears it. That is the safe direction (STALE never switches on
// heating that is off) and it needs no threshold that could mask a genuine wrap. The
// caller is obliged to pass a monotonic clock; esp_timer_get_time()/1000 is one.
void ot_sensor_tick(ot_sensor_t *s, uint32_t now_ms);

ot_sensor_state_t ot_sensor_state(const ot_sensor_t *s);

// NaN until a value has been accepted, and the last accepted value afterwards -- including
// while STALE. Deciding what a stale value is worth belongs to the consumer (ot_room),
// which receives the state alongside it; erasing it here would take that
// decision away.
float ot_sensor_value(const ot_sensor_t *s);

// Milliseconds since the last accepted value, or since the first tick when there has never
// been one. The consumer compares it against a limit of its own (ot_room does, against a
// 24-hour hard STALE limit).
uint32_t ot_sensor_overdue_ms(const ot_sensor_t *s);

#ifdef __cplusplus
}
#endif
