#ifndef RP1210CLIP_H
#define RP1210CLIP_H

#include "rp1210scan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (RP1210_CALL *RP1210_PROGRESS_CALLBACK)(int percent,
                                                      const char *message);

/*
 * Pull the ECM calibration over CLIP/J1939 and write a .ccal file.
 *
 * api        RP1210 API implementation name, e.g. "NULN3R32".
 * device_id  RP1210 device ID from rp1210_get().
 * baud       J1939 baud in bits/s: 125000, 250000, 500000, 1000000.
 * tool_sa    J1939 source address used by the tool (normally 0xFA).
 * ecm_sa     ECM source/destination address (normally 0x00).
 * out_path   Destination .ccal path.
 * progress   Optional callback. May be NULL.
 *
 * Returns 0 on success, negative on failure.  Call rp1210_get_last_error()
 * for a human-readable error string.
 */
RP1210_EXPORT int RP1210_CALL rp1210_pull_ccal(
    const char *api,
    int device_id,
    int baud,
    unsigned char tool_sa,
    unsigned char ecm_sa,
    const char *out_path,
    RP1210_PROGRESS_CALLBACK progress
);

/* Verify and program a Cummins .ccal file into the ECM.  The native
 * implementation verifies the Cummins file CRC before opening the adapter. */
RP1210_EXPORT int RP1210_CALL rp1210_upload_ccal(
    const char *api,
    int device_id,
    int baud,
    unsigned char tool_sa,
    unsigned char ecm_sa,
    const char *ccal_path,
    RP1210_PROGRESS_CALLBACK progress
);

RP1210_EXPORT int RP1210_CALL rp1210_get_last_error(
    char *buffer,
    int buffer_size
);

#ifdef __cplusplus
}
#endif

#endif /* RP1210CLIP_H */
