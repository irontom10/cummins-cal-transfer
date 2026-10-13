/*
 * Experimental GTIS4.6 DA00 UDS firmware transfer.
 * An actual CAN sender (not merely a frame-count planner).
 *
 * The request order is taken from the authenticated 2026-10-11 passive
 * INSITE UDS/ISO-TP capture. It is *not* the legacy 4B/4D CLIP loader.
 * The OEM CLIP service-11 commit is outside this module and still missing.
 * Unsupported/negative ECM responses produce errors, never fake success.
 *
 * No static secrets. All credentials come from local operator TOML.
 * ISO C89, no framework, platform-specific CAN I/O or dynamic allocations.
 */
#include "clip46_uds.h"
#include "clip46_plan.h"
#include "clip_cal.h"
#include "da00_sendkey.h"
#include "ct_platform.h"
#include "clip46_poc_secure.h"
#include <stdio.h>
#include <string.h>

#define UDS_FRAME 8U
#define UDS_MAX_MESSAGE 4095U
#define UDS_CHUNK 1496U
#define UDS_DEFAULT_TIMEOUT 5000UL
#define UDS_ROUTINE_TIMEOUT 90000UL
#define UDS_TRANSFER_TIMEOUT 15000UL

static void set_error(char *error, size_t capacity, const char *message)
{
    size_t n;
    if (!error || capacity == 0U) return;
    if (!message) message = "Unknown DA00 error.";
    n = strlen(message);
    if (n >= capacity) n = capacity - 1U;
    memcpy(error, message, n);
    error[n] = '\0';
}

static int iso_send_frame(const struct clip46_uds_io *io,
                          const unsigned char frame[8])
{
    if (!io || !io->send || !io->recv || !frame) return -1;
    return io->send(io->user, frame);
}

static int iso_recv_frame(const struct clip46_uds_io *io,
                          unsigned char frame[8],
                          unsigned long timeout)
{
    if (!io || !io->recv || !frame) return -1;
    return io->recv(io->user, frame, timeout);
}

static int await_fc(const struct clip46_uds_io *io,
                    unsigned char *bs, unsigned char *st)
{
    unsigned char frame[8];
    unsigned int retries;
    for (retries = 0U; retries < 12U; ++retries) {
        if (iso_recv_frame(io, frame, UDS_DEFAULT_TIMEOUT) != 0)
            return -1;
        if ((frame[0] & 0xF0U) != 0x30U)
            return -1;
        if ((frame[0] & 0x0FU) == 0U) {
            *bs = frame[1];
            *st = frame[2];
            return 0;
        }
        if ((frame[0] & 0x0FU) != 1U)
            return -1;
        /* ECU sent Wait FC; never force a transfer on overflow. */
    }
    return -1;
}

/* Traditional 11-bit length ISO 15765-2 framing over 29-bit DA00 CAN IDs. */
static int iso_send(const struct clip46_uds_io *io,
                    const unsigned char *msg, size_t len)
{
    unsigned char frame[8];
    unsigned char block_size, stmin, sequence;
    size_t done, n;
    unsigned int within_block;
    if (!io || !msg || len == 0U || len > UDS_MAX_MESSAGE) return -1;
    memset(frame, 0xCC, sizeof(frame));
    if (len <= 7U) {
        frame[0] = (unsigned char)len;
        memcpy(frame + 1U, msg, len);
        return iso_send_frame(io, frame);
    }
    frame[0] = (unsigned char)(0x10U | ((len >> 8) & 0x0FU));
    frame[1] = (unsigned char)(len & 0xFFU);
    memcpy(frame + 2U, msg, 6U);
    if (iso_send_frame(io, frame) != 0) return -1;
    if (await_fc(io, &block_size, &stmin) != 0) return -1;
    done = 6U;
    sequence = 1U;
    within_block = 0U;
    while (done < len) {
        memset(frame, 0xCC, sizeof(frame));
        frame[0] = (unsigned char)(0x20U | (sequence & 0x0FU));
        n = len - done;
        if (n > 7U) n = 7U;
        memcpy(frame + 1U, msg + done, n);
        if (stmin != 0U) {
            if (stmin <= 0x7FU)
                ct_sleep_ms((unsigned long)stmin);
            else if (stmin >= 0xF1U && stmin <= 0xF9U)
                ct_sleep_ms(1UL); /* conservative microsecond STmin */
            else
                return -1;
        }
        if (iso_send_frame(io, frame) != 0) return -1;
        done += n;
        sequence = (unsigned char)((sequence + 1U) & 0x0FU);
        ++within_block;
        if (block_size != 0U &&
            within_block >= (unsigned int)block_size &&
            done < len) {
            if (await_fc(io, &block_size, &stmin) != 0) return -1;
            within_block = 0U;
        }
    }
    return 0;
}

