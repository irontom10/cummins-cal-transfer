/*
 * CM2450E field-log regression tests for the secure 02 02 seed family.
 * These are synthetic challenge bytes: no ECM credentials or raw traces.
 * ISO C89.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "clip_crypto.h"

static void
test_legacy_remains_legacy(void)
{
    static const clip_u8 legacy[] = {
        0x01U, 0x02U, 0x02U, 0x00U,
        0x10U, 0x20U, 0x30U, 0x40U
    };
    struct clip_seed_reply reply;
    struct clip_secure_seed_reply secure;

    assert(clip_parse_seed_reply(legacy, sizeof(legacy), &reply)
           == CLIP_OK);
    assert(reply.encryption_level == 2U);
    assert(reply.seed[3] == 0x40U);
    assert(clip_parse_secure_seed_reply(legacy, sizeof(legacy), &secure)
           == CLIP_ERR_FORMAT);
}

static void
test_secure_seed_is_distinct(void)
{
    static const clip_u8 secure_pdu[CLIP_SECURE_SEED_PDU_SIZE] = {
        0x02U, 0x02U, 0x6bU, 0x1fU,
        0x10U, 0x22U, 0x34U, 0x46U,
        0x58U, 0x6aU, 0x7cU, 0x8eU,
        0x90U, 0xa2U, 0xb4U, 0xc6U,
        0xd8U, 0xeaU, 0xfcU, 0x0eU
    };
    struct clip_seed_reply legacy;
    struct clip_secure_seed_reply reply;

    assert(clip_parse_seed_reply(secure_pdu, sizeof(secure_pdu),
                                  &legacy) == CLIP_ERR_FORMAT);
    assert(clip_parse_secure_seed_reply(
               secure_pdu, sizeof(secure_pdu), &reply) == CLIP_OK);
    assert(reply.descriptor[0] == 0x6bU);
    assert(reply.descriptor[1] == 0x1fU);
    assert(memcmp(reply.challenge, secure_pdu + 4U,
                  CLIP_SECURE_CHALLENGE_SIZE) == 0);
}

static void
test_malformed_secure_seed_rejected(void)
{
    clip_u8 pdu[CLIP_SECURE_SEED_PDU_SIZE + 1U];
    struct clip_secure_seed_reply reply;

    memset(pdu, 0x55, sizeof(pdu));
    pdu[0] = 0x02U;
    pdu[1] = 0x02U;

    assert(clip_parse_secure_seed_reply(NULL, sizeof(pdu) - 1U,
                                        &reply) == CLIP_ERR_ARGUMENT);
    assert(clip_parse_secure_seed_reply(pdu, sizeof(pdu) - 1U,
                                        NULL) == CLIP_ERR_ARGUMENT);
    assert(clip_parse_secure_seed_reply(pdu, sizeof(pdu) - 2U,
                                        &reply) == CLIP_ERR_FORMAT);
    assert(clip_parse_secure_seed_reply(pdu, sizeof(pdu),
                                        &reply) == CLIP_ERR_FORMAT);

    pdu[1] = 0x03U;
    assert(clip_parse_secure_seed_reply(pdu, sizeof(pdu) - 1U,
                                        &reply) == CLIP_ERR_FORMAT);
}

int
main(void)
{
    test_legacy_remains_legacy();
    test_secure_seed_is_distinct();
    test_malformed_secure_seed_rejected();
    puts("secure CLIP seed tests passed");
    return 0;
}
