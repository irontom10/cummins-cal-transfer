/* Synthetic P-256 only: never use captured cryptographic material. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clip46_p256.h"
#include "clip46_poc_secure.h"
int main(void)
{
    unsigned char a[32], b[32], bad_x[32], pa[33], pb[33];
    unsigned char s1[32], s2[32], zero[32], invalid[32];
    struct clip46_p256_keypair fresh;
    memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
    memset(zero, 0, sizeof(zero)); memset(bad_x, 0xff, sizeof(bad_x));
    {
        unsigned int i;
        for (i = 0U; i < 32U; ++i) {
            a[i] = (unsigned char)(0x11U + i);
            b[i] = (unsigned char)(0x33U + i);
        }
    }
    assert(clip46_p256_generate(&fresh) == 0);
    puts("P256 fresh key generation succeeded");
    fflush(stdout);
    assert(clip46_p256_public_from_private(a, pa) == 0);
    assert(clip46_p256_public_from_private(b, pb) == 0);
    assert(clip46_p256_ecdh_xonly(a, pb + 1U, s1) == 0);
    assert(clip46_p256_ecdh_xonly(b, pa + 1U, s2) == 0);
    assert(memcmp(s1, s2, 32U) == 0);
    assert(memcmp(s1, zero, 32U) != 0);
    assert(clip46_p256_ecdh_xonly(a, bad_x, invalid) != 0);
    assert(memcmp(invalid, zero, 32U) == 0);
    memset(a, 0, sizeof(a));
    assert(clip46_p256_public_from_private(a, pa) != 0);
    assert(clip46_p256_ecdh_xonly(a, pb + 1U, invalid) != 0);
    assert(memcmp(invalid, zero, 32U) == 0);
    assert(clip46_p256_generate(&fresh) == 0);
    assert(fresh.compressed_public[0] == 0x02U ||
           fresh.compressed_public[0] == 0x03U);
    assert(clip46_p256_ecdh_xonly(b, fresh.compressed_public + 1U, s1) == 0);
    assert(clip46_p256_ecdh_xonly(fresh.private_scalar, pb + 1U, s2) == 0);
    assert(memcmp(s1, s2, 32U) == 0);
    clip46_poc_wipe(&fresh, sizeof(fresh));
    puts("CLIP46 synthetic P-256 ECDH passed");
    return 0;
}
