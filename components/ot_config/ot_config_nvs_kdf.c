// SPDX-FileCopyrightText: 2026 Oleksandr Slukovskyi (@blade-running-man)
// SPDX-License-Identifier: Apache-2.0

// The device's one-way function for the UI password record: PBKDF2-HMAC-SHA256 from mbedtls and
// a salt from esp_random. The only file of this component that includes either, which is why it
// is a file: ot_config_record.c renders and checks the record on the host with a KDF the suite
// hands in. Compiled out on the host, for the reason at the top of ot_config_nvs.c.
#ifdef ESP_PLATFORM

#include "ot_config_nvs.h"

#include <string.h>

#include "esp_random.h"
#include "mbedtls/pkcs5.h"

// --- the one-way function ---------------------------------------------------------------------

static bool device_kdf(const char *password, const uint8_t *salt, size_t salt_len,
                       uint32_t iterations, uint8_t *out, size_t out_len)
{
    // MBEDTLS_PKCS5_C is defined unconditionally by the IDF's mbedtls port
    // (mbedtls/port/include/mbedtls/esp_config.h:2415) and has no Kconfig switch, so this cannot
    // be configured away by an sdkconfig change. SHA-256 rides the hardware accelerator
    // (CONFIG_MBEDTLS_HARDWARE_SHA=y, sdkconfig.m5stack-nanoc6:3156).
    //
    // _ext, not mbedtls_pkcs5_pbkdf2_hmac: the latter is deprecated in mbedtls 3.x
    // (pkcs5.h:160) and wants a prepared md context this has no reason to own.
    const int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)password,
                                                 strlen(password), salt, salt_len,
                                                 (unsigned int)iterations, (uint32_t)out_len, out);
    return rc == 0;
}

ot_config_kdf_t ot_config_device_kdf(void) { return device_kdf; }

void ot_config_device_hash_ctx(ot_config_hash_ctx_t *ctx,
                                     uint8_t salt[OT_CONFIG_SALT_LEN])
{
    if (ctx == NULL || salt == NULL)
        return;
    // esp_random() is only a TRUE random source once Wi-Fi or Bluetooth is running
    // (esp_random.h, esp_random). A password can be set before that -- the first one is set on the
    // access point -- so this salt may come from a weaker source, and that is ACCEPTABLE HERE and
    // nowhere else in this firmware: a salt has to be unique, not unpredictable. Its entire job is
    // to stop one recovered flash image from answering for every device whose owner chose the same
    // word, and a counter would do that. DO NOT copy this reasoning to a key or a token.
    esp_fill_random(salt, OT_CONFIG_SALT_LEN);
    ctx->kdf        = device_kdf;
    ctx->salt       = salt;
    ctx->iterations = OT_CONFIG_KDF_ITERATIONS;
}

#endif  // ESP_PLATFORM
