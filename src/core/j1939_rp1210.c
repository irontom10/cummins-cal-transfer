/*
 * RP1210 binding for the generic J1939 session.
 * Owns the RP1210 protocol strings, filters, address protection, and
 * vendor-specific J1939 record headers. ISO C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "j1939_transport.h"
#include "j1939_backend.h"
#include "rp1210_transport.h"
#include "j_string.h"

#define MAX_PAYLOAD 4096U
#define RX_OVERHEAD 32U

enum rp1210_j1939_command {
    CMD_SET_J1939_FILTER = 4,
    CMD_ALL_FILTERS_DISCARD = 17,
    CMD_PROTECT_J1939_ADDRESS = 19,
    CMD_SET_J1939_FILTER_TYPE = 25,
    CMD_FLUSH_TX_RX = 39
};
enum rp1210_j1939_filter {
    FILTER_PGN = 0x01,
    FILTER_SOURCE = 0x04,
    FILTER_DESTINATION = 0x08,
    FILTER_INCLUSIVE = 0x00
};

struct j1939_rp1210 {
    struct rp1210_transport *rp1210;
    struct j1939_frame session;
    char error[512];
};

static void
set_error(struct j1939_rp1210 *b, const char *message)
{
    j_str_copy(b->error, sizeof(b->error), message);
}

static const char *
backend_error(void *context)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    if (b->error[0] != '\0')
        return b->error;
    return rp1210_transport_error(b->rp1210);
}

static int
map_result(int rc)
{
    switch (rc) {
    case RP1210_TRANSPORT_OK: return J1939_TRANSPORT_OK;
    case RP1210_TRANSPORT_ERR_ARGUMENT: return J1939_TRANSPORT_ERR_ARGUMENT;
    case RP1210_TRANSPORT_ERR_LOAD_API: return J1939_TRANSPORT_ERR_LOAD_API;
    case RP1210_TRANSPORT_ERR_SYMBOL: return J1939_TRANSPORT_ERR_SYMBOL;
    case RP1210_TRANSPORT_ERR_CONNECT: return J1939_TRANSPORT_ERR_CONNECT;
    case RP1210_TRANSPORT_ERR_SEND: return J1939_TRANSPORT_ERR_SEND;
    case RP1210_TRANSPORT_ERR_RECEIVE: return J1939_TRANSPORT_ERR_RECEIVE;
    case RP1210_TRANSPORT_ERR_TIMEOUT: return J1939_TRANSPORT_ERR_TIMEOUT;
    case RP1210_TRANSPORT_ERR_MEMORY: return J1939_TRANSPORT_ERR_MEMORY;
    default: return J1939_TRANSPORT_ERR_PROTOCOL;
    }
}

static int
open_protocol(struct j1939_rp1210 *b, const char *api, int device,
              const char *protocol)
{
    return map_result(rp1210_transport_open(b->rp1210, api, device, protocol));
}

static int
connect_j1939(struct j1939_rp1210 *b, const char *api,
              int device, int baud)
{
    char protocol[64];
    char message[512];
    const char *lower;
    int rc;

    if (baud == 0) {
        rc = open_protocol(b, api, device, "J1939:Baud=Auto");
        if (rc != J1939_TRANSPORT_OK) {
            lower = rp1210_transport_error(b->rp1210);
            if (lower != NULL && lower[0] != '\0') {
                sprintf(message, "J1939:Baud=Auto connection failed: %.440s",
                        lower);
                set_error(b, message);
            }
        }
        return rc;
    }

    sprintf(protocol, "J1939:Baud=%d", baud / 1000);
    rc = open_protocol(b, api, device, protocol);
    if (rc != J1939_TRANSPORT_ERR_CONNECT)
        return rc;

    sprintf(protocol, "J1939:Baud=%d", baud);
    rc = open_protocol(b, api, device, protocol);
    if (rc != J1939_TRANSPORT_ERR_CONNECT)
        return rc;

    lower = rp1210_transport_error(b->rp1210);
    if (lower != NULL && lower[0] != '\0')
        sprintf(message,
                "Unable to connect at requested J1939 bitrate %d bit/s: %.390s",
                baud, lower);
    else
        sprintf(message, "Unable to connect at requested J1939 bitrate %d bit/s.",
                baud);
    set_error(b, message);
    return rc;
}

static int
command_or_error(struct j1939_rp1210 *b, int command,
                 const unsigned char *data, size_t length,
                 const char *name)
{
    int rc;
    char message[512];
    const char *lower;
    rc = rp1210_transport_command(b->rp1210, command, data, length);
    if (rc == RP1210_TRANSPORT_OK)
        return J1939_TRANSPORT_OK;
    lower = rp1210_transport_error(b->rp1210);
    if (lower != NULL && lower[0] != '\0')
        sprintf(message, "%s: %.440s", name, lower);
    else
        sprintf(message, "%s failed.", name);
    set_error(b, message);
    return J1939_TRANSPORT_ERR_CONNECT;
}

static int
configure_j1939(struct j1939_rp1210 *b)
{
    unsigned char filter_type;
    unsigned char filter[7];
    unsigned char claim[10];
    int rc;
    rc = command_or_error(b, CMD_ALL_FILTERS_DISCARD, NULL, 0U,
                          "RP1210 Set_All_Filters_States_to_Discard");
    if (rc != J1939_TRANSPORT_OK)
        return rc;
    filter_type = FILTER_INCLUSIVE;
    rc = command_or_error(b, CMD_SET_J1939_FILTER_TYPE, &filter_type, 1U,
                          "RP1210 Set_J1939_Filter_Type");
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    filter[0] = (unsigned char)(FILTER_PGN | FILTER_SOURCE |
                                FILTER_DESTINATION);
    filter[1] = (unsigned char)(b->session.pgn & 0xffUL);
    filter[2] = (unsigned char)((b->session.pgn >> 8) & 0xffUL);
    filter[3] = (unsigned char)((b->session.pgn >> 16) & 0xffUL);
    filter[4] = 0x00U;
    filter[5] = b->session.destination_address;
    filter[6] = b->session.source_address;
    rc = command_or_error(b, CMD_SET_J1939_FILTER, filter, sizeof(filter),
                          "RP1210 Set_Message_Filtering_For_J1939");
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    /* Advisory command; some devices do not support it. */
    (void)rp1210_transport_command(b->rp1210, CMD_FLUSH_TX_RX, NULL, 0U);

    claim[0] = b->session.source_address;
    claim[1] = 0x01U;
    claim[2] = 0x00U;
    claim[3] = 0x00U;
    claim[4] = 0x00U;
    claim[5] = 0x00U;
    claim[6] = 0x81U;
    claim[7] = 0x00U;
    claim[8] = 0x80U;
    claim[9] = 0x00U;
    return command_or_error(b, CMD_PROTECT_J1939_ADDRESS, claim,
                            sizeof(claim), "RP1210 Protect_J1939_Address");
}

