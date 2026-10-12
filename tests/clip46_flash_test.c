/* Synthetic framing tests against the captured OEM flash layout.
 * No captured session secrets, keys or ECM data. ISO C89. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "clip46_flash.h"

static void
fill_run(unsigned char *buf, size_t n, unsigned char seed)
{
    size_t i;
    for (i = 0U; i < n; ++i)
        buf[i] = (unsigned char)(seed + i);
}

int main(void)
{
    unsigned char frames[CLIP46_FLASH_SUPER_FRAMES *
                         CLIP46_FLASH_FRAME_BYTES];
    unsigned char image[CLIP46_FLASH_SUPER_IMAGE_BYTES];
    unsigned char chunk[200];
    size_t nframes;
    int rc;
    unsigned int i;

    /* ---- region announcement: byte-for-byte vs the capture ---- */
    {
        static const unsigned char want[] = {
            0x10, 0x0B, 0x34, 0x00, 0x44, 0x02, 0x00, 0x00,
            0x21, 0x00, 0x00, 0x00, 0x00, 0x10, 0xCC, 0xCC
        };
        rc = clip46_flash_build_region_block(0x02000000UL, 0x10UL,
                                             frames, sizeof(frames),
                                             &nframes);
        assert(rc == 0);
        assert(nframes == 2U);
        assert(memcmp(frames, want, sizeof(want)) == 0);
    }
    {
        /* capture: 10 0B 34 00 44 00 00 10 | 21 80 00 00 EF 78 CC CC */
        static const unsigned char want[] = {
            0x10, 0x0B, 0x34, 0x00, 0x44, 0x00, 0x00, 0x10,
            0x21, 0x80, 0x00, 0x00, 0xEF, 0x78, 0xCC, 0xCC
        };
        rc = clip46_flash_build_region_block(0x00001080UL, 0x0000EF78UL,
                                             frames, sizeof(frames),
                                             &nframes);
        assert(rc == 0);
        assert(nframes == 2U);
        assert(memcmp(frames, want, sizeof(want)) == 0);
    }

    /* ---- full super-block: 13 blocks, 215 frames, 1496 bytes ---- */
    fill_run(image, sizeof(image), 0x00);
    rc = clip46_flash_build_super(image, sizeof(image), 0x07U,
                                  frames, sizeof(frames), &nframes);
    assert(rc == 0);
    assert(nframes == CLIP46_FLASH_SUPER_FRAMES);

    /* head block: 15 DA 36 07 then image[0..3] */
    assert(frames[0] == 0x15U);
    assert(frames[1] == 0xDAU && frames[2] == 0x36U && frames[3] == 0x07U);
    assert(frames[4] == image[0] && frames[7] == image[3]);

    /* head block continuation markers 21..2F across 15 more frames */
    for (i = 1U; i < 16U; ++i)
        assert(frames[i * 8U] == (unsigned char)(0x20U + i));

    /* second block starts with 20 and carries image[109..115] */
    assert(frames[16U * 8U] == 0x20U);
    assert(frames[16U * 8U + 1U] == image[109]);

    /* 12 full data blocks, then the 7-frame short tail block */
    assert(frames[(16U + 12U * 16U) * 8U] == 0x20U);
    assert(frames[(16U + 12U * 16U) * 8U + 1U] ==
           image[109U + 12U * 112U]);

    /* payload reassembly check */
    {
        unsigned char out[CLIP46_FLASH_SUPER_IMAGE_BYTES];
        size_t out_len;
        size_t fi;

        out_len = 0U;
        for (fi = 0U; fi < nframes; ++fi) {
            const unsigned char *f = frames + fi * 8U;
            size_t k;
            for (k = 1U; k < 8U; ++k) {
                unsigned char b;
                b = f[k];
                if (b == 0xCCU) {
                    /* only the final pad may be CC in this synthetic run */
                    if (fi + 1U == nframes)
                        continue;
                    if (out_len < 3U && fi == 0U)
                        continue; /* DA 36 seq header */
                }
                if (fi == 0U && k <= 3U)
                    continue; /* DA 36 seq */
                if (out_len < sizeof(out))
                    out[out_len++] = b;
            }
        }
        assert(out_len >= CLIP46_FLASH_SUPER_IMAGE_BYTES - 1U);
        assert(memcmp(out, image, CLIP46_FLASH_SUPER_IMAGE_BYTES) == 0);
    }

    /* ---- short final chunk: truncated sequence ---- */
    fill_run(chunk, sizeof(chunk), 0x40U);
    rc = clip46_flash_build_super(chunk, sizeof(chunk), 0x2AU,
                                  frames, sizeof(frames), &nframes);
    assert(rc == 0);
    /* 200 = 109 (head block) + 91 tail bytes; 91 = 13 frames exactly. */
    assert(nframes == 16U + 13U);
    assert(frames[0] == 0x15U && frames[3] == 0x2AU);
    /* tail block carries chunk[109..199] */
    assert(frames[16U * 8U] == 0x20U);
    assert(frames[16U * 8U + 1U] == chunk[109]);
    assert(frames[(16U + 12U) * 8U + 7U] == chunk[199]);

    /* frame-unaligned tail: 195 = 109 + 86; 86 = 12 frames + 2 bytes
     * plus 5 x 0xCC pad in the 13th tail frame. */
    rc = clip46_flash_build_super(chunk, 195U, 0x2BU,
                                  frames, sizeof(frames), &nframes);
    assert(rc == 0);
    assert(nframes == 16U + 13U);
    assert(frames[0] == 0x15U && frames[3] == 0x2BU);
    assert(frames[16U * 8U + 1U] == chunk[109]);
    assert(frames[(16U + 12U) * 8U + 1U] == chunk[193]);
    assert(frames[(16U + 12U) * 8U + 2U] == chunk[194]);
    assert(frames[(16U + 12U) * 8U + 3U] == 0xCCU);
    assert(frames[(16U + 12U) * 8U + 7U] == 0xCCU);

    /* ---- status parse ---- */
    {
        static const unsigned char status[8] = {
            0x02, 0x76, 0x2BU, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        static const unsigned char other[8] = {
            0x03, 0x7F, 0x36, 0x78, 0x00, 0x00, 0x00, 0x00
        };
        unsigned int counter;

        counter = 0U;
        assert(clip46_flash_parse_status(status, &counter) == 1);
        assert(counter == 0x2BU);
        assert(clip46_flash_parse_status(other, &counter) == 0);
        assert(clip46_flash_parse_status(NULL, &counter) == -1);
    }

    /* ---- argument guards ---- */
    assert(clip46_flash_build_super(image, 0U, 0U, frames,
                                    sizeof(frames), &nframes) == -1);
    assert(clip46_flash_build_super(image, CLIP46_FLASH_SUPER_IMAGE_BYTES + 1U,
                                    0U, frames, sizeof(frames),
                                    &nframes) == -1);
    assert(clip46_flash_build_super(image, sizeof(image), 0U,
                                    frames, 16U, &nframes) == -1);

    puts("clip46 flash framing tests passed");
    return 0;
}
