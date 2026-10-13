#ifndef CLIP46_UDS_H
#define CLIP46_UDS_H

#include <stddef.h>
#include "clip46_runtime.h"

/* ISO-TP frame callbacks: receive one 8-byte CAN payload at a time.
 * recv returns 0 for a received frame and negative for timeout/error.
 * send must transmit exactly 8 bytes and return 0 on success.
 */
struct clip46_uds_io {
    void *user;
    int (*send)(void *user, const unsigned char frame[8]);
    int (*recv)(void *user, unsigned char frame[8], unsigned long timeout_ms);
    void (*progress)(void *user, int percent, const char *message);
};

/* Real diagnostic transport, including SecurityAccess, RequestDownload,
 * TransferData/Exit, routine controls, and reset. Not the legacy 24-bit
 * CLIP raw loader. NEVER called unless explicitly enabled by TOML.
 *
 * Return:
 *   0 successful DA00 UDS sequence, including positive final reset reply
 *  -1 protocol/transport failure (see error)
 * Caller must still separately verify the OEM encrypted CLIP post-flash
 * commit. This function does NOT implement that proprietary phase.
 */
/* OEM DA00 status probes + bounded NRC 21 retry on 10 02 only.
 * Testable independently without writing ECU memory. */
int clip46_uds_begin_programming(const struct clip46_uds_io *io,
                                 char *error, size_t error_capacity);

int clip46_uds_flash(const struct clip46_uds_io *io,
                     const struct clip46_uds_config *credentials,
                     const char *ccal_path,
                     char *error, size_t error_capacity);


/*
 * Parsed and CRC-verified CCAL in memory. Prepare this BEFORE opening any
 * authenticated CLIP session; subsequent UDS operations never need to
 * re-read or revalidate multi-megabyte disk files.
 */
#define CLIP46_UDS_MAX_REGIONS 4U
struct clip46_uds_image_region {
    unsigned long address;
    unsigned long length;
    unsigned char *bytes;
};
struct clip46_uds_image {
    unsigned int count;
    unsigned int identity_index;
    struct clip46_uds_image_region regions[CLIP46_UDS_MAX_REGIONS];
};

int clip46_uds_prepare_file(const char *path, struct clip46_uds_image *image);
void clip46_uds_free_image(struct clip46_uds_image *image);

int clip46_uds_flash_prepared(const struct clip46_uds_io *io,
                              const struct clip46_uds_config *credentials,
                              const struct clip46_uds_image *image,
                              char *error, size_t error_capacity);

/* ISO-TP helper for controlled synthetic tests. */
int clip46_uds_exchange(const struct clip46_uds_io *io,
                        const unsigned char *request, size_t request_len,
                        unsigned char *reply, size_t reply_capacity,
                        size_t *reply_len, unsigned long timeout_ms);

#endif
