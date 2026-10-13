/*
 * Four-region GTIS4.6 fixture: three DA00 stream regions plus the
 * high-address 0x02000000 identity metadata. C89, synthetic bytes only.
 */
#include "clip46_plan.h"
#include "clip46_uds.h"
#include "clip_cal.h"
#include "ccal_crc.h"
#include <assert.h>
#include <stdio.h>

static void
hex_record(FILE *fp, unsigned int type, unsigned int addr,
           const unsigned char *payload, unsigned int length)
{
    unsigned int i;
    unsigned int sum;
    sum = length + (addr >> 8) + (addr & 255U) + type;
    assert(fprintf(fp, ":%02X%04X%02X", length, addr, type) > 0);
    for (i = 0U; i < length; ++i) {
        assert(fprintf(fp, "%02X", payload[i]) > 0);
        sum += payload[i];
    }
    assert(fprintf(fp, "%02X\n", (0U - sum) & 255U) > 0);
}

int
main(void)
{
    const char *path = "clip46_plan_synthetic.ccal";
    unsigned char upper[2];
    unsigned char value[16];
    unsigned int i;
    FILE *fp;
    struct clip46_plan_stats stats;
    struct clip46_uds_image prepared;

    fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fputs("0000\n", fp) >= 0);

    upper[0] = 0x00U;
    upper[1] = 0x00U;
    hex_record(fp, 4U, 0U, upper, 2U);
    value[0] = 0x11U;
    hex_record(fp, 0U, 0x1080U, value, 1U);

    upper[1] = 0x01U;
    hex_record(fp, 4U, 0U, upper, 2U);
    value[0] = 0x22U;
    hex_record(fp, 0U, 0x1000U, value, 1U);

    upper[1] = 0x84U;
    hex_record(fp, 4U, 0U, upper, 2U);
    value[0] = 0x33U;
    hex_record(fp, 0U, 0x0000U, value, 1U);

    upper[0] = 0x02U;
    upper[1] = 0x00U;
    hex_record(fp, 4U, 0U, upper, 2U);
    for (i = 0U; i < 16U; ++i)
        value[i] = (unsigned char)i;
    hex_record(fp, 0U, 0U, value, 16U);
    hex_record(fp, 1U, 0U, NULL, 0U);
    assert(fclose(fp) == 0);

    assert(ccal_set_cal_file_crc(path) != 0);
    assert(clip_cal_validate_ccal(path) == CLIP_CAL_ERR_RANGE);
    assert(clip46_plan_ccal(path, &stats) == 0);
    assert(stats.regions == 3U);
    assert(stats.identity_regions == 1U);
    assert(stats.image_bytes == 3UL);
    assert(stats.super_blocks == 3UL);
    assert(stats.wire_frames == 54UL);
    /* Regression: load the complete CRC-verified image before CLIP.
     * Nothing in the live transfer should touch the CCAL disk file. */
    assert(clip46_uds_prepare_file(path, &prepared) == 0);
    assert(prepared.count == 4U);
    assert(prepared.identity_index == 3U);
    assert(prepared.regions[0].address == 0x00001080UL);
    assert(prepared.regions[1].address == 0x00011000UL);
    assert(prepared.regions[2].address == 0x00840000UL);
    assert(prepared.regions[3].address == 0x02000000UL);
    assert(prepared.regions[3].length == 16UL);
    assert(prepared.regions[3].bytes[0] == 0U);
    assert(prepared.regions[3].bytes[15] == 15U);
    clip46_uds_free_image(&prepared);
    assert(prepared.count == 0U);
    assert(remove(path) == 0);
    puts("GTIS4.6 high-address DA00 offline plan: PASS");
    return 0;
}
