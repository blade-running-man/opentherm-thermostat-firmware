// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The getters: what is at THIS key, one field at a time. See ot_json.h for the contract and for
// why this is not cJSON.
//
// THIS FILE IS ONE OF FOUR, and the seams are load-bearing rather than cosmetic -- CLAUDE.md's
// 350-line ceiling forced the cut, but it was made where the file was already jointed:
//   * ot_json_scan.{h,cpp} -- what a TOKEN is. One walk over a string, one number grammar, shared.
//   * ot_json_check.cpp    -- whether the WHOLE BODY is a document. Called once, before trust.
//   * ot_json.cpp (here)   -- what is at a key, and which key is n-th. Called per field, after.
//   * ot_json_escape.cpp   -- the writer, which shares nothing with the reader by design.
// DO NOT merge them back. The public contract is ot_json.h and it did not change when they split.
//
// THE ONE RULE, restated because it governs every line below: nothing reads past the terminator,
// whatever the bytes inside the document claim. The input is chosen by whoever is talking to the
// device, the setup access point is open, and this is the first code the bytes reach.
// test_nothing_is_read_past_the_terminator asserts it over every prefix of a realistic body.
#include "ot_json_scan.h"

#include <cstdlib>
#include <cstring>

using ot_json_detail::Number;
using ot_json_detail::Scan;
using ot_json_detail::Sink;
using ot_json_detail::StrResult;
using ot_json_detail::ValueKind;

namespace {

// True when the decoded key at the cursor equals `want`. Compared BYTE FOR BYTE against what the
// escape sequences decode to, so `{"a":1}` answers to "a" -- a client that escapes a key
// would otherwise have its field silently ignored, which for a config patch is a save that
// dropped a setting.
bool key_matches(Scan &s, const char *want, bool *malformed)
{
    char      buf[64];
    Sink      sink = {buf, sizeof buf, 0, false};
    Scan      copy = s;
    const StrResult r = ot_json_detail::read_string(copy, sink);
    if (r == StrResult::Malformed) {
        *malformed = true;
        return false;
    }
    s = copy;
    if (r == StrResult::TooLong)
        return false;  // longer than any key this firmware has; it is simply not the one asked for
    buf[sink.used] = '\0';
    return strcmp(buf, want) == 0;
}

// Walks to the value of `key`, leaving the cursor on its first byte. Does its OWN scanning rather
// than trusting a prior ot_json_check(): handlers are supposed to check first, and one day
// one will not -- a getter that assumed a checked document would run off the end of an unchecked
// one.
ot_json_read_t seek(const char *doc, const char *key, Scan &s)
{
    if (doc == nullptr || key == nullptr)
        return OT_JSON_MISSING;

    s = Scan{doc};
    s.spaces();
    if (*s.p != '{')
        return OT_JSON_MISSING;
    s.p++;

    for (;;) {
        s.spaces();
        if (*s.p == '}')
            return OT_JSON_MISSING;
        if (*s.p != '"')
            return OT_JSON_MALFORMED;

        bool       malformed = false;
        const bool hit       = key_matches(s, key, &malformed);
        if (malformed)
            return OT_JSON_MALFORMED;

        s.spaces();
        if (*s.p != ':')
            return OT_JSON_MALFORMED;
        s.p++;
        s.spaces();
        if (hit)
            return OT_JSON_FOUND;

        // Step over the value we are not interested in. A nested one stops the walk: this reader
        // does not know how long it is, and guessing is how it would read past its own document.
        ValueKind                  kind   = ValueKind::Null;
        const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, nullptr, nullptr);
        if (status != OT_JSON_FOUND)
            return OT_JSON_MALFORMED;

        s.spaces();
        if (*s.p == ',') {
            s.p++;
            continue;
        }
        return OT_JSON_MISSING;  // '}' or rubbish: either way the key is not here
    }
}

// What has to follow a value for it to have been a value at all.
//
// Without this a getter answers 12 for `{"a":12` -- the number parses, the document does not, and
// nothing said so. It never reads past the terminator either way, so this is not a memory rule;
// it is the same rule as ot_json_check()'s trailing-rubbish test, applied per key so that a
// handler which forgot to check the body cannot be handed half a document's worth of answers.
ot_json_read_t value_is_closed(Scan &s)
{
    s.spaces();
    return (*s.p == ',' || *s.p == '}') ? OT_JSON_FOUND : OT_JSON_MALFORMED;
}

}  // namespace

