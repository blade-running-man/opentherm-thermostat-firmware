// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// ot_auth, the half that needs a crypto library. Five lines, and the reason they are five
// lines rather than a hash of their own is the whole design of this component.
//
// THE GUARD BELOW IS NOT DECORATION -- same arrangement, same reason, as
// components/ot_config/ot_config_nvs.c. This component is an ESP-IDF component AND a
// PlatformIO library at once (platformio.ini, lib_extra_dirs), so the host test build compiles
// every .c in this directory the moment test_auth includes ot_auth.h, and this one cannot
// compile without mbedtls. Faking mbedtls to get it to would mean test/test_auth was testing the
// fake; what can be decided without a hash is decided in ot_auth.c, where the suite reaches
// it for real, and the verifier arrives there as a function pointer.
//
// THE PRICE OF THAT GUARD, said out loud because it is easy to forget: no host build compiles a
// single line of this file, so a parameter added to or dropped from the call below would be found
// only by a full device build -- and this component otherwise prefers to be told at compile time
// (ot_auth.c ties the two password-length limits together with a _Static_assert rather than
// trusting them to stay equal). test/test_auth pays what it can of that price with two
// static_asserts: ot_auth_verifier_t's shape, and the whole signature of
// ot_config_check_ui_password_against(), both declared in headers the host CAN compile.
//
// What they cannot reach is ot_config_device_kdf(): it is declared in ot_config_nvs.h,
// which includes esp_err.h, so no host build sees it. Its RESULT type is pinned all the same --
// it goes in as the third argument, and that parameter's type is part of the signature asserted
// over there -- but the function's own declaration is not. That much is still device-build-only,
// and it is one line, which is the other reason to KEEP THE BODY THIS SHORT.
#ifdef ESP_PLATFORM

#include "ot_auth.h"
#include "ot_config.h"
#include "ot_config_nvs.h"

// The stored form of the web UI password, in one place, and this is not it -- it is
// ot_config's `1$<iterations>$<salt hex>$<digest hex>`: PBKDF2-HMAC-SHA256 over mbedtls at
// OT_CONFIG_KDF_ITERATIONS rounds, the salt fresh per stored password and carried inside
// the record so raising the count later does not invalidate what is already written.
//
// DO NOT give this component a hash of its own, and a plain salted SHA-256 least of all. Two
// reasons, and the second one is fatal rather than merely worse:
//
//  1. One pass of SHA-256 is a few hundred nanoseconds on hardware that costs nothing, so a
//     recovered flash image is a word list away from the password. The iteration count is the
//     only thing standing between those two facts.
//  2. ot_config_sanitize() ERASES a ui_pw_hash its own parser cannot read
//     (ot_config_defaults.c), deliberately, so that an unreadable record leaves the device open
//     rather than sealed with the owner outside. A password written in any other shape would
//     therefore be set successfully and then vanish at the next boot.
//
// So this file binds, and does not decide. If the record format ever changes it changes over
// there, once, and nothing here notices.
static bool device_verify(const char *stored, const char *candidate)
{
    return ot_config_check_ui_password_against(stored, candidate,
                                                     ot_config_device_kdf());
}

ot_auth_verifier_t ot_auth_device_verifier(void) { return device_verify; }

#endif  // ESP_PLATFORM
