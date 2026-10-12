#include "da00_sendkey.h"
#include <stdio.h>
#include <string.h>

static const unsigned char expected[32] = {
    0x95,0x4f,0x64,0xf2,0xe4,0xe8,0x6e,0x9e,
    0xee,0x82,0xd2,0x02,0x16,0x68,0x48,0x99,
    0x5d,0x72,0xd0,0x07,0xa9,0x2a,0x20,0x38,
    0x7e,0xbd,0xfa,0x04,0x24,0xf0,0x44,0x96
};
int main(void)
{
    unsigned char seed[32], key[16], family[6], out[32];
    unsigned int i;
    for (i = 0U; i < 32U; ++i) seed[i] = (unsigned char)i;
    for (i = 0U; i < 16U; ++i) key[i] = (unsigned char)i;
    family[0]=1U; family[1]=2U; family[2]=3U;
    family[3]=4U; family[4]=5U; family[5]=6U;
    if (!da00_sendkey(seed,key,15U,0x10000006UL,family,1,2U,out) ||
        memcmp(expected,out,32U) != 0) {
        puts("DA00 offline synthetic: FAIL");
        return 1;
    }
    if (da00_sendkey(seed,key,65536U,0UL,family,1,2U,out) ||
        da00_sendkey(seed,key,15U,0UL,family,2,2U,out)) {
        puts("DA00 input validation: FAIL");
        return 1;
    }
    puts("DA00 offline synthetic: PASS");
    return 0;
}
