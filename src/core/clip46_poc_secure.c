/* Experimental CM2450E authentication-envelope PoC, ISO C89.
 * AES-128-CBC, PKCS#7, with externally-provided key, IV, and plaintext.
 * No OEM private key or credential is embedded here.
 */
#include "clip46_poc_secure.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

static const unsigned char sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static unsigned char xtime(unsigned char x)
{
    return (unsigned char)((x << 1) ^ ((x & 0x80U) ? 0x1bU : 0U));
}
void clip46_poc_wipe(void *buffer, size_t n)
{
    volatile unsigned char *p;
    p = (volatile unsigned char *)buffer;
    while (n-- != 0U) *p++ = 0U;
}
static void expand_key(const unsigned char *key, unsigned char rk[176])
{
    unsigned int i, j;
    unsigned char temp[4], rc;
    memcpy(rk, key, 16U);
    rc = 1U;
    for (i = 16U; i < 176U; i += 4U) {
        for (j = 0U; j < 4U; ++j) temp[j] = rk[i - 4U + j];
        if ((i & 15U) == 0U) {
            unsigned char x;
            x = temp[0];
            temp[0] = sbox[temp[1]];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[x];
            temp[0] ^= rc;
            rc = xtime(rc);
        }
        for (j = 0U; j < 4U; ++j) rk[i+j] = (unsigned char)(rk[i-16U+j] ^ temp[j]);
    }
    clip46_poc_wipe(temp, sizeof(temp));
}
static void aes128_block(const unsigned char in[16], unsigned char out[16], const unsigned char rk[176])
{
    unsigned char state[16], t[16], a,b,c,d,e;
    unsigned int r, i, col, row;
    memcpy(state, in, 16U);
    for (i = 0; i < 16U; ++i) state[i] ^= rk[i];
    for (r = 1U; r <= 10U; ++r) {
        for (i = 0U; i < 16U; ++i) state[i] = sbox[state[i]];
        for (row = 0U; row < 4U; ++row)
            for (col = 0U; col < 4U; ++col)
                t[col*4U+row] = state[((col+row)&3U)*4U+row];
        memcpy(state, t, 16U);
        if (r != 10U) {
            for (col = 0U; col < 4U; ++col) {
                i = 4U*col;
                a=state[i]; b=state[i+1U]; c=state[i+2U]; d=state[i+3U];
                e = (unsigned char)(a^b^c^d);
                state[i] = (unsigned char)(a ^ e ^ xtime((unsigned char)(a^b)));
                state[i+1U] = (unsigned char)(b ^ e ^ xtime((unsigned char)(b^c)));
                state[i+2U] = (unsigned char)(c ^ e ^ xtime((unsigned char)(c^d)));
                state[i+3U] = (unsigned char)(d ^ e ^ xtime((unsigned char)(d^a)));
            }
        }
        for (i = 0U; i < 16U; ++i) state[i] ^= rk[r*16U+i];
    }
    memcpy(out, state, 16U);
    clip46_poc_wipe(state, sizeof(state));
    clip46_poc_wipe(t, sizeof(t));
}
int clip46_poc_encrypt(const unsigned char key[16],
                       const unsigned char iv[16],
                       const unsigned char plaintext[CLIP46_PLAINTEXT_BYTES],
                       unsigned char body[CLIP46_BODY_BYTES])
{
    unsigned char rk[176], prev[16], block[16];
    unsigned int off, i, n;
    if (key == NULL || iv == NULL || plaintext == NULL || body == NULL) return -1;
    expand_key(key,rk);
    memcpy(body,iv,16U);
    memcpy(prev,iv,16U);
    for (off=0U;off<112U;off+=16U) {
        for(i=0U;i<16U;++i) {
            n=off+i;
            block[i]=(unsigned char)((n<99U ? plaintext[n] : 13U)^prev[i]);
        }
        aes128_block(block, body+16U+off, rk);
        memcpy(prev,body+16U+off,16U);
    }
    clip46_poc_wipe(rk,sizeof(rk));
    clip46_poc_wipe(prev,sizeof(prev));
    clip46_poc_wipe(block,sizeof(block));
    return 0;
}
/*
 * CBC + PKCS#7 for independently established application session material.
 * This has been validated with synthetic vectors only. Actual GTIS4.6
 * application cipher/IV/key derivation remains subject to live tracing.
 */