static int iso_receive(const struct clip46_uds_io *io,
                       unsigned char *result, size_t cap,
                       size_t *outlen, unsigned long timeout)
{
    unsigned char frame[8];
    unsigned char fc[8];
    unsigned char seq;
    size_t expected, done, take;
    if (!io || !result || !outlen) return -1;
    *outlen = 0U;
    if (iso_recv_frame(io, frame, timeout) != 0) return -1;
    if ((frame[0] & 0xF0U) == 0x00U) {
        expected = (size_t)(frame[0] & 0x0FU);
        if (expected > 7U || expected > cap) return -1;
        memcpy(result, frame + 1U, expected);
        *outlen = expected;
        return 0;
    }
    if ((frame[0] & 0xF0U) != 0x10U) return -1;
    expected = ((size_t)(frame[0] & 0x0FU) << 8) |
               (size_t)frame[1];
    if (expected < 8U || expected > cap) return -1;
    memcpy(result, frame + 2U, 6U);
    done = 6U;
    seq = 1U;
    memset(fc, 0xCC, sizeof(fc));
    fc[0] = 0x30U; /* CTS, unlimited block size, STmin 0 */
    fc[1] = 0U;
    fc[2] = 0U;
    if (iso_send_frame(io, fc) != 0) return -1;
    while (done < expected) {
        if (iso_recv_frame(io, frame, timeout) != 0 ||
            frame[0] != (unsigned char)(0x20U | (seq & 0x0FU)))
            return -1;
        take = expected - done;
        if (take > 7U) take = 7U;
        memcpy(result + done, frame + 1U, take);
        done += take;
        seq = (unsigned char)((seq + 1U) & 0x0FU);
    }
    *outlen = expected;
    return 0;
}

int clip46_uds_exchange(const struct clip46_uds_io *io,
                        const unsigned char *request, size_t request_len,
                        unsigned char *reply, size_t reply_capacity,
                        size_t *reply_len, unsigned long timeout_ms)
{
    unsigned long start, now, left, last_keepalive;
    size_t len;
    if (!request || request_len == 0U ||
        !reply || !reply_len || timeout_ms == 0U)
        return -1;
    *reply_len = 0U;
    if (iso_send(io, request, request_len) != 0)
        return -1;
    start = ct_monotonic_ms();
    last_keepalive = start;
    for (;;) {
        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= timeout_ms)
            return -1;
        left = timeout_ms - (unsigned long)(now - start);
        if (left > UDS_DEFAULT_TIMEOUT) left = UDS_DEFAULT_TIMEOUT;
        if (iso_receive(io, reply, reply_capacity, &len, left) != 0)
            return -1;
        if (len >= 3U && reply[0] == 0x7FU &&
            reply[1] == request[0] && reply[2] == 0x78U) {
            /* Suppressed-response TesterPresent, as in the OEM capture.
             * Keep the diagnostic session alive through long FF00 routines. */
            if ((unsigned long)(ct_monotonic_ms() - last_keepalive) > 2000UL) {
                unsigned char alive[2];
                alive[0] = 0x3EU;
                alive[1] = 0x80U;
                if (iso_send(io, alive, sizeof(alive)) != 0)
                    return -1;
                last_keepalive = ct_monotonic_ms();
                /* Keepalive does NOT reset the absolute request deadline. */
            }
            continue;
        }
        *reply_len = len;
        if (len == 0U || reply[0] !=
            (unsigned char)((request[0] + 0x40U) & 0xFFU))
            return -1;
        return 0;
    }
}

static void report(const struct clip46_uds_io *io,
                   int percent, const char *message)
{
    if (io && io->progress)
        io->progress(io->user, percent, message);
}

static int uds_do(const struct clip46_uds_io *io,
                  const unsigned char *req, size_t req_len,
                  unsigned char *rx, size_t rx_cap, size_t *rx_len,
                  unsigned long timeout, char *error, size_t err_cap,
                  const char *what)
{
    char msg[256];
    if (clip46_uds_exchange(io, req, req_len, rx,
                             rx_cap, rx_len, timeout) == 0)
        return 0;
    if (*rx_len >= 3U && rx[0] == 0x7FU)
        sprintf(msg, "%s rejected by ECM: UDS NRC %02X.",
                what, (unsigned)rx[2]);
    else
        sprintf(msg, "%s: CAN transport timeout or malformed response.", what);
    set_error(error, err_cap, msg);
    return -1;
}

struct region_context {
    const struct clip46_uds_io *io;
    char *error;
    size_t error_cap;
    unsigned char identity[16];
    int got_identity;
    unsigned int region_index;
    int failed;
};

