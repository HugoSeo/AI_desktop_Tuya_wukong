/**
 * @file aes_port.c
 * @brief Redirect dubhe_aes_* symbols (used by libifly_aec_wakeup.a) to
 *        standard mbedtls software AES, bypassing the unavailable dubhe
 *        hardware engine (arm_ce_sca_*).
 *
 * libifly_aec_wakeup.a references:
 *   dubhe_aes_init / dubhe_aes_free
 *   dubhe_aes_setkey_enc / dubhe_aes_setkey_dec
 *   dubhe_aes_crypt_ecb
 */

/* Ensure we get the pure-software mbedtls AES context, not the dubhe alt. */
#ifdef MBEDTLS_AES_ALT
#undef MBEDTLS_AES_ALT
#endif

#include "mbedtls/aes.h"
#include <string.h>

void dubhe_aes_init(mbedtls_aes_context *ctx)
{
    mbedtls_aes_init(ctx);
}

void dubhe_aes_free(mbedtls_aes_context *ctx)
{
    if (ctx == NULL)
        return;
    mbedtls_aes_free(ctx);
    memset(ctx, 0, sizeof(mbedtls_aes_context));
}

int dubhe_aes_setkey_enc(mbedtls_aes_context *ctx,
                         const unsigned char *key,
                         unsigned int keybits)
{
    return mbedtls_aes_setkey_enc(ctx, key, keybits);
}

int dubhe_aes_setkey_dec(mbedtls_aes_context *ctx,
                         const unsigned char *key,
                         unsigned int keybits)
{
    return mbedtls_aes_setkey_dec(ctx, key, keybits);
}

int dubhe_aes_crypt_ecb(mbedtls_aes_context *ctx,
                        int mode,
                        const unsigned char input[16],
                        unsigned char output[16])
{
    return mbedtls_aes_crypt_ecb(ctx, mode, input, output);
}
