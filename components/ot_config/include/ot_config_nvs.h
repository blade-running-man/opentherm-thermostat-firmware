// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The other half of ot_config: the one that touches flash.
//
// Deliberately dull, and deliberately small. Every rule about what a value may be, what a
// document means and what a reset destroys lives in ot_config.h, where a host test can
// reach it; this file opens namespaces, moves bytes, and asks those questions. The split is the
// point -- what is decided here is only ever "did the flash answer", and the answer to "no" is
// never a panic.
//
// TWO RULES RUN THROUGH EVERY FUNCTION BELOW, and both are in the decisions document under the
// invariants that are not choices:
//
//  * NO ESP_ERROR_CHECK, ANYWHERE, ON ANYTHING THAT CAME OUT OF FLASH. A bad stored value that
//    aborts the boot is a reboot loop, and the configuration survives the reboot -- so the loop
//    has no way out over the air, on a thermostat screwed to a wall with no keypad and no USB
//    socket in reach. Every call here returns its error to a caller that can
//    carry on with defaults.
//  * THE WI-FI PAIR IS ONE BLOB UNDER ONE KEY. NVS gives no atomicity between two keys, and a
//    power cut between two writes leaves a new SSID with an old password -- a typo nobody made.
//    ot_wifi_t goes in and out whole, and the only sizes accepted are
//    exactly its own.
//
// Device only. It is compiled out on the host (see the guard at the top of ot_config_nvs.c)
// because a host test of this half could only ever test a fake NVS.
#pragma once

#include "ot_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Reads the whole document, brings an older schema forward, and hands back a document every
// validator in ot_config.h accepts -- whatever the flash actually held.
//
// `device_id` is the full MAC as lower-case hex; it seeds the defaults. `repairs` may be
// NULL, and names which fields the flash could not answer for: what ot_config_sanitize()
// replaced, PLUS what was in the store and would not fit this build's fields, which the sanitizer
// cannot see because a dropped value leaves a perfectly valid default behind. Both are shown
// to the owner rather than swallowed, because settings that vanish silently are indistinguishable
// from a firmware that reset itself.
//
// Returns ESP_OK when the store was readable. A failure is NOT fatal and must not be treated as
// one: `cfg` is filled with defaults either way, and a device on its own access point with
// default settings is a device the owner can reach.
esp_err_t ot_config_nvs_load(ot_config_t *cfg, const char *device_id,
                                   ot_config_repairs_t *repairs);

// Writes the whole document back. Call it after ot_config_apply() has accepted one.
//
// Not atomic across namespaces, and it cannot be -- NVS offers no such thing. What IS atomic is
// the only pair where atomicity was ever load-bearing: the Wi-Fi record is one key. A power cut
// in the middle of this leaves some settings old and some new, which is recoverable by saving
// again; the pair is what would not have been.
esp_err_t ot_config_nvs_save(const ot_config_t *cfg);

// The Wi-Fi pair on its own, as one blob under one key. This is the write that
// POST /api/provision makes, and it is separate from the one above so that provisioning never
// rewrites the broker settings on its way past.
esp_err_t ot_config_nvs_save_wifi(const ot_wifi_t *wifi);

// --- the rollback that protects against a typo ---------------------------------------------

// "The pair currently stored has just produced an address": copy it into the known-good slot.
// ot_provision.h raises OT_PROV_ACTION_COMMIT_CREDENTIALS for exactly this, on the
// transition and not on every reconnection -- one flash cycle per network change.
esp_err_t ot_config_nvs_commit_known_good(void);

// "The pair currently stored never worked": put the known-good one back, as one write.
// OT_PROV_ACTION_RESTORE_CREDENTIALS. Without this the most likely catastrophic scenario
// in the whole review -- not an attack, a mistyped character -- ends with a device that has
// credentials (so it raises no access point) and cannot use them.
//
// ESP_ERR_NVS_NOT_FOUND when there is nothing to roll back to, which is a device being
// provisioned for the first time. `out` may be NULL; when it is not, it receives the restored
// pair so the caller can connect with it without reading it back.
esp_err_t ot_config_nvs_restore_known_good(ot_wifi_t *out);

