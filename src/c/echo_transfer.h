#ifndef ECHO_TRANSFER_H
#define ECHO_TRANSFER_H

#ifdef __cplusplus
extern "C" {
#endif

#define ECHO_TRANSFER_OK             0
#define ECHO_TRANSFER_ERR_ARGUMENT  -1
#define ECHO_TRANSFER_ERR_TRANSPORT -2
#define ECHO_TRANSFER_ERR_PROTOCOL  -3
#define ECHO_TRANSFER_ERR_MEMORY    -4
#define ECHO_TRANSFER_ERR_FILE      -5
#define ECHO_TRANSFER_ERR_CRC       -6

typedef int (*echo_send_fn)(void *user,
                            const unsigned char *data,
                            unsigned int length);

typedef int (*echo_recv_fn)(void *user,
                            unsigned char *data,
                            unsigned int capacity,
                            unsigned int *length,
                            unsigned long timeout_ms);

typedef void (*echo_progress_fn)(void *user,
                                 int percent,
                                 const char *message);

typedef struct echo_io_tag {
    void *user;
    echo_send_fn send;
    echo_recv_fn recv;
    echo_progress_fn progress;
} echo_io;

/*
 * Probe the legacy raw Proprietary-A parameter service for ProductID "ECH".
 * Returns 1 for ECH/ECHO, 0 for another legacy platform, or a negative
 * ECHO_TRANSFER_ERR_* code for an invalid caller.
 */
int echo_probe(const echo_io *io);

/*
 * Read an ECH/ECHO calibration and write the legacy header + Intel-HEX .ccal
 * format observed in the supplied validated readback trace.
 *
 * This module intentionally implements readback only.  It contains no
 * calibration/configuration programming path.
 */
int echo_pull_ccal(const echo_io *io, const char *path);

const char *echo_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ECHO_TRANSFER_H */
