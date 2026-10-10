#ifndef CLIP46_POC_SECURE_H
#define CLIP46_POC_SECURE_H

#include <stddef.h>
#define CLIP46_PLAINTEXT_BYTES 99U
#define CLIP46_BODY_BYTES 128U
#define CLIP46_APP_BYTES 130U

/* No embedded keys. Caller supplies all unknown material. */
struct clip46_poc_config {
    unsigned char key[16];
    unsigned char context[51];
    unsigned char opaque32[32];
};
/* Strict local-only config: KEY_HEX, CONTEXT_HEX, OPAQUE32_HEX. */
int clip46_poc_load_config(const char *path, struct clip46_poc_config *cfg);
int clip46_poc_encrypt(const unsigned char key[16],
                       const unsigned char iv[16],
                       const unsigned char plaintext[CLIP46_PLAINTEXT_BYTES],
                       unsigned char body[CLIP46_BODY_BYTES]);
/* Generic AES-128-CBC/PKCS#7 envelope for application data.
 * Caller MUST supply an independently established session key and IV;
 * the reusable auth key does not qualify as a session key.
 */
int clip46_poc_encrypt_application(const unsigned char key[16],
                                   const unsigned char iv[16],
                                   const unsigned char *plain,
                                   size_t plain_len,
                                   unsigned char *cipher,
                                   size_t capacity,
                                   size_t *cipher_len);
int clip46_poc_build(const unsigned char challenge[16],
                     const unsigned char context51[51],
                     const unsigned char opaque32[32],
                     unsigned char plaintext[CLIP46_PLAINTEXT_BYTES]);
int clip46_poc_decode_hex(const char *hex, unsigned char *output, size_t len);
int clip46_poc_random_iv(unsigned char iv[16]);
void clip46_poc_wipe(void *buffer, size_t n);

#endif
