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
    puts("CLIP46 C89 AES-CBC synthetic vector passed");
    return 0;
}
