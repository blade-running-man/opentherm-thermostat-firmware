// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// "Is this a document at all" -- the whole-body verdict, and nothing else. It is the only function
// here that can be turned into a 400, and it is the only one that has to hold state across keys
// (the duplicate table).
//
// WHY IT IS NOT IN ot_json.cpp WITH THE GETTERS. The two answer different questions and are called
// at different moments: this one once, on the raw body, before anything is trusted; the getters
// afterwards, one per field, each independently safe on rubbish (see seek() for why they do not
// trust this one's verdict). Keeping the whole-document rules -- duplicates, trailing rubbish,
// nesting -- in one file is what makes it possible to read them all without reading the getters.
//
// DO NOT merge back: ot_json.cpp is past CLAUDE.md's 350-line ceiling, and this was the cut.
#include "ot_json_scan.h"

#include <cstring>

using ot_json_detail::Scan;
using ot_json_detail::Sink;
using ot_json_detail::StrResult;
using ot_json_detail::ValueKind;

ot_json_doc_t ot_json_check(const char *doc)
{
    if (doc == nullptr)
        return OT_JSON_DOC_NOT_AN_OBJECT;

    Scan s = {doc};
    s.spaces();
    if (*s.p != '{')
        return OT_JSON_DOC_NOT_AN_OBJECT;
    s.p++;
    s.spaces();

    // Every key seen so far, so the same one twice is refused. Bounded: the largest document this
    // firmware accepts has ten fields, and a body with more keys than this is not one of ours.
    char   seen[16][64];
    size_t seen_count = 0;

    if (*s.p == '}') {
        s.p++;
        s.spaces();
        return *s.p == '\0' ? OT_JSON_DOC_OK : OT_JSON_DOC_MALFORMED;
    }

    for (;;) {
        s.spaces();
        if (*s.p != '"')
            return OT_JSON_DOC_MALFORMED;  // a bare word as a key, or the document ended

        char      key[64];
        Sink      key_sink = {key, sizeof key, 0, false};
        const StrResult r  = ot_json_detail::read_string(s, key_sink);
        if (r == StrResult::Malformed)
            return OT_JSON_DOC_MALFORMED;
        if (r != StrResult::TooLong) {
            key[key_sink.used] = '\0';
            for (size_t i = 0; i < seen_count; i++)
                if (strcmp(seen[i], key) == 0)
                    return OT_JSON_DOC_DUPLICATE;
            if (seen_count < sizeof seen / sizeof seen[0])
                memcpy(seen[seen_count++], key, key_sink.used + 1);
        }

        s.spaces();
        if (*s.p != ':')
            return OT_JSON_DOC_MALFORMED;
        s.p++;
        s.spaces();

        ValueKind                  kind   = ValueKind::Null;
        const ot_json_read_t status = ot_json_detail::read_value(s, &kind, nullptr, nullptr, nullptr);
        if (status == OT_JSON_WRONG_TYPE)
            return OT_JSON_DOC_NESTED;
        if (status != OT_JSON_FOUND)
            return OT_JSON_DOC_MALFORMED;

        s.spaces();
        if (*s.p == ',') {
            s.p++;
            s.spaces();
            // A trailing comma. Refused, because the alternative is a grammar of our own.
            if (*s.p == '}')
                return OT_JSON_DOC_MALFORMED;
            continue;
        }
        if (*s.p == '}') {
            s.p++;
            break;
        }
        return OT_JSON_DOC_MALFORMED;
    }

    s.spaces();
    // Trailing rubbish. "{}{}"" is two documents and this reads one, so accepting it would mean
    // the second was silently ignored -- and the obvious use of that is to put the fields you
    // want ignored in the first.
    return *s.p == '\0' ? OT_JSON_DOC_OK : OT_JSON_DOC_MALFORMED;
}
