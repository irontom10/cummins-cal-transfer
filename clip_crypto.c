/*
 * clip_crypto.c
 *
 * C89 implementation of the CLIP session primitives recovered from
 * PCLWrapper.dll.
 *
 * Implements:
 *   - 01 01 00 00 seed request
 *   - 01 02 seed reply parsing
 *   - 51-byte tool-context serialization
 *   - level 1 / level 2 seed-derived TEA keys
 *   - CLIP TEA encryption/decryption framing
 *   - 01 03 encrypted context request
 *   - simple 01 04 / 01 05 reply recognition
 *
 * The final partial TEA block is zero-padded.  This has been verified
 * against the supplied eight-key-cycle capture: both unique 51-byte tool
 * contexts re-encrypt byte-for-byte to the captured 01 03 requests, including
 * the final padded block.
 *
 * In the observed 01 02 seed replies, byte 2 is the encryption level used
 * for both the outgoing 01 03 context request and the incoming encrypted
 * 01 04 context reply.  Byte 3 is preserved as a separate seed parameter;
 * its semantic meaning is not asserted here (it is 0x00 in all supplied
 * samples).
 */

#include "clip_crypto.h"
#include <string.h>

static clip_u32
clip_load_be32(const clip_u8 *p)
{
    clip_u32 v;

    v = ((clip_u32)p[0] << 24);
    v |= ((clip_u32)p[1] << 16);
    v |= ((clip_u32)p[2] << 8);
    v |= (clip_u32)p[3];
    return v;
}

static void
clip_store_be32(clip_u8 *p, clip_u32 v)
{
    p[0] = (clip_u8)(v >> 24);
    p[1] = (clip_u8)(v >> 16);
    p[2] = (clip_u8)(v >> 8);
    p[3] = (clip_u8)v;
}

static void
clip_store_be16(clip_u8 *p, unsigned long v)
{
    p[0] = (clip_u8)((v >> 8) & 0xffUL);
    p[1] = (clip_u8)(v & 0xffUL);
}

static void
clip_put_field_header(clip_u8 *out, clip_u32 id, clip_u8 length)
{
    clip_store_be32(out, id);
    out[4] = length;
}

void
clip_build_seed_request(clip_u8 out[CLIP_SEED_REQUEST_SIZE])
{
    out[0] = 0x01U;
    out[1] = CLIP_OPCODE_OPEN;
    out[2] = 0x00U;
    out[3] = 0x00U;
}

int
clip_parse_seed_reply(const clip_u8 *pdu, size_t pdu_len,
                      struct clip_seed_reply *reply)
{
    if (pdu == NULL || reply == NULL)
        return CLIP_ERR_ARGUMENT;

    if (pdu_len < 8U)
        return CLIP_ERR_FORMAT;

    if (pdu[0] != 0x01U || pdu[1] != CLIP_OPCODE_SEED)
        return CLIP_ERR_FORMAT;

    reply->encryption_level = pdu[2];
    reply->seed_parameter = pdu[3];
    reply->seed[0] = pdu[4];
    reply->seed[1] = pdu[5];
    reply->seed[2] = pdu[6];
    reply->seed[3] = pdu[7];

    return CLIP_OK;
}

/*
 * Serialized tool context, exactly 0x33 bytes:
 *
 *   06
 *   01 00 22 28 01 [password_type]
 *   01 00 22 29 06 [password:6]
 *   01 00 22 2A 01 [tool_family]
 *   01 00 22 2B 02 [tool_id:2, BE]
 *   01 00 22 2C 04 [tool_version:4, BE]
 *   01 00 22 2D 06 [tool_instance:6]
 */
int
clip_build_tool_context(clip_u8 out[CLIP_TOOL_CONTEXT_SIZE],
                        clip_u8 password_type,
                        const clip_u8 password[6],
                        clip_u8 tool_family,
                        unsigned long tool_id,
                        clip_u32 tool_version,
                        const clip_u8 tool_instance[6])
{
    size_t n;

    if (out == NULL || password == NULL || tool_instance == NULL)
        return CLIP_ERR_ARGUMENT;

    if (tool_id > 0xffffUL)
        return CLIP_ERR_LENGTH;

    n = 0U;
    out[n++] = 0x06U;

    clip_put_field_header(out + n, (clip_u32)0x01002228, 1U);
    n += 5U;
    out[n++] = password_type;

    clip_put_field_header(out + n, (clip_u32)0x01002229, 6U);
    n += 5U;
    memcpy(out + n, password, 6U);
    n += 6U;

    clip_put_field_header(out + n, (clip_u32)0x0100222a, 1U);
    n += 5U;
    out[n++] = tool_family;

    clip_put_field_header(out + n, (clip_u32)0x0100222b, 2U);
    n += 5U;
    clip_store_be16(out + n, tool_id);
    n += 2U;

    clip_put_field_header(out + n, (clip_u32)0x0100222c, 4U);
    n += 5U;
    clip_store_be32(out + n, tool_version);
    n += 4U;

    clip_put_field_header(out + n, (clip_u32)0x0100222d, 6U);
    n += 5U;
    memcpy(out + n, tool_instance, 6U);
    n += 6U;

    if (n != CLIP_TOOL_CONTEXT_SIZE)
        return CLIP_ERR_FORMAT;

    return CLIP_OK;
}

