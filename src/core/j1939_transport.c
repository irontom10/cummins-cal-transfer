/*
 * j1939_transport.c
 *
 * J1939 session/framing over the generic RP1210 transport.
 *
 * J1939-specific connection strings, RP1210 J1939 filtering commands,
 * address protection, PGN/source/destination matching, and the RP1210 J1939
 * message header live here rather than in rp1210_transport.c.
 *
 * C89 source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "j1939_transport.h"
#include "rp1210_transport.h"
#include "ct_platform.h"

#define J1939_TRANSPORT_MAX_PAYLOAD        4096U
#define J1939_RP1210_RX_OVERHEAD             32U

#define RP1210_CMD_SET_J1939_FILTER            4
#define RP1210_CMD_SET_ALL_FILTERS_DISCARD    17
#define RP1210_CMD_PROTECT_J1939_ADDRESS      19
#define RP1210_CMD_SET_J1939_FILTER_TYPE      25
#define RP1210_CMD_FLUSH_TX_RX                39

#define RP1210_FILTER_PGN          0x01U
#define RP1210_FILTER_SOURCE       0x04U
#define RP1210_FILTER_DESTINATION  0x08U
#define RP1210_FILTER_INCLUSIVE    0x00U

struct j1939_transport {
    struct rp1210_transport *rp1210;
    unsigned long pgn;
    unsigned char priority;
    unsigned char source_address;
    unsigned char destination_address;
    char last_error[512];
};

static void
safe_copy(char *dst, const char *src, size_t dst_size)
{
    size_t n;

    if (dst == NULL || dst_size == 0U)
        return;

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    n = strlen(src);
    if (n >= dst_size)
        n = dst_size - 1U;

    if (n != 0U)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static void
set_error_text(struct j1939_transport *transport, const char *text)
{
    if (transport == NULL)
        return;

    safe_copy(transport->last_error, text, sizeof(transport->last_error));
}

static int
map_rp1210_result(struct j1939_transport *transport, int rc)
{
    const char *message;

    if (rc == RP1210_TRANSPORT_OK)
        return J1939_TRANSPORT_OK;

    message = rp1210_transport_error(
        transport != NULL ? transport->rp1210 : NULL);
    if (message != NULL && message[0] != '\0')
        set_error_text(transport, message);

    switch (rc) {
    case RP1210_TRANSPORT_ERR_ARGUMENT:
        return J1939_TRANSPORT_ERR_ARGUMENT;
    case RP1210_TRANSPORT_ERR_LOAD_API:
        return J1939_TRANSPORT_ERR_LOAD_API;
    case RP1210_TRANSPORT_ERR_SYMBOL:
        return J1939_TRANSPORT_ERR_SYMBOL;
    case RP1210_TRANSPORT_ERR_CONNECT:
        return J1939_TRANSPORT_ERR_CONNECT;
    case RP1210_TRANSPORT_ERR_SEND:
        return J1939_TRANSPORT_ERR_SEND;
    case RP1210_TRANSPORT_ERR_RECEIVE:
        return J1939_TRANSPORT_ERR_RECEIVE;
    case RP1210_TRANSPORT_ERR_TIMEOUT:
        return J1939_TRANSPORT_ERR_TIMEOUT;
    case RP1210_TRANSPORT_ERR_MEMORY:
        return J1939_TRANSPORT_ERR_MEMORY;
    default:
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }
}

static int
open_protocol(struct j1939_transport *transport,
              const char *api_name,
              int device_id,
              const char *protocol)
{
    int rc;

    rc = rp1210_transport_open(transport->rp1210,
                               api_name,
                               device_id,
                               protocol);
    return map_rp1210_result(transport, rc);
}

static int
connect_j1939(struct j1939_transport *transport,
              const char *api_name,
              int device_id,
              int baud)
{
    char protocol[64];
    char protocol_alt[64];
    int kbps;
    int rc;

    if (baud == 0) {
        rc = open_protocol(transport,
                           api_name,
                           device_id,
                           "J1939:Baud=Auto");
        if (rc != J1939_TRANSPORT_OK) {
            const char *lower_error;
            char tmp[512];

            lower_error = rp1210_transport_error(transport->rp1210);
            if (lower_error != NULL && lower_error[0] != '\0') {
                sprintf(tmp,
                        "J1939:Baud=Auto connection failed: %.440s",
                        lower_error);
                set_error_text(transport, tmp);
            }
        }
        return rc;
    }

    if (baud != 125000 &&
        baud != 250000 &&
        baud != 500000 &&
        baud != 1000000) {
        set_error_text(transport, "Unsupported J1939 baud rate.");
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    kbps = baud / 1000;
    sprintf(protocol, "J1939:Baud=%d", kbps);
    rc = open_protocol(transport, api_name, device_id, protocol);
    if (rc == J1939_TRANSPORT_OK)
        return rc;
    if (rc != J1939_TRANSPORT_ERR_CONNECT)
        return rc;

    sprintf(protocol_alt, "J1939:Baud=%d", baud);
    rc = open_protocol(transport, api_name, device_id, protocol_alt);
    if (rc == J1939_TRANSPORT_OK)
        return rc;
    if (rc != J1939_TRANSPORT_ERR_CONNECT)
        return rc;

    {
        const char *lower_error;
        char tmp[512];

        lower_error = rp1210_transport_error(transport->rp1210);
        if (lower_error != NULL && lower_error[0] != '\0') {
            sprintf(tmp,
                    "Unable to connect at requested J1939 bitrate %d bit/s: %.390s",
                    baud,
                    lower_error);
        } else {
            sprintf(tmp,
                    "Unable to connect at requested J1939 bitrate %d bit/s.",
                    baud);
        }
        set_error_text(transport, tmp);
    }

    return rc;
}

static int
command_or_connect_error(struct j1939_transport *transport,
                         int command,
                         const unsigned char *data,
                         size_t data_len,
                         const char *what)
{
    int rc;
    const char *message;
    char tmp[512];

    rc = rp1210_transport_command(transport->rp1210,
                                  command,
                                  data,
                                  data_len);
    if (rc == RP1210_TRANSPORT_OK)
        return J1939_TRANSPORT_OK;

    message = rp1210_transport_error(transport->rp1210);
    if (message != NULL && message[0] != '\0')
        sprintf(tmp, "%s: %.440s", what, message);
    else
        sprintf(tmp, "%s failed.", what);
    set_error_text(transport, tmp);

    return J1939_TRANSPORT_ERR_CONNECT;
}

static int
configure_j1939(struct j1939_transport *transport)
{
    unsigned char filter_type;
    unsigned char filter[7];
    unsigned char claim[10];
    int rc;

    rc = command_or_connect_error(
        transport,
        RP1210_CMD_SET_ALL_FILTERS_DISCARD,
        NULL,
        0U,
        "RP1210 Set_All_Filters_States_to_Discard");
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    filter_type = RP1210_FILTER_INCLUSIVE;
    rc = command_or_connect_error(
        transport,
        RP1210_CMD_SET_J1939_FILTER_TYPE,
        &filter_type,
        1U,
        "RP1210 Set_J1939_Filter_Type");
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    filter[0] = (unsigned char)(RP1210_FILTER_PGN |
                                RP1210_FILTER_SOURCE |
                                RP1210_FILTER_DESTINATION);
    filter[1] = (unsigned char)(transport->pgn & 0xffUL);
    filter[2] = (unsigned char)((transport->pgn >> 8) & 0xffUL);
    filter[3] = (unsigned char)((transport->pgn >> 16) & 0xffUL);
    filter[4] = 0x00U;
    filter[5] = transport->destination_address;
    filter[6] = transport->source_address;

    rc = command_or_connect_error(
        transport,
        RP1210_CMD_SET_J1939_FILTER,
        filter,
        sizeof(filter),
        "RP1210 Set_Message_Filtering_For_J1939");
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    /*
     * Flush is advisory here, matching the old transport behavior.  Some
     * adapters may not implement it, and failure is not fatal to the session.
     */
    (void)rp1210_transport_command(transport->rp1210,
                                   RP1210_CMD_FLUSH_TX_RX,
                                   NULL,
                                   0U);

    claim[0] = transport->source_address;
    claim[1] = 0x01U;
    claim[2] = 0x00U;
    claim[3] = 0x00U;
    claim[4] = 0x00U;
    claim[5] = 0x00U;
    claim[6] = 0x81U;
    claim[7] = 0x00U;
    claim[8] = 0x80U;
    claim[9] = 0x00U;

    return command_or_connect_error(
        transport,
        RP1210_CMD_PROTECT_J1939_ADDRESS,
        claim,
        sizeof(claim),
        "RP1210 Protect_J1939_Address");
}

