// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The field-by-field helpers of the device half: read one value, write one value, warn about one
// key. Shared by the document (ot_config_nvs.c) and the ways back (ot_config_nvs_recover.c)
// through ot_config_nvs_internal.h. Compiled out on the host, for the reason at the top of
// ot_config_nvs.c.
#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "nvs.h"

#include "ot_config_nvs_internal.h"

// What may never reach a log line is a VALUE for which ot_secret_key() is true: logging is
// captured whole and served by /api/log to whoever can reach the device. A
// key NAME is not a value, and it is exactly what the owner needs to know when a store fails --
// so names appear below and nothing else does.
void ot_config_io_warn_store(const char *key, esp_err_t err)
{
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not store %s: %s", key, esp_err_to_name(err));
}

// --- reading -----------------------------------------------------------------------------------

// The default already in `out` stands when the key is absent, unreadable, or longer than the
// field. FALSE says a value was there and this build could not take it, which is not the same
// fact and does not have the same answer: an absent key on a fresh device is nothing to report,
// while a stored broker address that does not fit becomes "no broker at all" and the
// owner has to be told, because settings that vanish silently are indistinguishable from a
// firmware that reset itself. Nothing else notices this one -- what replaces
// the value is a perfectly valid default, so ot_config_sanitize() sees nothing wrong.
static bool load_str(nvs_handle_t handle, const char *key, char *out, size_t cap)
{
    if (key == NULL)
        return true;
    // The length first, with a NULL buffer, which is the documented way to ask (nvs.h,
    // nvs_get_str). It is also what turns "stored by a firmware with bigger fields" into a
    // decision instead of an accident, and it saves carrying a second buffer on a task stack.
    size_t          len   = 0;
    const esp_err_t probe = nvs_get_str(handle, key, NULL, &len);
    if (probe == ESP_ERR_NVS_NOT_FOUND)
        return true;  // never written: the default is the answer, and nothing was lost
    if (probe != ESP_OK)
        return false;  // it is in there and the store would not say what it is
    // len counts the terminator. A value that does not fit is DROPPED, never truncated: half a
    // broker password authenticates against nothing and looks exactly like a broker that changed
    // its credentials.
    if (len == 0 || len > cap)
        return false;
    if (nvs_get_str(handle, key, out, &len) != ESP_OK)
        return false;
    // The window in which this struct holds an unterminated string is zero bytes wide. Not
    // instead of ot_config_sanitize() -- that is the real backstop and it runs over
    // everything this function fills -- but a terminator costs nothing here.
    out[cap - 1] = '\0';
    return true;
}

// Everything below addresses NVS by FIELD, never by a bare string, so the table in
// ot_config.c is the only place a key name is written down. The NULL check is not
// defensive noise: ot_config_field() answers NULL for an enumerator that was added without
// a row, and a NULL dereference at BOOT is the one failure this component may never have -- an
// unnamed field must simply not persist. The name pinned by test_config_names is what stops that
// happening quietly.
const char *ot_config_io_key_of(ot_config_field_t field)
{
    const ot_config_field_info_t *f = ot_config_field(field);
    return f != NULL ? f->key : NULL;
}

// Addressed by FIELD so the repair bit and the NVS key cannot drift apart -- the bitmask uses the
// same enumerator that names the key, which is why it can be told to the owner at all.
void ot_config_io_load_str_field(nvs_handle_t handle, ot_config_field_t field, char *out,
                                 size_t cap, ot_config_repairs_t *lost)
{
    if (!load_str(handle, ot_config_io_key_of(field), out, cap))
        *lost |= OT_CONFIG_REPAIRED(field);
}

void ot_config_io_load_u16(nvs_handle_t handle, const char *key, uint16_t *out)
{
    if (key == NULL)
        return;
    uint16_t value = 0;
    if (nvs_get_u16(handle, key, &value) == ESP_OK)
        *out = value;
}