// The two booleans ot_prov_boot_t wants, answered without loading the document.
//
// has_credentials is "a pair this radio could actually use is stored", NOT "the key exists": the
// an earlier design's has_sta() was true for a saved-but-zeroed blob and it spent a boot
// associating with "" (wifi_component.cpp:655-663, and ap_guard.cpp:143-153 is the fix).
//
// The test itself is ot_config_wifi_usable(), in the pure half, and it is the SAME call
// ot_config_sanitize() makes over the same blob. It has to be: this answer decides whether
// an access point goes on air and that one decides what the station is handed, so two predicates
// meant a device that was told it had a network and given an empty record -- no network, no
// access point, no way back, permanently. It lives over there rather than
// here because a host test can reach it there, and test_config_sanitize runs both over one table.
bool ot_config_nvs_has_credentials(void);
bool ot_config_nvs_has_known_good(void);

// --- the two facts a reboot must not erase ------------------------------------------------

// The other two fields of ot_prov_boot_t. Neither is a setting and neither is a value the
// owner ever sees; both are ways back, and losing one costs a device that cannot be reached.
//
// The loader NEVER FAILS and takes no error out. Absent is false, unreadable is false, and false
// is the SAFE answer for both: a device that has forgotten a window ran out offers the longer
// first-run window, and one that has forgotten it was never given an address simply tries the
// network again. The opposite default would put an open access point on the air at every boot of
// a device whose flash merely hiccuped -- which is the failure the thirty-minute window exists to
// avoid. `window_closed` and `address_never_arrived` may not be NULL.
void ot_config_nvs_load_prov_flags(bool *window_closed, bool *address_never_arrived);

// Written on a TRANSITION by the caller, not on every tick: this is a flash cycle, and the
// provisioning task decides four times a second.
//
// Both in one call and one commit, because they are read back together as one answer about the
// last run. Not gated on read_only: like _save_wifi() and the two known-good calls, this is
// part of the way back, and a rolled-back device that cannot record that its window closed is one
// whose access point never comes back at the right length.
esp_err_t ot_config_nvs_save_prov_flags(bool window_closed, bool address_never_arrived);

// --- the two-tier reset -----------------------------------------------------------------------

// Erases every namespace ot_config_reset_erases() names for the tier, with
// nvs_erase_all() -- never a list of keys. A key list goes stale the first time a field is
// added, and it can never name a key an OLDER firmware wrote and this build has not heard of.
// The failure that shape prevents is a factory reset that leaves the previous owner's broker
// password in the flash of a device that has changed hands.
//
// Every namespace is attempted even after one fails: a reset that stops halfway leaves the
// network gone and the password kept, or the reverse, and neither is what the gesture means.
// Returns the first error, or ESP_OK.
esp_err_t ot_config_nvs_reset(ot_config_reset_t tier);

// --- the one-way function this device has -----------------------------------------------------

// PBKDF2-HMAC-SHA256 over mbedtls, with the SHA accelerator behind it. It lives in this file
// rather than in one of its own because it is the other thing that only exists on the device --
// a second device-only source file holding one function is a file nobody finds.
ot_config_kdf_t ot_config_device_kdf(void);

// Fills `ctx` for ot_config_apply(), with a fresh salt written into the caller's buffer.
//
// The caller owns the salt because `ctx` only borrows it: it has to outlive the apply() call and
// nothing here has anywhere to keep it. Fresh per stored password, never per device and never
// per build -- otherwise one recovered flash image answers for every device whose owner chose
// the same word.
void ot_config_device_hash_ctx(ot_config_hash_ctx_t *ctx,
                                     uint8_t salt[OT_CONFIG_SALT_LEN]);

#ifdef __cplusplus
}
#endif