struct j1939_transport *
j1939_transport_create(void)
{
    struct j1939_transport *transport;

    transport = (struct j1939_transport *)calloc(
        1U,
        sizeof(struct j1939_transport));
    if (transport == NULL)
        return NULL;

    transport->rp1210 = rp1210_transport_create();
    if (transport->rp1210 == NULL) {
        free(transport);
        return NULL;
    }

    return transport;
}

void
j1939_transport_destroy(struct j1939_transport *transport)
{
    if (transport == NULL)
        return;

    rp1210_transport_destroy(transport->rp1210);
    transport->rp1210 = NULL;
    free(transport);
}

int
j1939_transport_open(struct j1939_transport *transport,
                     const char *api_name,
                     int device_id,
                     int baud,
                     unsigned long pgn,
                     unsigned char priority,
                     unsigned char source_address,
                     unsigned char destination_address)
{
    int rc;

    if (transport == NULL ||
        api_name == NULL || api_name[0] == '\0' ||
        device_id < 0 ||
        pgn > 0x00ffffffUL) {
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    j1939_transport_close(transport);
    transport->last_error[0] = '\0';
    transport->pgn = pgn;
    transport->priority = priority;
    transport->source_address = source_address;
    transport->destination_address = destination_address;

    rc = connect_j1939(transport, api_name, device_id, baud);
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    rc = configure_j1939(transport);
    if (rc != J1939_TRANSPORT_OK) {
        rp1210_transport_close(transport->rp1210);
        return rc;
    }

    return J1939_TRANSPORT_OK;
}

void
j1939_transport_close(struct j1939_transport *transport)
{
    if (transport == NULL || transport->rp1210 == NULL)
        return;

    rp1210_transport_close(transport->rp1210);
}

int
j1939_transport_send(struct j1939_transport *transport,
                     const unsigned char *payload,
                     size_t payload_len)
{
    unsigned char buffer[J1939_TRANSPORT_MAX_PAYLOAD + 6U];
    int rc;

    if (transport == NULL ||
        payload == NULL ||
        payload_len > J1939_TRANSPORT_MAX_PAYLOAD) {
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    buffer[0] = (unsigned char)(transport->pgn & 0xffUL);
    buffer[1] = (unsigned char)((transport->pgn >> 8) & 0xffUL);
    buffer[2] = (unsigned char)((transport->pgn >> 16) & 0xffUL);
    buffer[3] = transport->priority;
    buffer[4] = transport->source_address;
    buffer[5] = transport->destination_address;
    memcpy(buffer + 6U, payload, payload_len);

    rc = rp1210_transport_send(transport->rp1210,
                               buffer,
                               payload_len + 6U);
    return map_rp1210_result(transport, rc);
}

int
j1939_transport_receive(struct j1939_transport *transport,
                        unsigned char *payload,
                        size_t payload_capacity,
                        size_t *payload_len,
                        unsigned long timeout_ms)
{
    unsigned char buffer[J1939_TRANSPORT_MAX_PAYLOAD +
                         J1939_RP1210_RX_OVERHEAD];
    unsigned long start;
    unsigned long now;
    unsigned long elapsed;
    unsigned long remaining;
    unsigned long pgn;
    unsigned char source_address;
    unsigned char destination_address;
    size_t message_len;
    size_t n_payload;
    int rc;

    if (transport == NULL ||
        payload == NULL ||
        payload_len == NULL) {
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    *payload_len = 0U;
    start = ct_monotonic_ms();

    for (;;) {
        now = ct_monotonic_ms();
        elapsed = (unsigned long)((unsigned long)(now - start));
        if (elapsed >= timeout_ms)
            remaining = 0UL;
        else
            remaining = timeout_ms - elapsed;

        message_len = 0U;
        rc = rp1210_transport_receive(transport->rp1210,
                                      buffer,
                                      sizeof(buffer),
                                      &message_len,
                                      remaining);
        if (rc != RP1210_TRANSPORT_OK)
            return map_rp1210_result(transport, rc);

        /*
         * RP1210 J1939 receive framing is vendor-dependent when transmitted
         * message echoing is enabled.  The normal form is:
         *
         *   timestamp[4], PGN[3], priority, source, destination, payload
         *
         * Some implementations (notably the NEXIQ mobile runtime) expose the
         * echo-status byte in the receive header instead:
         *
         *   timestamp[4], echo, PGN[3], priority, source, destination, payload
         *
         * Do not change the vendor's echo mode just to make the parser fit.
         * Match the frame structurally and accept either legal header.
         */
        {
            size_t header_len;
            header_len = 0U;

            if (message_len >= 10U) {
                pgn = (unsigned long)buffer[4] |
                      ((unsigned long)buffer[5] << 8) |
                      ((unsigned long)buffer[6] << 16);
                source_address = buffer[8];
                destination_address = buffer[9];

                if (pgn == transport->pgn &&
                    source_address == transport->destination_address &&
                    (destination_address == transport->source_address ||
                     destination_address == 0xffU)) {
                    header_len = 10U;
                }
            }

            if (header_len == 0U && message_len >= 11U) {
                pgn = (unsigned long)buffer[5] |
                      ((unsigned long)buffer[6] << 8) |
                      ((unsigned long)buffer[7] << 16);
                source_address = buffer[9];
                destination_address = buffer[10];

                if (pgn == transport->pgn &&
                    source_address == transport->destination_address &&
                    (destination_address == transport->source_address ||
                     destination_address == 0xffU)) {
                    header_len = 11U;
                }
            }

            if (header_len != 0U) {
                n_payload = message_len - header_len;
                if (n_payload > payload_capacity) {
                    set_error_text(
                        transport,
                        "Incoming J1939 payload exceeds receive buffer.");
                    return J1939_TRANSPORT_ERR_PROTOCOL;
                }

                memcpy(payload, buffer + header_len, n_payload);
                *payload_len = n_payload;
                return J1939_TRANSPORT_OK;
            }
        }

        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= (unsigned long)timeout_ms)
            break;
    }

    set_error_text(transport,
                   "Timed out waiting for matching J1939 response.");
    return J1939_TRANSPORT_ERR_TIMEOUT;
}

const char *
j1939_transport_error(const struct j1939_transport *transport)
{
    if (transport == NULL)
        return "J1939 transport is not initialized.";

    return transport->last_error;
}
