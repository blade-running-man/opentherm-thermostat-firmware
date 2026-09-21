// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The lexical layer of the reader: what a JSON token IS, with no opinion about what the document
// around it means. Private to components/ot_json -- it is NOT under include/ and nothing outside
// this directory may include it. The public contract is ot_json.h and only ot_json.h.
//
// WHY THIS SEAM EXISTS. ot_json.cpp was past CLAUDE.md's 350-line ceiling,
// so it was cut where it was already jointed: three callers -- ot_json_check (is this a
// document at all), the getters (what is at this key), ot_json_escape (the writer) -- share ONE
// walk over a string and ONE number grammar. That sharing was the original design decision, stated
// where Sink is declared: three walks would be three sets of rules about what a legal escape is.
// Cutting here makes the sharing visible instead of implicit in an anonymous namespace.
//
// DO NOT merge these files back together, and DO NOT let a caller reimplement a token. If a
// grammar question needs answering -- is this a number, does this escape decode -- it is answered
// here, once, and every reader gets the same answer.
//
// THE ONE RULE THAT GOVERNS EVERY FUNCTION HERE AND IN THE FILES THAT USE IT: nothing reads past
// the terminator, whatever the bytes inside the document claim. The input is chosen by whoever is
// talking to the device, the setup access point is open, and this is the first code the bytes
// reach. test_nothing_is_read_past_the_terminator asserts it over every prefix of a realistic body.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ot_json.h"

namespace ot_json_detail {

// A cursor over the document. `p` never advances past the NUL, so every helper below can look at
// *p without checking the length first -- the terminator IS the check.
struct Scan {
    const char *p;

    void spaces()
    {
        // RFC 8259 section 2: space, tab, newline, carriage return. Nothing else, and in
        // particular not a vertical tab or a form feed -- accepting those would make this reader
        // take documents no other reader does.
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
    }
};

// Where a decoded string byte goes. The same walk serves three callers -- checking a document,
// comparing a key, and copying a value -- and one walk is the point: three walks would be three
// sets of rules about what a legal escape is.
struct Sink {
    char  *out;      // may be null: then nothing is written and only `used` grows
    size_t cap;      // includes the terminator
    size_t used;
    bool   overflow;

    void put(char c)
    {
        if (out == nullptr) {
            used++;
            return;
        }
        if (used + 1 >= cap) {
            overflow = true;
            return;
        }
        out[used++] = c;
    }
};

enum class StrResult { Ok, Malformed, TooLong };

// Reads one JSON string, starting AT its opening quote, and leaves the cursor just past the
// closing one. Writes the decoded bytes into `sink` when it has somewhere to put them.
StrResult read_string(Scan &s, Sink &sink);

// JSON's number grammar, exactly: an optional minus, an integer part with no leading zero, an
// optional fraction, an optional exponent. `whole` says whether what was read is an integer, and
// `value` carries it when it is one and fits.
struct Number {
    bool     ok;
    bool     whole;
    bool     negative;
    bool     overflow;
    uint64_t value;
    // The token itself, for the reader that wants the FRACTION as well. Meaningful only when
    // `ok`. Kept as bounds into the caller's document rather than accumulated here: deciding
    // whether these bytes are a JSON number and converting them to a float are two jobs, and
    // JSON's grammar is the narrower of the two.
    const char *begin;
    const char *end;
};

enum class ValueKind { String, Number, Bool, Null };

// Reads one value and leaves the cursor just past it. `sink` receives a string's decoded bytes.
ot_json_read_t read_value(Scan &s, ValueKind *kind, Sink *sink, Number *number, bool *flag);

}  // namespace ot_json_detail
