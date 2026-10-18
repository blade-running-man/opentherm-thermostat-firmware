// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The ways back: promoting and restoring the known-good Wi-Fi pair, the two provisioning facts a
// reboot must not erase, and the two-tier reset. None of them takes a document, so none is gated
// by read_only -- the DO NOT in ot_config_nvs_load() says why the first two must never be.
// Compiled out on the host, for the reason at the top of ot_config_nvs.c.
#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "nvs.h"

#include "ot_config_nvs_internal.h"

// --- the rollback ---------------------------------------------------------------------------

esp_err_t ot_config_nvs_commit_known_good(void)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    ot_wifi_t wifi;
    if (!ot_config_io_load_wifi(handle, OT_CONFIG_KEY_STA, &wifi)) {
        nvs_close(handle);
        // Nothing to promote. Not an error worth propagating as one: the caller asked to record
        // that what is stored works, and what is stored is nothing.
        return ESP_ERR_NVS_NOT_FOUND;
    }
    err = nvs_set_blob(handle, OT_CONFIG_KEY_STA_GOOD, &wifi, sizeof wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    ot_config_io_warn_store(OT_CONFIG_KEY_STA_GOOD, err);
    nvs_close(handle);
    return err;
}

esp_err_t ot_config_nvs_restore_known_good(ot_wifi_t *out)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    ot_wifi_t wifi;
    if (!ot_config_io_load_wifi(handle, OT_CONFIG_KEY_STA_GOOD, &wifi)) {
        nvs_close(handle);
        // A first provisioning has nothing to roll back to, and that is a state, not a fault:
        // the way back for that device is its own access point, which it still has.
        return ESP_ERR_NVS_NOT_FOUND;
    }
    err = nvs_set_blob(handle, OT_CONFIG_KEY_STA, &wifi, sizeof wifi);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    ot_config_io_warn_store(OT_CONFIG_KEY_STA, err);
    nvs_close(handle);
    if (err == ESP_OK && out != NULL)
        *out = wifi;
    return err;
}

static bool has_pair(const char *key)
{
    nvs_handle_t handle;
    if (nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READONLY, &handle) != ESP_OK)
        return false;
    ot_wifi_t wifi;
    const bool      found = ot_config_io_load_wifi(handle, key, &wifi);
    nvs_close(handle);
    // ot_config_wifi_usable(), and NOT a length test written out here. This answer decides
    // whether the device raises its own access point; ot_config_sanitize() decides what the
    // station is handed, from the same blob. A second, weaker predicate here made those two
    // disagree -- present to the state machine, cleared in the document -- which is a device with
    // no network, no access point and no way back. The test that keeps them
    // one is test_config_sanitize's table over both.
    return found && ot_config_wifi_usable(&wifi);
}

bool ot_config_nvs_has_credentials(void) { return has_pair(OT_CONFIG_KEY_STA); }

bool ot_config_nvs_has_known_good(void) { return has_pair(OT_CONFIG_KEY_STA_GOOD); }

// --- the two facts a reboot must not erase ------------------------------------------------

void ot_config_nvs_load_prov_flags(bool *window_closed, bool *address_never_arrived)
{
    if (window_closed == NULL || address_never_arrived == NULL)
        return;
    // False before anything is read, and false after anything that fails. Both are ways back and
    // the safe answer is the one that offers MORE of a way back: forgetting that a window closed
    // costs the owner a longer window, forgetting a no-address failure costs one more attempt at
    // a network. The opposite default costs an open access point at every boot.
    *window_closed         = false;
    *address_never_arrived = false;

    nvs_handle_t handle;
    // READONLY: opening NVS_READWRITE creates the namespace, and this runs at the first boot of
    // every device -- a fresh unit would be given an empty cfg_wifi to remember for ever.
    if (nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READONLY, &handle) != ESP_OK)
        return;
    // u8 and normalised, never a bool straight out of flash: a _Bool holding a bit pattern that
    // is neither 0 nor 1 is not a value a C program may read at all, and flash can hand back
    // 0xff. Same rule and same reason as ot_config_io_load_bool() in ot_config_nvs_io.c.
    uint8_t value = 0;
    if (nvs_get_u8(handle, OT_CONFIG_KEY_WINDOW_CLOSED, &value) == ESP_OK)
        *window_closed = value != 0;
    value = 0;
    if (nvs_get_u8(handle, OT_CONFIG_KEY_NO_ADDRESS, &value) == ESP_OK)
        *address_never_arrived = value != 0;
    nvs_close(handle);
}

esp_err_t ot_config_nvs_save_prov_flags(bool window_closed, bool address_never_arrived)
{
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(OT_CONFIG_NS_WIFI_NAME, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    // Both attempted before either is reported, and ONE commit for the pair. They are read back
    // together as one answer about the last run, and a commit between them would let a power cut
    // land on a half-answer.
    esp_err_t first = nvs_set_u8(handle, OT_CONFIG_KEY_WINDOW_CLOSED, window_closed ? 1 : 0);
    ot_config_io_warn_store(OT_CONFIG_KEY_WINDOW_CLOSED, first);
    const esp_err_t second =
        nvs_set_u8(handle, OT_CONFIG_KEY_NO_ADDRESS, address_never_arrived ? 1 : 0);
    ot_config_io_warn_store(OT_CONFIG_KEY_NO_ADDRESS, second);
    if (first == ESP_OK)
        first = second;

    const esp_err_t commit = nvs_commit(handle);
    if (first == ESP_OK)
        first = commit;
    nvs_close(handle);
    return first;
}

// --- the two-tier reset ----------------------------------------------------------------------

// Erasing is by NAMESPACE, with nvs_erase_all(), and never by a list of keys: a list goes stale
// the first time a field is added, and it can never name a key an OLDER firmware wrote and this
// build has not heard of. Both callers below want exactly this and nothing more.
static esp_err_t erase_namespace(const char *name)
{
    // Asked READONLY first, and only to find out whether the namespace exists: nvs_open in
    // NVS_READWRITE CREATES one. Without this probe, a factory reset on a device that has never
    // been configured leaves behind three namespaces it never had -- an erase that WRITES is a
    // contradiction, and the next boot would read them back as configured.
    nvs_handle_t probe;
    esp_err_t    err = nvs_open(name, NVS_READONLY, &probe);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;  // never written: nothing to erase, and nothing wrong
    if (err == ESP_OK)
        nvs_close(probe);

    nvs_handle_t handle;
    err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not open %s to erase it: %s", name, esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "could not erase %s: %s", name, esp_err_to_name(err));
    return err;
}

esp_err_t ot_config_nvs_reset(ot_config_reset_t tier)
{
    esp_err_t first = ESP_OK;

    // Over the ENUM, so a namespace added later is erased by a factory reset without anyone
    // remembering to extend anything here. DO NOT replace this with a list of keys: a list cannot
    // name a key an older firmware wrote and this build has never heard of, and nvs_erase_all()
    // takes both.
    for (int i = 0; i < OT_CONFIG_NS_COUNT; i++) {
        const ot_config_ns_t ns = (ot_config_ns_t)i;
        if (!ot_config_reset_erases(tier, ns))
            continue;

        // The loop CARRIES ON past a failure. A reset that stops at the first bad namespace
        // leaves the network gone and the password kept, or the reverse, and neither of those is
        // what either gesture means to the person holding the button.
        const esp_err_t err = erase_namespace(ot_config_ns_name(ns));
        if (err != ESP_OK && first == ESP_OK)
            first = err;
    }
    return first;
}

#endif  // ESP_PLATFORM
