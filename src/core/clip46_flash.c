/*
 * clip46_flash.c - CM2450E / GTIS4.6 PGN 0x00DA00 flash stream framing.
 *
 * Layout decoded from the passive OEM flash capture of 2026-10-11;
 * see clip46_flash.h and docs/cm2450e-secure-flash-capture.md.
 * ISO C89. No ECM I/O happens here: framing only.
 */
#include "clip46_flash.h"
#include <string.h>

#define POS_FIRST 0x21U
#define POS_LAST  0x2FU

static void
put_frame(unsigned char *out, unsigned char first, size_t payload_off,
          const unsigned char *payload, size_t payload_len, size_t done)
{
    size_t i;
    size_t n;

    out[0] = first;
    n = CLIP46_FLASH_DATA_PER_FRAME;
    if (done + n > payload_len)
        n = payload_len - done;
    for (i = 0U; i < n; ++i)
        out[1U + i] = payload[payload_off + done + i];
    for (; i < CLIP46_FLASH_DATA_PER_FRAME; ++i)
        out[1U + i] = 0xCCU;
}

int
clip46_flash_build_block(unsigned char type,
                         const unsigned char *payload,
                         size_t payload_len,
                         unsigned char *frames,
                         size_t frames_capacity,
                         size_t *frame_count)
{
    size_t need_frames;
    size_t done;
    size_t fi;

    if (frames == NULL || frame_count == NULL)
        return -1;
    if (payload_len > CLIP46_FLASH_BLOCK_MAX_BYTES)
        return -1;
    if (payload == NULL && payload_len != 0U)
        return -1;

    need_frames = payload_len / CLIP46_FLASH_DATA_PER_FRAME;
    if (payload_len % CLIP46_FLASH_DATA_PER_FRAME != 0U ||
        need_frames == 0U)
        need_frames++;
    if (need_frames > CLIP46_FLASH_BLOCK_MAX_FRAMES)
        return -1;
    if (frames_capacity < need_frames * CLIP46_FLASH_FRAME_BYTES)
        return -1;

    done = 0U;
    fi = 0U;
    while (done < payload_len || fi == 0U) {
        unsigned char marker;

        if (fi == 0U)
            marker = type;
        else
            marker = (unsigned char)(POS_FIRST + (fi - 1U));
        put_frame(frames + fi * CLIP46_FLASH_FRAME_BYTES,
                  marker, 0U, payload, payload_len, done);
        done += CLIP46_FLASH_DATA_PER_FRAME;
        fi++;
        if (fi >= CLIP46_FLASH_BLOCK_MAX_FRAMES)
            break;
    }
    *frame_count = fi;
    return 0;
}

int
clip46_flash_build_region_block(unsigned long address,
                                unsigned long length,
                                unsigned char *frames,
                                size_t frames_capacity,
                                size_t *frame_count)
{
    unsigned char payload[12];

    if (address > 0xFFFFFFFFUL || length > 0xFFFFFFFFUL)
        return -1;

    payload[0] = 0x0BU;
    payload[1] = 0x34U;
    payload[2] = 0x00U;
    payload[3] = 0x44U;
    payload[4] = (unsigned char)((address >> 24) & 0xffUL);
    payload[5] = (unsigned char)((address >> 16) & 0xffUL);
    payload[6] = (unsigned char)((address >> 8) & 0xffUL);
    payload[7] = (unsigned char)(address & 0xffUL);
    payload[8] = (unsigned char)((length >> 24) & 0xffUL);
    payload[9] = (unsigned char)((length >> 16) & 0xffUL);
    payload[10] = (unsigned char)((length >> 8) & 0xffUL);
    payload[11] = (unsigned char)(length & 0xffUL);

    return clip46_flash_build_block(CLIP46_FLASH_TYPE_META,
                                    payload, sizeof(payload),
                                    frames, frames_capacity, frame_count);
}

/*
 * Super-block layout, exactly as captured:
 *
 *   block 1: type 0x15, payload "DA 36 <seq>" + 109 image bytes
 *   blocks 2..13: type 0x20 blocks of 112 bytes; the final one is a
 *   short block of the remaining bytes (<= 49) padded with 0xCC.
 *
 * A chunk shorter than 1496 bytes produces the corresponding truncated
 * sequence; the tail block is always padded to whole frames.
 */
int
clip46_flash_build_super(const unsigned char *image_chunk,
                         size_t chunk_len,
                         unsigned char seq,
                         unsigned char *frames,
                         size_t frames_capacity,
                         size_t *frame_count)
{
    unsigned char head[112];
    size_t off;
    size_t fi;
    size_t total_frames;

    if (frames == NULL || frame_count == NULL || image_chunk == NULL)
        return -1;
    if (chunk_len == 0U || chunk_len > CLIP46_FLASH_SUPER_IMAGE_BYTES)
        return -1;
    if (frames_capacity < CLIP46_FLASH_SUPER_FRAMES *
                          CLIP46_FLASH_FRAME_BYTES)
        return -1;

    /* Block 1: DA 36 <seq> + up to 109 image bytes. */
    memset(head, 0xCC, sizeof(head));
    head[0] = 0xDAU;
    head[1] = 0x36U;
    head[2] = seq;
    if (chunk_len < 109U)
        memcpy(head + 3U, image_chunk, chunk_len);
    else
        memcpy(head + 3U, image_chunk, 109U);
    if (clip46_flash_build_block(CLIP46_FLASH_TYPE_SUPER,
                                 head, 112U,
                                 frames, frames_capacity, &fi) != 0)
        return -1;
    total_frames = fi;
    off = 109U;
    if (off > chunk_len)
        off = chunk_len;

    /* Full 112-byte data blocks while at least 112 bytes remain. */
    while (chunk_len - off >= 112U) {
        if (clip46_flash_build_block(CLIP46_FLASH_TYPE_DATA,
                                     image_chunk + off, 112U,
                                     frames + total_frames *
                                         CLIP46_FLASH_FRAME_BYTES,
                                     frames_capacity - total_frames *
                                         CLIP46_FLASH_FRAME_BYTES,
                                     &fi) != 0)
            return -1;
        total_frames += fi;
        off += 112U;
    }

    /* Tail block: remaining bytes (0 < n <= 111), padded with 0xCC. */
    if (off < chunk_len) {
        size_t tail;

        tail = chunk_len - off;
        if (clip46_flash_build_block(CLIP46_FLASH_TYPE_DATA,
                                     image_chunk + off, tail,
                                     frames + total_frames *
                                         CLIP46_FLASH_FRAME_BYTES,
                                     frames_capacity - total_frames *
                                         CLIP46_FLASH_FRAME_BYTES,
                                     &fi) != 0)
            return -1;
        total_frames += fi;
    }

    *frame_count = total_frames;
    return 0;
}

int
clip46_flash_parse_status(const unsigned char *frame,
                          unsigned int *counter)
{
    if (frame == NULL)
        return -1;
    if (frame[0] == 0x02U && frame[1] == 0x76U) {
        if (counter != NULL)
            *counter = (unsigned int)frame[2];
        return 1;
    }
    return 0;
}
