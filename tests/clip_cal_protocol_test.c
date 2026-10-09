#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "clip_cal.h"
#include "ccal_crc.h"

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

/*
 * Captured in unlock.jlog (PropA / CLIP application PDU):
 * 12 90 00 18 00 -> 01 90 FF FF (unlock)
 * 12 97 00 18 01 -> 01 97 FF FF (lock)
 * 12 9E 00 18 00 -> 01 9E FF FF (unlock)
 * The sequence is dynamic; no captured sequence bytes are replayed.
 */
static void
test_calibration_lock_modes(void)
{
    clip_u8 request[CLIP_CAL_PHASE_REQUEST_SIZE];
    clip_u8 reply[4];
    const clip_u8 *data;
    size_t data_len;
    unsigned int i;
    static const clip_u8 sequences[3] = {0x90U, 0x97U, 0x9eU};
    static const clip_u8 values[3] = {0x00U, 0x01U, 0x00U};

    for (i = 0U; i < 3U; ++i) {
        assert(clip_cal_build_mode_request(
            sequences[i], 0x0018U, values[i], request) == CLIP_CAL_OK);
        assert(request[0] == 0x12U);
        assert(request[1] == sequences[i]);
        assert(request[2] == 0x00U);
        assert(request[3] == 0x18U);
        assert(request[4] == values[i]);

        reply[0] = 0x01U;
        reply[1] = sequences[i];
        reply[2] = 0xffU;
        reply[3] = 0xffU;
        assert(clip_cal_parse_reply(
            reply, sizeof(reply), sequences[i], &data, &data_len)
            == CLIP_CAL_OK);
        assert(data_len == 2U);
        assert(data[0] == 0xffU && data[1] == 0xffU);
        assert(clip_cal_parse_lock_ack(
            reply, sizeof(reply), sequences[i]) == CLIP_CAL_OK);
        assert(clip_cal_parse_lock_ack(
            reply, sizeof(reply), (clip_u8)(sequences[i] + 1U))
            == CLIP_CAL_ERR_SEQUENCE);
        reply[3] = 0x00U;
        assert(clip_cal_parse_lock_ack(
            reply, sizeof(reply), sequences[i]) == CLIP_CAL_ERR_FORMAT);
        reply[3] = 0xffU;
        assert(clip_cal_parse_lock_ack(
            reply, sizeof(reply) - 1U, sequences[i]) == CLIP_CAL_ERR_FORMAT);
    }
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
    /*
     * Exact 74-byte positive descriptor reply captured from a CM23xx ECM.
     * It proves the second 16-bit field is an element width, not a duplicate
     * range count: range_count=5 while all three array widths are 4 bytes.
     */
    static const clip_u8 reply[] = {
        0x01U, 0x17U,
        0x00U, 0x46U, 0x06U, 0x72U,
        0x00U, 0x05U,
        0x00U, 0x04U,

        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x50U, 0x00U, 0x00U,
        0x00U, 0x60U, 0x00U, 0x00U,
        0x00U, 0x64U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x02U, 0x80U,

        0x00U, 0x04U,
        0x00U, 0x06U, 0x00U, 0x00U,
        0x00U, 0x0fU, 0x00U, 0x00U,
        0x00U, 0x00U, 0x49U, 0x30U,
        0x00U, 0x0cU, 0x00U, 0x00U,
        0x00U, 0x00U, 0x1dU, 0x7fU,

        0x00U, 0x04U,
        0x00U, 0x05U, 0x11U, 0xf8U,
        0x00U, 0x0dU, 0x4cU, 0xa4U,
        0x00U, 0x00U, 0x49U, 0x30U,
        0x00U, 0x07U, 0x6eU, 0x30U,
        0x00U, 0x00U, 0x0bU, 0xa4U
    };
    struct clip_cal_map map;

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply, sizeof(reply), 0x17U, &map) == CLIP_CAL_OK);

    assert(map.descriptor_value == (clip_u32)0x00460672UL);
    assert(map.range_count == 5U);

    assert(map.ranges[0].address == (clip_u32)0x00010000UL);
    assert(map.ranges[0].auxiliary == (clip_u32)0x00060000UL);
    assert(map.ranges[0].length == (clip_u32)0x000511f8UL);

    assert(map.ranges[1].address == (clip_u32)0x00500000UL);
    assert(map.ranges[1].auxiliary == (clip_u32)0x000f0000UL);
    assert(map.ranges[1].length == (clip_u32)0x000d4ca4UL);

    assert(map.ranges[2].address == (clip_u32)0x00600000UL);
    assert(map.ranges[2].auxiliary == (clip_u32)0x00004930UL);
    assert(map.ranges[2].length == (clip_u32)0x00004930UL);

    assert(map.ranges[3].address == (clip_u32)0x00640000UL);
    assert(map.ranges[3].auxiliary == (clip_u32)0x000c0000UL);
    assert(map.ranges[3].length == (clip_u32)0x00076e30UL);

    assert(map.ranges[4].address == (clip_u32)0x01000280UL);
    assert(map.ranges[4].auxiliary == (clip_u32)0x00001d7fUL);
    assert(map.ranges[4].length == (clip_u32)0x00000ba4UL);

    assert(map.trailer_len == 0U);
}

