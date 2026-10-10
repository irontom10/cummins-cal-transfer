/* Standalone P-256/OpenSSL backend regression. Synthetic data only. */
#include "clip46_ecdh.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct clip46_ecdh_session a, b;
    unsigned char av[16], ak[16], bv[16], bk[16], invalid[32];
    unsigned int i;

    for (i = 0U; i < 20U; ++i) {
        assert(clip46_ecdh_begin(&a) == 0);
        assert(clip46_ecdh_begin(&b) == 0);
        assert(clip46_ecdh_complete(&a, 0x03U, b.public_x, av, ak) == 0);
        assert(clip46_ecdh_complete(&b, 0x03U, a.public_x, bv, bk) == 0);
        assert(memcmp(av, bv, 16U) == 0);
        assert(memcmp(ak, bk, 16U) == 0);
        memset(invalid, 0xff, sizeof(invalid));
        assert(clip46_ecdh_complete(&a, 0x02U, invalid, av, ak) != 0);
        clip46_ecdh_end(&a);
        clip46_ecdh_end(&b);
    }
    {
        static const unsigned char cipher[16] = {
            0x6c,0xc3,0x75,0x64,0xdd,0x4c,0xe8,0x9f,
            0x15,0x26,0xfa,0x5e,0x80,0xc4,0x59,0x99
        };
        static const unsigned char wanted[5] = {
            0x01,0x01,0x00,0x00,0x84
        };
        unsigned char out[32], key[16], iv[16], body[32], bad[16];
        size_t n;
        assert(clip46_ecdh_begin(&a) == 0);
        for (i = 0U; i < 16U; ++i) {
            key[i] = (unsigned char)i;
            iv[i] = (unsigned char)(i + 16U);
        }
        assert(clip46_ecdh_decrypt_app(&a, key, iv, cipher,
               16U, out, sizeof(out), &n) == 0);
        assert(n == 5U && memcmp(out, wanted, 5U) == 0);
        memcpy(body, iv, 16U);
        memcpy(body + 16U, cipher, 16U);
        assert(clip46_ecdh_decrypt_auth(&a, key, body,
               32U, out, sizeof(out), &n) == 0);
        assert(n == 5U && memcmp(out, wanted, 5U) == 0);
        memcpy(bad, cipher, 16U);
        bad[15] ^= 1U;
        assert(clip46_ecdh_decrypt_app(&a, key, iv, bad,
               16U, out, sizeof(out), &n) != 0);
        clip46_ecdh_end(&a);
    }
    puts("20 P-256 pairs, invalid point, CBC decrypt and padding rejection PASS");
    return 0;
}
