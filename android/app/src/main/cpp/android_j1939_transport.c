/*
 * Android raw-CAN J1939 transport.
 *
 * This file exports the same j1939_transport_* ABI used by clip_transfer.c,
 * but replaces the Windows RP1210 layer with raw 29-bit CAN callbacks supplied
 * by android_jni.c.  It implements destination-specific J1939 TP (RTS/CTS)
 * because RP1210 normally hides that job from the application.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "j1939_transport.h"

#define J1939_MAX_TP_PAYLOAD 1785U
#define J1939_PGN_TP_CM      0x00ec00UL
#define J1939_PGN_TP_DT      0x00eb00UL
#define J1939_PGN_ADDRESS    0x00ee00UL

#define TP_CM_RTS            0x10U
#define TP_CM_CTS            0x11U
#define TP_CM_EOM_ACK        0x13U
#define TP_CM_BAM            0x20U
#define TP_CM_ABORT          0xffU

#define TP_WINDOW_PACKETS    16U
#define TP_CONTROL_TIMEOUT   5000UL

extern int android_can_open(int baud);
extern void android_can_close(void);
extern int android_can_send(unsigned long can_id,
                            const unsigned char *data,
                            unsigned int length);
extern int android_can_receive(unsigned long *can_id,
                               unsigned char data[8],
                               unsigned int *length,
                               unsigned long timeout_ms);

struct j1939_transport {
    unsigned long pgn;
    unsigned char priority;
    unsigned char source_address;
    unsigned char destination_address;
    int is_open;
    char last_error[512];
};

struct parsed_id {
    unsigned char priority;
    unsigned long pgn;
    unsigned char source;
    unsigned char destination;
};

static unsigned long
now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0UL;

    return (unsigned long)ts.tv_sec * 1000UL +
           (unsigned long)(ts.tv_nsec / 1000000L);
}

static void
sleep_ms(unsigned long ms)
{
    struct timespec req;

    req.tv_sec = (time_t)(ms / 1000UL);
    req.tv_nsec = (long)(ms % 1000UL) * 1000000L;
    while (nanosleep(&req, &req) != 0)
        ;
}

static void
set_error(struct j1939_transport *transport, const char *text)
{
    size_t n;

    if (transport == NULL)
        return;

    if (text == NULL) {
        transport->last_error[0] = '\0';
        return;
    }

    n = strlen(text);
    if (n >= sizeof(transport->last_error))
        n = sizeof(transport->last_error) - 1U;

    if (n != 0U)
        memcpy(transport->last_error, text, n);
    transport->last_error[n] = '\0';
}

static unsigned long
make_id(unsigned char priority,
        unsigned long pgn,
        unsigned char source,
        unsigned char destination)
{
    unsigned long id;
    unsigned int pf;
    unsigned int ps;

    pf = (unsigned int)((pgn >> 8) & 0xffUL);
    if (pf < 240U)
        ps = destination;
    else
        ps = (unsigned int)(pgn & 0xffUL);

    id = ((unsigned long)(priority & 7U) << 26);
    id |= ((pgn >> 16) & 0x03UL) << 24;
    id |= (unsigned long)pf << 16;
    id |= (unsigned long)ps << 8;
    id |= (unsigned long)source;
    return id & 0x1fffffffUL;
}

static void
parse_id(unsigned long id, struct parsed_id *out)
{
    unsigned int pf;
    unsigned int ps;
    unsigned long dp;

    out->priority = (unsigned char)((id >> 26) & 7UL);
    dp = (id >> 24) & 0x03UL;
    pf = (unsigned int)((id >> 16) & 0xffUL);
    ps = (unsigned int)((id >> 8) & 0xffUL);
    out->source = (unsigned char)(id & 0xffUL);

    if (pf < 240U) {
        out->destination = (unsigned char)ps;
        out->pgn = (dp << 16) | ((unsigned long)pf << 8);
    }
    else {
        out->destination = 0xffU;
        out->pgn = (dp << 16) |
                   ((unsigned long)pf << 8) |
                   (unsigned long)ps;
    }
}

static void
store_pgn(unsigned char *p, unsigned long pgn)
{
    p[0] = (unsigned char)(pgn & 0xffUL);
    p[1] = (unsigned char)((pgn >> 8) & 0xffUL);
    p[2] = (unsigned char)((pgn >> 16) & 0xffUL);
}

static unsigned long
load_pgn(const unsigned char *p)
{
    return (unsigned long)p[0] |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16);
}

static int
send_can(struct j1939_transport *transport,
         unsigned long pgn,
         unsigned char priority,
         unsigned char destination,
         const unsigned char *data,
         unsigned int length)
{
    unsigned long id;

    if (length > 8U)
        return J1939_TRANSPORT_ERR_ARGUMENT;

    id = make_id(priority,
                 pgn,
                 transport->source_address,
                 destination);

    if (!android_can_send(id, data, length)) {
        set_error(transport, "Android USB CAN transmit failed.");
        return J1939_TRANSPORT_ERR_SEND;
    }

    return J1939_TRANSPORT_OK;
}

static int
receive_can_until(struct j1939_transport *transport,
                  unsigned long deadline,
                  unsigned long *id,
                  unsigned char data[8],
                  unsigned int *length)
{
    unsigned long now;
    unsigned long remaining;
    int rc;

    now = now_ms();
    if (now >= deadline)
        return J1939_TRANSPORT_ERR_TIMEOUT;

    remaining = deadline - now;
    rc = android_can_receive(id, data, length, remaining);
    if (rc > 0)
        return J1939_TRANSPORT_OK;

    if (rc == 0) {
        set_error(transport, "Timed out waiting for CAN frame.");
        return J1939_TRANSPORT_ERR_TIMEOUT;
    }

    set_error(transport, "Android USB CAN receive failed.");
    return J1939_TRANSPORT_ERR_RECEIVE;
}

static int
send_cts(struct j1939_transport *transport,
         unsigned char count,
         unsigned char next_packet)
{
    unsigned char cm[8];

    cm[0] = TP_CM_CTS;
    cm[1] = count;
    cm[2] = next_packet;
    cm[3] = 0xffU;
    cm[4] = 0xffU;
    store_pgn(cm + 5, transport->pgn);

    return send_can(transport,
                    J1939_PGN_TP_CM,
                    7U,
                    transport->destination_address,
                    cm,
                    8U);
}

static int
send_eom_ack(struct j1939_transport *transport,
             unsigned int total_size,
             unsigned int total_packets)
{
    unsigned char cm[8];

    cm[0] = TP_CM_EOM_ACK;
    cm[1] = (unsigned char)(total_size & 0xffU);
    cm[2] = (unsigned char)((total_size >> 8) & 0xffU);
    cm[3] = (unsigned char)total_packets;
    cm[4] = 0xffU;
    store_pgn(cm + 5, transport->pgn);

    return send_can(transport,
                    J1939_PGN_TP_CM,
                    7U,
                    transport->destination_address,
                    cm,
                    8U);
}

static int
receive_tp(struct j1939_transport *transport,
           const unsigned char cm[8],
           int is_bam,
           unsigned char *payload,
           size_t payload_capacity,
           size_t *payload_len,
           unsigned long deadline)
{
    unsigned int total_size;
    unsigned int total_packets;
    unsigned int sender_window;
    unsigned int window_left;
    unsigned int expected;
    unsigned int copied;
    unsigned long id;
    unsigned char frame[8];
    unsigned int length;
    struct parsed_id parsed;
    int rc;

    total_size = (unsigned int)cm[1] |
                 ((unsigned int)cm[2] << 8);
    total_packets = (unsigned int)cm[3];
    sender_window = (unsigned int)cm[4];

    if (total_size == 0U ||
        total_size > J1939_MAX_TP_PAYLOAD ||
        total_packets == 0U ||
        total_packets != (total_size + 6U) / 7U ||
        (size_t)total_size > payload_capacity) {
        set_error(transport, "Invalid or oversized incoming J1939 TP message.");
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }

    expected = 1U;
    copied = 0U;

    if (is_bam) {
        window_left = total_packets;
    }
    else {
        if (sender_window == 0U)
            sender_window = 1U;
        if (sender_window > TP_WINDOW_PACKETS)
            sender_window = TP_WINDOW_PACKETS;
        if (sender_window > total_packets)
            sender_window = total_packets;

        rc = send_cts(transport,
                      (unsigned char)sender_window,
                      (unsigned char)expected);
        if (rc != J1939_TRANSPORT_OK)
            return rc;
        window_left = sender_window;
    }

    while (expected <= total_packets) {
        rc = receive_can_until(
            transport, deadline, &id, frame, &length);
        if (rc != J1939_TRANSPORT_OK)
            return rc;

        parse_id(id, &parsed);
        if (parsed.pgn != J1939_PGN_TP_DT ||
            parsed.source != transport->destination_address ||
            (parsed.destination != transport->source_address &&
             parsed.destination != 0xffU) ||
            length != 8U) {
            continue;
        }

        if ((unsigned int)frame[0] != expected) {
            set_error(transport, "Unexpected J1939 TP.DT sequence number.");
            return J1939_TRANSPORT_ERR_PROTOCOL;
        }

        {
            unsigned int i;
            for (i = 1U; i < 8U && copied < total_size; ++i)
                payload[copied++] = frame[i];
        }

        ++expected;
        if (window_left != 0U)
            --window_left;

        if (!is_bam &&
            expected <= total_packets &&
            window_left == 0U) {
            unsigned int left;
            unsigned int request;

            left = total_packets - expected + 1U;
            request = sender_window;
            if (request > left)
                request = left;

            rc = send_cts(transport,
                          (unsigned char)request,
                          (unsigned char)expected);
            if (rc != J1939_TRANSPORT_OK)
                return rc;
            window_left = request;
        }
    }

    if (copied != total_size) {
        set_error(transport, "J1939 TP length mismatch.");
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }

    if (!is_bam) {
        rc = send_eom_ack(transport, total_size, total_packets);
        if (rc != J1939_TRANSPORT_OK)
            return rc;
    }

    *payload_len = (size_t)total_size;
    return J1939_TRANSPORT_OK;
}

static int
wait_for_cts_or_ack(struct j1939_transport *transport,
                    unsigned int wanted_control,
                    unsigned char *cm,
                    unsigned long timeout_ms)
{
    unsigned long deadline;
    unsigned long id;
    unsigned char frame[8];
    unsigned int length;
    struct parsed_id parsed;
    int rc;

    deadline = now_ms() + timeout_ms;

    for (;;) {
        rc = receive_can_until(
            transport, deadline, &id, frame, &length);
        if (rc != J1939_TRANSPORT_OK)
            return rc;

        parse_id(id, &parsed);
        if (parsed.pgn != J1939_PGN_TP_CM ||
            parsed.source != transport->destination_address ||
            parsed.destination != transport->source_address ||
            length != 8U) {
            continue;
        }

        if (load_pgn(frame + 5) != transport->pgn)
            continue;

        if (frame[0] == TP_CM_ABORT) {
            set_error(transport, "ECM aborted J1939 transport protocol.");
            return J1939_TRANSPORT_ERR_PROTOCOL;
        }

        if ((unsigned int)frame[0] != wanted_control)
            continue;

        memcpy(cm, frame, 8U);
        return J1939_TRANSPORT_OK;
    }
}

static int
send_tp(struct j1939_transport *transport,
        const unsigned char *payload,
        size_t payload_len)
{
    unsigned int total_size;
    unsigned int total_packets;
    unsigned int next_packet;
    unsigned char cm[8];
    int rc;

    if (payload_len > J1939_MAX_TP_PAYLOAD) {
        set_error(transport,
                  "J1939 TP payload exceeds the classic 1785-byte limit.");
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }

    total_size = (unsigned int)payload_len;
    total_packets = (total_size + 6U) / 7U;

    cm[0] = TP_CM_RTS;
    cm[1] = (unsigned char)(total_size & 0xffU);
    cm[2] = (unsigned char)((total_size >> 8) & 0xffU);
    cm[3] = (unsigned char)total_packets;
    cm[4] = (unsigned char)(
        total_packets < TP_WINDOW_PACKETS ?
        total_packets : TP_WINDOW_PACKETS);
    store_pgn(cm + 5, transport->pgn);

    rc = send_can(transport,
                  J1939_PGN_TP_CM,
                  7U,
                  transport->destination_address,
                  cm,
                  8U);
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    next_packet = 1U;

    while (next_packet <= total_packets) {
        unsigned int count;
        unsigned int requested_start;
        unsigned int i;

        rc = wait_for_cts_or_ack(
            transport, TP_CM_CTS, cm, TP_CONTROL_TIMEOUT);
        if (rc != J1939_TRANSPORT_OK)
            return rc;

        count = (unsigned int)cm[1];
        requested_start = (unsigned int)cm[2];

        if (count == 0U)
            continue;
        if (requested_start == 0U ||
            requested_start > total_packets ||
            count > total_packets - requested_start + 1U) {
            set_error(transport, "Invalid J1939 CTS window from ECM.");
            return J1939_TRANSPORT_ERR_PROTOCOL;
        }

        next_packet = requested_start;

        for (i = 0U; i < count; ++i) {
            unsigned char dt[8];
            unsigned int seq;
            unsigned int base;
            unsigned int j;

            seq = next_packet;
            base = (seq - 1U) * 7U;
            dt[0] = (unsigned char)seq;
            for (j = 0U; j < 7U; ++j) {
                unsigned int index = base + j;
                dt[j + 1U] = index < total_size ?
                             payload[index] : 0xffU;
            }

            rc = send_can(transport,
                          J1939_PGN_TP_DT,
                          7U,
                          transport->destination_address,
                          dt,
                          8U);
            if (rc != J1939_TRANSPORT_OK)
                return rc;

            ++next_packet;
            sleep_ms(1UL);
        }
    }

    rc = wait_for_cts_or_ack(
        transport, TP_CM_EOM_ACK, cm, TP_CONTROL_TIMEOUT);
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    if (((unsigned int)cm[1] |
         ((unsigned int)cm[2] << 8)) != total_size ||
        (unsigned int)cm[3] != total_packets) {
        set_error(transport, "Invalid J1939 TP end-of-message acknowledgement.");
        return J1939_TRANSPORT_ERR_PROTOCOL;
    }

    return J1939_TRANSPORT_OK;
}

static int
claim_address(struct j1939_transport *transport)
{
    static const unsigned char name[8] = {
        0x00U, 0x00U, 0x00U, 0x00U,
        0x81U, 0x00U, 0x80U, 0x00U
    };
    int rc;

    rc = send_can(transport,
                  J1939_PGN_ADDRESS,
                  6U,
                  0xffU,
                  name,
                  8U);
    if (rc != J1939_TRANSPORT_OK)
        return rc;

    sleep_ms(250UL);
    return J1939_TRANSPORT_OK;
}

struct j1939_transport *
j1939_transport_create(void)
{
    return (struct j1939_transport *)calloc(
        1U, sizeof(struct j1939_transport));
}

void
j1939_transport_destroy(struct j1939_transport *transport)
{
    if (transport == NULL)
        return;

    j1939_transport_close(transport);
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
        pgn > 0x00ffffffUL ||
        (baud != 125000 &&
         baud != 250000 &&
         baud != 500000 &&
         baud != 1000000)) {
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    j1939_transport_close(transport);
    transport->pgn = pgn;
    transport->priority = priority;
    transport->source_address = source_address;
    transport->destination_address = destination_address;
    transport->last_error[0] = '\0';

    if (!android_can_open(baud)) {
        set_error(transport,
                  "Android CAN adapter could not open at the requested bitrate.");
        return J1939_TRANSPORT_ERR_CONNECT;
    }

    transport->is_open = 1;
    rc = claim_address(transport);
    if (rc != J1939_TRANSPORT_OK) {
        j1939_transport_close(transport);
        return rc;
    }

    return J1939_TRANSPORT_OK;
}

void
j1939_transport_close(struct j1939_transport *transport)
{
    if (transport == NULL || !transport->is_open)
        return;

    android_can_close();
    transport->is_open = 0;
}

int
j1939_transport_send(struct j1939_transport *transport,
                     const unsigned char *payload,
                     size_t payload_len)
{
    if (transport == NULL || payload == NULL || !transport->is_open)
        return J1939_TRANSPORT_ERR_ARGUMENT;

    if (payload_len <= 8U) {
        return send_can(transport,
                        transport->pgn,
                        transport->priority,
                        transport->destination_address,
                        payload,
                        (unsigned int)payload_len);
    }

    return send_tp(transport, payload, payload_len);
}

int
j1939_transport_receive(struct j1939_transport *transport,
                        unsigned char *payload,
                        size_t payload_capacity,
                        size_t *payload_len,
                        unsigned long timeout_ms)
{
    unsigned long deadline;
    unsigned long id;
    unsigned char frame[8];
    unsigned int length;
    struct parsed_id parsed;
    int rc;

    if (transport == NULL ||
        payload == NULL ||
        payload_len == NULL ||
        !transport->is_open) {
        return J1939_TRANSPORT_ERR_ARGUMENT;
    }

    *payload_len = 0U;
    deadline = now_ms() + timeout_ms;

    for (;;) {
        rc = receive_can_until(
            transport, deadline, &id, frame, &length);
        if (rc != J1939_TRANSPORT_OK) {
            if (rc == J1939_TRANSPORT_ERR_TIMEOUT)
                set_error(transport,
                          "Timed out waiting for matching J1939 response.");
            return rc;
        }

        parse_id(id, &parsed);

        if (parsed.pgn == transport->pgn &&
            parsed.source == transport->destination_address &&
            (parsed.destination == transport->source_address ||
             parsed.destination == 0xffU)) {
            if ((size_t)length > payload_capacity) {
                set_error(transport, "Incoming J1939 payload exceeds buffer.");
                return J1939_TRANSPORT_ERR_PROTOCOL;
            }

            memcpy(payload, frame, length);
            *payload_len = (size_t)length;
            return J1939_TRANSPORT_OK;
        }

        if (parsed.pgn == J1939_PGN_TP_CM &&
            parsed.source == transport->destination_address &&
            (parsed.destination == transport->source_address ||
             parsed.destination == 0xffU) &&
            length == 8U &&
            load_pgn(frame + 5) == transport->pgn) {
            if (frame[0] == TP_CM_RTS) {
                return receive_tp(
                    transport,
                    frame,
                    0,
                    payload,
                    payload_capacity,
                    payload_len,
                    deadline);
            }

            if (frame[0] == TP_CM_BAM) {
                return receive_tp(
                    transport,
                    frame,
                    1,
                    payload,
                    payload_capacity,
                    payload_len,
                    deadline);
            }

            if (frame[0] == TP_CM_ABORT) {
                set_error(transport, "ECM aborted incoming J1939 transport.");
                return J1939_TRANSPORT_ERR_PROTOCOL;
            }
        }
    }
}

const char *
j1939_transport_error(const struct j1939_transport *transport)
{
    if (transport == NULL)
        return "Android J1939 transport is not initialized.";

    return transport->last_error;
}