ot_json_read_t ot_json_string(const char *doc, const char *key, char *out, size_t cap)
{
    if (out == nullptr || cap == 0)
        return OT_JSON_TOO_LONG;

    Scan                 s      = {nullptr};
    const ot_json_read_t found = seek(doc, key, s);
    if (found != OT_JSON_FOUND)
        return found;

    if (*s.p != '"')
        return OT_JSON_WRONG_TYPE;

    // Written into the caller's buffer only on success. A refused value must not leave half of
    // itself where the caller will read it as the whole.
    Sink                       sink   = {out, cap, 0, false};
    ValueKind                  kind   = ValueKind::String;
    const ot_json_read_t status = ot_json_detail::read_value(s, &kind, &sink, nullptr, nullptr);
    if (status != OT_JSON_FOUND)
        return status;
    const ot_json_read_t closed = value_is_closed(s);
    if (closed != OT_JSON_FOUND)
        return closed;
    out[sink.used] = '\0';
    return OT_JSON_FOUND;
}

ot_json_read_t ot_json_u32(const char *doc, const char *key, uint32_t *out)
{
    if (out == nullptr)
        return OT_JSON_MISSING;

    Scan                       s     = {nullptr};
    const ot_json_read_t found = seek(doc, key, s);
    if (found != OT_JSON_FOUND)
        return found;

    ValueKind kind   = ValueKind::Null;
    Number    number = {false, false, false, false, 0};
    const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, &number, nullptr);
    if (status == OT_JSON_WRONG_TYPE)
        return OT_JSON_WRONG_TYPE;  // an object or an array
    if (status != OT_JSON_FOUND)
        return status;
    if (kind != ValueKind::Number)
        return OT_JSON_WRONG_TYPE;
    const ot_json_read_t closed = value_is_closed(s);
    if (closed != OT_JSON_FOUND)
        return closed;
    if (!number.whole || number.negative || number.overflow || number.value > UINT32_MAX)
        return OT_JSON_OUT_OF_RANGE;

    *out = (uint32_t)number.value;
    return OT_JSON_FOUND;
}

ot_json_read_t ot_json_i32(const char *doc, const char *key, int32_t *out)
{
    if (out == nullptr)
        return OT_JSON_MISSING;

    Scan                       s     = {nullptr};
    const ot_json_read_t found = seek(doc, key, s);
    if (found != OT_JSON_FOUND)
        return found;

    ValueKind kind   = ValueKind::Null;
    Number    number = {false, false, false, false, 0};
    const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, &number, nullptr);
    if (status == OT_JSON_WRONG_TYPE)
        return OT_JSON_WRONG_TYPE;
    if (status != OT_JSON_FOUND)
        return status;
    if (kind != ValueKind::Number)
        return OT_JSON_WRONG_TYPE;
    const ot_json_read_t closed = value_is_closed(s);
    if (closed != OT_JSON_FOUND)
        return closed;
    // A fraction and an exponent are refused here exactly as they are in _u32: 18.5 is a typo,
    // not a number to round, and the caller cannot tell the difference afterwards.
    if (!number.whole || number.overflow)
        return OT_JSON_OUT_OF_RANGE;
    // The magnitude is bounded against the SIGNED limits, and the negative side is one larger.
    if (number.negative) {
        if (number.value > (uint64_t)INT32_MAX + 1u)
            return OT_JSON_OUT_OF_RANGE;
        *out = (int32_t)(-(int64_t)number.value);
    } else {
        if (number.value > (uint64_t)INT32_MAX)
            return OT_JSON_OUT_OF_RANGE;
        *out = (int32_t)number.value;
    }
    return OT_JSON_FOUND;
}

ot_json_read_t ot_json_bool(const char *doc, const char *key, bool *out)
{
    if (out == nullptr)
        return OT_JSON_MISSING;

    Scan                       s     = {nullptr};
    const ot_json_read_t found = seek(doc, key, s);
    if (found != OT_JSON_FOUND)
        return found;

    ValueKind kind = ValueKind::Null;
    bool      flag = false;
    const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, nullptr, &flag);
    if (status == OT_JSON_WRONG_TYPE)
        return OT_JSON_WRONG_TYPE;
    if (status != OT_JSON_FOUND)
        return status;
    if (kind != ValueKind::Bool)
        return OT_JSON_WRONG_TYPE;
    const ot_json_read_t closed = value_is_closed(s);
    if (closed != OT_JSON_FOUND)
        return closed;

    *out = flag;
    return OT_JSON_FOUND;
}

