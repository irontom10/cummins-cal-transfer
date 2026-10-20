#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "j1939_transport.h"
#include "j1939_backend.h"

/* This test deliberately never links RP1210. */
struct fake_backend {
    int opened;
    int closed;
    int sent;
    int read_count;
};
static int g_destroyed;

static int
fake_open(void *context, const char *name, int device, int baud,
          const struct j1939_frame *session)
{
    struct fake_backend *b = (struct fake_backend *)context;
    assert(strcmp(name, "TEST") == 0);
    assert(device == 0 && baud == 250000);
    assert(session->pgn == 0x00ef00UL);
    assert(session->priority == 6U);
    b->opened++;
    return J1939_TRANSPORT_OK;
}

static void
fake_close(void *context)
{
    ((struct fake_backend *)context)->closed++;
}

static int
fake_send(void *context, const struct j1939_frame *frame,
          const unsigned char *payload, size_t length)
{
    struct fake_backend *b = (struct fake_backend *)context;
    assert(frame->pgn == 0x00ef00UL);
    assert(frame->source_address == 0xfaU);
    assert(frame->destination_address == 0x00U);
    assert(length == 2U && payload[0] == 0x12U && payload[1] == 0x34U);
    b->sent++;
    return J1939_TRANSPORT_OK;
}

static int
fake_receive(void *context, struct j1939_frame *frame,
             unsigned char *payload, size_t capacity,
             size_t *length, unsigned long timeout_ms)
{
    struct fake_backend *b = (struct fake_backend *)context;
    (void)timeout_ms;
    assert(capacity >= 2U);
    b->read_count++;

    frame->pgn = b->read_count == 1 ? 0x00ee00UL : 0x00ef00UL;
    frame->priority = 6U;
    frame->source_address = 0x00U;
    frame->destination_address = 0xfaU;
    payload[0] = 0xabU;
    payload[1] = 0xcdU;
    *length = 2U;
    return J1939_TRANSPORT_OK;
}

static const char *
fake_error(void *context)
{
    (void)context;
    return "synthetic backend failure";
}

static void
fake_destroy(void *context)
{
    free(context);
    g_destroyed++;
}

int
main(void)
{
    struct fake_backend *context;
    struct j1939_backend backend;
    struct j1939_transport *transport;
    unsigned char data[4];
    size_t length;
    static const unsigned char tx[] = {0x12U, 0x34U};

    context = (struct fake_backend *)calloc(1U, sizeof(*context));
    assert(context != NULL);
    backend.context = context;
    backend.open = fake_open;
    backend.close = fake_close;
    backend.send = fake_send;
    backend.receive = fake_receive;
    backend.error = fake_error;
    backend.destroy = fake_destroy;

    transport = j1939_transport_create_with_backend(&backend);
    assert(transport != NULL);
    assert(j1939_transport_open(transport, "TEST", 0, 250000, 0x00ef00UL,
                                6U, 0xfaU, 0x00U) == J1939_TRANSPORT_OK);
    assert(j1939_transport_send(transport, tx, sizeof(tx)) ==
           J1939_TRANSPORT_OK);

    length = 0U;
    assert(j1939_transport_receive(transport, data, sizeof(data),
                                    &length, 100UL) == J1939_TRANSPORT_OK);
    assert(context->read_count == 2);
    assert(length == 2U && data[0] == 0xabU && data[1] == 0xcdU);

    length = 0U;
    assert(j1939_transport_receive(transport, data, 1U, &length, 100UL) ==
           J1939_TRANSPORT_ERR_PROTOCOL);
    assert(strstr(j1939_transport_error(transport), "exceeds") != NULL);
    assert(context->opened == 1 && context->sent == 1);
    j1939_transport_close(transport);
    assert(context->closed >= 2);
    j1939_transport_destroy(transport);
    assert(g_destroyed == 1);
    puts("J1939 backend independence test passed");
    return 0;
}
