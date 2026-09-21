// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The bytes of a discovery document: the generated template with its six tokens filled
// (ot_ha.h; tools/render_discovery.py says what each token is and is its Python twin).
#include "ot_ha.h"

#include <stdio.h>
#include <string.h>

#include "ot_json.h"

// A bounded append: overflow is remembered, and a caller is told 0 rather than handed a
// document that stops mid-object. The DISCIPLINE is shared with the project's other renderers,
// the code is not: ot_api has its own ot_api_sink_t (ot_api/ot_api_sink.h), which accumulates the
// size a document NEEDED and leaves a truncated string behind, because a REST reply must at least
// be readable. A discovery document must not: a half-object published under homeassistant/ is a
// broken entity HA keeps. Each component therefore keeps its sink private and shaped to its own
// failure -- DO NOT hoist these into one shared type.
typedef struct {
    char  *buf;
    size_t cap;
    size_t used;
    bool   overflowed;
} writer_t;

static void put_n(writer_t *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (w->used + 1 >= w->cap) {
            w->overflowed = true;
            return;
        }
        w->buf[w->used++] = s[i];
    }
    w->buf[w->used] = '\0';
}

static void put(writer_t *w, const char *s) { put_n(w, s, strlen(s)); }

// The longest field put_escaped() may be given, and the room its escape needs: six bytes per input
// byte (a control byte becomes \u00XX) plus the NUL. ONE number, in both places -- as two literals
// a raised guard silently overflows the buffer, and no test in this suite could see it.
#define OT_HA_ESCAPE_MAX 64

// put_escaped() does not check ot_json_escape()'s return value, and that call drops what does not
// fit rather than reporting it -- so this pre-check is the only thing standing between a too-long
// field and a silently truncated escape.
static void put_escaped(writer_t *w, const char *s)
{
    char tmp[6 * OT_HA_ESCAPE_MAX + 1];
    if (strlen(s) > OT_HA_ESCAPE_MAX) {
        w->overflowed = true;
        return;
    }
    ot_json_escape(s, tmp, sizeof tmp);
    put(w, tmp);
}

// ,"key":"value" -- or nothing for "" (rule 5: absent metadata is omitted, never "").
static void put_opt(writer_t *w, const char *key, const char *value)
{
    if (value[0] == '\0')
        return;
    put(w, ",\"");
    put(w, key);
    put(w, "\":\"");
    put_escaped(w, value);
    put(w, "\"");
}

// Tenths as a JSON number: 400 -> 40, 455 -> 45.5, -5 -> -0.5. Integers only, so no float is
// formatted and there is no "nan" to guard against.
static void put_dc(writer_t *w, int16_t dc)
{
    char tmp[12];
    const int v = dc < 0 ? -(int)dc : (int)dc;
    if (v % 10 != 0)
        snprintf(tmp, sizeof tmp, "%s%d.%d", dc < 0 ? "-" : "", v / 10, v % 10);
    else
        snprintf(tmp, sizeof tmp, "%s%d", dc < 0 ? "-" : "", v / 10);
    put(w, tmp);
}

// Rule 3 guarded at the source: the device id is the discovery topic's NODE segment and the stem
// of every uniq_id (ot_ha.h). HA's TOPIC_MATCHER (mqtt/discovery.py) ignores a topic whose segment
// carries anything outside [a-zA-Z0-9_-], so a MAC spelled with colons would be published to a
// topic HA never reads -- no entity, no error, nowhere. Exactly twelve, because it is the MAC's
// hex and a shorter id would silently re-key every entity the owner already has.
static bool hex_id(const char *id)
{
    size_t n = 0;
    for (; id[n] != '\0'; n++)
        if (!((id[n] >= '0' && id[n] <= '9') || (id[n] >= 'a' && id[n] <= 'f')))
            return false;
    return n == 12;
}

// Rule 4 guarded at the source: configuration_url is built from this, and a value HA's URL
// validator refuses takes the whole device block -- every entity -- with it. A dotted quad or
// nothing: anything else is omitted rather than sent.
static bool dotted_quad(const char *ip)
{
    size_t n = 0;
    for (; ip[n] != '\0'; n++)
        if (!((ip[n] >= '0' && ip[n] <= '9') || ip[n] == '.'))
            return false;
    return n >= 7 && n <= 15;
}

