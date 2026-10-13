/*
 * Real ISO-TP encoder/decoder regression, exercised without an ECM.
 * C89. Validates the exact 1498-byte DA00 UDS TransferData framing and
 * the five-frame 0x67 07 seed response, including RequestPending.
 */
#include "clip46_uds.h"
#include <stdio.h>
#include <string.h>

struct wire {
    unsigned char inbound[8][8];
    unsigned int rx_count;
    unsigned int rx_next;
    unsigned int sends;
    unsigned int seq_fail;
    unsigned int first_fail;
    unsigned int fc_sent;
};

static int tx(void *user, const unsigned char frame[8])
{
    struct wire *w;
    unsigned int index;
    w = (struct wire *)user;
    index = w->sends++;
    if (frame[0] == 0x30U) {
        w->fc_sent++;
        return 0;
    }
    if ((frame[0] & 0xF0U) == 0x10U) {
        if (frame[0] != 0x15U || frame[1] != 0xDAU ||
            frame[2] != 0x36U || frame[3] != 0x01U)
            w->first_fail++;
    }
    if (index > 0U && (frame[0] & 0xF0U) == 0x20U &&
        (frame[0] & 0x0FU) != (unsigned char)(index & 0x0FU))
        w->seq_fail++;
    return 0;
}

static int rx(void *user, unsigned char frame[8], unsigned long timeout)
{
    struct wire *w;
    (void)timeout;
    w = (struct wire *)user;
    if (w->rx_next >= w->rx_count)
        return -1;
    memcpy(frame, w->inbound[w->rx_next++], 8U);
    return 0;
}

static void init(struct wire *w)
{
    memset(w, 0, sizeof(*w));
}

static void set_rx(struct wire *w, unsigned int index,
                   const unsigned char *bytes, size_t n)
{
    memset(w->inbound[index], 0xCC, 8U);
    memcpy(w->inbound[index], bytes, n);
    if (w->rx_count <= index)
        w->rx_count = index + 1U;
}

static int test_bulk(void)
{
    struct wire w;
    struct clip46_uds_io io;
    unsigned char req[1498], reply[32], fc[3], ack[3];
    size_t n;
    unsigned int i;
    init(&w);
    io.user = &w; io.send = tx; io.recv = rx; io.progress = NULL;
    req[0] = 0x36U; req[1] = 1U;
    for (i = 2U; i < sizeof(req); ++i)
        req[i] = (unsigned char)i;
    fc[0] = 0x30U; fc[1] = 0U; fc[2] = 0U;
    ack[0] = 0x02U; ack[1] = 0x76U; ack[2] = 1U;
    set_rx(&w, 0U, fc, sizeof(fc));
    set_rx(&w, 1U, ack, sizeof(ack));
    n = 0U;
    if (clip46_uds_exchange(&io, req, sizeof(req), reply, sizeof(reply),
                            &n, 5000UL) != 0 ||
        n != 2U || reply[0] != 0x76U || reply[1] != 1U ||
        w.sends != 215U || w.first_fail != 0U || w.seq_fail != 0U ||
        w.rx_next != 2U)
        return 1;
    return 0;
}

static int test_seed(void)
{
    struct wire w;
    struct clip46_uds_io io;
    unsigned char msg[34], req[2], reply[34];
    unsigned char raw[8];
    size_t n;
    unsigned int i, k;
    init(&w);
    io.user = &w; io.send = tx; io.recv = rx; io.progress = NULL;
    msg[0] = 0x67U; msg[1] = 0x07U;
    for (i = 2U; i < 34U; ++i)
        msg[i] = (unsigned char)i;
    memset(raw, 0xCC, sizeof(raw));
    raw[0] = 0x10U; raw[1] = 0x22U;
    memcpy(raw + 2U, msg, 6U);
    set_rx(&w, 0U, raw, 8U);
    k = 6U;
    for (i = 0U; i < 4U; ++i) {
        memset(raw, 0xCC, sizeof(raw));
        raw[0] = (unsigned char)(0x21U + i);
        memcpy(raw + 1U, msg + k, 7U);
        set_rx(&w, i + 1U, raw, 8U);
        k += 7U;
    }
    req[0] = 0x27U; req[1] = 0x07U;
    n = 0U;
    if (clip46_uds_exchange(&io, req, 2U, reply, sizeof(reply),
                            &n, 5000UL) != 0 ||
        n != 34U || memcmp(msg, reply, n) != 0 ||
        w.fc_sent != 1U)
        return 1;
    return 0;
}

static int test_pending(void)
{
    struct wire w;
    struct clip46_uds_io io;
    unsigned char req[4], reply[16], pending[4], positive[5];
    size_t n;
    init(&w);
    io.user = &w; io.send = tx; io.recv = rx; io.progress = NULL;
    req[0] = 0x31U; req[1] = 0x01U; req[2] = 0xFFU; req[3] = 0x00U;
    pending[0] = 3U; pending[1] = 0x7FU;
    pending[2] = 0x31U; pending[3] = 0x78U;
    positive[0] = 4U; positive[1] = 0x71U;
    positive[2] = 0x01U; positive[3] = 0xFFU; positive[4] = 0U;
    set_rx(&w, 0U, pending, sizeof(pending));
    set_rx(&w, 1U, positive, sizeof(positive));
    n = 0U;
    if (clip46_uds_exchange(&io, req, 4U, reply, sizeof(reply),
                            &n, 5000UL) != 0 ||
        n != 4U || reply[0] != 0x71U || reply[3] != 0U ||
        w.rx_next != 2U)
        return 1;
    return 0;
}

int main(void)
{
    if (test_bulk()) { puts("ISO-TP bulk framing FAILED"); return 1; }
    if (test_seed()) { puts("ISO-TP seed response FAILED"); return 2; }
    if (test_pending()) { puts("UDS ResponsePending FAILED"); return 3; }
    puts("GTIS4.6 DA00 UDS ISO-TP synthetic tests passed");
    return 0;
}
