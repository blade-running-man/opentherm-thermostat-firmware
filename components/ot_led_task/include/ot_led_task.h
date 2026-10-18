// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "board.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the low-priority status-LED task. Reader-only: NEVER writes an OpenTherm frame,
// never takes the bus lock, never blocks on the network -- a WiFi/MQTT/HA failure only
// changes the colour shown. Safe to call only when board->rgb.gpio >= 0 (caller guards).
esp_err_t ot_led_task_start(const board_t *board);

// Dormant in v1 (no OTA subsystem yet). A future OTA path calls this true around the write so
// the LED shows the fast-blue "do not power off" pattern. Backed by an atomic, read by the task.
void ot_led_note_ota(bool active);

#ifdef __cplusplus
}
#endif
