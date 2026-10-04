#ifndef CLIP_CAL_H
#define CLIP_CAL_H

/*
 * clip_cal.h
 *
 * C89 helpers for CLIP calibration readback and calibration
 * programming procedures observed in the supplied reference tool recordings.
 *
 * This module operates on CLIP application PDUs only.  It intentionally does
 * not implement the surrounding J1939 Proprietary-A / transport framing.
 *
 * Observed calibration readback sequence after clip_crypto authentication:
 *
 *   12 <seq> 00 11 00                  enter main calibration phase
 *   10 <seq> 01 01 00 00 01            query calibration memory descriptor
 *   10 <seq> 01 01 <24-bit-id>          query compatibility metadata
 *   13 <seq> <addr:32be> <len:16be>     read calibration bytes
 *       ... repeat over all descriptor ranges, normally <= 1000 bytes/read
 *   1A 00 00 00                         end main phase
 *
 *   10 <seq> 01 01 00 22 26            observed status query
 *   12 <seq> 00 11 01                  enter auxiliary phase
 *   15 <seq> 00 00 29 27               resolve harness-key-list pointer
 *   13 <seq> <pointer+n> 00 01          read eight compatibility bytes
 *   1A 00 00 00                         end auxiliary phase
 *
 * Positive replies use 01 <seq> <data...>.
 */

#include "clip_crypto.h"

#define CLIP_CAL_OK                 CLIP_OK
#define CLIP_CAL_ERR_ARGUMENT       CLIP_ERR_ARGUMENT
#define CLIP_CAL_ERR_BUFFER         CLIP_ERR_BUFFER
#define CLIP_CAL_ERR_FORMAT         CLIP_ERR_FORMAT
#define CLIP_CAL_ERR_LENGTH         CLIP_ERR_LENGTH
#define CLIP_CAL_ERR_SEQUENCE       -6
#define CLIP_CAL_ERR_RANGE_COUNT    -7

#define CLIP_CAL_SERVICE_REPLY          0x01U
#define CLIP_CAL_SERVICE_QUERY          0x10U
#define CLIP_CAL_SERVICE_PHASE          0x12U
#define CLIP_CAL_SERVICE_READ_MEMORY    0x13U
#define CLIP_CAL_SERVICE_RESOLVE_PTR    0x15U
#define CLIP_CAL_SERVICE_END_PHASE      0x1aU

#define CLIP_CAL_PHASE_MAIN             0x00U
#define CLIP_CAL_PHASE_AUX              0x01U

#define CLIP_CAL_QUERY_REQUEST_SIZE     7U
#define CLIP_CAL_PHASE_REQUEST_SIZE     5U
#define CLIP_CAL_READ_REQUEST_SIZE      8U
#define CLIP_CAL_PTR_REQUEST_SIZE       6U
#define CLIP_CAL_END_REQUEST_SIZE       4U

/* reference tool used 0x03E8-byte reads for the captured main readback. */
#define CLIP_CAL_DEFAULT_CHUNK_SIZE     1000U

/* Conservative parser capacity; the supplied descriptor contains four. */
#define CLIP_CAL_MAX_RANGES             16U
#define CLIP_CAL_MAX_TRAILER            32U

/* Compatibility/header identifiers observed in the capture. */
#define CLIP_CAL_ID_MEMORY_DESCRIPTOR   0x000001UL
#define CLIP_CAL_ID_CAL_VERSION         0x0021bdUL
#define CLIP_CAL_ID_MODULE_NAME         0x0021a7UL
#define CLIP_CAL_ID_PRODUCT_ID          0x000084UL
#define CLIP_CAL_ID_INTERFACE_LEVEL     0x0021c8UL
#define CLIP_CAL_ID_MODULE_PART_NUMBER  0x00b825UL
#define CLIP_CAL_ID_BOOT_START_VERSION  0x0021beUL
#define CLIP_CAL_ID_BOOT_END_VERSION    0x0021bfUL
#define CLIP_CAL_ID_BYTE_ORDER          0x002160UL
#define CLIP_CAL_ID_INDEX_TABLE_ADDRESS 0x0021c7UL
#define CLIP_CAL_ID_STATUS_2226         0x002226UL
#define CLIP_CAL_ID_POST_2285           0x002285UL