static int
backend_open(void *context, const char *api, int device,
             int baud, const struct j1939_frame *session)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    int rc;
    b->error[0] = '\0';
    b->session = *session;
    rc = connect_j1939(b, api, device, baud);
    if (rc != J1939_TRANSPORT_OK)
        return rc;
    rc = configure_j1939(b);
    if (rc != J1939_TRANSPORT_OK)
        rp1210_transport_close(b->rp1210);
    return rc;
}

static void
backend_close(void *context)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    rp1210_transport_close(b->rp1210);
}

static int
backend_send(void *context, const struct j1939_frame *frame,
             const unsigned char *payload, size_t length)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    unsigned char buffer[MAX_PAYLOAD + 6U];
    if (length > MAX_PAYLOAD)
        return J1939_TRANSPORT_ERR_ARGUMENT;
    buffer[0] = (unsigned char)(frame->pgn & 0xffUL);
    buffer[1] = (unsigned char)((frame->pgn >> 8) & 0xffUL);
    buffer[2] = (unsigned char)((frame->pgn >> 16) & 0xffUL);
    buffer[3] = frame->priority;
    buffer[4] = frame->source_address;
    buffer[5] = frame->destination_address;
    if (length != 0U)
        memcpy(buffer + 6U, payload, length);
    return map_result(rp1210_transport_send(b->rp1210, buffer, length + 6U));
}

