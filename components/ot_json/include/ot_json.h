// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// JSON, in both directions, for documents this firmware actually exchanges.
//
// The reader half exists because this device reads a body somebody else wrote -- the first code
// on it that does. The setup access point is OPEN by design, so on it "the client" is
// anyone in radio range, and this runs BEFORE the access policy has anything to say -- the policy
// decides on a path, this decides what the bytes meant. That is why it is a pure function over a
// NUL-terminated buffer instead of a loop inside a request handler: every malformed shape in
// test/test_json is a body somebody can send, and none of them needs a radio to reproduce.
//
// FLAT OBJECTS ONLY, and the narrowness is the design rather than a shortcut. All three documents
// this device accepts are flat -- DeviceConfig, ConfigPatch and ProvisionRequest in
// web/src/api/client.ts -- so a nested value is a broken client or somebody probing, and the
// answer to both is 400. Skipping over a nested value instead would mean tracking depth inside a
// value that is never used, which is where a parser of this shape gets its interesting bugs.
//
// WHY NOT cJSON, which is in the IDF and is already linked into this image: it cannot be built for
// the host test environment (platformio.ini, lib_extra_dirs, which sees this repository's
// components and nothing else), so every case in test/test_json would have had to be run on a
// device. A parser that reads attacker-chosen bytes and can only be tested by flashing a board is
// not one this project can keep honest. The narrow grammar above is what makes writing one instead
// a reasonable trade rather than an indulgence.
//
// THE WRITER HALF IS NOT NEW. ot_json_escape() is the escaper the state API has always used,
// moved here so that there is ONE set of rules for \u, for surrogate pairs and for control
// characters -- ot_api_escape_json() now forwards to it. A second implementation would be a
// second set of rules, which is CLAUDE.md's "one entity list, ever" applied to an encoding.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- is this a document at all --------------------------------------------------------------

typedef enum {
    OT_JSON_DOC_OK,
    // Not `{...}` at the top level, or nothing at all. DISTINCT from an empty object, and the
    // distinction is load-bearing: every field of a config patch is optional, so a body read as
    // "no keys" is a successful save that changed nothing and told the owner Saved.
    OT_JSON_DOC_NOT_AN_OBJECT,
    // It is an object and something inside it is not JSON -- an unterminated string, a bad
    // escape, a number with a leading zero, a bare word.
    OT_JSON_DOC_MALFORMED,
    // A value is an object or an array. See the header comment.
    OT_JSON_DOC_NESTED,
    // The same key twice. JSON permits it and parsers disagree about which one wins, and on this
    // device the disagreement is exploitable -- {"ui_password":"letmein","ui_password":
    // "__UNCHANGED__"} means opposite things under first-wins and last-wins. There is no reading
    // safe to guess.
    OT_JSON_DOC_DUPLICATE,
} ot_json_doc_t;

// Walks the whole document once. Call it before reading anything: the getters below are
// individually safe on rubbish, but only this says whether the body as a whole was understood,
// and only that can be turned into a 400.
ot_json_doc_t ot_json_check(const char *doc);

// --- reading one key ------------------------------------------------------------------------

typedef enum {
    OT_JSON_FOUND,
    // No such key. NOT the same as a key whose value is "" -- absent leaves a stored value alone
    // and empty clears it (ot_config.h, ot_config_patch_t), and a reader that
    // conflated them would make an untouched form delete the broker password.
    OT_JSON_MISSING,
    // Present, and not the type asked for.
    OT_JSON_WRONG_TYPE,
    // A string that does not fit the caller's buffer. REFUSED, never truncated: a truncated
    // credential asks a different question from the one the client asked, and asks it silently.
    OT_JSON_TOO_LONG,
    // A number that is not a whole number, or not one that fits. ot_config_check_port takes
    // a uint32_t precisely so 70000 can be REFUSED; a reader that wrapped it to 4464 would hand
    // the validator a port the owner never typed and the validator would accept it.
    OT_JSON_OUT_OF_RANGE,
    // The document is not readable at this key -- a bad escape, an unterminated string. Reported
    // per key as well as by ot_json_check() so that a getter is safe on an unchecked body.
    OT_JSON_MALFORMED,
} ot_json_read_t;