// The ONE gate, taken at the top of both entry points, so that nothing downstream needs a NULL
// check: put_device(), put_origin() and put_escaped() dereference every field unconditionally.
// "" is the contract's "we do not know it" and is legal here (rule 5 omits the key); NULL is not,
// and neither is an empty prefix, which would publish state topics starting at "/".
static bool usable(const ot_ha_ctx_t *c)
{
    return c != NULL && c->prefix != NULL && c->prefix[0] != '\0' && c->device_id != NULL &&
           hex_id(c->device_id) && c->mac != NULL && c->name != NULL && c->model != NULL &&
           c->sw_version != NULL && c->ip != NULL;
}

// {D}. The identifiers are the device id and the MAC, never the name: renaming must not
// re-key anything. Home Assistant merges devices by the MAC in `connections`.
static void put_device(writer_t *w, const ot_ha_ctx_t *c)
{
    put(w, "{\"ids\":[\"");
    put(w, c->device_id);
    put(w, "\"]");
    if (c->mac[0] != '\0') {
        put(w, ",\"cns\":[[\"mac\",\"");
        put_escaped(w, c->mac);
        put(w, "\"]]");
    }
    put(w, ",\"name\":\"");
    put_escaped(w, c->name);
    put(w, "\"");
    put_opt(w, "mdl", c->model);
    put_opt(w, "sw", c->sw_version);
    if (dotted_quad(c->ip)) {
        put(w, ",\"cu\":\"http://");
        put(w, c->ip);
        put(w, "/\"");
    }
    put(w, "}");
}

// {O}. The origin HA logs a discovered entity against.
static void put_origin(writer_t *w, const ot_ha_ctx_t *c)
{
    put(w, "{\"name\":\"" OT_HA_ORIGIN_NAME "\"");
    put_opt(w, "sw", c->sw_version);
    put(w, "}");
}

uint16_t ot_ha_doc_count(void) { return OT_HA_DOC_COUNT; }

const ot_ha_doc_t *ot_ha_doc_at(uint16_t index)
{
    return index < OT_HA_DOC_COUNT ? &OT_HA_DOCS[index] : NULL;
}

size_t ot_ha_topic(const ot_ha_doc_t *doc, const ot_ha_ctx_t *ctx, char *out, size_t cap)
{
    if (doc == NULL || out == NULL || cap == 0 || !usable(ctx))
        return 0;
    writer_t w = {out, cap, 0, false};
    out[0] = '\0';
    put(&w, OT_HA_DISCOVERY_PREFIX "/");
    put(&w, doc->component);
    put(&w, "/");
    put(&w, ctx->device_id);
    put(&w, "/");
    put(&w, doc->object_id);
    put(&w, "/config");
    return w.overflowed ? 0 : w.used;
}

size_t ot_ha_render(const ot_ha_doc_t *doc, const ot_ha_ctx_t *ctx, int16_t lo_dc, int16_t hi_dc,
                    char *out, size_t cap)
{
    if (doc == NULL || out == NULL || cap == 0 || !usable(ctx))
        return 0;
    if (doc->bounds != OT_HA_BOUNDS_NONE && lo_dc > hi_dc)
        return 0;
    writer_t w = {out, cap, 0, false};
    out[0] = '\0';
    for (const char *p = doc->body; *p != '\0' && !w.overflowed; p++) {
        // A token is '{', one capital letter, '}' (render_discovery.py). DO NOT widen the test to
        // "any capital letter": an unknown one is copied as text, which the tests catch as a
        // token left in a rendered document; a guessed meaning would not be caught.
        if (p[0] == '{' && p[1] != '\0' && p[2] == '}') {
            switch (p[1]) {
            case 'P': put_escaped(&w, ctx->prefix);   p += 2; continue;
            case 'I': put(&w, ctx->device_id);        p += 2; continue;
            case 'D': put_device(&w, ctx);            p += 2; continue;
            case 'O': put_origin(&w, ctx);            p += 2; continue;
            case 'L': put_dc(&w, lo_dc);              p += 2; continue;
            case 'H': put_dc(&w, hi_dc);              p += 2; continue;
            default: break;
            }
        }
        put_n(&w, p, 1);
    }
    return w.overflowed ? 0 : w.used;
}