static void
test_memory_descriptor_corpus_shapes(void)
{
    /*
     * Distilled descriptor vectors from the December 2025 INCAL corpus.
     * These cover every range-count/width shape measured in that scan:
     * 3/4/4/4, 4/4/4/4, and 5/4/4/4.
     */
    static const clip_u8 reply3[] = {
        0x01U, 0x00U,
        0x00U, 0x2eU, 0x06U, 0xd6U,
        0x00U, 0x03U, 0x00U, 0x04U,
        0x00U, 0x00U, 0x10U, 0x80U,
        0x00U, 0x00U, 0x41U, 0x00U,
        0x00U, 0x04U, 0x00U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x2fU, 0x78U,
        0x00U, 0x00U, 0x62U, 0xf8U,
        0x00U, 0x1cU, 0x00U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x2fU, 0x78U,
        0x00U, 0x00U, 0x55U, 0xb0U,
        0x00U, 0x1aU, 0xbfU, 0xd8U
    };
    static const clip_u8 reply4[] = {
        0x01U, 0x00U,
        0x00U, 0x3aU, 0x06U, 0xd6U,
        0x00U, 0x04U, 0x00U, 0x04U,
        0x00U, 0xa0U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x10U, 0x80U,
        0x00U, 0x00U, 0x41U, 0x00U,
        0x00U, 0x10U, 0x00U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x00U, 0x10U,
        0x00U, 0x00U, 0x2fU, 0x78U,
        0x00U, 0x00U, 0xb6U, 0x57U,
        0x00U, 0x10U, 0x00U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x00U, 0x10U,
        0x00U, 0x00U, 0x2fU, 0x78U,
        0x00U, 0x00U, 0xb6U, 0x50U,
        0x00U, 0x09U, 0x02U, 0x38U
    };
    static const clip_u8 reply5[] = {
        0x01U, 0x00U,
        0x00U, 0x46U, 0x00U, 0xf0U,
        0x00U, 0x05U, 0x00U, 0x04U,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x04U, 0xc0U, 0x00U,
        0x00U, 0x52U, 0x00U, 0x00U,
        0x00U, 0x5fU, 0x00U, 0x00U,
        0x01U, 0x00U, 0x01U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x03U, 0xc0U, 0x00U,
        0x00U, 0x02U, 0x40U, 0x00U,
        0x00U, 0x0aU, 0x80U, 0x00U,
        0x00U, 0x00U, 0x80U, 0x00U,
        0x00U, 0x00U, 0x3eU, 0xfeU,
        0x00U, 0x04U,
        0x00U, 0x03U, 0x9dU, 0xe0U,
        0x00U, 0x02U, 0x36U, 0x5cU,
        0x00U, 0x0aU, 0x46U, 0xd8U,
        0x00U, 0x00U, 0x49U, 0xf8U,
        0x00U, 0x00U, 0x15U, 0x2aU
    };
    struct clip_cal_map map;

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply3, sizeof(reply3), 0x00U, &map) == CLIP_CAL_OK);
    assert(map.descriptor_value == (clip_u32)0x002e06d6UL);
    assert(map.range_count == 3U);
    assert(map.ranges[0].address == (clip_u32)0x00001080UL);
    assert(map.ranges[2].address == (clip_u32)0x00040000UL);
    assert(map.ranges[2].length == (clip_u32)0x001abfd8UL);

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply4, sizeof(reply4), 0x00U, &map) == CLIP_CAL_OK);
    assert(map.descriptor_value == (clip_u32)0x003a06d6UL);
    assert(map.range_count == 4U);
    assert(map.ranges[0].address == (clip_u32)0x00a00000UL);
    assert(map.ranges[3].address == (clip_u32)0x00100000UL);
    assert(map.ranges[3].length == (clip_u32)0x00090238UL);

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply5, sizeof(reply5), 0x00U, &map) == CLIP_CAL_OK);
    assert(map.descriptor_value == (clip_u32)0x004600f0UL);
    assert(map.range_count == 5U);
    assert(map.ranges[0].address == (clip_u32)0x00010000UL);
    assert(map.ranges[4].address == (clip_u32)0x01000100UL);
    assert(map.ranges[4].length == (clip_u32)0x0000152aUL);
}