#define CLIP_CAL_PTR_HARNESS_COMPAT     0x2927U
#define CLIP_CAL_HARNESS_COMPAT_SIZE    8U

struct clip_cal_range {
    clip_u32 address;
    clip_u32 length;

    /*
     * A parallel per-range 32-bit field exists in the descriptor between the
     * address and length arrays.  Its semantic meaning is not asserted yet,
     * so preserve it verbatim.
     */
    clip_u32 auxiliary;
};

struct clip_cal_map {
    /* First 32-bit descriptor field; preserved without assigning semantics. */
    clip_u32 descriptor_value;

    unsigned int range_count;
    struct clip_cal_range ranges[CLIP_CAL_MAX_RANGES];

    clip_u8 trailer[CLIP_CAL_MAX_TRAILER];
    size_t trailer_len;
};

struct clip_cal_cursor {
    unsigned int range_index;
    clip_u32 range_offset;
};

struct clip_cal_read_info {
    unsigned int range_index;
    clip_u32 address;
    unsigned int length;
};

/* Build: 12 seq 00 11 phase. */
int
clip_cal_build_phase_request(clip_u8 sequence,
                             clip_u8 phase,
                             clip_u8 out[CLIP_CAL_PHASE_REQUEST_SIZE]);

/* Build the general service-0x12 mode request: 12 seq mode[15:0] value. */
int
clip_cal_build_mode_request(clip_u8 sequence,
                            unsigned int mode,
                            clip_u8 value,
                            clip_u8 out[CLIP_CAL_PHASE_REQUEST_SIZE]);

/* Build a single-ID query: 10 seq 01 01 id[23:0]. */
int
clip_cal_build_query_request(clip_u8 sequence,
                             unsigned long identifier,
                             clip_u8 out[CLIP_CAL_QUERY_REQUEST_SIZE]);

/*
 * Parse a generic positive response: 01 seq data...
 * data points into pdu and remains owned by the caller.
 */
int
clip_cal_parse_reply(const clip_u8 *pdu,
                     size_t pdu_len,
                     clip_u8 expected_sequence,
                     const clip_u8 **data,
                     size_t *data_len);

/*
 * Parse the response to CLIP_CAL_ID_MEMORY_DESCRIPTOR.
 *
 * The supplied CM2350A descriptor has:
 *   u32 descriptor_value
 *   u16 count_a
 *   u16 count_b
 *   u32 start[count_b]
 *   u32 auxiliary[count_b]
 *   u32 length_count
 *   u32 length[length_count]
 *   trailing bytes
 *
 * count_a, count_b and length_count were all four in the capture.  The parser
 * requires them to agree and intentionally does not assign semantics to the
 * descriptor_value, auxiliary array, or trailer yet.
 */
int
clip_cal_parse_memory_descriptor(const clip_u8 *pdu,
                                 size_t pdu_len,
                                 clip_u8 expected_sequence,
                                 struct clip_cal_map *map);

/* Build: 13 seq address[31:0] length[15:0]. */
int
clip_cal_build_read_request(clip_u8 sequence,
                            clip_u32 address,
                            unsigned int length,
                            clip_u8 out[CLIP_CAL_READ_REQUEST_SIZE]);

/*
 * Parse 01 seq data... for a memory read.  expected_length bytes are returned;
 * any trailing bytes are ignored so a short read can tolerate frame padding.
 */
int
clip_cal_parse_read_reply(const clip_u8 *pdu,
                          size_t pdu_len,
                          clip_u8 expected_sequence,
                          size_t expected_length,
                          const clip_u8 **data);

/* Build: 15 seq 00 00 pointer_id[15:0]. */
int
clip_cal_build_pointer_request(clip_u8 sequence,
                               unsigned int pointer_id,
                               clip_u8 out[CLIP_CAL_PTR_REQUEST_SIZE]);

/* Parse 01 seq address[31:0]. */
int
clip_cal_parse_pointer_reply(const clip_u8 *pdu,
                             size_t pdu_len,
                             clip_u8 expected_sequence,
                             clip_u32 *address);

/* Build the phase terminator observed in the recording: 1A 00 00 00. */
void
clip_cal_build_end_request(clip_u8 out[CLIP_CAL_END_REQUEST_SIZE]);

