#ifndef CLIP46_POC_SECURE_H
#define CLIP46_POC_SECURE_H
#include <stddef.h>
#define CLIP46_PLAINTEXT_BYTES 99U
#define CLIP46_BODY_BYTES 128U
#define CLIP46_APP_BYTES 130U
struct clip46_poc_config {
    unsigned char key[16];
    unsigned char context[51];
};
/* KEY_HEX and CONTEXT_HEX only; reject stale captured session material. */
int clip46_poc_load_config(const char *path, struct clip46_poc_config *cfg);
int clip46_poc_encrypt(const unsigned char key[16],
                       const unsigned char iv[16],
                       const unsigned char plaintext[CLIP46_PLAINTEXT_BYTES],
                       unsigned char body[CLIP46_BODY_BYTES]);
int clip46_poc_encrypt_application(const unsigned char key[16],
                                   const unsigned char iv[16],
                                   const unsigned char *plain,
                                   size_t plain_len,
                                   unsigned char *cipher,
                                   size_t capacity,
                                   size_t *cipher_len);
int clip46_poc_decrypt_cbc(const unsigned char key[16],
                            const unsigned char iv[16],
                            const unsigned char *cipher, size_t cipher_len,
                            unsigned char *plain, size_t capacity,
                            size_t *plain_len);
int clip46_poc_decrypt_envelope(const unsigned char key[16],
                                const unsigned char *body, size_t body_len,
                                unsigned char *plain, size_t capacity,
                                size_t *plain_len);
int clip46_poc_build(const unsigned char challenge[16],
                     const unsigned char context51[51],
                     const unsigned char public_x[32],
                     unsigned char plaintext[CLIP46_PLAINTEXT_BYTES]);
/*
 * Derive the first-protected-request connection ID from the ECM frame slot.
 *
 * Live evidence (combined INSITE debugger + passive bus capture,
 * 2026-10-11, two consecutive sessions against one CM2450E):
 *
 *   session   ECM frame b3 ("slot")   first 10 01 outer b4
 *     1             0x03                    0x0B
 *     2             0x04                    0x0A
 *
 * Ten historical first-session INSITE traces also used 0x0B, and the
 * ECM 02 04 outer b4 is a constant 0x08 in every observed session.
 * The rule connection_id = 0x0E - slot reproduces all observations,
 * adapts to the connection count, and uses no source-address mapping.
 * It remains a two-live-session rule pending an offset-pinned plaintext
 * confirmation inside the decrypted 02 04.
 */
int clip46_poc_derive_connection_id(unsigned char slot,
                                    unsigned char *connection_id);
int clip46_poc_decode_hex(const char *hex, unsigned char *output, size_t len);
int clip46_poc_random_iv(unsigned char iv[16]);
void clip46_poc_wipe(void *buffer, size_t n);
#endif
