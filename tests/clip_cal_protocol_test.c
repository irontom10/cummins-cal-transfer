#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "clip_cal.h"

static void
test_mode_request(void)
{
    clip_u8 out[CLIP_CAL_PHASE_REQUEST_SIZE];
    static const clip_u8 expected[CLIP_CAL_PHASE_REQUEST_SIZE] = {
        0x12U, 0x7eU, 0x00U, 0x17U, 0x00U
    };

    assert(clip_cal_build_mode_request(0x7eU, 0x0017U, 0x00U, out) ==
           CLIP_CAL_OK);
    assert(memcmp(out, expected, sizeof(expected)) == 0);
}

static void
test_query_request(void)
{
    clip_u8 out[CLIP_CAL_QUERY_REQUEST_SIZE];
    static const clip_u8 expected[CLIP_CAL_QUERY_REQUEST_SIZE] = {
        0x10U, 0x22U, 0x01U, 0x01U, 0x00U, 0xb8U, 0x25U
    };

    assert(clip_cal_build_query_request(0x22U, 0x00b825UL, out) ==
           CLIP_CAL_OK);
    assert(memcmp(out, expected, sizeof(expected)) == 0);
}

static void
test_read_request(void)
{
    clip_u8 out[CLIP_CAL_READ_REQUEST_SIZE];
    static const clip_u8 expected[CLIP_CAL_READ_REQUEST_SIZE] = {
        0x13U, 0x33U, 0x12U, 0x34U, 0x56U, 0x78U, 0x03U, 0xe8U
    };

    assert(clip_cal_build_read_request(
               0x33U, (clip_u32)0x12345678UL, 1000U, out) ==
           CLIP_CAL_OK);
    assert(memcmp(out, expected, sizeof(expected)) == 0);
}

static void
test_memory_descriptor(void)
{
    static const clip_u8 reply[] = {
        0x01U, 0x2aU,
        0x11U, 0x22U, 0x33U, 0x44U,
        0x00U, 0x01U,
        0x00U, 0x01U,
        0x00U, 0x10U, 0x00U, 0x00U,
        0x55U, 0x66U, 0x77U, 0x88U,
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x03U, 0xe8U
    };
    struct clip_cal_map map;

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply, sizeof(reply), 0x2aU, &map) == CLIP_CAL_OK);
    assert(map.descriptor_value == (clip_u32)0x11223344UL);
    assert(map.range_count == 1U);
    assert(map.ranges[0].address == (clip_u32)0x00100000UL);
    assert(map.ranges[0].auxiliary == (clip_u32)0x55667788UL);
    assert(map.ranges[0].length == (clip_u32)1000UL);
    assert(map.trailer_len == 0U);
}

static void
test_sequence_and_crc(void)
{
    static const unsigned char text[] = "123456789";

    assert(clip_cal_next_sequence(0xfeU) == 0x00U);
    assert(clip_cal_next_sequence(0x00U) == 0x01U);
    assert(clip_cal_crc16_kermit(text, 9UL) == 0x2189U);
}

int
main(void)
{
    test_mode_request();
    test_query_request();
    test_read_request();
    test_memory_descriptor();
    test_sequence_and_crc();

    puts("clip_cal protocol tests passed");
    return 0;
}