// Copies the DECODED string -- escapes resolved, \uXXXX turned into UTF-8 -- into `out`, always
// NUL-terminated on success and untouched otherwise. `cap` includes the terminator.
//
// Two refusals are worth knowing about because both look like ordinary JSON:
//  * `\u0000` is legal JSON and this hands back a C string, so decoding it would truncate a
//    credential silently. Refused as malformed.
//  * A lone surrogate is refused rather than emitted; it is not valid UTF-8 and the byte sequence
//    a lenient decoder produces for it is not what any other reader would produce.
ot_json_read_t ot_json_string(const char *doc, const char *key, char *out, size_t cap);

// A whole, non-negative number that fits in 32 bits. "18.5" and "1e3" are OUT_OF_RANGE rather
// than rounded: a port of 18.5 is a typo, not a rounding problem.
ot_json_read_t ot_json_u32(const char *doc, const char *key, uint32_t *out);

// A whole number that fits in 32 bits, sign included. Separate from _u32 rather than folded into
// it because the two have opposite needs: a PORT of -1 is not a port and must be refused, while a
// DURATION of -1 is a legal request meaning "until told otherwise".
//
// The distinction that makes this its own function rather than a flag: a caller recovering a
// negative from _u32's OUT_OF_RANGE cannot tell a minus sign from a FRACTION, because both answer
// the same. It would read {"duration": 18.5} -- a typo -- as -1, which is "forever" on three of
// the four timed operations. Here a fraction is OUT_OF_RANGE and a negative is FOUND.
ot_json_read_t ot_json_i32(const char *doc, const char *key, int32_t *out);

// `true` or `false`, and nothing else -- not 1, not "true", not null. A toggle a firmware reads
// out of a string is a toggle whose meaning depends on which client sent it.
ot_json_read_t ot_json_bool(const char *doc, const char *key, bool *out);

// Any JSON number, fraction and exponent included. SEPARATE FROM _u32 AND _i32 RATHER THAN
// REPLACING THEM, and the split is the point: those two refuse 18.5 because a port of 18.5 is a
// typo, while a SETPOINT of 18.5 is what a thermostat is for. A single lenient reader would have
// to pick one of those answers for both callers.
//
// OUT_OF_RANGE, never an infinity: a magnitude float cannot hold is not a value the caller can
// act on, and an inf handed to a bounds check is a number as far as the check is concerned.
// `*out` is untouched on every refusal.
ot_json_read_t ot_json_f32(const char *doc, const char *key, float *out);

// --- enumerating keys -------------------------------------------------------------------------

// The DECODED name of the `index`-th key, in document order, NUL-terminated. OT_JSON_MISSING once
// the index is past the last key -- which is how a caller counts them.
//
// This is the only reader here that learns a key the firmware did not already know, and it exists
// so that operation bodies -- which name their own parameters -- can be read without a second
// parser being written next to this one (CLAUDE.md's one-list rule, applied to a grammar).
//
// TOO_LONG rather than a truncated name, for ot_json_string's reason: a shortened key is a
// different key, and the caller would match it against one it recognises.
ot_json_read_t ot_json_key_at(const char *doc, size_t index, char *out, size_t cap);

// --- writing --------------------------------------------------------------------------------

// Escapes one string into JSON, without the surrounding quotes. Bytes above ASCII are emitted as
// \uXXXX rather than passed through, so the output is valid regardless of what the consumer
// assumes about encoding. Returns the number of bytes written, excluding the terminator.
//
// ALL OR NOTHING per escape: copying "as much as fits" is what once cut a \uXXXX in half, and half
// an escape does not make a document shorter, it makes it unparseable.
size_t ot_json_escape(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