ot_json_read_t ot_json_f32(const char *doc, const char *key, float *out)
{
    if (out == nullptr)
        return OT_JSON_MISSING;

    Scan                 s     = {nullptr};
    const ot_json_read_t found = seek(doc, key, s);
    if (found != OT_JSON_FOUND)
        return found;

    ValueKind            kind   = ValueKind::Null;
    Number               number = {};
    const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, &number, nullptr);
    if (status != OT_JSON_FOUND)
        return status;  // WRONG_TYPE for an object or an array, MALFORMED for rubbish
    if (kind != ValueKind::Number)
        return OT_JSON_WRONG_TYPE;
    const ot_json_read_t closed = value_is_closed(s);
    if (closed != OT_JSON_FOUND)
        return closed;

    // The token is converted only AFTER the walk has agreed it is a JSON number. strtod accepts
    // things JSON does not -- "0x10", "inf", a leading plus, a leading zero -- so handing it the
    // raw document would widen the grammar this device answers to, silently.
    const size_t len = (size_t)(number.end - number.begin);
    char         buf[48];
    if (len == 0 || len >= sizeof buf)
        return OT_JSON_OUT_OF_RANGE;
    memcpy(buf, number.begin, len);
    buf[len] = '\0';

    const double v = strtod(buf, nullptr);
    // Written as a range test rather than isinf(), because this also refuses a NaN. A magnitude
    // float cannot hold is not a value the caller can act on, and an infinity handed to a bounds
    // check passes for a number.
    if (!(v >= -3.4028234663852886e38 && v <= 3.4028234663852886e38))
        return OT_JSON_OUT_OF_RANGE;

    *out = (float)v;
    return OT_JSON_FOUND;
}

// --- enumerating keys -------------------------------------------------------------------------

ot_json_read_t ot_json_key_at(const char *doc, size_t index, char *out, size_t cap)
{
    if (doc == nullptr || out == nullptr || cap == 0)
        return OT_JSON_MISSING;

    Scan s = {doc};
    s.spaces();
    if (*s.p != '{')
        return OT_JSON_MISSING;
    s.p++;

    for (size_t i = 0;; i++) {
        s.spaces();
        if (*s.p == '}')
            return OT_JSON_MISSING;  // the document has fewer keys than that
        if (*s.p != '"')
            return OT_JSON_MALFORMED;

        // Sixty-four is the key bound this component already works to -- ot_json_check's duplicate
        // table and key_matches' buffer both use it -- so a name longer than that is not one of
        // this firmware's keys whatever the caller's buffer can hold.
        char            keybuf[64];
        Sink            sink = {keybuf, sizeof keybuf, 0, false};
        const StrResult r    = ot_json_detail::read_string(s, sink);
        if (r == StrResult::Malformed)
            return OT_JSON_MALFORMED;

        s.spaces();
        if (*s.p != ':')
            return OT_JSON_MALFORMED;
        s.p++;
        s.spaces();

        if (i == index) {
            // All or nothing, like ot_json_string: a truncated key is a DIFFERENT key, and the
            // caller is about to compare it against ones it recognises.
            if (r == StrResult::TooLong || sink.used + 1 > cap)
                return OT_JSON_TOO_LONG;
            memcpy(out, keybuf, sink.used);
            out[sink.used] = '\0';
            return OT_JSON_FOUND;
        }

        // Step over the value. A nested one stops the walk for seek()'s reason: this reader does
        // not know how long it is, and guessing is how it would read past its own document.
        ValueKind            kind   = ValueKind::Null;
        const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, nullptr, nullptr);
        if (status != OT_JSON_FOUND)
            return OT_JSON_MALFORMED;

        s.spaces();
        if (*s.p == ',') {
            s.p++;
            continue;
        }
        return OT_JSON_MISSING;  // '}' or rubbish: there is no key at this index
    }
}
