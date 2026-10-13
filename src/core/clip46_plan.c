#include "clip46_plan.h"
#include "clip46_flash.h"
#include "clip_cal.h"
#include <string.h>

/*
 * Construct every DA00 region announcement and superblock, but do not send.
 * The 0x02000000 identity region is metadata carried in an OEM-specific
 * preannouncement header, not an ordinary flash region.
 *
 * This planner is deliberately NOT a programming engine. A real upload also
 * needs an ECM identity/value merge, session-derived DA00 auth, completion
 * acknowledgments, tail variants, encrypted commit and recovery handling.
 */
struct plan_context {
    struct clip46_plan_stats *stats;
    int bad;
};

static int
plan_region(void *user, unsigned long address,
            const unsigned char *data, unsigned long len)
{
    struct plan_context *p;
    unsigned char wire[CLIP46_FLASH_SUPER_FRAMES *
                       CLIP46_FLASH_FRAME_BYTES];
    size_t frame_count;
    unsigned long offset;
    size_t take;
    unsigned char seq;

    p = (struct plan_context *)user;
    if (p == NULL || p->stats == NULL || data == NULL || len == 0UL)
        return -1;
    if (address > 0xffffffffUL || len > 0xffffffffUL - address)
        return -1;
    if (address == 0x02000000UL) {
        if (len != 16UL || p->stats->identity_regions != 0U)
            return -1;
        p->stats->identity_regions++;
        return 0;
    }
    if (clip46_flash_build_region_block(address, len, wire,
                                        sizeof(wire), &frame_count) != 0)
        return -1;
    if (p->stats->wire_frames > (unsigned long)-1 - (unsigned long)frame_count)
        return -1;
    p->stats->wire_frames += (unsigned long)frame_count;
    p->stats->regions++;

    offset = 0UL;
    seq = 1U;
    while (offset < len) {
        take = (size_t)((len - offset > CLIP46_FLASH_SUPER_IMAGE_BYTES)
                        ? CLIP46_FLASH_SUPER_IMAGE_BYTES : len - offset);
        if (clip46_flash_build_super(data + offset, take, seq,
                                     wire, sizeof(wire), &frame_count) != 0)
            return -1;
        if (p->stats->wire_frames >
            (unsigned long)-1 - (unsigned long)frame_count ||
            p->stats->image_bytes > (unsigned long)-1 - (unsigned long)take)
            return -1;
        p->stats->wire_frames += (unsigned long)frame_count;
        p->stats->image_bytes += (unsigned long)take;
        p->stats->super_blocks++;
        offset += (unsigned long)take;
        seq = (unsigned char)(seq + 1U);
    }
    return 0;
}

int
clip46_plan_ccal(const char *path, struct clip46_plan_stats *stats)
{
    struct plan_context p;
    if (path == NULL || stats == NULL)
        return -1;
    memset(stats, 0, sizeof(*stats));
    memset(&p, 0, sizeof(p));
    p.stats = stats;
    if (clip_cal_visit_ccal_regions(path, plan_region, &p) != CLIP_CAL_OK)
        return -1;
    return stats->regions != 0U ? 0 : -1;
}