static void
decode_header(const unsigned char *data, size_t offset,
              struct j1939_frame *frame)
{
    frame->pgn = (unsigned long)data[offset] |
                 ((unsigned long)data[offset + 1U] << 8) |
                 ((unsigned long)data[offset + 2U] << 16);
    frame->priority = data[offset + 3U];
    frame->source_address = data[offset + 4U];
    frame->destination_address = data[offset + 5U];
}

static int
matches(const struct j1939_rp1210 *b, const struct j1939_frame *frame)
{
    return frame->pgn == b->session.pgn &&
           frame->source_address == b->session.destination_address &&
           (frame->destination_address == b->session.source_address ||
            frame->destination_address == 0xffU);
}

static int
backend_receive(void *context, struct j1939_frame *frame,
                unsigned char *payload, size_t capacity,
                size_t *length, unsigned long timeout_ms)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    unsigned char buffer[MAX_PAYLOAD + RX_OVERHEAD];
    size_t raw_length;
    size_t header;
    size_t data_length;
    int rc;

    raw_length = 0U;
    *length = 0U;
    frame->pgn = 0xffffffffUL;
    frame->priority = 0U;
    frame->source_address = 0U;
    frame->destination_address = 0U;
    rc = rp1210_transport_receive(b->rp1210, buffer, sizeof(buffer),
                                  &raw_length, timeout_ms);
    if (rc != RP1210_TRANSPORT_OK)
        return map_result(rc);

    /*
     * RP1210: timestamp[4], PGN[3], priority, source, destination, data.
     * NEXIQ mobile can insert an echo-status byte after timestamp[4].
     * Compare both headers against the configured session to resolve
     * the ambiguity without changing the adapter's echo setting.
     */
    header = 0U;
    if (raw_length >= 10U) {
        decode_header(buffer, 4U, frame);
        if (matches(b, frame))
            header = 10U;
    }
    if (header == 0U && raw_length >= 11U) {
        decode_header(buffer, 5U, frame);
        if (matches(b, frame))
            header = 11U;
    }
    if (header == 0U) {
        frame->pgn = 0xffffffffUL;
        return J1939_TRANSPORT_OK;
    }
    data_length = raw_length - header;
    if (data_length > capacity) {
        set_error(b, "Incoming J1939 frame exceeds backend buffer.");
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }
    if (data_length != 0U)
        memcpy(payload, buffer + header, data_length);
    *length = data_length;
    return J1939_TRANSPORT_OK;
}

static void
backend_destroy(void *context)
{
    struct j1939_rp1210 *b = (struct j1939_rp1210 *)context;
    rp1210_transport_destroy(b->rp1210);
    free(b);
}

struct j1939_transport *
j1939_transport_create(void)
{
    struct j1939_rp1210 *b;
    struct j1939_transport *t;
    struct j1939_backend backend;
    b = (struct j1939_rp1210 *)calloc(1U, sizeof(*b));
    if (b == NULL)
        return NULL;
    b->rp1210 = rp1210_transport_create();
    if (b->rp1210 == NULL) {
        free(b);
        return NULL;
    }
    backend.context = b;
    backend.open = backend_open;
    backend.close = backend_close;
    backend.send = backend_send;
    backend.receive = backend_receive;
    backend.error = backend_error;
    backend.destroy = backend_destroy;
    t = j1939_transport_create_with_backend(&backend);
    if (t == NULL)
        backend_destroy(b);
    return t;
}
