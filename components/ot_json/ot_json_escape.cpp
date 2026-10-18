// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The writer half, and it shares nothing with the reader half by design: escaping walks a string
// the firmware itself produced, decoding walks one an attacker chose. Different input, different
// failure that matters (an unparseable response versus a credential read wrong), no shared state.
//
// That is why this is its own file rather than the tail of ot_json.cpp: the reader's governing
// rule -- never read past the terminator of a hostile buffer -- has nothing to say here, and a
// reader of ot_json.cpp should not have to decide, function by function, which half they are in.
//
// DO NOT merge back, and DO NOT grow a second escaper anywhere else: ot_api_escape_json() forwards
// to this one precisely so that there is ONE set of rules for \u, for surrogate pairs and for
// control characters. See ot_json.h.
#include "ot_json.h"

#include <stdint.h>
#include <stdio.h>

// Moved verbatim from ot_api.cpp, which now forwards to it. One set of rules for \u, for
// surrogate pairs and for control characters -- a second implementation would be a second set.
size_t ot_json_escape(const char *in, char *out, size_t cap)
{
    if (out == nullptr || cap == 0)
        return 0;
    size_t used = 0;
    // All or nothing. Copying "as much as fits" is what let a \uXXXX escape be cut in half,
    // and half an escape does not make the document shorter, it makes it unparseable.
    const auto emit = [&](const char *s) {
        size_t need = 0;
        while (s[need])
            need++;
        if (used + need + 1 > cap)
            return;
        for (size_t i = 0; i < need; i++)
            out[used++] = s[i];
    };
    const auto emit_codepoint = [&](uint32_t cp) {
        // Neither of these is representable in JSON, and emitting them anyway produced
        // output that parsed as text but was not valid: a surrogate pair whose high half
        // came from the low range, or a lone surrogate. Dropping is the honest answer --
        // the input was not valid UTF-8 to begin with.
        if (cp > 0x10ffff)
            return;
        if (cp >= 0xd800 && cp <= 0xdfff)
            return;

        char esc[16];
        if (cp < 0x10000) {
            snprintf(esc, sizeof esc, "\\u%04x", (unsigned)cp);
        } else {
            // Outside the BMP needs a surrogate pair. Nothing in pdo_table reaches here,
            // but a truncated pair is invalid JSON and the failure would be silent.
            cp -= 0x10000;
            snprintf(esc, sizeof esc, "\\u%04x\\u%04x", (unsigned)(0xd800 + (cp >> 10)),
                     (unsigned)(0xdc00 + (cp & 0x3ff)));
        }
        emit(esc);
    };

    for (const unsigned char *p = (const unsigned char *)in; in && *p;) {
        switch (*p) {
        case '"':  emit("\\\""); p++; continue;
        case '\\': emit("\\\\"); p++; continue;
        case '\b': emit("\\b");  p++; continue;
        case '\f': emit("\\f");  p++; continue;
        case '\n': emit("\\n");  p++; continue;
        case '\r': emit("\\r");  p++; continue;
        case '\t': emit("\\t");  p++; continue;
        default:   break;
        }

        if (*p < 0x20) {
            emit_codepoint(*p);
            p++;
            continue;
        }
        if (*p < 0x80) {
            if (used + 2 > cap)
                break;
            out[used++] = (char)*p++;
            continue;
        }

        // Multi-byte UTF-8, decoded so it can be re-emitted as \uXXXX. A byte that is not part
        // of a well-formed sequence is DROPPED rather than passed through: passing it through
        // is how invalid UTF-8 gets into a document that claims to be JSON.
        uint32_t cp    = 0;
        int      extra = 0;
        if ((*p & 0xe0) == 0xc0) {
            cp    = (uint32_t)(*p & 0x1f);
            extra = 1;
        } else if ((*p & 0xf0) == 0xe0) {
            cp    = (uint32_t)(*p & 0x0f);
            extra = 2;
        } else if ((*p & 0xf8) == 0xf0) {
            cp    = (uint32_t)(*p & 0x07);
            extra = 3;
        } else {
            p++;
            continue;
        }
        p++;
        bool good = true;
        for (int i = 0; i < extra; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                good = false;
                break;
            }
            cp = (cp << 6) | (uint32_t)(p[i] & 0x3f);
        }
        if (!good)
            continue;
        p += extra;
        emit_codepoint(cp);
    }

    out[used < cap ? used : cap - 1] = '\0';
    return used;
}