static int flash_region(struct region_context *state,
                        unsigned long addr,
                        const unsigned char *bytes, unsigned long len)
{
    unsigned char req[UDS_CHUNK + 2U];
    unsigned char rx[64];
    unsigned char seq;
    unsigned long off;
    size_t n, rx_len;
    char note[160];
    if (!state || !bytes || len == 0UL ||
        len > 0xffffffffUL - addr)
        return -1;
    req[0] = 0x34U;
    req[1] = 0x00U;
    req[2] = 0x44U;
    req[3] = (unsigned char)(addr >> 24);
    req[4] = (unsigned char)(addr >> 16);
    req[5] = (unsigned char)(addr >> 8);
    req[6] = (unsigned char)addr;
    req[7] = (unsigned char)(len >> 24);
    req[8] = (unsigned char)(len >> 16);
    req[9] = (unsigned char)(len >> 8);
    req[10] = (unsigned char)len;
    sprintf(note, "DA00 RequestDownload for %08lX + %lu",
            addr, len);
    report(state->io, 20, note);
    rx_len = 0U;
    if (uds_do(state->io, req, 11U, rx, sizeof(rx), &rx_len,
               UDS_DEFAULT_TIMEOUT, state->error, state->error_cap,
               "RequestDownload (34)") != 0)
        return -1;
    if (rx_len < 4U || rx[1] != 0x20U ||
        (((unsigned int)rx[2] << 8) | (unsigned int)rx[3]) <
            UDS_CHUNK + 2U) {
        set_error(state->error, state->error_cap,
                  "RequestDownload returned unexpected max-block length.");
        return -1;
    }
    off = 0UL;
    seq = 1U;
    while (off < len) {
        n = (size_t)(len - off > UDS_CHUNK ? UDS_CHUNK : len - off);
        req[0] = 0x36U;
        req[1] = seq;
        memcpy(req + 2U, bytes + off, n);
        rx_len = 0U;
        if (uds_do(state->io, req, n + 2U, rx, sizeof(rx), &rx_len,
                   UDS_TRANSFER_TIMEOUT, state->error, state->error_cap,
                   "TransferData (36)") != 0)
            return -1;
        if (rx_len < 2U || rx[1] != seq) {
            set_error(state->error, state->error_cap,
                      "TransferData acknowledgment sequence mismatch.");
            return -1;
        }
        off += (unsigned long)n;
        seq = (unsigned char)(seq + 1U);
        if (off % (128UL * UDS_CHUNK) == 0UL || off == len) {
            sprintf(note, "DA00 region %u: %lu / %lu bytes acknowledged.",
                    state->region_index, off, len);
            report(state->io, 25 + (int)((off * 60UL) / len), note);
        }
    }
    req[0] = 0x37U;
    rx_len = 0U;
    if (uds_do(state->io, req, 1U, rx, sizeof(rx), &rx_len,
               UDS_DEFAULT_TIMEOUT, state->error, state->error_cap,
               "TransferExit (37)") != 0)
        return -1;
    state->region_index++;
    return 0;
}

static int collect_identity(void *user, unsigned long address,
                            const unsigned char *data, unsigned long len)
{
    struct region_context *state;
    state = (struct region_context *)user;
    if (address == 0x02000000UL) {
        if (len != 16UL || state->got_identity)
            return -1;
        memcpy(state->identity, data, 16U);
        state->got_identity = 1;
    }
    return 0;
}

static int flash_callback(void *user, unsigned long address,
                          const unsigned char *data, unsigned long len)
{
    struct region_context *state;
    state = (struct region_context *)user;
    if (address == 0x02000000UL)
        return 0; /* Already transferred before 0x00001080. */
    if (flash_region(state, address, data, len) != 0) {
        state->failed = 1;
        return -1;
    }
    return 0;
}