void ot_config_io_load_bool(nvs_handle_t handle, const char *key, bool *out)
{
    if (key == NULL)
        return;
    // Read as u8 and normalised, never as a bool. A _Bool holding a bit pattern that is neither
    // 0 nor 1 is not a value a C program may read at all, and flash can hand back 0xff -- so a
    // blob straight into a struct with a bool in it is undefined before anything has validated
    // anything. This is also why the document is written field by field rather than as one blob.
    uint8_t value = 0;
    if (nvs_get_u8(handle, key, &value) == ESP_OK)
        *out = value != 0;
}

// The pair, whole or not at all. A blob of any other size was written by a firmware whose
// ot_wifi_t is not this one, and reinterpreting it field by field is how a length byte
// becomes an SSID length. The size IS the version here; there is no room in the record for one
// and no need, because the two lengths are already at fixed offsets and any change to them
// changes the size.
bool ot_config_io_load_wifi(nvs_handle_t handle, const char *key, ot_wifi_t *out)
{
    ot_wifi_t record;
    size_t          len = sizeof record;
    if (nvs_get_blob(handle, key, &record, &len) != ESP_OK)
        return false;
    if (len != sizeof record)
        return false;
    *out = record;
    return true;
}

// --- writing ------------------------------------------------------------------------------------

// One field, one warning, and the first failure kept. Written as three helpers rather than a
// chain of `if (e == ESP_OK)` because that chain reports the FIRST field's error against the
// SECOND field's name, and a warning that blames the wrong setting is worse than none.
void ot_config_io_set_str_field(nvs_handle_t handle, ot_config_field_t field, const char *value,
                                esp_err_t *first)
{
    const char *key = ot_config_io_key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_str(handle, key, value);
    ot_config_io_warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

void ot_config_io_set_u16_field(nvs_handle_t handle, ot_config_field_t field, uint16_t value,
                                esp_err_t *first)
{
    const char *key = ot_config_io_key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_u16(handle, key, value);
    ot_config_io_warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

void ot_config_io_set_bool_field(nvs_handle_t handle, ot_config_field_t field, bool value,
                                 esp_err_t *first)
{
    // Stored as u8; see ot_config_io_load_bool() for why a bool never goes near flash in
    // either direction.
    const char *key = ot_config_io_key_of(field);
    if (key == NULL)
        return;
    const esp_err_t e = nvs_set_u8(handle, key, value ? 1 : 0);
    ot_config_io_warn_store(key, e);
    if (*first == ESP_OK)
        *first = e;
}

// A key that is not there is the answer asked for, not a failure: a fresh device and a device whose
// migration already ran take the same path. Anything else is warned about by
// key NAME, like every store, and handed back so the caller does not mark the migration done.
esp_err_t ot_config_io_erase_key(nvs_handle_t handle, const char *key)
{
    const esp_err_t e = nvs_erase_key(handle, key);
    if (e == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (e != ESP_OK)
        ESP_LOGW(TAG, "could not erase %s: %s", key, esp_err_to_name(e));
    return e;
}

// Every retired key, by name and one at a time: they sit in cfg_app beside keys that are still in
// use, so the namespace cannot be erased whole the way a reset tier erases one. Every key is
// attempted and warns under its own name; the first failure is returned, and NOT_FOUND is not one.
//
// Shared by ot_config_nvs_load.c's migration and ot_config_nvs_save.c's every save -- moved here,
// out of the file that used to hold both, when the room-slot fields pushed that file over
// the 350-line ceiling and it was cut along the load/write seam it already had (CLAUDE.md, File
// ceiling). A retired key is erased on EVERY writable boot, not only while the schema number is
// behind: cl_auto and cl_man_ch were retired with no schema bump, from a store already current, and
// a current store never migrates again -- gating the erase on the schema would leave them in the
// flash for an OTA rollback to read back as live.
esp_err_t ot_config_io_erase_retired(nvs_handle_t app)
{
    esp_err_t first = ESP_OK;
    for (size_t i = 0; ot_config_retired_key(i) != NULL; i++) {
        const esp_err_t e = ot_config_io_erase_key(app, ot_config_retired_key(i));
        if (first == ESP_OK)
            first = e;
    }
    return first;
}

#endif  // ESP_PLATFORM