static void
test_memory_descriptor_auxiliary_is_opaque(void)
{
    /*
     * The corpus contained 264 exact-match descriptors where auxiliary was
     * smaller than length.  This synthetic vector makes that relationship
     * explicit so it cannot accidentally become a validation rule later.
     */
    static const clip_u8 reply[] = {
        0x01U, 0x55U,
        0x00U, 0x2eU, 0x06U, 0xd6U,
        0x00U, 0x03U, 0x00U, 0x04U,
        0x00U, 0x00U, 0x10U, 0x80U,
        0x00U, 0x00U, 0x41U, 0x00U,
        0x00U, 0x04U, 0x00U, 0x00U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x00U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x02U,
        0x00U, 0x00U, 0x00U, 0x03U,
        0x00U, 0x04U,
        0x00U, 0x00U, 0x2fU, 0x78U,
        0x00U, 0x00U, 0x55U, 0xb0U,
        0x00U, 0x1aU, 0xbfU, 0xd8U
    };
    struct clip_cal_map map;

    memset(&map, 0, sizeof(map));
    assert(clip_cal_parse_memory_descriptor(
               reply, sizeof(reply), 0x55U, &map) == CLIP_CAL_OK);
    assert(map.range_count == 3U);
    assert(map.ranges[0].auxiliary < map.ranges[0].length);
    assert(map.ranges[1].auxiliary < map.ranges[1].length);
    assert(map.ranges[2].auxiliary < map.ranges[2].length);
}

static void
test_sequence_and_crc(void)
{
    static const unsigned char text[] = "123456789";

    assert(clip_cal_next_sequence(0xfeU) == 0x00U);
    assert(clip_cal_next_sequence(0x00U) == 0x01U);
    assert(clip_cal_crc16_kermit(text, 9UL) == 0x2189U);
}

static void
write_ccal_fixture(const char *path, int include_loader_metadata)
{
    FILE *fp;

    fp = fopen(path, "wb");
    assert(fp != NULL);

    assert(fputs("0000\n", fp) >= 0);

    if (include_loader_metadata) {
        assert(fputs(":0200000400A05A\n", fp) >= 0);
        assert(fputs(":0100000012ED\n", fp) >= 0);
    } else {
        assert(fputs(":020000040010EA\n", fp) >= 0);
        assert(fputs(":0100000034CB\n", fp) >= 0);
    }

    assert(fputs(":00000001FF\n", fp) >= 0);
    assert(fclose(fp) == 0);
    assert(ccal_set_cal_file_crc(path) != 0);
}

/*
 * The three-range CM2250 readback layout has no 0x00A00000 short region.
 * These are its actual region start addresses, with one-byte test payloads.
 * Do not store production calibration data in the repository.
 */
static void
write_three_region_fixture(const char *path)
{
    FILE *fp;

    fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fputs("0000\n", fp) >= 0);
    assert(fputs(":020000040000FA\n", fp) >= 0);
    assert(fputs(":01108000343B\n", fp) >= 0);
    assert(fputs(":014100005668\n", fp) >= 0);
    assert(fputs(":020000040004F6\n", fp) >= 0);
    assert(fputs(":010000007887\n", fp) >= 0);
    assert(fputs(":00000001FF\n", fp) >= 0);
    assert(fclose(fp) == 0);
    assert(ccal_set_cal_file_crc(path) != 0);
}

struct upload_trace {
    unsigned char last_request[5];
    unsigned int count;
    unsigned int short_writes;
    unsigned int bulk_writes;
    unsigned int region_lengths;
    unsigned int finish_values;
    unsigned int start_commands;
    unsigned int end_commands;
    unsigned long bulk_addresses[3];
};

