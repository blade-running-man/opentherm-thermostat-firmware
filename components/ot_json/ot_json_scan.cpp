// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// Tokens, and nothing above them. See ot_json_scan.h for why this is its own file and why the
// three readers above it share exactly this one walk. Nothing here knows what a key is, what a
// document is, or which of the getters asked -- that is the whole point of the seam.
//
// DO NOT fold this back into a caller: the moment a caller has its own copy of "what is a legal
// escape" there are two answers to that question in one component.
#include "ot_json_scan.h"

#include <cstring>

namespace ot_json_detail {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void put_utf8(Sink &sink, uint32_t cp)
{
    if (cp < 0x80) {
        sink.put((char)cp);
    } else if (cp < 0x800) {
        sink.put((char)(0xc0 | (cp >> 6)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        sink.put((char)(0xe0 | (cp >> 12)));
        sink.put((char)(0x80 | ((cp >> 6) & 0x3f)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    } else {
        sink.put((char)(0xf0 | (cp >> 18)));
        sink.put((char)(0x80 | ((cp >> 12) & 0x3f)));
        sink.put((char)(0x80 | ((cp >> 6) & 0x3f)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    }
}

}  // namespace

StrResult read_string(Scan &s, Sink &sink)
{
    if (*s.p != '"')
        return StrResult::Malformed;
    s.p++;

    for (;;) {
        const unsigned char c = (unsigned char)*s.p;
        if (c == '\0')
            return StrResult::Malformed;  // unterminated: the document ended inside it
        if (c == '"') {
            s.p++;
            break;
        }
        if (c < 0x20) {
            // RFC 8259 section 7: everything below 0x20 must be escaped. A raw newline in a
            // string is a broken client, and guessing what it meant is how a parser starts
            // accepting documents no other parser does.
            return StrResult::Malformed;
        }
        if (c != '\\') {
            // Raw UTF-8 passes through byte for byte. An SSID is an OCTET string -- 802.11 does
            // not say what encoding it is in -- and whatever arrives has to reach the radio
            // unchanged, or a household whose router is named in Cyrillic cannot be joined.
            sink.put((char)c);
            s.p++;
            continue;
        }

        s.p++;
        const char esc = *s.p;
        switch (esc) {
        case '"':  sink.put('"');  s.p++; continue;
        case '\\': sink.put('\\'); s.p++; continue;
        case '/':  sink.put('/');  s.p++; continue;
        case 'b':  sink.put('\b'); s.p++; continue;
        case 'n':  sink.put('\n'); s.p++; continue;
        case 'f':  sink.put('\f'); s.p++; continue;
        case 'r':  sink.put('\r'); s.p++; continue;
        case 't':  sink.put('\t'); s.p++; continue;
        case 'u':  break;
        default:   return StrResult::Malformed;  // including a backslash at the very end
        }

        s.p++;  // past the 'u'
        uint32_t cp = 0;
        for (int i = 0; i < 4; i++) {
            const int v = hex_value(s.p[i]);
            if (v < 0)
                return StrResult::Malformed;  // also catches the document ending mid-escape
            cp = (cp << 4) | (uint32_t)v;
        }
        s.p += 4;

        if (cp == 0) {
            // Legal JSON, and this function hands back a C string. Decoding it would truncate
            // silently -- a password shorter than the owner set, an SSID that is not the one
            // typed. ot_config_check_ssid refuses an embedded NUL for the same reason;
            // this is that rule one layer earlier, where it is still a bad request.
            return StrResult::Malformed;
        }

        if (cp >= 0xd800 && cp <= 0xdbff) {
            // A high surrogate must be followed by its low half. Anything else is not valid
            // UTF-16 and the bytes a lenient decoder emits for it are not what any other reader
            // would emit.
            if (s.p[0] != '\\' || s.p[1] != 'u')
                return StrResult::Malformed;
            uint32_t low = 0;
            for (int i = 0; i < 4; i++) {
                const int v = hex_value(s.p[2 + i]);
                if (v < 0)
                    return StrResult::Malformed;
                low = (low << 4) | (uint32_t)v;
            }
            if (low < 0xdc00 || low > 0xdfff)
                return StrResult::Malformed;
            s.p += 6;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return StrResult::Malformed;  // a low surrogate on its own
        }

        put_utf8(sink, cp);
    }

    return sink.overflow ? StrResult::TooLong : StrResult::Ok;
}

namespace {

Number read_number(Scan &s)
{
    Number n = {false, true, false, false, 0, s.p, s.p};

    if (*s.p == '-') {
        n.negative = true;
        s.p++;
    }
    if (!is_digit(*s.p))
        return n;  // "-" alone, or ".5", or "+1"

    if (*s.p == '0') {
        // No leading zeros: "01" is not a number, it is two tokens, and a reader that took it
        // would accept documents no other reader does.
        s.p++;
    } else {
        while (is_digit(*s.p)) {
            const uint64_t digit = (uint64_t)(*s.p - '0');
            if (n.value > (UINT64_MAX - digit) / 10u)
                n.overflow = true;
            else
                n.value = n.value * 10u + digit;
            s.p++;
        }
    }

    if (*s.p == '.') {
        s.p++;
        if (!is_digit(*s.p))
            return n;  // "1." is not a number
        n.whole = false;
        while (is_digit(*s.p))
            s.p++;
    }
    if (*s.p == 'e' || *s.p == 'E') {
        s.p++;
        if (*s.p == '+' || *s.p == '-')
            s.p++;
        if (!is_digit(*s.p))
            return n;
        // "1e3" is a whole number mathematically and is not one anybody typed into a port box.
        // Refused rather than evaluated: there is nothing to gain from supporting it and a
        // conversion to lose precision in.
        n.whole = false;
        while (is_digit(*s.p))
            s.p++;
    }

    n.ok  = true;
    n.end = s.p;
    return n;
}

bool read_literal(Scan &s, const char *word)
{
    const size_t n = strlen(word);
    if (strncmp(s.p, word, n) != 0)
        return false;
    // The byte AFTER it has to end the token, or "truthy" would read as "true" with rubbish
    // behind it that the value loop then tries to parse as a separator.
    const char next = s.p[n];
    if (next != '\0' && next != ',' && next != '}' && next != ' ' && next != '\t' &&
        next != '\n' && next != '\r')
        return false;
    s.p += n;
    return true;
}

}  // namespace

ot_json_read_t read_value(Scan &s, ValueKind *kind, Sink *sink, Number *number, bool *flag)
{
    Sink discard = {nullptr, 0, 0, false};

    switch (*s.p) {
    case '"': {
        *kind                = ValueKind::String;
        const StrResult r    = read_string(s, sink != nullptr ? *sink : discard);
        if (r == StrResult::Malformed)
            return OT_JSON_MALFORMED;
        if (r == StrResult::TooLong)
            return OT_JSON_TOO_LONG;
        return OT_JSON_FOUND;
    }
    case 't':
        *kind = ValueKind::Bool;
        if (!read_literal(s, "true"))
            return OT_JSON_MALFORMED;
        if (flag != nullptr)
            *flag = true;
        return OT_JSON_FOUND;
    case 'f':
        *kind = ValueKind::Bool;
        if (!read_literal(s, "false"))
            return OT_JSON_MALFORMED;
        if (flag != nullptr)
            *flag = false;
        return OT_JSON_FOUND;
    case 'n':
        *kind = ValueKind::Null;
        return read_literal(s, "null") ? OT_JSON_FOUND : OT_JSON_MALFORMED;
    case '{':
    case '[':
        // Nested. Reported by the caller as OT_JSON_DOC_NESTED; nothing here tries to skip
        // over it. See the header for why the narrowness is deliberate.
        *kind = ValueKind::Null;
        return OT_JSON_WRONG_TYPE;
    default: {
        *kind          = ValueKind::Number;
        const Number n = read_number(s);
        if (!n.ok)
            return OT_JSON_MALFORMED;
        if (number != nullptr)
            *number = n;
        return OT_JSON_FOUND;
    }
    }
}

}  // namespace ot_json_detail
