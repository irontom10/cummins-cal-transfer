/* Experimental GTIS4.6 P-256 wrapper, ISO C89.
 * Vendored BSD-2-Clause micro-ecc compiled in this translation unit,
 * limited to secp256r1 and a checked operating-system CSPRNG.
 */
#define uECC_PLATFORM 0
#define uECC_SUPPORTS_secp160r1 0
#define uECC_SUPPORTS_secp192r1 0
#define uECC_SUPPORTS_secp224r1 0
#define uECC_SUPPORTS_secp256r1 1
#define uECC_SUPPORTS_secp256k1 0
#define uECC_SUPPORT_COMPRESSED_POINT 1
#include "third_party/micro_ecc/uECC.c"
#include "clip46_p256.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

static void p256_wipe(void *p, size_t n)
{
    volatile unsigned char *q;
    q = (volatile unsigned char *)p;
    while (n-- != 0U) *q++ = 0U;
}
static int p256_rng(uint8_t *buf, unsigned int len)
{
#if defined(_WIN32)
    HCRYPTPROV ctx;
    BOOL ok;
    if (buf == NULL) return 0;
    ctx = 0;
    if (!CryptAcquireContextA(&ctx, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) return 0;
    ok = CryptGenRandom(ctx, (DWORD)len, (BYTE *)buf);
    CryptReleaseContext(ctx, 0);
    return ok ? 1 : 0;
#else
    FILE *f;
    unsigned int left;
    size_t n;
    if (buf == NULL) return 0;
    f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    left = len;
    while (left != 0U) {
        n = fread(buf + (len - left), 1U, (size_t)left, f);
        if (n == 0U) { fclose(f); return 0; }
        left -= (unsigned int)n;
    }
    fclose(f);
    return 1;
#endif
}
int clip46_p256_generate(struct clip46_p256_keypair *pair)
{
    unsigned char public_xy[64];
    int ok;
    if (!pair) return -1;
    memset(pair, 0, sizeof(*pair));
    memset(public_xy, 0, sizeof(public_xy));
    uECC_set_rng(p256_rng);
    ok = uECC_make_key(public_xy, pair->private_scalar, uECC_secp256r1());
    if (ok == 1 && uECC_valid_public_key(public_xy, uECC_secp256r1()) == 1)
        uECC_compress(public_xy, pair->compressed_public, uECC_secp256r1());
    else ok = 0;
    p256_wipe(public_xy, sizeof(public_xy));
    if (!ok) { p256_wipe(pair, sizeof(*pair)); return -1; }
    return 0;
}
int clip46_p256_public_from_private(const unsigned char scalar[32],
                                    unsigned char compressed[33])
{
    unsigned char public_xy[64];
    int ok;
    if (!scalar || !compressed) return -1;
    memset(public_xy, 0, sizeof(public_xy));
    memset(compressed, 0, 33U);
    uECC_set_rng(p256_rng);
    ok = uECC_compute_public_key(scalar, public_xy, uECC_secp256r1());
#if defined(CLIP46_TEST_DIAG)
    fprintf(stderr, "P256 diagnostic: computed=%d valid=%d\n", ok,
            uECC_valid_public_key(public_xy, uECC_secp256r1()));
#endif
    if (ok == 1 && uECC_valid_public_key(public_xy, uECC_secp256r1()) == 1)
        uECC_compress(public_xy, compressed, uECC_secp256r1());
    else ok = 0;
    p256_wipe(public_xy, sizeof(public_xy));
    return ok ? 0 : -1;
}
int clip46_p256_ecdh_xonly(const unsigned char scalar[32],
                           const unsigned char peer_x[32],
                           unsigned char shared[32])
{
    unsigned char compressed[33], public_xy[64];
    unsigned char even_secret[32], odd_secret[32];
    unsigned int diff, i;
    int rc;
    if (!scalar || !peer_x || !shared) return -1;
    memset(shared, 0, 32U);
    memset(compressed, 0, sizeof(compressed));
    memset(public_xy, 0, sizeof(public_xy));
    memset(even_secret, 0, sizeof(even_secret));
    memset(odd_secret, 0, sizeof(odd_secret));
    memcpy(compressed + 1U, peer_x, 32U);
    uECC_set_rng(p256_rng);
    rc = -1;
    compressed[0] = 0x02U;
    uECC_decompress(compressed, public_xy, uECC_secp256r1());
    if (uECC_valid_public_key(public_xy, uECC_secp256r1()) != 1 ||
        uECC_shared_secret(public_xy, scalar, even_secret, uECC_secp256r1()) != 1)
        goto done;
    compressed[0] = 0x03U;
    uECC_decompress(compressed, public_xy, uECC_secp256r1());
    if (uECC_valid_public_key(public_xy, uECC_secp256r1()) != 1 ||
        uECC_shared_secret(public_xy, scalar, odd_secret, uECC_secp256r1()) != 1)
        goto done;
    diff = 0U;
    for (i = 0U; i < 32U; ++i)
        diff |= (unsigned int)(even_secret[i] ^ odd_secret[i]);
    if (diff != 0U) goto done;
    memcpy(shared, even_secret, 32U);
    rc = 0;
done:
    p256_wipe(public_xy, sizeof(public_xy));
    p256_wipe(compressed, sizeof(compressed));
    p256_wipe(even_secret, sizeof(even_secret));
    p256_wipe(odd_secret, sizeof(odd_secret));
    if (rc != 0) p256_wipe(shared, 32U);
    return rc;
}
