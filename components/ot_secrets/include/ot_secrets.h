// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Secrets on the way out of the API and on the way back in.
//
// The rules are the SERVER half of a write-only scheme; the client half is
// web/src/pages/settings/sections/SecretField.tsx.
//
// The problem being solved: the settings page loads the whole configuration and submits the
// whole configuration back. A stored password must never leave the device, and yet an
// untouched field has to round-trip without wiping what is stored.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// What a set secret reads back as. It must match CONFIG_UNCHANGED in
// web/src/api/client.ts, and it is a published string -- see ot_secret_decide().
#define OT_SECRET_SENTINEL "__UNCHANGED__"

// True when a key names something that must never be sent to a client.
//
// Matched by case-insensitive SUBSTRING against secret-bearing words, not against a list of
// exact key names. The stored document is user-editable and has carried different key sets
// across firmware versions, so a future "MQTT Password" or "API Token" is redacted without
// anyone having to remember to extend a whitelist. Over-redaction shows a field as
// unchanged and the user retypes it; under-redaction publishes a credential. The asymmetry
// is the whole argument.
bool ot_secret_key(const char *key);

// What GET should return in place of a stored value: the sentinel when something is set,
// "" when nothing is. Never the value itself.
const char *ot_secret_redact(const char *stored);

typedef enum {
    OT_SECRET_KEEP,    // the field was not touched, or there is nothing to change
    OT_SECRET_CLEAR,   // the user emptied it deliberately
    OT_SECRET_STORE,   // store the submitted value
    OT_SECRET_REJECT,  // the submission is not acceptable; change nothing and say so
} ot_secret_action_t;

// What to do with a submitted value. `submitted` may be NULL, meaning the key was absent.
ot_secret_action_t ot_secret_decide(bool has_stored, const char *submitted);

// Constant-time comparison. An early return on the first wrong byte tells an attacker how
// much of a guess was right, one byte at a time, and a device on a LAN can be guessed at
// quickly. An empty or absent stored secret matches nothing -- otherwise a device with no
// password set would accept an empty one as correct.
bool ot_secret_equals(const char *stored, const char *candidate);

// How many bytes the last comparison read. Exists so a test can pin the constant-time
// property, which it cannot do by measuring a clock. Not for production use.
size_t ot_secret_compared_bytes(void);

#ifdef __cplusplus
}
#endif