/*
 * PCLWrapper.dll level-key derivation.
 *
 * The DLL starts with 16 bytes of FF, reverse-copies the four seed bytes
 * into the first four positions, then XORs that 16-byte mask with one of
 * the embedded 128-bit level keys.
 *
 * Because the resulting key words are consumed natively on little-endian
 * x86, this becomes the following four 32-bit words.
 */
static int
clip_derive_key(unsigned int level, const clip_u8 seed[4], clip_u32 key[4])
{
    clip_u32 s;

    if (seed == NULL || key == NULL)
        return CLIP_ERR_ARGUMENT;

    s = clip_load_be32(seed);

    if (level == 1U) {
        key[0] = s ^ (clip_u32)0xa79bdcfe;
        key[1] = (clip_u32)0x59d5e6af;
        key[2] = (clip_u32)0x20a384bc;
        key[3] = (clip_u32)0x6078b0b9;
        return CLIP_OK;
    }

    if (level == 2U) {
        key[0] = s ^ (clip_u32)0x67dbc719;
        key[1] = (clip_u32)0xc3636180;
        key[2] = (clip_u32)0xa8536675;
        key[3] = (clip_u32)0x1faff9d4;
        return CLIP_OK;
    }

    return CLIP_ERR_LEVEL;
}

static void
clip_tea_encrypt_block(const clip_u8 in[8], clip_u8 out[8],
                       const clip_u32 key[4])
{
    clip_u32 v0;
    clip_u32 v1;
    clip_u32 sum;
    clip_u32 delta;
    unsigned int i;

    v0 = clip_load_be32(in);
    v1 = clip_load_be32(in + 4);
    sum = (clip_u32)0;
    delta = (clip_u32)0x9e3779b9;

    for (i = 0U; i < 32U; ++i) {
        sum += delta;
        v0 += (((v1 << 4) + key[0]) ^
               (v1 + sum) ^
               ((v1 >> 5) + key[1]));
        v1 += (((v0 << 4) + key[2]) ^
               (v0 + sum) ^
               ((v0 >> 5) + key[3]));
    }

    clip_store_be32(out, v0);
    clip_store_be32(out + 4, v1);
}

static void
clip_tea_decrypt_block(const clip_u8 in[8], clip_u8 out[8],
                       const clip_u32 key[4])
{
    clip_u32 v0;
    clip_u32 v1;
    clip_u32 sum;
    clip_u32 delta;
    unsigned int i;

    v0 = clip_load_be32(in);
    v1 = clip_load_be32(in + 4);
    delta = (clip_u32)0x9e3779b9;
    sum = (clip_u32)0xc6ef3720;

    for (i = 0U; i < 32U; ++i) {
        v1 -= (((v0 << 4) + key[2]) ^
               (v0 + sum) ^
               ((v0 >> 5) + key[3]));
        v0 -= (((v1 << 4) + key[0]) ^
               (v1 + sum) ^
               ((v1 >> 5) + key[1]));
        sum -= delta;
    }

    clip_store_be32(out, v0);
    clip_store_be32(out + 4, v1);
}

size_t
clip_encrypted_size(size_t plain_len)
{
    size_t blocks;
    size_t padded_len;

    if (plain_len > ((size_t)-1) - 7U)
        return 0U;

    blocks = (plain_len + 7U) / 8U;

    if (blocks > (((size_t)-1) - 4U) / 8U)
        return 0U;

    padded_len = blocks * 8U;
    return padded_len + 4U;
}

/*
 * CLIP encrypted payload format:
 *
 *   TEA(8-byte block 0)
 *   TEA(8-byte block 1)
 *   ...
 *   uint32_be(original_plaintext_length)
 */