static int
trace_send(void *user, const unsigned char *data, unsigned int length)
{
    struct upload_trace *trace;
    unsigned long address;

    trace = (struct upload_trace *)user;
    if (data == NULL || length < 5U)
        return -1;

    memcpy(trace->last_request, data, 5U);
    ++trace->count;

    if (data[0] == 0x02U) {
        ++trace->start_commands;
    } else if (data[0] == 0x07U) {
        ++trace->end_commands;
    } else if (data[0] == 0x44U && data[2] == 0x18U) {
        ++trace->region_lengths;
    } else if (data[0] == 0x44U && data[2] == 0x0BU) {
        ++trace->finish_values;
    } else if (data[0] == 0x4BU) {
        ++trace->short_writes;
    } else if (data[0] == 0x4DU) {
        if (length < 9U)
            return -1;
        address = ((unsigned long)data[2] << 16) |
                  ((unsigned long)data[3] << 8) |
                  (unsigned long)data[4];
        if (trace->bulk_writes < 3U)
            trace->bulk_addresses[trace->bulk_writes] = address;
        ++trace->bulk_writes;
    } else {
        return -1;
    }

    return 0;
}

static int
trace_recv(void *user,
           unsigned char *data,
           unsigned int capacity,
           unsigned int *length,
           unsigned long timeout_ms)
{
    struct upload_trace *trace;

    trace = (struct upload_trace *)user;
    if (data == NULL || length == NULL || capacity < 6U ||
        timeout_ms == 0UL)
        return -1;

    data[0] = 0x0CU;
    memcpy(data + 1, trace->last_request, 5U);
    *length = 6U;
    return 0;
}

static void
test_three_region_upload_stream(void)
{
    const char *three_path = "build/tests/three-region.ccal";
    const char *legacy_path = "build/tests/short-region.ccal";
    const char *bad_path = "build/tests/missing-region.ccal";
    struct upload_trace trace;
    clip_cal_io io;
    clip_cal_options options;

    io.user = &trace;
    io.send = trace_send;
    io.recv = trace_recv;
    clip_cal_options_init(&options);
    options.timeout_ms = 1000UL;

    write_three_region_fixture(three_path);
    assert(clip_cal_validate_ccal(three_path) == CLIP_CAL_OK);

    memset(&trace, 0, sizeof(trace));
    assert(clip_cal_send_ccal(three_path, &io, &options) == CLIP_CAL_OK);
    assert(trace.start_commands == 1U);
    assert(trace.end_commands == 1U);
    assert(trace.region_lengths == 3U);
    assert(trace.bulk_writes == 3U);
    assert(trace.short_writes == 0U);
    assert(trace.finish_values == 1U);
    assert(trace.count == 9U);
    assert(trace.bulk_addresses[0] == 0x00001080UL);
    assert(trace.bulk_addresses[1] == 0x00004100UL);
    assert(trace.bulk_addresses[2] == 0x00040000UL);

    /* Preserve the already validated short-region programming sequence. */
    write_ccal_fixture(legacy_path, 1);
    memset(&trace, 0, sizeof(trace));
    assert(clip_cal_send_ccal(legacy_path, &io, &options) == CLIP_CAL_OK);
    assert(trace.start_commands == 1U);
    assert(trace.end_commands == 1U);
    assert(trace.region_lengths == 1U);
    assert(trace.bulk_writes == 0U);
    assert(trace.short_writes == 1U);
    assert(trace.finish_values == 1U);
    assert(trace.count == 5U);

    /* A CRC-correct but incomplete one-region file still sends nothing. */
    write_ccal_fixture(bad_path, 0);
    memset(&trace, 0, sizeof(trace));
    assert(clip_cal_send_ccal(bad_path, &io, &options) ==
           CLIP_CAL_ERR_LAYOUT);
    assert(trace.count == 0U);

    remove(three_path);
    remove(legacy_path);
    remove(bad_path);
}

static void
test_full_ccal_preflight(void)
{
    const char *valid_path = "build/tests/valid-preflight.ccal";
    const char *bad_layout_path = "build/tests/bad-layout.ccal";

    write_ccal_fixture(valid_path, 1);
    write_ccal_fixture(bad_layout_path, 0);

    assert(clip_cal_validate_ccal(valid_path) == CLIP_CAL_OK);
    assert(clip_cal_validate_ccal(bad_layout_path) == CLIP_CAL_ERR_LAYOUT);

    remove(valid_path);
    remove(bad_layout_path);
}

int
main(void)
{
    test_mode_request();
    test_calibration_lock_modes();
    test_query_request();
    test_read_request();
    test_memory_descriptor();
    test_memory_descriptor_corpus_shapes();
    test_memory_descriptor_auxiliary_is_opaque();
    test_sequence_and_crc();
    test_full_ccal_preflight();
    test_three_region_upload_stream();

    puts("clip_cal protocol tests passed");
    return 0;
}
