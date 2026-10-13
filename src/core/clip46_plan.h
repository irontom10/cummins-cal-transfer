#ifndef CLIP46_PLAN_H
#define CLIP46_PLAN_H

/* Offline, no-transmit DA00 preparation; not a live ECM uploader. */
struct clip46_plan_stats {
    unsigned int regions;
    unsigned int identity_regions;
    unsigned long image_bytes;
    unsigned long wire_frames;
    unsigned long super_blocks;
};

/* Returns 0 when CCAL CRC and DA00 framing plan succeed.
 * Explicitly DOES NOT authorize writing: identity merge, auth block,
 * region-tail variants and commit protocol remain unresolved. */
int clip46_plan_ccal(const char *path, struct clip46_plan_stats *stats);

#endif
