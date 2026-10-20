#ifndef J1939_BACKEND_H
#define J1939_BACKEND_H
#include <stddef.h>
#include "j1939_transport.h"

/* Backend-independent J1939 message fields (not RP1210 wire bytes). */
struct j1939_frame {
    unsigned long pgn;
    unsigned char priority;
    unsigned char source_address;
    unsigned char destination_address;
};

/* All callbacks return J1939_TRANSPORT_* codes. On successful creation,
 * the session owns context and calls destroy when it is released. */
struct j1939_backend {
    void *context;
    int (*open)(void *context, const char *api_name, int device_id,
                int baud, const struct j1939_frame *session);
    void (*close)(void *context);
    int (*send)(void *context, const struct j1939_frame *frame,
                const unsigned char *payload, size_t length);
    int (*receive)(void *context, struct j1939_frame *frame,
                   unsigned char *payload, size_t capacity,
                   size_t *length, unsigned long timeout_ms);
    const char *(*error)(void *context);
    void (*destroy)(void *context);
};
#endif
