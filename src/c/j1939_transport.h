#ifndef J1939_TRANSPORT_H
#define J1939_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define J1939_TRANSPORT_OK              0
#define J1939_TRANSPORT_ERR_ARGUMENT   -1
#define J1939_TRANSPORT_ERR_LOAD_API   -2
#define J1939_TRANSPORT_ERR_SYMBOL     -3
#define J1939_TRANSPORT_ERR_CONNECT    -4
#define J1939_TRANSPORT_ERR_SEND       -5
#define J1939_TRANSPORT_ERR_RECEIVE    -6
#define J1939_TRANSPORT_ERR_TIMEOUT    -7
#define J1939_TRANSPORT_ERR_PROTOCOL   -8
#define J1939_TRANSPORT_ERR_MEMORY     -9

struct j1939_transport;

struct j1939_transport *
j1939_transport_create(void);

void
j1939_transport_destroy(struct j1939_transport *transport);

/*
 * Open a J1939 session over an RP1210 transport.
 *
 * baud may be 125000, 250000, 500000, 1000000, or 0 for
 * J1939:Baud=Auto.  Auto mode intentionally does not fall back to a
 * configured/default fixed bitrate.
 */
int
j1939_transport_open(struct j1939_transport *transport,
                     const char *api_name,
                     int device_id,
                     int baud,
                     unsigned long pgn,
                     unsigned char priority,
                     unsigned char source_address,
                     unsigned char destination_address);

void
j1939_transport_close(struct j1939_transport *transport);

int
j1939_transport_send(struct j1939_transport *transport,
                     const unsigned char *payload,
                     size_t payload_len);

int
j1939_transport_receive(struct j1939_transport *transport,
                        unsigned char *payload,
                        size_t payload_capacity,
                        size_t *payload_len,
                        unsigned long timeout_ms);

const char *
j1939_transport_error(const struct j1939_transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* J1939_TRANSPORT_H */
