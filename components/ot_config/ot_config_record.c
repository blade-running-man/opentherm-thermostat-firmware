// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The UI password record, `1$<iterations>$<salt hex>$<digest hex>`: rendering it, parsing it,
// checking a candidate against it. The KDF is not here -- the host suite hands one in, and the
// device's own lives in ot_config_nvs_kdf.c -- which is what keeps this file pure.
#include "ot_config.h"

#include <stdio.h>
#include <string.h>

#include "ot_config_internal.h"

// --- the password record ---------------------------------------------------------------------

static const char HEX_DIGITS[] = "0123456789abcdef";

static void to_hex(const uint8_t *in, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = HEX_DIGITS[in[i] >> 4];
        out[i * 2 + 1] = HEX_DIGITS[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static bool from_hex(const char *in, size_t chars, uint8_t *out)
{
    if (chars % 2 != 0)
        return false;
    for (size_t i = 0; i < chars; i += 2) {
        if (!is_hex(in[i]) || !is_hex(in[i + 1]))
            return false;
        unsigned value = 0;
        for (size_t k = 0; k < 2; k++) {
            const char c = in[i + k];
            const unsigned nibble =
                (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            value = value * 16u + nibble;
        }
        out[i / 2] = (uint8_t)value;
    }
    return true;
}

// `1$<iterations>$<salt hex>$<digest hex>`, parsed by hand. sscanf with %s over a record that
// came out of flash is an unbounded write, and strtok would need a mutable copy of a value this
// function is not allowed to damage.
bool ot_config_parse_record(const char *record, password_record_t *out)
{
    if (record == NULL)
        return false;
    // The record's own version, not the store's schema. A record written by a build that used
    // another format is not guessed at.
    if (record[0] != '1' || record[1] != '$')
        return false;

    const char *p      = record + 2;
    uint32_t    iters  = 0;
    size_t      digits = 0;
    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        // Stops the accumulator overflowing, and nothing else -- the guard that decides what is
        // an acceptable COUNT is below, on the finished number. This one was doing both jobs and
        // could only do the first: sitting before the multiply, it let through ten times its own
        // limit plus nine, so a record could carry 100,000,009 iterations and parse.
        if (iters > OT_CONFIG_KDF_ITERATIONS_MAX)
            return false;
        iters = iters * 10u + (uint32_t)(*p - '0');
    }
    // Zero iterations is not a cheap password, it is a record whose cost was lost. And a count
    // above the cap is a derivation nobody will wait for: at 100 ms per 10,000 on a C6 the number
    // the old guard admitted is about seventeen minutes of PBKDF2 on the HTTP task, per attempt,
    // with no lockout to bound how often it is asked for. The cap is what
    // ot_config_hash_password() will write, so nothing this build stores is refused here.
    if (digits == 0 || iters == 0 || iters > OT_CONFIG_KDF_ITERATIONS_MAX || *p != '$')
        return false;
    p++;

    const char *salt_hex = p;
    while (*p != '\0' && *p != '$')
        p++;
    if (*p != '$')
        return false;
    const size_t salt_chars = (size_t)(p - salt_hex);
    p++;

    if (salt_chars != OT_CONFIG_SALT_LEN * 2)
        return false;
    if (strlen(p) != OT_CONFIG_DIGEST_LEN * 2)
        return false;

    if (out == NULL) {
        uint8_t discard[OT_CONFIG_SALT_LEN];
        return from_hex(salt_hex, salt_chars, discard);
    }
    if (!from_hex(salt_hex, salt_chars, out->salt))
        return false;
    out->iterations = iters;
    out->digest_hex = p;
    return true;
}

bool ot_config_hash_password(const char *password, const ot_config_hash_ctx_t *ctx,
                                   char *out, size_t cap)
{
    if (password == NULL || ctx == NULL || ctx->kdf == NULL || ctx->salt == NULL || out == NULL)
        return false;

    const uint32_t iterations =
        ctx->iterations != 0 ? ctx->iterations : OT_CONFIG_KDF_ITERATIONS;
    // Refused HERE, before the derivation, because the parser refuses it too: a record this
    // build writes and cannot read back is a password set successfully and rejected at the next
    // login, on a device whose owner has no other way in. The two limits are one constant for
    // exactly that reason.
    if (iterations > OT_CONFIG_KDF_ITERATIONS_MAX)
        return false;

    uint8_t digest[OT_CONFIG_DIGEST_LEN];
    if (!ctx->kdf(password, ctx->salt, OT_CONFIG_SALT_LEN, iterations, digest, sizeof digest))
        return false;

    char salt_hex[OT_CONFIG_SALT_LEN * 2 + 1];
    char digest_hex[OT_CONFIG_DIGEST_LEN * 2 + 1];
    to_hex(ctx->salt, OT_CONFIG_SALT_LEN, salt_hex);
    to_hex(digest, sizeof digest, digest_hex);

    // Rendered into a local first. `out` is usually the stored record itself, and an snprintf
    // straight into a buffer one byte short would leave a TRUNCATED record behind -- one that
    // verifies nothing and locks the owner out of a device that is otherwise working.
    char record[OT_CONFIG_HASH_MAX + 1];
    const int n = snprintf(record, sizeof record, "1$%u$%s$%s", (unsigned)iterations, salt_hex,
                           digest_hex);
    if (n < 0 || (size_t)n >= sizeof record || (size_t)n >= cap)
        return false;
    memcpy(out, record, (size_t)n + 1);
    return true;
}

bool ot_config_check_ui_password_against(const char *record, const char *candidate,
                                               ot_config_kdf_t kdf)
{
    // An empty candidate matches nothing, exactly as in ot_secret_equals: the difference
    // between "no password is set" and "the password is the empty string" is the whole of
    // whether an unconfigured device is open or owned.
    if (candidate == NULL || candidate[0] == '\0' || kdf == NULL)
        return false;

    password_record_t parsed;
    if (!ot_config_parse_record(record, &parsed))
        return false;

    uint8_t digest[OT_CONFIG_DIGEST_LEN];
    if (!kdf(candidate, parsed.salt, sizeof parsed.salt, parsed.iterations, digest, sizeof digest))
        return false;

    char computed[OT_CONFIG_DIGEST_LEN * 2 + 1];
    to_hex(digest, sizeof digest, computed);
    // Constant time, and ot_secrets.h says why: an early return on the first wrong byte
    // tells a guesser how much of the guess was right, one byte at a time, and a device on a LAN
    // can be guessed at quickly.
    return ot_secret_equals(parsed.digest_hex, computed);
}
