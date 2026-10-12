#ifndef CLIP46_FLASH_H
#define CLIP46_FLASH_H

/*
 * CM2450E / GTIS4.6 secure flash: PGN 0x00DA00 streaming protocol.
 *
 * Every wire frame is 8 bytes on J1939 PGN 0x00DA00.  Byte 0 of the
 * first frame of a block is the block type and bytes 1..7 carry payload;
 * continuation frames carry position markers 0x21..0x2F in byte 0 and
 * payload in bytes 1..7.  A block is at most 16 frames, so it carries
 * at most 16 * 7 = 112 payload bytes.
 *
 * Decoded from the passive OEM flash capture of 2026-10-11 (see
 * docs/cm2450e-secure-flash-capture.md).  Image data flows in
 * super-blocks of 13 blocks / 215 frames carrying 1496 image bytes:
 *
 *   [15] "DA 36 <seq>" + 109 image bytes     (16 frames, 112 payload)
 *   [20] 112 image bytes                     (16 frames)  x 12
 *   [20] 43 image bytes + 6 x 0xCC pad       (7 frames)
 *
 * One super-block is transferred per ~100 ms ECM status period; the
 * ECM answers with the observed status triplet
 *   03 7F 36 78 .., 02 76 <n> .., 30 00 .. .
 *
 * Region announcements are META (type 0x10) blocks:
 *   0B 34 00 44 <address BE32> <length BE32>
 *
 * NOT IMPLEMENTED (unknown from passive capture, gated in the caller):
 * the per-session DA00 authentication block (type 0x10, "22 27 08" +
 * 32 session-derived bytes), the encrypted service-0x11 commit phase,
 * and the identity-field merge the OEM tool performs before streaming.
 */

#include <stddef.h>

#define CLIP46_FLASH_FRAME_BYTES        8U
#define CLIP46_FLASH_DATA_PER_FRAME     7U

#define CLIP46_FLASH_BLOCK_MAX_FRAMES   16U
#define CLIP46_FLASH_BLOCK_MAX_BYTES    112U  /* 16 frames x 7 bytes */

#define CLIP46_FLASH_SUPER_IMAGE_BYTES  1496U
#define CLIP46_FLASH_SUPER_BLOCKS       13U
#define CLIP46_FLASH_SUPER_FRAMES       215U
#define CLIP46_FLASH_SUPER_PAD          6U    /* 0xCC tail of the short block */

/* Block types observed in the reference capture. */
#define CLIP46_FLASH_TYPE_ACK      0x01U
#define CLIP46_FLASH_TYPE_POLL     0x02U  /* payload 3E 80: readiness poll */
#define CLIP46_FLASH_TYPE_INIT     0x03U  /* payload 22 xx: channel init */
#define CLIP46_FLASH_TYPE_CTRL     0x04U
#define CLIP46_FLASH_TYPE_META     0x10U  /* region announce, metadata */
#define CLIP46_FLASH_TYPE_SUPER    0x15U  /* DA 36 <seq> super-block head */
#define CLIP46_FLASH_TYPE_DATA     0x20U

/* ECM status frames observed during the stream. */
#define CLIP46_FLASH_STATUS_A_LEN  8U

/*
 * Build one wire block.  payload_len may be 0..105; the block is padded
 * with 0xCC to a whole frame when it does not end on a frame boundary.
 * frames_capacity is in bytes (frame_count * 8 is written on success).
 * Returns 0 on success, -1 on argument or capacity errors.
 */
int clip46_flash_build_block(unsigned char type,
                             const unsigned char *payload,
                             size_t payload_len,
                             unsigned char *frames,
                             size_t frames_capacity,
                             size_t *frame_count);

/*
 * Region announcement block: "0B 34 00 44 <address BE32> <length BE32>".
 */
int clip46_flash_build_region_block(unsigned long address,
                                    unsigned long length,
                                    unsigned char *frames,
                                    size_t frames_capacity,
                                    size_t *frame_count);

/*
 * Build one super-block from an image chunk of at most 1496 bytes.
 * seq is the running super-block sequence byte carried in "DA 36 <seq>".
 * Writes up to 215 frames (1720 bytes).  A short final chunk produces
 * the corresponding truncated block sequence with 0xCC padding.
 */
int clip46_flash_build_super(const unsigned char *image_chunk,
                             size_t chunk_len,
                             unsigned char seq,
                             unsigned char *frames,
                             size_t frames_capacity,
                             size_t *frame_count);

/*
 * Parse one ECM frame.  Returns:
 *   1  status frame "02 76 <n>": *counter receives n
 *   0  any other frame
 *  -1  argument error
 */
int clip46_flash_parse_status(const unsigned char *frame,
                              unsigned int *counter);

#endif /* CLIP46_FLASH_H */
