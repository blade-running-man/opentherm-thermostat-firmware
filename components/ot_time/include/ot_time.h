// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

// The wall clock: SNTP and the time zone.
//
// Nothing in the firmware reads the wall clock yet (SNTP dead: nothing changes):
// ot_time_now() has no caller, and the log's timestamps are milliseconds since boot
// (CONFIG_LOG_TIMESTAMP_SOURCE_RTOS on both boards), not the time of day. The last_failsafe_at
// value will be the first reader. Every clock the firmware keeps is monotonic milliseconds, and
// deliberately so -- a clock that can jump backwards is not a thing to build a poll ring or a
// watchdog on.
//
// FAILURE: there is no failing call here. An unreachable NTP server leaves ot_time_now()
// answering false for ever. NOTHING REBOOTS BECAUSE SNTP IS UNREACHABLE -- that rule is
// carried over from the source project, where it was a consequence of a real defect.

#ifdef __cplusplus
extern "C" {
#endif

// The NTP server is a host name, and it shares ot_config's cap for one rather than growing a
// second opinion about how long a host name may be.
#define OT_TIME_SERVER_MAX 253

typedef struct {
    uint8_t  weekday;         // 0 = MONDAY. See the note on ot_time_now().
    uint16_t minute_of_day;   // 0..1439, local time
    int64_t  epoch;           // seconds since 1970-01-01 UTC
} ot_wallclock_t;

// DO NOT include this header from a pure component.
//
// PlatformIO's dependency finder links a library because a header of it was included, and it
// then compiles ALL of that library's sources -- so one #include here would drag
// esp_netif_sntp.h into a host suite and stop it building. That is not a hypothetical: it is
// found by running the ot_bus_sched host suite, which failed to build for exactly this reason.

// Applies a POSIX TZ string (handed to setenv() verbatim; NULL or "" is UTC). EVERY call applies
// it -- there is no "already started" guard, which is what made a saved zone wait for a power
// cycle in 5a. ot_thermostat calls it from its once-a-second configuration poll whenever the
// stored zone differs from the one it last applied. Any task; returns at once.
void ot_time_set_zone(const char *tz);

// Starts SNTP against `ntp_server` (a host name, copied). Idempotent: the second and later calls
// do nothing, because the network callback fires on every reconnection and esp_netif_sntp_init()
// must be called once.
//
// A CHANGED SERVER THEREFORE TAKES EFFECT AT THE NEXT BOOT, deliberately. Re-initialising SNTP
// (esp_netif_sntp_deinit() then init) from the thermostat's poll would race this function on the
// network task's reconnect callback, and would need a lock here for a value nothing reads yet:
// ot_time_now() has no caller, and the log's timestamps do not come from it (the top of this
// file).
//
// RETURNS IMMEDIATELY. Silence from the master longer than 5 s makes the boiler treat the
// thermostat as short-circuited and go into a demand for heat, so falling silent is the hottest
// state and not a safe one. DO NOT add esp_netif_sntp_sync_wait() to this function or to
// anything the bus waits behind.
void ot_time_start(const char *ntp_server);

// False until SNTP has synchronised at least once, and true afterwards.
//
// VALIDITY IS "A SYNC HAPPENED", not "time() returned something". A device that never reached an
// NTP server returns 1970 with complete confidence, and a threshold on the epoch would call that
// invalid by luck rather than by knowledge -- and would call a genuinely wrong clock valid.
//
// weekday is 0 = MONDAY, which struct tm is NOT: tm_wday is 0 = Sunday. The conversion happens
// here, once, because an off-by-one in a weekday is a schedule that fires on the wrong day.
bool ot_time_now(ot_wallclock_t *out);

#ifdef __cplusplus
}
#endif
