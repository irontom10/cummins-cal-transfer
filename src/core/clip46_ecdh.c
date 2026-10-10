/* Native C89 OpenSSL 3 provider for experimental P-256 ECDH.
 * This is intentionally separate from the reusable authentication AES key.
 * On Windows x86, requires 32-bit libcrypto-3.dll.
 * On POSIX, requires libcrypto.so.3; Android must provide its own OpenSSL.
 * Not yet wired into the live GTIS4.6 authentication state machine.
 */
#include "clip46_ecdh.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define CLIP46_NID_PRIME256V1 415
#define CLIP46_POINT_COMPRESSED 2

typedef struct ec_key_st CT_EC_KEY;
typedef struct ec_group_st CT_EC_GROUP;
typedef struct ec_point_st CT_EC_POINT;
typedef struct bignum_ctx CT_BN_CTX;
typedef struct evp_cipher_ctx_st CT_EVP_CIPHER_CTX;
typedef struct evp_cipher_st CT_EVP_CIPHER;

struct ct_crypto_api {
    CT_EC_KEY *(*new_key)(int);
    int (*keygen)(CT_EC_KEY *);
    const CT_EC_GROUP *(*key_group)(const CT_EC_KEY *);
    const CT_EC_POINT *(*key_point)(const CT_EC_KEY *);
    size_t (*point_to_oct)(const CT_EC_GROUP *, const CT_EC_POINT *,
                           int, unsigned char *, size_t, CT_BN_CTX *);
    CT_EC_POINT *(*new_point)(const CT_EC_GROUP *);
    int (*point_from_oct)(const CT_EC_GROUP *, CT_EC_POINT *,
                          const unsigned char *, size_t, CT_BN_CTX *);
    int (*shared_secret)(void *, size_t, const CT_EC_POINT *,
                         const CT_EC_KEY *, void *);
    void (*free_point)(CT_EC_POINT *);
    void (*free_key)(CT_EC_KEY *);
    CT_EVP_CIPHER_CTX *(*new_cipher_ctx)(void);
    void (*free_cipher_ctx)(CT_EVP_CIPHER_CTX *);
    const CT_EVP_CIPHER *(*aes128cbc)(void);
    int (*decrypt_init)(CT_EVP_CIPHER_CTX *, const CT_EVP_CIPHER *,
                        void *, const unsigned char *, const unsigned char *);
    int (*decrypt_update)(CT_EVP_CIPHER_CTX *, unsigned char *, int *,
                          const unsigned char *, int);
    int (*decrypt_final)(CT_EVP_CIPHER_CTX *, unsigned char *, int *);
};

struct ct_ecdh_impl {
#if defined(_WIN32)
    HMODULE dll;
#else
    void *dll;
#endif
    struct ct_crypto_api api;
    CT_EC_KEY *key;
};

static void *crypto_symbol(struct ct_ecdh_impl *s, const char *symbol)
{
#if defined(_WIN32)
    FARPROC p;
    p = GetProcAddress(s->dll, symbol);
    return (void *)p;
#else
    return dlsym(s->dll, symbol);
#endif
}

#define CT_RESOLVE(field_, name_) do { \
    void *sym_; \
    sym_ = crypto_symbol(s, name_); \
    if (sym_ == NULL || sizeof(s->api.field_) != sizeof(sym_)) return -1; \
    memcpy(&s->api.field_, &sym_, sizeof(sym_)); \
} while (0)

static int load_crypto(struct ct_ecdh_impl *s)
{
#if defined(_WIN32)
    s->dll = LoadLibraryA("libcrypto-3.dll");
#else
    s->dll = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
#endif
    if (!s->dll) return -1;
    CT_RESOLVE(new_key, "EC_KEY_new_by_curve_name");
    CT_RESOLVE(keygen, "EC_KEY_generate_key");
    CT_RESOLVE(key_group, "EC_KEY_get0_group");
    CT_RESOLVE(key_point, "EC_KEY_get0_public_key");
    CT_RESOLVE(point_to_oct, "EC_POINT_point2oct");
    CT_RESOLVE(new_point, "EC_POINT_new");
    CT_RESOLVE(point_from_oct, "EC_POINT_oct2point");
    CT_RESOLVE(shared_secret, "ECDH_compute_key");
    CT_RESOLVE(free_point, "EC_POINT_free");
    CT_RESOLVE(free_key, "EC_KEY_free");
    CT_RESOLVE(new_cipher_ctx, "EVP_CIPHER_CTX_new");
    CT_RESOLVE(free_cipher_ctx, "EVP_CIPHER_CTX_free");
    CT_RESOLVE(aes128cbc, "EVP_aes_128_cbc");
    CT_RESOLVE(decrypt_init, "EVP_DecryptInit_ex");
    CT_RESOLVE(decrypt_update, "EVP_DecryptUpdate");
    CT_RESOLVE(decrypt_final, "EVP_DecryptFinal_ex");
    return 0;
}
#undef CT_RESOLVE

static void wipe(void *p, size_t size)
{
    volatile unsigned char *q;
    q = (volatile unsigned char *)p;
    while (size--) *q++ = 0U;
}

void clip46_ecdh_end(struct clip46_ecdh_session *session)
{
    struct ct_ecdh_impl *s;
    if (!session) return;
    s = (struct ct_ecdh_impl *)session->impl;
    if (s) {
        if (s->key && s->api.free_key) s->api.free_key(s->key);
        if (s->dll) {
#if defined(_WIN32)
            FreeLibrary(s->dll);
#else
            dlclose(s->dll);
#endif
        }
        wipe(s, sizeof(*s));
        free(s);
    }
    wipe(session, sizeof(*session));
}

