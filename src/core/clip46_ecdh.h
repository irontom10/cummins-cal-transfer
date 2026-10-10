#ifndef CLIP46_ECDH_H
#define CLIP46_ECDH_H

#include <stddef.h>

/* Experimental P-256 provider. OpenSSL 3 runtime required.
 * Private scalar is retained inside the session and erased by cleanup.
 * This interface is not yet wired into the GTIS4.6 authentication path.
 */
struct clip46_ecdh_session {
    void *impl;
    unsigned char public_x[32];
};

int clip46_ecdh_begin(struct clip46_ecdh_session *session);
int clip46_ecdh_complete(struct clip46_ecdh_session *session,
                         unsigned char peer_prefix,
                         const unsigned char peer_x[32],
                         unsigned char iv[16], unsigned char key[16]);
int clip46_ecdh_decrypt_auth(struct clip46_ecdh_session *session,
                             const unsigned char auth_key[16],
                             const unsigned char *body, size_t body_len,
                             unsigned char *out, size_t capacity,
                             size_t *out_len);
int clip46_ecdh_decrypt_app(struct clip46_ecdh_session *session,
                            const unsigned char key[16],
                            const unsigned char iv[16],
                            const unsigned char *cipher, size_t cipher_len,
                            unsigned char *out, size_t capacity,
                            size_t *out_len);
void clip46_ecdh_end(struct clip46_ecdh_session *session);

#endif