int clip46_uds_flash(const struct clip46_uds_io *io,
                     const struct clip46_uds_config *credentials,
                     const char *ccal_path, char *error,
                     size_t error_capacity)
{
    struct clip46_plan_stats plan;
    struct region_context state;
    unsigned char req[34];
    unsigned char rx[128];
    unsigned char key[32];
    size_t rx_len;
    int rc;
    memset(&state, 0, sizeof(state));
    memset(req, 0, sizeof(req));
    memset(key, 0, sizeof(key));
    if (!io || !io->send || !io->recv || !credentials || !ccal_path) {
        set_error(error, error_capacity, "Invalid DA00 programming arguments.");
        return -1;
    }
    state.io = io;
    state.error = error;
    state.error_cap = error_capacity;
    set_error(error, error_capacity, "");
    if (clip46_plan_ccal(ccal_path, &plan) != 0 ||
        plan.regions != 3U || plan.identity_regions != 1U ||
        clip_cal_visit_ccal_regions(ccal_path, collect_identity, &state)
            != CLIP_CAL_OK || !state.got_identity) {
        set_error(error, error_capacity,
                  "Expected a CRC-verified GTIS4.6 four-region CCAL.");
        return -1;
    }

    /* Reference flash sequence recorded at 16:08:14 through 16:15:55. */
    report(io, 2, "GTIS4.6 UDS 10 02: programming diagnostic session...");
    req[0] = 0x10U; req[1] = 0x02U; rx_len = 0U;
    if (uds_do(io, req, 2U, rx, sizeof(rx), &rx_len,
               UDS_DEFAULT_TIMEOUT, error, error_capacity,
               "DiagnosticSessionControl (10 02)") != 0)
        goto fail;

    report(io, 5, "GTIS4.6 UDS 27 07: requesting fresh seed...");
    req[0] = 0x27U; req[1] = 0x07U; rx_len = 0U;
    if (uds_do(io, req, 2U, rx, sizeof(rx), &rx_len,
               UDS_DEFAULT_TIMEOUT, error, error_capacity,
               "SecurityAccess RequestSeed (27 07)") != 0)
        goto fail;
    if (rx_len != 34U || rx[1] != 0x07U) {
        set_error(error, error_capacity, "Expected UDS 67 07 plus 32-byte seed.");
        goto fail;
    }

    if (!da00_sendkey(rx + 2U, credentials->aes_key,
                      credentials->tool_version, credentials->tool_instance,
                      credentials->tool_family, credentials->include_optional,
                      credentials->optional_byte, key)) {
        set_error(error, error_capacity, "UDS 27 08 cryptogram generation failed.");
        goto fail;
    }
    req[0] = 0x27U; req[1] = 0x08U;
    memcpy(req + 2U, key, 32U);
    clip46_poc_wipe(key, sizeof(key));
    report(io, 8, "GTIS4.6 UDS 27 08: authenticating fresh session...");
    rx_len = 0U;
    rc = uds_do(io, req, 34U, rx, sizeof(rx), &rx_len,
                UDS_DEFAULT_TIMEOUT, error, error_capacity,
                "SecurityAccess SendKey (27 08)");
    clip46_poc_wipe(req, sizeof(req));
    if (rc != 0)
        goto fail;
    if (rx_len < 2U || rx[1] != 0x08U) {
        set_error(error, error_capacity, "Expected UDS 67 08 acceptance.");
        goto fail;
    }

    report(io, 10, "GTIS4.6 UDS 31 01 FF00: preparing flash...");
    req[0] = 0x31U; req[1] = 0x01U; req[2] = 0xFFU; req[3] = 0x00U;
    rx_len = 0U;
    if (uds_do(io, req, 4U, rx, sizeof(rx), &rx_len,
               UDS_ROUTINE_TIMEOUT, error, error_capacity,
               "RoutineControl flash prepare (31 FF00)") != 0)
        goto fail;

    /* Identity metadata (0x02000000) comes FIRST in the OEM trace. */
    state.region_index = 0U;
    if (flash_region(&state, 0x02000000UL,
                     state.identity, sizeof(state.identity)) != 0)
        goto fail;

    if (clip_cal_visit_ccal_regions(ccal_path, flash_callback, &state) !=
        CLIP_CAL_OK || state.failed) {
        if (error && error[0] == '\0')
            set_error(error, error_capacity,
                      "DA00 failed before the calibration region completed.");
        goto fail;
    }

    report(io, 90, "GTIS4.6 UDS 31 01 FF01: flash finalize routine...");
    req[0] = 0x31U; req[1] = 0x01U; req[2] = 0xFFU; req[3] = 0x01U;
    rx_len = 0U;
    if (uds_do(io, req, 4U, rx, sizeof(rx), &rx_len,
               UDS_ROUTINE_TIMEOUT, error, error_capacity,
               "RoutineControl finalize (31 FF01)") != 0)
        goto fail;
    report(io, 95, "GTIS4.6 UDS 11 01: requesting ECM reset...");
    req[0] = 0x11U; req[1] = 0x01U; rx_len = 0U;
    if (uds_do(io, req, 2U, rx, sizeof(rx), &rx_len,
               UDS_DEFAULT_TIMEOUT, error, error_capacity,
               "ECUReset (11 01)") != 0)
        goto fail;

    report(io, 100, "DA00 UDS transfer completed; OEM CLIP commit not performed.");
    set_error(error, error_capacity,
              "DA00 UDS sequence acknowledged; OEM encrypted CLIP commit "
              "and post-reset verification remain unimplemented.");
    clip46_poc_wipe(state.identity, sizeof(state.identity));
    return 0;

fail:
    clip46_poc_wipe(key, sizeof(key));
    clip46_poc_wipe(req, sizeof(req));
    clip46_poc_wipe(rx, sizeof(rx));
    clip46_poc_wipe(state.identity, sizeof(state.identity));
    return -1;
}
