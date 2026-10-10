#ifndef CLIP46_P256_H
#define CLIP46_P256_H

/* Portable ISO C89 interface; caller must wipe retained secrets. */
struct clip46_p256_keypair {
    unsigned char private_scalar[32];
    unsigned char compressed_public[33];
};
int clip46_p256_generate(struct clip46_p256_keypair *pair);
int clip46_p256_public_from_private(const unsigned char scalar[32],
                                    unsigned char compressed[33]);
/* ECM transports X alone. Validate both parities and require
 * identical derived X-only shared secrets. */
int clip46_p256_ecdh_xonly(const unsigned char scalar[32],
                           const unsigned char peer_x[32],
                           unsigned char shared[32]);
#endif
