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

#include "j_int.h"

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
    j_u8 encryption_level;
    j_u8 seed_parameter;
    j_u8 seed[4];
};

/*
 * Distinct secure-handshake family observed on CM2450E.
 * This is NOT the legacy CLIP level-2 (four-byte seed / TEA) mode.
 * The two descriptor bytes are deliberately opaque until validated.
 */
#define CLIP_SECURE_SEED_PDU_SIZE       20U
#define CLIP_SECURE_CHALLENGE_SIZE      16U

struct clip_secure_seed_reply {
    j_u8 descriptor[2];
    j_u8 challenge[CLIP_SECURE_CHALLENGE_SIZE];
};

/* Parse a complete application PDU: 02 02 descriptor[2] challenge[16].
 * This only recognizes the seed; it does not authenticate a session.
 */
int
clip_parse_secure_seed_reply(const j_u8 *pdu,
                             size_t pdu_len,
                             struct clip_secure_seed_reply *reply);

/* Build the initial CLIP session-open request: 01 01 00 00. */
void
clip_build_seed_request(j_u8 out[CLIP_SEED_REQUEST_SIZE]);

/* Parse an 8-byte 01 02 seed reply. */
int
clip_parse_seed_reply(const j_u8 *pdu,
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
clip_build_tool_context(j_u8 out[CLIP_TOOL_CONTEXT_SIZE],
                        j_u8 password_type,
                        const j_u8 password[6],
                        j_u8 tool_family,
                        unsigned long tool_id,
                        j_u32 tool_version,
                        const j_u8 tool_instance[6]);

/* Return encrypted payload size, including trailing BE plaintext length. */
size_t
clip_encrypted_size(size_t plain_len);

/* Encrypt a CLIP payload using encryption level 1 or 2. */
int
clip_encrypt(unsigned int level,
             const j_u8 seed[4],
             const j_u8 *plain,
             size_t plain_len,
             j_u8 *out,
             size_t out_capacity,
             size_t *out_len);

/* Decrypt a CLIP encrypted payload using encryption level 1 or 2. */
int
clip_decrypt(unsigned int level,
             const j_u8 seed[4],
             const j_u8 *in,
             size_t in_len,
             j_u8 *plain,
             size_t plain_capacity,
             size_t *plain_len);

/* Build 01 03 || encrypted(tool_context). */
int
clip_build_context_request(unsigned int level,
                           const j_u8 seed[4],
                           const j_u8 *tool_context,
                           size_t tool_context_len,
                           j_u8 *out,
                           size_t out_capacity,
                           size_t *out_len);

/* Simple opcode recognizers. */
int
clip_is_context_reply(const j_u8 *pdu, size_t pdu_len);

int
clip_is_refused(const j_u8 *pdu, size_t pdu_len);

#endif /* CLIP_CRYPTO_H */
