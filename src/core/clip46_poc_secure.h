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
 * SUPERSEDED INVESTIGATION RECORD - no longer used by the session flow.
 *
 * This rule reproduced the observed first-request byte 4 values (slot
 * 0x03 -> 0x0B, 0x04 -> 0x0A) but was a coincidence: byte 4 is the
 * transport block-count acknowledgement ceil(previous_reply_wire_len/16)
 * of the preceding reply (167-byte 02 04 -> 0x0B, 151-byte -> 0x0A), as
 * documented in clip_transfer.c and verified against the reference
 * capture (23B -> 0x02, 823B -> 0x34). Retained with its tests as the
 * record of the investigation that the live stall pattern disproved.
 */
int clip46_poc_derive_connection_id(unsigned char slot,
                                    unsigned char *connection_id);
int clip46_poc_decode_hex(const char *hex, unsigned char *output, size_t len);
int clip46_poc_random_iv(unsigned char iv[16]);
void clip46_poc_wipe(void *buffer, size_t n);
#endif
