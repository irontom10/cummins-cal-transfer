/*
 * J1939 sessions and frame matching, independent of adapter APIs.
 * RP1210 specifics reside in j1939_rp1210.c.
 * C89.
 */
#include <stdlib.h>
#include <string.h>
#include "j1939_transport.h"
#include "j1939_backend.h"
#include "ct_platform.h"
#include "j_string.h"

#define J1939_MAX_PAYLOAD 4096U

struct j1939_transport {
    struct j1939_backend backend;
    struct j1939_frame session;
    char last_error[512];
};

static void
set_error(struct j1939_transport *t, const char *s)
{
    j_str_copy(t->last_error, sizeof(t->last_error), s);
}

static int
backend_result(struct j1939_transport *t, int rc)
{
    const char *message;
    if (rc != J1939_TRANSPORT_OK) {
        message = t->backend.error(t->backend.context);
        if (message != NULL && message[0] != '\0')
            set_error(t, message);
    }
    return rc;
}

struct j1939_transport *
j1939_transport_create_with_backend(const struct j1939_backend *backend)
{
    struct j1939_transport *t;
    if (backend == NULL || backend->open == NULL ||
        backend->close == NULL || backend->send == NULL ||
        backend->receive == NULL || backend->error == NULL ||
        backend->destroy == NULL)
        return NULL;
    t = (struct j1939_transport *)calloc(1U, sizeof(*t));
    if (t != NULL)
        t->backend = *backend;
    return t;
}

void
j1939_transport_destroy(struct j1939_transport *t)
{
    if (t == NULL)
        return;
    t->backend.destroy(t->backend.context);
    free(t);
}

int
j1939_transport_open(struct j1939_transport *t, const char *api_name,
                     int device_id, int baud, unsigned long pgn,
                     unsigned char priority, unsigned char source_address,
                     unsigned char destination_address)
{
    int rc;
    if (t == NULL || api_name == NULL || api_name[0] == '\0' ||
        device_id < 0 || pgn > 0x00ffffffUL)
        return J1939_TRANSPORT_ERR_ARGUMENT;
    j1939_transport_close(t);
    t->last_error[0] = '\0';
    if (baud != 0 && baud != 125000 && baud != 250000 &&
        baud != 500000 && baud != 1000000) {
        set_error(t, "Unsupported J1939 baud rate.");
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }
    t->session.pgn = pgn;
    t->session.priority = priority;
    t->session.source_address = source_address;
    t->session.destination_address = destination_address;
    rc = t->backend.open(t->backend.context, api_name, device_id,
                          baud, &t->session);
    return backend_result(t, rc);
}

void
j1939_transport_close(struct j1939_transport *t)
{
    if (t != NULL)
        t->backend.close(t->backend.context);
}

int
j1939_transport_send(struct j1939_transport *t,
                     const unsigned char *payload, size_t length)
{
    int rc;
    if (t == NULL || payload == NULL || length > J1939_MAX_PAYLOAD)
        return J1939_TRANSPORT_ERR_ARGUMENT;
    rc = t->backend.send(t->backend.context, &t->session,
                         payload, length);
    return backend_result(t, rc);
}

int
j1939_transport_receive(struct j1939_transport *t,
                        unsigned char *payload, size_t capacity,
                        size_t *length, unsigned long timeout_ms)
{
    unsigned char buffer[J1939_MAX_PAYLOAD];
    struct j1939_frame frame;
    size_t size;
    unsigned long start;
    unsigned long elapsed;
    unsigned long remaining;
    int rc;

    if (t == NULL || payload == NULL || length == NULL)
        return J1939_TRANSPORT_ERR_ARGUMENT;
    *length = 0U;
    start = ct_monotonic_ms();

    for (;;) {
        elapsed = (unsigned long)(ct_monotonic_ms() - start);
        remaining = elapsed >= timeout_ms ? 0UL : timeout_ms - elapsed;
        size = 0U;
        rc = t->backend.receive(t->backend.context, &frame, buffer,
                                 sizeof(buffer), &size, remaining);
        if (rc != J1939_TRANSPORT_OK)
            return backend_result(t, rc);
        if (size > sizeof(buffer)) {
            set_error(t, "Incoming J1939 frame exceeds receive buffer.");
            return J1939_TRANSPORT_ERR_PROTOCOL;
        }
        if (frame.pgn == t->session.pgn &&
            frame.source_address == t->session.destination_address &&
            (frame.destination_address == t->session.source_address ||
             frame.destination_address == 0xffU)) {
            if (size > capacity) {
                set_error(t, "Incoming J1939 payload exceeds receive buffer.");
                return J1939_TRANSPORT_ERR_PROTOCOL;
            }
            if (size != 0U)
                memcpy(payload, buffer, size);
            *length = size;
            return J1939_TRANSPORT_OK;
        }
        if ((unsigned long)(ct_monotonic_ms() - start) >= timeout_ms)
            break;
    }
    set_error(t, "Timed out waiting for matching J1939 response.");
    return J1939_TRANSPORT_ERR_TIMEOUT;
}

const char *
j1939_transport_error(const struct j1939_transport *t)
{
    if (t == NULL)
        return "J1939 transport is not initialized.";
    return t->last_error;
}
