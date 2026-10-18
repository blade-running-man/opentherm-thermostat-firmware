// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Private to components/ot_config: the NVS helpers the device-side sources share.
//
// Beside the sources and NOT in include/, for the reason ot_config_internal.h gives: nothing
// outside this directory may reach these names. Each was a file-local static of ot_config_nvs.c
// and is exported only because the device half became four files; the
// ot_config_io_ prefix is what keeps them from colliding in the firmware link. The two rules at
// the top of ot_config_nvs.h -- no ESP_ERROR_CHECK on anything from flash, the Wi-Fi pair as one
// blob -- bind these helpers exactly as they bind the public functions.
//
// Device only. Every includer sits inside #ifdef ESP_PLATFORM (the reason is at the top of
// ot_config_nvs.c), and so does this header, so that a host file including it by mistake gets
// nothing rather than a missing nvs.h.
#pragma once

#ifdef ESP_PLATFORM

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nvs.h"
#include "ot_config_nvs.h"

// The one log tag of this component. Every file that includes this header logs through it.
static const char *TAG = "cfg";

// Logs a failed store by key NAME, never by value; the reason is at the definition.
void ot_config_io_warn_store(const char *key, esp_err_t err);

// The NVS key of a field, or NULL for an enumerator with no row -- which every caller treats as
// "do not persist", never as a dereference at boot.
const char *ot_config_io_key_of(ot_config_field_t field);

// Reading. The default already in `out` stands whenever the key is absent or unreadable; only the
// _str_field variant reports a value it had to drop, into *lost.
void ot_config_io_load_str_field(nvs_handle_t handle, ot_config_field_t field, char *out,
                                 size_t cap, ot_config_repairs_t *lost);
void ot_config_io_load_u16(nvs_handle_t handle, const char *key, uint16_t *out);
void ot_config_io_load_bool(nvs_handle_t handle, const char *key, bool *out);
// The Wi-Fi record, whole or not at all: false leaves *out untouched.
bool ot_config_io_load_wifi(nvs_handle_t handle, const char *key, ot_wifi_t *out);

// Writing, one field at a time: each warns under its own key and keeps the FIRST failure in
// *first. None commits -- the caller commits once per namespace.
void ot_config_io_set_str_field(nvs_handle_t handle, ot_config_field_t field, const char *value,
                                esp_err_t *first);
void ot_config_io_set_u16_field(nvs_handle_t handle, ot_config_field_t field, uint16_t value,
                                esp_err_t *first);
void ot_config_io_set_bool_field(nvs_handle_t handle, ot_config_field_t field, bool value,
                                 esp_err_t *first);
// Erases one key by NAME -- the retired keys have no field. ESP_ERR_NVS_NOT_FOUND comes back as
// ESP_OK. Does not commit.
esp_err_t ot_config_io_erase_key(nvs_handle_t handle, const char *key);
// Every retired key in ot_config_retired_key()'s list, one at a time under its own name. Shared by
// ot_config_nvs_load.c (the migration) and ot_config_nvs_save.c (every save); see the definition
// in ot_config_nvs_io.c for why it runs on every writable boot and not only while migrating.
esp_err_t ot_config_io_erase_retired(nvs_handle_t app);

#endif  // ESP_PLATFORM
