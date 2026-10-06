#ifndef CLIP_CRYPTO_H
#define CLIP_CRYPTO_H

/*
 * clip_crypto.h
 *
 * Public C89 interface for the CLIP session/crypto primitives recovered
 * from reference implementation and verified against the supplied key-cycle capture.
 */

#include <limits.h>
#include <stddef.h>

#if UINT_MAX == 0xffffffffU
typedef unsigned int clip_u32;
#elif ULONG_MAX == 0xffffffffUL
typedef unsigned long clip_u32;
#else
#error "clip_crypto requires a 32-bit unsigned integer type"
#endif

typedef unsigned char clip_u8;

#define CLIP_OK                     0
#define CLIP_ERR_ARGUMENT          -1
#define CLIP_ERR_BUFFER            -2
#define CLIP_ERR_FORMAT            -3
#define CLIP_ERR_LEVEL             -4
#define CLIP_ERR_LENGTH            -5

#define CLIP_TOOL_CONTEXT_SIZE     51U
#define CLIP_SEED_REQUEST_SIZE      4U

#define CLIP_OPCODE_OPEN            0x01U
#define CLIP_OPCODE_SEED            0x02U
#define CLIP_OPCODE_CONTEXT         0x03U
#define CLIP_OPCODE_CONTEXT_REPLY   0x04U
#define CLIP_OPCODE_REFUSED         0x05U

struct clip_seed_reply {
    clip_u8 encryption_level;
    clip_u8 seed_parameter;
    clip_u8 seed[4];
};

/* Build the initial CLIP session-open request: 01 01 00 00. */
void
clip_build_seed_request(clip_u8 out[CLIP_SEED_REQUEST_SIZE]);

/* Parse an 8-byte 01 02 seed reply. */
int
clip_parse_seed_reply(const clip_u8 *pdu,
                      size_t pdu_len,
                      struct clip_seed_reply *reply);

/*
 * Build the fixed 51-byte CLIP tool-context structure.
 *
 * password       must point to 6 bytes.
 * tool_instance  must point to 6 bytes.
 * tool_id        must fit in 16 bits.
 */
int
clip_build_tool_context(clip_u8 out[CLIP_TOOL_CONTEXT_SIZE],
                        clip_u8 password_type,
                        const clip_u8 password[6],
                        clip_u8 tool_family,
                        unsigned long tool_id,
                        clip_u32 tool_version,
                        const clip_u8 tool_instance[6]);

/* Return encrypted payload size, including trailing BE plaintext length. */
size_t
clip_encrypted_size(size_t plain_len);

/* Encrypt a CLIP payload using encryption level 1 or 2. */
int
clip_encrypt(unsigned int level,
             const clip_u8 seed[4],
             const clip_u8 *plain,
             size_t plain_len,
             clip_u8 *out,
             size_t out_capacity,
             size_t *out_len);

/* Decrypt a CLIP encrypted payload using encryption level 1 or 2. */
int
clip_decrypt(unsigned int level,
             const clip_u8 seed[4],
             const clip_u8 *in,
             size_t in_len,
             clip_u8 *plain,
             size_t plain_capacity,
             size_t *plain_len);

/* Build 01 03 || encrypted(tool_context). */
int
clip_build_context_request(unsigned int level,
                           const clip_u8 seed[4],
                           const clip_u8 *tool_context,
                           size_t tool_context_len,
                           clip_u8 *out,
                           size_t out_capacity,
                           size_t *out_len);

/* Simple opcode recognizers. */
int
clip_is_context_reply(const clip_u8 *pdu, size_t pdu_len);

int
clip_is_refused(const clip_u8 *pdu, size_t pdu_len);

#endif /* CLIP_CRYPTO_H */
