/* Synthetic AES test vector only; no captured secrets. ISO C89. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clip46_poc_secure.h"

int main(void)
{
    static const char expected_hex[] =
        "101112131415161718191a1b1c1d1e1f"
        "954f64f2e4e86e9eee82d20216684899"
        "a93b9ddb22e8ab104c61e728831d6d5a"
        "1f51afc19343249ef3b4495be12dc4f3"
        "e0b296f5c91be273a66c1216a66da5fd"
        "24dbfbf2953d5862f57436198ada0a43"
        "7e92bdee860d80b1c4014a69edecddc6"
        "091628438c665329a97ea61327c51f8c";
    unsigned char key[16], iv[16], plaintext[99], out[128], want[128];
    unsigned char challenge[16], context[51], opaque[32], assembled[99];
    unsigned int i;

    for (i=0U; i<16U; ++i) {
        key[i]=(unsigned char)i;
        iv[i]=(unsigned char)(i+16U);
        challenge[i]=(unsigned char)i;
    }
    for (i=0U; i<99U; ++i) plaintext[i]=(unsigned char)i;
    for (i=0U; i<51U; ++i) context[i]=(unsigned char)(i+16U);
    for (i=0U; i<32U; ++i) opaque[i]=(unsigned char)(i+67U);
    assert(clip46_poc_decode_hex(expected_hex,want,sizeof(want))==0);
    assert(clip46_poc_encrypt(key,iv,plaintext,out)==0);
    assert(memcmp(out,want,sizeof(want))==0);
    assert(clip46_poc_build(challenge,context,opaque,assembled)==0);
    assert(memcmp(assembled,plaintext,sizeof(plaintext))==0);
    assert(clip46_poc_random_iv(iv)==0);
    /* First application message: 10 01 + 16-byte protected query body.
     * This proves the generic primitive only; session key derivation
     * and actual on-wire application mode remain unverified.
     */
    {
        static const unsigned char query_plain[5] =
            {0x01U,0x01U,0x00U,0x00U,0x84U};
        static const char query_cipher_hex[] =
            "6cc37564dd4ce89f1526fa5e80c45999";
        unsigned char query_cipher[16], query_want[16];
        size_t cipher_len;
        for (i = 0U; i < 16U; ++i)
            iv[i] = (unsigned char)(i+16U);
        assert(clip46_poc_decode_hex(query_cipher_hex,
                                    query_want, 16U) == 0);
        cipher_len = 0U;
        assert(clip46_poc_encrypt_application(key, iv,
                 query_plain, sizeof(query_plain), query_cipher,
                 sizeof(query_cipher), &cipher_len) == 0);
        assert(cipher_len == 16U);
        assert(memcmp(query_cipher, query_want, 16U) == 0);
    }
    /* Synthetic AES envelope roundtrip: both observed response sizes. */
    {
        unsigned char sample[144], body[160], decoded[144];
        size_t ct_len, pt_len;
        unsigned int n, k;
        for (k = 0U; k < 2U; ++k) {
            n = k == 0U ? 124U : 136U;
            for (i = 0U; i < n; ++i) sample[i] = (unsigned char)(i ^ 0x5aU);
            for (i = 0U; i < 16U; ++i) iv[i] = (unsigned char)(i+16U);
            memcpy(body, iv, 16U);
            assert(clip46_poc_encrypt_application(key, iv, sample, (size_t)n,
                body+16U, sizeof(body)-16U, &ct_len) == 0);
            assert(clip46_poc_decrypt_envelope(key, body, 16U+ct_len, decoded,
                sizeof(decoded), &pt_len) == 0);
            assert(pt_len == (size_t)n);
            assert(memcmp(sample, decoded, pt_len) == 0);
            body[16U+ct_len-1U] ^= 0x7fU;
            assert(clip46_poc_decrypt_envelope(key, body, 16U+ct_len, decoded,
                sizeof(decoded), &pt_len) != 0);
        }
    }
    /* Connection-ID derivation rule: cid = 0x0E - slot, observed window
     * only (slot 0x03 -> 0x0B first session, 0x04 -> 0x0A second).
     * Out-of-window slots must be rejected, not guessed.
     */
    {
        unsigned char cid;
        cid = 0U;
        assert(clip46_poc_derive_connection_id(0x03U, &cid) == 0);
        assert(cid == 0x0BU);
        cid = 0U;
        assert(clip46_poc_derive_connection_id(0x04U, &cid) == 0);
        assert(cid == 0x0AU);
        cid = 0U;
        assert(clip46_poc_derive_connection_id(0x05U, &cid) == 0);
        assert(cid == 0x09U);
        assert(clip46_poc_derive_connection_id(0x02U, &cid) != 0);
        assert(clip46_poc_derive_connection_id(0x0CU, &cid) != 0);
        assert(clip46_poc_derive_connection_id(0x00U, &cid) != 0);
        assert(clip46_poc_derive_connection_id(0xFFU, &cid) != 0);
        assert(clip46_poc_derive_connection_id(0x03U, NULL) != 0);
    }
    /* OEM special CLIP 0x000B state-entry byte-4 override.
     * INSITE: 81 00 .. 00 00 12 03 ...
     * Before this fix we instead sent byte 4 = last_blocks (e.g. 03).
     * Other protected application requests must retain the reply-block
     * acknowledgment, even if their caller supplies session_id=00.
     */
    {
        unsigned char mode_enter[4], query[7], other_mode[5];
        mode_enter[0] = 0x12U;
        mode_enter[1] = 0x03U;
        mode_enter[2] = 0x00U;
        mode_enter[3] = 0x0BU;
        assert(clip46_poc_outer_ack(0x03U, 0x00U,
                                   mode_enter, sizeof(mode_enter)) == 0U);
        mode_enter[1] = 0x04U;
        assert(clip46_poc_outer_ack(0x34U, 0x00U,
                                   mode_enter, sizeof(mode_enter)) == 0U);
        assert(clip46_poc_outer_ack(0x03U, 0x01U,
                                   mode_enter, sizeof(mode_enter)) == 0x03U);
        query[0] = 0x10U;
        query[1] = 0x02U;
        query[2] = 0x07U;
        query[3] = 0x01U;
        query[4] = 0x00U;
        query[5] = 0x00U;
        query[6] = 0x82U;
        assert(clip46_poc_outer_ack(0x02U, 0x00U,
                                   query, sizeof(query)) == 0x02U);
        other_mode[0] = 0x12U;
        other_mode[1] = 0x05U;
        other_mode[2] = 0x00U;
        other_mode[3] = 0x18U;
        other_mode[4] = 0x00U;
        assert(clip46_poc_outer_ack(0x34U, 0x00U,
                                   other_mode, sizeof(other_mode)) == 0x34U);
        assert(clip46_poc_outer_ack(0x03U, 0x00U,
                                   mode_enter, 3U) == 0x03U);
        assert(clip46_poc_outer_ack(0x03U, 0x00U,
                                   NULL, 0U) == 0x03U);
    }
    puts("CLIP46 C89 AES-CBC synthetic vector passed");
    return 0;
}
