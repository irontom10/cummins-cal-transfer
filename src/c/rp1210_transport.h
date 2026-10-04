#ifndef RP1210_TRANSPORT_H
#define RP1210_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RP1210_TRANSPORT_OK              0
#define RP1210_TRANSPORT_ERR_ARGUMENT   -1
#define RP1210_TRANSPORT_ERR_LOAD_API   -2
#define RP1210_TRANSPORT_ERR_SYMBOL     -3
#define RP1210_TRANSPORT_ERR_CONNECT    -4
#define RP1210_TRANSPORT_ERR_SEND       -5
#define RP1210_TRANSPORT_ERR_RECEIVE    -6
#define RP1210_TRANSPORT_ERR_TIMEOUT    -7
#define RP1210_TRANSPORT_ERR_PROTOCOL   -8
#define RP1210_TRANSPORT_ERR_MEMORY     -9

struct rp1210_transport;

struct rp1210_transport *
rp1210_transport_create(void);

void
rp1210_transport_destroy(struct rp1210_transport *transport);

int
rp1210_transport_open(struct rp1210_transport *transport,
                      const char *api_name,
                      int device_id,
                      int baud,
                      unsigned long pgn,
                      unsigned char priority,
                      unsigned char source_address,
                      unsigned char destination_address);

void
rp1210_transport_close(struct rp1210_transport *transport);

int
rp1210_transport_send(struct rp1210_transport *transport,
                      const unsigned char *payload,
                      size_t payload_len);

int
rp1210_transport_receive(struct rp1210_transport *transport,
                         unsigned char *payload,
                         size_t payload_capacity,
                         size_t *payload_len,
                         unsigned long timeout_ms);

const char *
rp1210_transport_error(const struct rp1210_transport *transport);

#ifdef __cplusplus
}
#endif

#endif /* RP1210_TRANSPORT_H */