/*
 * Application sequence numbers run 00..FE; FF is skipped in the capture.
 * Thus FE advances to 00.
 */
clip_u8
clip_cal_next_sequence(clip_u8 sequence);

/* Cursor helpers for walking every range in a parsed map. */
void
clip_cal_cursor_init(struct clip_cal_cursor *cursor);

int
clip_cal_next_read(const struct clip_cal_map *map,
                   struct clip_cal_cursor *cursor,
                   clip_u8 sequence,
                   unsigned int max_chunk,
                   clip_u8 out[CLIP_CAL_READ_REQUEST_SIZE],
                   struct clip_cal_read_info *info);

/* Sum all declared range lengths; returns 0 on arithmetic overflow. */
size_t
clip_cal_total_size(const struct clip_cal_map *map);



/* -------------------------------------------------------------------------
 * Calibration programming / CCAL upload API.
 *
 * These callbacks operate on complete/reassembled PGN 61184 payloads after
 * the ECM has been transitioned from CLIP into its raw calibration loader.
 * ------------------------------------------------------------------------- */

#define CLIP_CAL_PGN_PROPA          61184UL
#define CLIP_CAL_DEFAULT_TIMEOUT    60000UL
#define CLIP_CAL_BLOCK_SIZE         0x0640U
#define CLIP_CAL_CAPTURE_FINISH     0x2B16U

/* Upload-specific errors use a separate range so the readback API remains ABI
 * compatible with the earlier clip_cal implementation. */
#define CLIP_CAL_ERR_OPEN           -20
#define CLIP_CAL_ERR_HEX_CHECKSUM   -21
#define CLIP_CAL_ERR_ORDER          -22
#define CLIP_CAL_ERR_MEMORY         -23
#define CLIP_CAL_ERR_LAYOUT         -24
#define CLIP_CAL_ERR_RANGE          -25
#define CLIP_CAL_ERR_TRANSPORT      -26
#define CLIP_CAL_ERR_TIMEOUT        -27
#define CLIP_CAL_ERR_ACK            -28
#define CLIP_CAL_ERR_CRC            -29

typedef int (*clip_cal_send_fn)(void *user,
                                const unsigned char *data,
                                unsigned int length);

typedef int (*clip_cal_recv_fn)(void *user,
                                unsigned char *data,
                                unsigned int capacity,
                                unsigned int *length,
                                unsigned long timeout_ms);

typedef void (*clip_cal_progress_fn)(void *user,
                                     unsigned long bytes_sent,
                                     unsigned long bytes_total,
                                     unsigned long address);

typedef struct clip_cal_io_tag {
    void *user;
    clip_cal_send_fn send;
    clip_cal_recv_fn recv;
} clip_cal_io;

typedef struct clip_cal_options_tag {
    unsigned long timeout_ms;
    unsigned int block_size;
    unsigned int finish_value;
    int send_start_command;
    int send_end_command;
    clip_cal_progress_fn progress;
    void *progress_user;
} clip_cal_options;

void clip_cal_options_init(clip_cal_options *options);

/* CRC-16/KERMIT appended to each raw calibration region by the loader. */
unsigned int clip_cal_crc16_kermit(const unsigned char *data,
                                   unsigned long length);

/* Verify the calibration file-level CRC token before any programming traffic. */
int clip_cal_verify_ccal_crc(const char *filename);

/*
 * Program a parsed .ccal over the raw loader protocol observed in the
 * reference tool upload capture:
 *
 *   02                    enter transfer phase
 *   44 00 18 04 <len32>   announce region length including CRC16
 *   4B ...                short write for 0x00A00000 metadata region
 *   4D ...                24-bit bulk write, <=0x0640 bytes per block
 *   44 00 0B 02 <value>   final captured parameter write
 *   07                    finish transfer phase
 *
 * The function refuses to transmit anything unless clip_cal_verify_ccal_crc()
 * succeeds first.  The caller must already have transitioned the ECM from the
 * authenticated CLIP session into the raw calibration loader.
 */
int clip_cal_send_ccal(const char *filename,
                       const clip_cal_io *io,
                       const clip_cal_options *options);

const char *clip_cal_strerror(int code);

#endif /* CLIP_CAL_H */