int
clip_encrypt(unsigned int level,
             const clip_u8 seed[4],
             const clip_u8 *plain,
             size_t plain_len,
             clip_u8 *out,
             size_t out_capacity,
             size_t *out_len)
{
    clip_u32 key[4];
    size_t padded_len;
    size_t total_len;
    size_t off;
    int rc;

    if (seed == NULL || plain == NULL || out == NULL || out_len == NULL)
        return CLIP_ERR_ARGUMENT;

    if (plain_len > (size_t)((clip_u32)-1))
        return CLIP_ERR_LENGTH;

    if (plain_len > ((size_t)-1) - 7U)
        return CLIP_ERR_LENGTH;

    padded_len = ((plain_len + 7U) / 8U) * 8U;
    if (padded_len > ((size_t)-1) - 4U)
        return CLIP_ERR_LENGTH;

    total_len = padded_len + 4U;
    if (out_capacity < total_len)
        return CLIP_ERR_BUFFER;

    rc = clip_derive_key(level, seed, key);
    if (rc != CLIP_OK)
        return rc;

    memset(out, 0, total_len);
    memcpy(out, plain, plain_len);

    off = 0U;
    while (off < padded_len) {
        clip_u8 block[8];

        clip_tea_encrypt_block(out + off, block, key);
        memcpy(out + off, block, 8U);
        off += 8U;
    }

    clip_store_be32(out + padded_len, (clip_u32)plain_len);
    *out_len = total_len;
    return CLIP_OK;
}

int
clip_decrypt(unsigned int level,
             const clip_u8 seed[4],
             const clip_u8 *in,
             size_t in_len,
             clip_u8 *plain,
             size_t plain_capacity,
             size_t *plain_len)
{
    clip_u32 key[4];
    clip_u32 original_len32;
    size_t original_len;
    size_t cipher_len;
    size_t off;
    size_t copy_len;
    size_t remaining;
    int rc;

    if (seed == NULL || in == NULL || plain == NULL || plain_len == NULL)
        return CLIP_ERR_ARGUMENT;

    if (in_len < 4U)
        return CLIP_ERR_FORMAT;

    cipher_len = in_len - 4U;
    if ((cipher_len & 7U) != 0U)
        return CLIP_ERR_FORMAT;

    original_len32 = clip_load_be32(in + cipher_len);
    original_len = (size_t)original_len32;

    if (original_len > cipher_len)
        return CLIP_ERR_FORMAT;

    if (plain_capacity < original_len)
        return CLIP_ERR_BUFFER;

    rc = clip_derive_key(level, seed, key);
    if (rc != CLIP_OK)
        return rc;

    off = 0U;
    remaining = original_len;
    while (off < cipher_len) {
        clip_u8 block[8];

        clip_tea_decrypt_block(in + off, block, key);

        copy_len = remaining;
        if (copy_len > 8U)
            copy_len = 8U;

        if (copy_len != 0U)
            memcpy(plain + off, block, copy_len);

        if (remaining >= copy_len)
            remaining -= copy_len;
        else
            remaining = 0U;

        off += 8U;
    }

    *plain_len = original_len;
    return CLIP_OK;
}

int
clip_build_context_request(unsigned int level,
                           const clip_u8 seed[4],
                           const clip_u8 *tool_context,
                           size_t tool_context_len,
                           clip_u8 *out,
                           size_t out_capacity,
                           size_t *out_len)
{
    size_t encrypted_len;
    int rc;

    if (seed == NULL || tool_context == NULL || out == NULL || out_len == NULL)
        return CLIP_ERR_ARGUMENT;

    if (out_capacity < 2U)
        return CLIP_ERR_BUFFER;

    out[0] = 0x01U;
    out[1] = CLIP_OPCODE_CONTEXT;

    rc = clip_encrypt(level,
                      seed,
                      tool_context,
                      tool_context_len,
                      out + 2U,
                      out_capacity - 2U,
                      &encrypted_len);
    if (rc != CLIP_OK)
        return rc;

    *out_len = encrypted_len + 2U;
    return CLIP_OK;
}

int
clip_is_context_reply(const clip_u8 *pdu, size_t pdu_len)
{
    if (pdu == NULL || pdu_len < 2U)
        return 0;

    return pdu[0] == 0x01U && pdu[1] == CLIP_OPCODE_CONTEXT_REPLY;
}

int
clip_is_refused(const clip_u8 *pdu, size_t pdu_len)
{
    if (pdu == NULL || pdu_len < 2U)
        return 0;

    return pdu[0] == 0x01U && pdu[1] == CLIP_OPCODE_REFUSED;
}