int clip46_ecdh_begin(struct clip46_ecdh_session *session)
{
    struct ct_ecdh_impl *s;
    unsigned char compressed[33];
    size_t length;
    unsigned int attempts;
    if (!session) return -1;
    memset(session, 0, sizeof(*session));
    s = (struct ct_ecdh_impl *)calloc(1U, sizeof(*s));
    if (!s) return -1;
    session->impl = s;
    if (load_crypto(s) != 0) goto error;
    s->key = s->api.new_key(CLIP46_NID_PRIME256V1);
    if (!s->key) goto error;
    /*
     * Current observed tool point prefix is 0x03 in both paired traces.
     * This is a limited bench assumption, not a proven universal rule.
     */
    for (attempts = 0U; attempts < 64U; ++attempts) {
        if (s->api.keygen(s->key) != 1) goto error;
        length = s->api.point_to_oct(s->api.key_group(s->key),
                 s->api.key_point(s->key), CLIP46_POINT_COMPRESSED,
                 compressed, sizeof(compressed), NULL);
        if (length != 33U) goto error;
        if (compressed[0] == 0x03U) {
            memcpy(session->public_x, compressed + 1U, 32U);
            wipe(compressed, sizeof(compressed));
            return 0;
        }
    }
error:
    wipe(compressed, sizeof(compressed));
    clip46_ecdh_end(session);
    return -1;
}

int clip46_ecdh_complete(struct clip46_ecdh_session *session,
                         unsigned char peer_prefix,
                         const unsigned char peer_x[32],
                         unsigned char iv[16], unsigned char key[16])
{
    struct ct_ecdh_impl *s;
    CT_EC_POINT *peer;
    unsigned char encoded[33], secret[32];
    int n;
    int rc;
    if (!session || !peer_x || !iv || !key ||
        (peer_prefix != 0x02U && peer_prefix != 0x03U)) return -1;
    s = (struct ct_ecdh_impl *)session->impl;
    if (!s || !s->key) return -1;
    memset(encoded, 0, sizeof(encoded));
    memset(secret, 0, sizeof(secret));
    encoded[0] = peer_prefix;
    memcpy(encoded + 1U, peer_x, 32U);
    peer = s->api.new_point(s->api.key_group(s->key));
    if (!peer) return -1;
    rc = -1;
    if (s->api.point_from_oct(s->api.key_group(s->key), peer,
                              encoded, sizeof(encoded), NULL) == 1) {
        n = s->api.shared_secret(secret, sizeof(secret), peer, s->key, NULL);
        if (n == 32) {
            memcpy(iv, secret, 16U);
            memcpy(key, secret + 16U, 16U);
            rc = 0;
        }
    }
    s->api.free_point(peer);
    wipe(encoded, sizeof(encoded));
    wipe(secret, sizeof(secret));
    return rc;
}

static int decrypt_cbc(struct ct_ecdh_impl *s,
                       const unsigned char key[16],
                       const unsigned char iv[16],
                       const unsigned char *cipher, size_t cipher_len,
                       unsigned char *out, size_t capacity, size_t *out_len)
{
    CT_EVP_CIPHER_CTX *ctx;
    int first, final_count, ok;
    if (!s || !key || !iv || !cipher || !out || !out_len ||
        cipher_len == 0U || (cipher_len % 16U) != 0U ||
        cipher_len > 4096U || capacity < cipher_len)
        return -1;
    *out_len = 0U;
    ctx = s->api.new_cipher_ctx();
    if (!ctx) return -1;
    first = 0;
    final_count = 0;
    ok = s->api.decrypt_init(ctx,s->api.aes128cbc(),NULL,key,iv) == 1 &&
         s->api.decrypt_update(ctx,out,&first,cipher,(int)cipher_len) == 1 &&
         s->api.decrypt_final(ctx,out+(size_t)first,&final_count) == 1;
    s->api.free_cipher_ctx(ctx);
    if (!ok || first < 0 || final_count < 0 ||
        (size_t)(first + final_count) > capacity) {
        wipe(out,capacity);
        return -1;
    }
    *out_len = (size_t)(first + final_count);
    return 0;
}

int clip46_ecdh_decrypt_auth(struct clip46_ecdh_session *session,
                             const unsigned char auth_key[16],
                             const unsigned char *body, size_t body_len,
                             unsigned char *out, size_t capacity,
                             size_t *out_len)
{
    struct ct_ecdh_impl *s;
    if (!session || !session->impl || !body || body_len < 32U ||
        (body_len % 16U) != 0U) return -1;
    s = (struct ct_ecdh_impl *)session->impl;
    return decrypt_cbc(s,auth_key,body,body+16U,body_len-16U,
                       out,capacity,out_len);
}

int clip46_ecdh_decrypt_app(struct clip46_ecdh_session *session,
                            const unsigned char key[16],
                            const unsigned char iv[16],
                            const unsigned char *cipher, size_t cipher_len,
                            unsigned char *out, size_t capacity,
                            size_t *out_len)
{
    struct ct_ecdh_impl *s;
    if (!session || !session->impl) return -1;
    s = (struct ct_ecdh_impl *)session->impl;
    return decrypt_cbc(s,key,iv,cipher,cipher_len,out,capacity,out_len);
}