int clip46_poc_encrypt_application(const unsigned char key[16],
                                   const unsigned char iv[16],
                                   const unsigned char *plain,
                                   size_t plain_len,
                                   unsigned char *cipher,
                                   size_t capacity,
                                   size_t *cipher_len)
{
    unsigned char rk[176], prev[16], block[16];
    size_t off, nblocks, total, i, idx, pad;
    if (!key || !iv || !plain || !cipher || !cipher_len)
        return -1;
    if (plain_len > ((size_t)-1) - 16U)
        return -1;
    nblocks = (plain_len / 16U) + 1U;
    total = nblocks * 16U;
    if (capacity < total)
        return -1;
    pad = total - plain_len;
    expand_key(key, rk);
    memcpy(prev, iv, sizeof(prev));
    for (off = 0U; off < total; off += 16U) {
        for (i = 0U; i < 16U; ++i) {
            idx = off + i;
            block[i] = (unsigned char)(
                (idx < plain_len ? plain[idx] : (unsigned char)pad) ^ prev[i]);
        }
        aes128_block(block, cipher + off, rk);
        memcpy(prev, cipher + off, 16U);
    }
    *cipher_len = total;
    clip46_poc_wipe(rk, sizeof(rk));
    clip46_poc_wipe(prev, sizeof(prev));
    clip46_poc_wipe(block, sizeof(block));
    return 0;
}
int clip46_poc_build(const unsigned char challenge[16],
                     const unsigned char context51[51],
                     const unsigned char opaque32[32],
                     unsigned char plaintext[CLIP46_PLAINTEXT_BYTES])
{
    if (!challenge || !context51 || !opaque32 || !plaintext) return -1;
    memcpy(plaintext,challenge,16U);
    memcpy(plaintext+16U,context51,51U);
    memcpy(plaintext+67U,opaque32,32U);
    return 0;
}
static int nibble(int c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
int clip46_poc_decode_hex(const char *hex, unsigned char *out, size_t len)
{
    size_t i;
    int h,l;
    if (!hex || !out || strlen(hex)!=len*2U) return -1;
    for(i=0U;i<len;++i) {
        h=nibble((unsigned char)hex[i*2U]);
        l=nibble((unsigned char)hex[i*2U+1U]);
        if(h<0||l<0) return -1;
        out[i]=(unsigned char)((h<<4)|l);
    }
    return 0;
}
int clip46_poc_random_iv(unsigned char iv[16])
{
#if defined(_WIN32)
    HCRYPTPROV ctx;
    BOOL ok;
    if(!iv) return -1;
    ctx = 0;
    if(!CryptAcquireContextA(&ctx, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return -1;
    ok=CryptGenRandom(ctx,16U,iv);
    CryptReleaseContext(ctx,0);
    return ok ? 0 : -1;
#else
    FILE *f;
    size_t n;
    if(!iv) return -1;
    f=fopen("/dev/urandom","rb");
    if(!f) return -1;
    n=fread(iv,1U,16U,f);
    fclose(f);
    return n==16U ? 0 : -1;
#endif
}
/* Reject unknown/missing/duplicate config fields and overlong lines. */
int clip46_poc_load_config(const char *path, struct clip46_poc_config *cfg)
{
    FILE *fp;
    char line[512];
    unsigned int flags;
    int valid;
    size_t len;
    if (!path || !cfg || !path[0]) return -1;
    memset(cfg,0,sizeof(*cfg));
    fp=fopen(path,"r");
    if (!fp) return -1;
    flags=0U; valid=1;
    while (fgets(line,sizeof(line),fp)!=NULL) {
        len=strlen(line);
        if(len==sizeof(line)-1U && line[len-1U]!='\n') {valid=0;break;}
        while(len>0U && (line[len-1U]=='\n'||line[len-1U]=='\r')) line[--len]='\0';
        if(line[0]=='#'||line[0]=='\0') continue;
        if(strncmp(line,"KEY_HEX=",8U)==0) {
            if((flags&1U)!=0U || clip46_poc_decode_hex(line+8U,cfg->key,16U)!=0) valid=0;
            flags|=1U;
        } else if(strncmp(line,"CONTEXT_HEX=",12U)==0) {
            if((flags&2U)!=0U || clip46_poc_decode_hex(line+12U,cfg->context,51U)!=0) valid=0;
            flags|=2U;
        } else if(strncmp(line,"OPAQUE32_HEX=",13U)==0) {
            if((flags&4U)!=0U || clip46_poc_decode_hex(line+13U,cfg->opaque32,32U)!=0) valid=0;
            flags|=4U;
        } else valid=0;
        if(!valid) break;
    }
    if(ferror(fp)) valid=0;
    fclose(fp);
    if(flags!=7U) valid=0;
    clip46_poc_wipe(line,sizeof(line));
    if(!valid) {clip46_poc_wipe(cfg,sizeof(*cfg));return -1;}
    return 0;
}
