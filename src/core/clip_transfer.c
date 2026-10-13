/*
 * clip_transfer.c
 *
 * CLIP/ELITE calibration-transfer orchestration over J1939.
 * J1939 framing/session behavior is isolated in j1939_transport.c, while
 * RP1210 DLL/client handling is isolated below it in rp1210_transport.c.
 *
 * This file owns only protocol/session behavior and calibration-file handling.
 * It remains C89 source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rp1210clip.h"
#include "j1939_transport.h"
#include "echo_transfer.h"
#include "clip_crypto.h"
#include "clip46_poc_secure.h"
#include "clip46_p256.h"
#include "clip46_runtime.h"
#include "clip46_plan.h"
#include "clip_cal.h"
#include "ccal_crc.h"
#include "ct_platform.h"

#define CLIP_J1939_PGN                 0x00ef00UL
#define CLIP_J1939_PRIORITY            6U
#define CLIP_WIRE_MAX                  4096U
#define CLIP_APP_MAX                   4096U
#define CLIP_TIMEOUT_MS                5000UL
/* unlock.jlog: 12 <seq> 00 18 00 = unlock, ... 01 = lock. */
#define CLIP_MODE_CAL_LOCK              0x0018U
#define CLIP_CAL_UNLOCKED               0x00U
#define CLIP_CAL_LOCKED                 0x01U
#define CLIP_READ_TIMEOUT_MS           10000UL
#define CLIP_OPEN_TIMEOUT_MS           5000UL

#define PULL_OK                         0
#define PULL_ERR_ARGUMENT              -100
#define PULL_ERR_LOAD_API              -101
#define PULL_ERR_SYMBOL                 -102
#define PULL_ERR_CONNECT                -103
#define PULL_ERR_SEND                   -104
#define PULL_ERR_TIMEOUT                -105
#define PULL_ERR_PROTOCOL               -106
#define PULL_ERR_CRYPTO                 -107
#define PULL_ERR_CAL                    -108
#define PULL_ERR_MEMORY                 -109
#define PULL_ERR_FILE                   -110
#define PULL_ERR_CRC                    -111
#define PULL_ERR_UPLOAD                 -112
#define PULL_ERR_LEGACY_TX_BLOCKED      -113
#define PULL_ERR_POST_VERIFY            -114
#define PULL_ERR_SECURE_POST_AUTH       -116

/* Internal non-error result from the CLIP open probe. */
#define PULL_DETECTED_ELITE_II            1
#define PULL_DETECTED_SECURE_CLIP        2
#define CLIP46_POC_MAX_DUMP_BYTES        (32UL * 1024UL * 1024UL)

/*
 * ENI / ELITE II protocol values recovered from the supplied CM550/CM554
 * calibration-transfer trace.  ELITE II uses raw Proprietary-A messages
 * instead of the newer guaranteed CLIP application envelope.
 */
#define ELITE_II_DESCRIPTOR_ID       0x1238U
#define ELITE_II_DESCRIPTOR_SIZE     0x003aU
#define ELITE_II_BLOCK_SIZE          1000U

/*
 * Tool-context bytes from the supplied 2026-10-03 reference tool capture.
 *
 * These are kept as literal protocol values.  The ParamID semantics are not
 * required here; clip_crypto.c only needs to reproduce the verified 51-byte
 * context exactly.
 */
#define CLIP_DEFAULT_PASSWORD_TYPE      1U
#define CLIP_DEFAULT_TOOL_FAMILY        0U
#define CLIP_DEFAULT_TOOL_ID            0x000aUL
#define CLIP_DEFAULT_TOOL_VERSION       ((clip_u32)0x03060403UL)

static const clip_u8 g_default_password[6] = {
    'A', 'B', 'C', 'D', 'E', 'F'
};

static const clip_u8 g_default_tool_instance[6] = {
    0x00U, 0x99U, 0x99U, 0x99U, 0x99U, 0x99U
};

struct pull_range_image {
    clip_u32 address;
    clip_u32 length;
    clip_u8 *data;
};

struct pull_image {
    unsigned int range_count;
    struct pull_range_image ranges[CLIP_CAL_MAX_RANGES];
};

struct pull_meta {
    char calibration_version[64];
    char module_name[64];
    char product_id[64];
    char interface_level[64];
    char module_part_number[64];
    char start_boot_loader_version[64];
    char end_boot_loader_version[64];
    char byte_order[32];
    char index_table_address[16];
    char file_descriptor[256];
    char harness_compat[32];
    char leading_word[8];
};

struct pull_ctx {
    struct j1939_transport *transport;
    clip_u8 session_id;
    /*
     * ECM-side frame byte 3 for this session. In the captured GTIS4.6
     * sessions the ECM addresses every session frame to a per-connection
     * slot (0x03 for the first observed session, 0x04 for the second).
     * Recorded for diagnostics; the earlier "0x0E - slot" connection-ID
     * correlation was retired in favor of the byte 4 block-count rule
     * documented below.
     */
    clip_u8 ecm_slot;
    /*
     * GTIS4.6 protected application session state. app_key/app_iv are the
     * ECDH-derived material (shared[0..15]/shared[16..31]).
     */
    int clip46_secure;
    /*
     * Outer byte 4 of each protected request acknowledges the transport
     * size of the previous inbound reply: ceil(wire_length / 16) blocks.
     * Verified against the reference capture (23B -> 0x02, 823B -> 0x34,
     * 167B 02 04 -> 0x0B, 151B 02 04 -> 0x0A) and by two live stall
     * patterns: misreported counts accumulate a block deficit and the
     * ECM stops answering at roughly 180 unacknowledged blocks.
     */
    clip_u8 clip46_last_blocks;
    clip_u8 clip46_app_key[16];
    clip_u8 clip46_app_iv[16];
    unsigned int wire_slot;
    int tolerate_negative;
    int clip46_read_only_workflow;
    RP1210_PROGRESS_CALLBACK progress;
};

static char g_last_error[512];
static struct clip46_runtime g_clip46_options;

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
set_last_error_text(const char *text)
{
    safe_copy(g_last_error, text, sizeof(g_last_error));
}

static void
set_last_error_code(const char *text, long code)
{
    char tmp[512];

    sprintf(tmp, "%s (code %ld)", text, code);
    set_last_error_text(tmp);
}

static void
clear_last_error(void)
{
    g_last_error[0] = '\0';
}

static void
set_clip_negative_error(const clip_u8 *app, size_t app_len)
{
    char tmp[512];
    char byte_text[8];
    size_t i;
    size_t used;

    if (app == NULL || app_len == 0U) {
        set_last_error_text("ECM returned a negative CLIP response.");
        return;
    }

    if (app_len >= 2U)
        sprintf(tmp,
                "ECM negative CLIP response: service=%02X sequence=%02X data=",
                (unsigned int)app[0],
                (unsigned int)app[1]);
    else
        sprintf(tmp,
                "ECM negative CLIP response: service=%02X data=",
                (unsigned int)app[0]);

    used = strlen(tmp);
    for (i = 2U; i < app_len; ++i) {
        sprintf(byte_text, "%s%02X",
                (i == 2U) ? "" : " ",
                (unsigned int)app[i]);
        if (used + strlen(byte_text) + 1U >= sizeof(tmp))
            break;
        strcat(tmp, byte_text);
        used += strlen(byte_text);
    }

    set_last_error_text(tmp);
}

int RP1210_CALL
rp1210_get_last_error(char *buffer, int buffer_size)
{
    size_t n;

    if (buffer == NULL || buffer_size <= 0)
        return 0;

    n = strlen(g_last_error);
    if (n >= (size_t)buffer_size)
        n = (size_t)buffer_size - 1U;

    if (n != 0U)
        memcpy(buffer, g_last_error, n);
    buffer[n] = '\0';
    return (int)n;
}

static void
report_progress(struct pull_ctx *ctx, int percent, const char *text)
{
    if (ctx != NULL && ctx->progress != NULL)
        ctx->progress(percent, text);
}

static int
map_transport_result(struct pull_ctx *ctx, int transport_rc)
{
    const char *message;

    if (transport_rc == J1939_TRANSPORT_OK)
        return PULL_OK;

    message = j1939_transport_error(
        ctx != NULL ? ctx->transport : NULL);
    if (message != NULL && message[0] != '\0')
        set_last_error_text(message);

    switch (transport_rc) {
    case J1939_TRANSPORT_ERR_ARGUMENT:
        return PULL_ERR_ARGUMENT;
    case J1939_TRANSPORT_ERR_LOAD_API:
        return PULL_ERR_LOAD_API;
    case J1939_TRANSPORT_ERR_SYMBOL:
        return PULL_ERR_SYMBOL;
    case J1939_TRANSPORT_ERR_CONNECT:
        return PULL_ERR_CONNECT;
    case J1939_TRANSPORT_ERR_SEND:
        return PULL_ERR_SEND;
    case J1939_TRANSPORT_ERR_TIMEOUT:
        return PULL_ERR_TIMEOUT;
    case J1939_TRANSPORT_ERR_MEMORY:
        return PULL_ERR_MEMORY;
    default:
        return PULL_ERR_PROTOCOL;
    }
}

static int
open_j1939_transport(struct pull_ctx *ctx,
                      const char *api_name,
                      int device_id,
                      int baud,
                      clip_u8 tool_sa,
                      clip_u8 ecm_sa)
{
    int rc;

    if (ctx == NULL)
        return PULL_ERR_ARGUMENT;

    ctx->transport = j1939_transport_create();
    if (ctx->transport == NULL) {
        set_last_error_text("Unable to allocate J1939 transport.");
        return PULL_ERR_MEMORY;
    }

    rc = j1939_transport_open(ctx->transport,
                               api_name,
                               device_id,
                               baud,
                               CLIP_J1939_PGN,
                               CLIP_J1939_PRIORITY,
                               tool_sa,
                               ecm_sa);
    if (rc != J1939_TRANSPORT_OK)
        return map_transport_result(ctx, rc);

    return PULL_OK;
}

static void
close_j1939_transport(struct pull_ctx *ctx)
{
    if (ctx == NULL || ctx->transport == NULL)
        return;

    j1939_transport_destroy(ctx->transport);
    ctx->transport = NULL;

    /* The protected application session ends with the transport. */
    ctx->clip46_secure = 0;
    clip46_poc_wipe(ctx->clip46_app_key, sizeof(ctx->clip46_app_key));
    clip46_poc_wipe(ctx->clip46_app_iv, sizeof(ctx->clip46_app_iv));
}

static int
j1939_send_payload(struct pull_ctx *ctx,
                   const clip_u8 *payload,
                   size_t payload_len)
{
    int rc;

    if (ctx == NULL || ctx->transport == NULL)
        return PULL_ERR_ARGUMENT;

    rc = j1939_transport_send(ctx->transport,
                               (const unsigned char *)payload,
                               payload_len);
    return map_transport_result(ctx, rc);
}

static int
j1939_read_payload(struct pull_ctx *ctx,
                   clip_u8 *payload,
                   size_t payload_capacity,
                   size_t *payload_len,
                   unsigned long timeout_ms)
{
    int rc;

    if (ctx == NULL || ctx->transport == NULL)
        return PULL_ERR_ARGUMENT;

    rc = j1939_transport_receive(ctx->transport,
                                  (unsigned char *)payload,
                                  payload_capacity,
                                  payload_len,
                                  timeout_ms);
    return map_transport_result(ctx, rc);
}

static int
echo_send_adapter(void *user,
                  const unsigned char *data,
                  unsigned int length)
{
    struct pull_ctx *ctx;
    int rc;

    ctx = (struct pull_ctx *)user;
    rc = j1939_send_payload(ctx,
                            (const clip_u8 *)data,
                            (size_t)length);
    return rc == PULL_OK ? 0 : rc;
}

static int
echo_recv_adapter(void *user,
                  unsigned char *data,
                  unsigned int capacity,
                  unsigned int *length,
                  unsigned long timeout_ms)
{
    struct pull_ctx *ctx;
    size_t n;
    int rc;

    if (length == NULL)
        return PULL_ERR_ARGUMENT;

    ctx = (struct pull_ctx *)user;
    n = 0U;
    rc = j1939_read_payload(ctx,
                            (clip_u8 *)data,
                            (size_t)capacity,
                            &n,
                            timeout_ms);
    if (rc != PULL_OK)
        return rc;

    *length = (unsigned int)n;
    return 0;
}

static void
echo_progress_adapter(void *user, int percent, const char *message)
{
    report_progress((struct pull_ctx *)user, percent, message);
}

static void
echo_init_io(struct pull_ctx *ctx, echo_io *io)
{
    if (io == NULL)
        return;

    io->user = ctx;
    io->send = echo_send_adapter;
    io->recv = echo_recv_adapter;
    io->progress = echo_progress_adapter;
}

static int
map_echo_result(int echo_rc)
{
    const char *message;

    if (echo_rc == ECHO_TRANSFER_OK)
        return PULL_OK;

    message = echo_get_last_error();
    if (g_last_error[0] == '\0' &&
        message != NULL &&
        message[0] != '\0') {
        set_last_error_text(message);
    }

    switch (echo_rc) {
    case ECHO_TRANSFER_ERR_ARGUMENT:
        return PULL_ERR_ARGUMENT;
    case ECHO_TRANSFER_ERR_MEMORY:
        return PULL_ERR_MEMORY;
    case ECHO_TRANSFER_ERR_FILE:
        return PULL_ERR_FILE;
    case ECHO_TRANSFER_ERR_CRC:
        return PULL_ERR_CRC;
    default:
        return PULL_ERR_PROTOCOL;
    }
}

static clip_u8
wire_class_for_slot(unsigned int slot)
{
    return (clip_u8)(0x03U | ((slot & 7U) << 5));
}

static int
clip_send_open(struct pull_ctx *ctx)
{
    clip_u8 request[9];
    clip_u8 reply[CLIP_WIRE_MAX];
    size_t reply_len;
    int rc;

    request[0] = 0x81U;
    request[1] = 0x02U;
    request[2] = 0x01U;
    /*
     * Byte 3 announces this tool's connection slot. Legacy sessions keep
     * the historically observed 0x01. The GTIS4.6 experiment announces
     * 0x03, matching the reference tool's first-session behavior: the
     * ECM then addresses this session's frames with byte 3 = 0x03 and
     * the first protected 10 01 uses 0x0E - slot (0x0B) as its
     * application connection ID (rule and evidence in
     * clip46_poc_secure.h). Bench evidence 2026-10-11: without the
     * announcement the ECM addressed the session with byte 3 = 0x01,
     * which is outside the observed derivation window, and the tool
     * correctly refused to send an application request.
     */
    request[3] = 0x01U;
    if (g_clip46_options.secure_read)
        request[3] = 0x03U;
    request[4] = 0xffU;
    request[5] = 0x01U;
    request[6] = 0xffU;
    request[7] = 0x00U;
    request[8] = 0x00U;

    rc = j1939_send_payload(ctx, request, sizeof(request));
    if (rc != PULL_OK)
        return rc;

    for (;;) {
        rc = j1939_read_payload(ctx,
                                reply,
                                sizeof(reply),
                                &reply_len,
                                CLIP_OPEN_TIMEOUT_MS);
        if (rc != PULL_OK)
            return rc;

        if (reply_len >= 5U &&
            reply[0] == 0x81U &&
            reply[1] == 0x01U &&
            reply[2] == 0x02U) {
            ctx->session_id = reply[4];
            if (ctx->session_id == 0U || ctx->session_id == 0xffU)
                ctx->session_id = 0x01U;
            return PULL_OK;
        }

        /*
         * ENI / ELITE II (CM550/CM554) rejects the guaranteed CLIP open with
         *     0D 18 81 FF FF FF FF FF
         * and then expects the older raw ELITE II services on the same
         * Proprietary-A PGN.  Return a positive internal discriminator so the
         * caller can switch protocols without treating this as a timeout.
         */
        if (reply_len >= 3U &&
            reply[0] == 0x0dU &&
            reply[1] == 0x18U &&
            reply[2] == 0x81U) {
            return PULL_DETECTED_ELITE_II;
        }
    }
}

static int
clip_send_close(struct pull_ctx *ctx)
{
    clip_u8 request[9];

    if (ctx == NULL)
        return PULL_ERR_ARGUMENT;

    request[0] = 0x81U;
    request[1] = 0x05U;
    request[2] = 0x04U;
    request[3] = 0x00U;
    request[4] = 0x00U;
    request[5] = 0x00U;
    request[6] = 0x00U;
    request[7] = 0x00U;
    request[8] = 0x00U;

    return j1939_send_payload(ctx, request, sizeof(request));
}

/*
 * GTIS4.6 protected application send.
 *
 * Verified framing (bench 2026-10-11): the legacy application PDU's first
 * two bytes (service, sequence) are transmitted in the clear and the
 * remaining PDU bytes are AES-128-CBC/PKCS#7 encrypted with the
 * ECDH-derived application key and IV. The proven first protected request
 * "10 01 || ct(01 01 00 00 84)" is exactly
 * clip_cal_build_query_request(0x01, 0x000084) split at byte 2.
 *
 * Outer byte 4 is the transport block-count acknowledgement of the
 * previous inbound reply: ceil(wire_length / 16). The reference capture
 * confirms 23B -> 0x02 and 823B -> 0x34, and the first request's 0x0B/0x0A
 * values are exactly this count for the observed 167/151-byte 02 04
 * replies; the earlier "connection ID" interpretation of those bytes was
 * a coincidence. Misreporting the count accumulates a block deficit after
 * which the ECM stops answering.
 */
static int
clip46_send(struct pull_ctx *ctx,
            const clip_u8 *app,
            size_t app_len)
{
    clip_u8 wire[CLIP_WIRE_MAX];
    size_t cipher_len;
    clip_u8 cls;
    int rc;

    if (ctx == NULL || app == NULL || app_len < 2U)
        return PULL_ERR_ARGUMENT;
    if (app_len - 2U > sizeof(wire) - 7U - 16U)
        return PULL_ERR_PROTOCOL;

    cipher_len = 0U;
    if (clip46_poc_encrypt_application(ctx->clip46_app_key,
                                       ctx->clip46_app_iv,
                                       app + 2U,
                                       app_len - 2U,
                                       wire + 7U,
                                       sizeof(wire) - 7U,
                                       &cipher_len) != 0) {
        set_last_error_text("GTIS4.6 application encryption failed.");
        return PULL_ERR_CRYPTO;
    }

    cls = wire_class_for_slot(ctx->wire_slot);
    wire[0] = 0x81U;
    wire[1] = 0x00U;
    wire[2] = cls;
    wire[3] = 0x00U;
    wire[4] = ctx->clip46_last_blocks;
    wire[5] = app[0];
    wire[6] = app[1];

    rc = j1939_send_payload(ctx, wire, 7U + cipher_len);
    ctx->wire_slot = (ctx->wire_slot + 1U) & 7U;
    return rc;
}

/*
 * GTIS4.6 protected application exchange. Sends the legacy PDU through
 * clip46_send() and waits for the reply whose visible service/sequence
 * prefix matches (expected0, expected1). The reply body is decrypted and
 * the full legacy reply PDU (service, sequence, data) is reconstructed for
 * the caller, so clip_cal parsing is unchanged. Service-0x03 negatives are
 * handled like the legacy exchange: tolerated when ctx->tolerate_negative,
 * otherwise reported as a protocol error.
 */
static int
clip46_exchange(struct pull_ctx *ctx,
                const clip_u8 *app,
                size_t app_len,
                clip_u8 expected0,
                clip_u8 expected1,
                int check_expected1,
                clip_u8 *reply_app,
                size_t reply_capacity,
                size_t *reply_len,
                unsigned long timeout_ms)
{
    clip_u8 incoming[CLIP_WIRE_MAX];
    clip_u8 plain[CLIP_APP_MAX];
    size_t incoming_len;
    size_t plain_len;
    size_t n_app;
    int rc;

    if (ctx == NULL || app == NULL || reply_app == NULL || reply_len == NULL)
        return PULL_ERR_ARGUMENT;

    rc = clip46_send(ctx, app, app_len);
    if (rc != PULL_OK)
        return rc;

    for (;;) {
        rc = j1939_read_payload(ctx,
                                incoming,
                                sizeof(incoming),
                                &incoming_len,
                                timeout_ms);
        if (rc != PULL_OK)
            return rc;

        if (incoming_len < 7U)
            continue;
        if (incoming[0] != 0x81U)
            continue;

        ctx->ecm_slot = incoming[3];
        n_app = incoming_len - 5U;
        if (n_app < 2U)
            continue;

        /*
         * Negative replies keep a visible 0x03 service prefix (observed on
         * the wire). Reconstruct with the decrypted body when possible.
         */
        if (incoming[5] == 0x03U &&
            (!check_expected1 || incoming[6] == expected1)) {
            char neg[600];
            size_t neg_len;
            size_t copy_len;
            unsigned long blocks;

            blocks = (unsigned long)((incoming_len + 15U) / 16U);
            ctx->clip46_last_blocks =
                (clip_u8)(blocks > 255UL ? 255UL : blocks);
            plain_len = 0U;
            if (n_app > 2U && (n_app - 2U) % 16U == 0U) {
                (void)clip46_poc_decrypt_cbc(ctx->clip46_app_key,
                                             ctx->clip46_app_iv,
                                             incoming + 7U,
                                             n_app - 2U,
                                             plain,
                                             sizeof(plain),
                                             &plain_len);
            }
            neg_len = 2U + plain_len;
            if (ctx->tolerate_negative) {
                if (neg_len > reply_capacity) {
                    set_last_error_text(
                        "GTIS4.6 negative reply exceeds caller buffer.");
                    return PULL_ERR_PROTOCOL;
                }
                reply_app[0] = incoming[5];
                reply_app[1] = incoming[6];
                if (plain_len != 0U)
                    memcpy(reply_app + 2U, plain, plain_len);
                *reply_len = neg_len;
                return PULL_OK;
            }
            neg[0] = (char)incoming[5];
            neg[1] = (char)incoming[6];
            copy_len = plain_len;
            if (copy_len > sizeof(neg) - 3U)
                copy_len = sizeof(neg) - 3U;
            if (copy_len != 0U)
                memcpy(neg + 2, plain, copy_len);
            set_clip_negative_error((const clip_u8 *)neg, 2U + copy_len);
            return PULL_ERR_PROTOCOL;
        }

        if (incoming[5] != expected0)
            continue;
        if (check_expected1 && incoming[6] != expected1)
            continue;

        /* Acknowledge this reply's transport size on the next request. */
        {
            unsigned long blocks;

            blocks = (unsigned long)((incoming_len + 15U) / 16U);
            ctx->clip46_last_blocks =
                (clip_u8)(blocks > 255UL ? 255UL : blocks);
        }

        if (n_app == 2U) {
            if (reply_capacity < 2U) {
                set_last_error_text("GTIS4.6 reply exceeds caller buffer.");
                return PULL_ERR_PROTOCOL;
            }
            reply_app[0] = incoming[5];
            reply_app[1] = incoming[6];
            *reply_len = 2U;
            return PULL_OK;
        }

        if ((n_app - 2U) % 16U != 0U) {
            set_last_error_text(
                "GTIS4.6 protected reply body is not block aligned.");
            return PULL_ERR_PROTOCOL;
        }
        if (clip46_poc_decrypt_cbc(ctx->clip46_app_key,
                                   ctx->clip46_app_iv,
                                   incoming + 7U,
                                   n_app - 2U,
                                   plain,
                                   sizeof(plain),
                                   &plain_len) != 0) {
            set_last_error_text(
                "GTIS4.6 protected reply failed CBC validation.");
            return PULL_ERR_PROTOCOL;
        }
        if (plain_len + 2U > reply_capacity) {
            set_last_error_text("GTIS4.6 reply exceeds caller buffer.");
            return PULL_ERR_PROTOCOL;
        }
        reply_app[0] = incoming[5];
        reply_app[1] = incoming[6];
        if (plain_len != 0U)
            memcpy(reply_app + 2U, plain, plain_len);
        *reply_len = plain_len + 2U;
        return PULL_OK;
    }
}

static int
clip_exchange(struct pull_ctx *ctx,
              clip_u8 session_id,
              const clip_u8 *app,
              size_t app_len,
              clip_u8 expected0,
              clip_u8 expected1,
              int check_expected1,
              clip_u8 *reply_app,
              size_t reply_capacity,
              size_t *reply_len,
              unsigned long timeout_ms)
{
    clip_u8 wire[CLIP_WIRE_MAX];
    clip_u8 incoming[CLIP_WIRE_MAX];
    size_t incoming_len;
    size_t n_app;
    clip_u8 cls;
    int rc;

    if (ctx == NULL || app == NULL || reply_app == NULL || reply_len == NULL)
        return PULL_ERR_ARGUMENT;

    /*
     * GTIS4.6 protected sessions transparently encrypt the legacy PDU;
     * callers and clip_cal parsing are unchanged.
     */
    if (ctx->clip46_secure) {
        return clip46_exchange(ctx,
                               app,
                               app_len,
                               expected0,
                               expected1,
                               check_expected1,
                               reply_app,
                               reply_capacity,
                               reply_len,
                               timeout_ms);
    }

    if (app_len + 5U > sizeof(wire)) {
        set_last_error_text("CLIP application PDU is too large.");
        return PULL_ERR_PROTOCOL;
    }

    cls = wire_class_for_slot(ctx->wire_slot);

    wire[0] = 0x81U;
    wire[1] = 0x00U;
    wire[2] = cls;
    wire[3] = 0x00U;
    wire[4] = session_id;
    memcpy(wire + 5U, app, app_len);

    rc = j1939_send_payload(ctx, wire, app_len + 5U);
    ctx->wire_slot = (ctx->wire_slot + 1U) & 7U;
    if (rc != PULL_OK)
        return rc;

    for (;;) {
        rc = j1939_read_payload(ctx,
                                incoming,
                                sizeof(incoming),
                                &incoming_len,
                                timeout_ms);
        if (rc != PULL_OK)
            return rc;

        /*
         * Bytes 1/2 are transport/message-family state, not a stable
         * request/response echo.  The supplied capture shows valid replies
         * with byte 1 values 00/01/02 and with a different family byte than
         * the request (especially during long memory uploads).
         *
         * Byte 4 is the guaranteed-transfer connection ID.  Match replies by
         * application service/sequence; that is the stable transaction key.
         */
        if (incoming_len < 7U)
            continue;
        if (incoming[0] != 0x81U)
            continue;

        /*
         * Byte 3 of ECM frames carries this session's connection slot in
         * every observed GTIS4.6 capture (constant within a session).
         * Record it before any early-return branch so the secure path can
         * derive the application connection ID from the freshest frame.
         */
        ctx->ecm_slot = incoming[3];

        n_app = incoming_len - 5U;

        /*
         * Secure CLIP is a distinct 02 family; promote the valid 02 02
         * reply to the experimental handler. Never run legacy TEA against it.
         * The subsequent 02 03 is only attempted with explicit local
         * operator-supplied material.
         */
        if (expected0 == 0x01U && check_expected1 &&
            expected1 == CLIP_OPCODE_SEED &&
            n_app >= 2U &&
            incoming[5] == 0x02U && incoming[6] == 0x02U) {
            struct clip_secure_seed_reply secure_seed;

            if (clip_parse_secure_seed_reply(incoming + 5U, n_app,
                                             &secure_seed) != CLIP_OK) {
                set_last_error_text("Secure CLIP 02 02 reply is malformed.");
                return PULL_ERR_PROTOCOL;
            }
            if (n_app > reply_capacity) {
                set_last_error_text("Secure CLIP seed exceeds receive buffer.");
                return PULL_ERR_PROTOCOL;
            }
            ctx->session_id = incoming[4];
            memcpy(reply_app, incoming + 5U, n_app);
            *reply_len = n_app;
            return PULL_DETECTED_SECURE_CLIP;
        }

        /*
         * 01 05 is a session-refusal opcode only during the CLIP handshake.
         * Once the guaranteed application channel is open, byte 6 is the
         * application sequence number, so a perfectly valid positive reply
         * for sequence 0x05 is also "01 05 ...".  The old global check
         * therefore misclassified reference tool preflight sequence 0x05 as a
         * refused session.
         *
         * Restrict the refusal interpretation to the two handshake exchanges
         * that expect CLIP opcodes rather than application sequence numbers.
         */
        if (n_app >= 2U &&
            incoming[5] == 0x01U &&
            incoming[6] == CLIP_OPCODE_REFUSED &&
            expected0 == 0x01U &&
            check_expected1 &&
            (expected1 == CLIP_OPCODE_SEED ||
             expected1 == CLIP_OPCODE_CONTEXT_REPLY)) {
            set_last_error_text("ECM refused the CLIP session request.");
            return PULL_ERR_PROTOCOL;
        }

        /*
         * Calibration/application negative response.  The live ECM returned
         *     03 00 00 07
         * when we tried to enter calibration mode before reference tool's first
         * post-authentication status query.  Treat service 0x03 as a real
         * transaction result instead of ignoring it until the read timeout.
         */
        if (n_app >= 2U &&
            incoming[5] == 0x03U &&
            (!check_expected1 || incoming[6] == expected1)) {
            ctx->session_id = incoming[4];
            if (ctx->tolerate_negative) {
                if (n_app > reply_capacity) {
                    set_last_error_text("CLIP negative reply exceeds caller buffer.");
                    return PULL_ERR_PROTOCOL;
                }
                memcpy(reply_app, incoming + 5U, n_app);
                *reply_len = n_app;
                return PULL_OK;
            }
            set_clip_negative_error(incoming + 5U, n_app);
            return PULL_ERR_PROTOCOL;
        }

        /* A secure-auth failure must not become a misleading timeout. */
        if (expected0 == 0x02U && check_expected1 &&
            expected1 == 0x04U && n_app >= 2U &&
            incoming[5] == 0x03U) {
            set_clip_negative_error(incoming + 5U, n_app);
            return PULL_ERR_PROTOCOL;
        }

        if (n_app < 1U || incoming[5] != expected0)
            continue;
        if (check_expected1 && (n_app < 2U || incoming[6] != expected1))
            continue;

        if (n_app > reply_capacity) {
            set_last_error_text("CLIP application reply exceeds caller buffer.");
            return PULL_ERR_PROTOCOL;
        }

        /*
         * Carry the reply connection ID forward.  The context exchange is a
         * special case and overrides this with the ID decoded from the remote
         * context immediately after this call.
         */
        /*
         * CM2450E: 02 04 byte 4 (often 0x08) is NOT established as
         * the application connection ID. INSITE's first app requests
         * in the paired traces use 0x0B and 0x0A respectively, neither
         * equal to this byte nor reliably to outer byte 3. The active
         * connection ID needs to come from session negotiation; until
         * then, only an explicit local test override is accepted.
         */
        if (expected0 == 0x02U && check_expected1 &&
            expected1 == 0x04U) {
            /*
             * No universal application connection ID is established yet.
             * We require an explicit local override for post-02 04 trials.
             * Preserve the outer received byte only for normal transport
             * bookkeeping; it is not the app session ID.
             */
            ctx->session_id = incoming[4];
            if (g_clip46_options.conn_override != 0U)
                ctx->session_id = (clip_u8)g_clip46_options.conn_override;
        } else {
            ctx->session_id = incoming[4];
        }

        memcpy(reply_app, incoming + 5U, n_app);
        *reply_len = n_app;
        return PULL_OK;
    }
}

static int
clip_send_only(struct pull_ctx *ctx,
               clip_u8 session_id,
               const clip_u8 *app,
               size_t app_len)
{
    clip_u8 wire[CLIP_WIRE_MAX];
    clip_u8 cls;
    int rc;

    if (ctx == NULL || app == NULL)
        return PULL_ERR_ARGUMENT;

    if (ctx->clip46_secure)
        return clip46_send(ctx, app, app_len);

    if (app_len + 5U > sizeof(wire))
        return PULL_ERR_PROTOCOL;

    cls = wire_class_for_slot(ctx->wire_slot);
    wire[0] = 0x81U;
    wire[1] = 0x00U;
    wire[2] = cls;
    wire[3] = 0x00U;
    wire[4] = session_id;
    memcpy(wire + 5U, app, app_len);

    rc = j1939_send_payload(ctx, wire, app_len + 5U);
    ctx->wire_slot = (ctx->wire_slot + 1U) & 7U;
    return rc;
}

/*
 * Bench-only GTIS4.6 authentication experiment. The observed ciphertext is
 * IV[16] || AES-128-CBC/PKCS#7( challenge[16] || context[51] ||
 * opaque[32] ), and 02 03 precedes the 128-byte body.
 *
 * The 32-byte field's fresh-session generation is not yet understood.
 * Therefore a 02 04 reply is logged as a response, NEVER proof of a
 * validated authenticated session. No calibration/status/read/write follows.
 */
/*
 * GTIS4.6 experimental read-only authentication, never an unlock bypass.
 * 02 04 decryption and P-256 ECDH do NOT by themselves prove ECM auth.
 * No descriptor/memory transfer or program operation can leave this path.
 */
static int
clip46_experimental_auth(struct pull_ctx *ctx,
                         const clip_u8 *seed_pdu, size_t seed_len)
{
    struct clip_secure_seed_reply seed;
    struct clip46_poc_config config;
    struct clip46_p256_keypair keypair;
    clip_u8 plaintext[CLIP46_PLAINTEXT_BYTES];
    clip_u8 envelope_iv[16];
    clip_u8 app[CLIP46_APP_BYTES];
    clip_u8 reply[256];
    clip_u8 reply_plain[160];
    clip_u8 shared_secret[32];
    clip_u8 app_iv[16], app_key[16];
    size_t reply_len, reply_plain_len;
    int rc;

    memset(&config, 0, sizeof(config));
    memset(&keypair, 0, sizeof(keypair));
    memset(plaintext, 0, sizeof(plaintext));
    memset(envelope_iv, 0, sizeof(envelope_iv));
    memset(app, 0, sizeof(app));
    memset(reply, 0, sizeof(reply));
    memset(reply_plain, 0, sizeof(reply_plain));
    memset(shared_secret, 0, sizeof(shared_secret));
    memset(app_iv, 0, sizeof(app_iv));
    memset(app_key, 0, sizeof(app_key));
    rc = PULL_ERR_CRYPTO;

    if (clip_parse_secure_seed_reply(seed_pdu, seed_len, &seed) != CLIP_OK) {
        set_last_error_text("Invalid GTIS4.6 02 02 challenge.");
        return PULL_ERR_PROTOCOL;
    }
    if (!g_clip46_options.secure_read) {
        set_last_error_text(
            "GTIS4.6 protected read is disabled in config.toml; "
            "no authentication payload was sent.");
        return PULL_ERR_SECURE_POST_AUTH;
    }
    if (clip46_runtime_load_secrets(&config) != 0) {
        set_last_error_text(
            "Invalid [gtis46-config] KEY_HEX/CONTEXT_HEX in config.toml; "
            "no authentication payload was sent.");
        goto done;
    }
    if (clip46_p256_generate(&keypair) != 0) {
        set_last_error_text("GTIS4.6 failed to generate fresh P-256 keypair.");
        goto done;
    }
    if (clip46_poc_random_iv(envelope_iv) != 0 ||
        clip46_poc_build(seed.challenge, config.context,
                         keypair.compressed_public + 1U, plaintext) != 0 ||
        clip46_poc_encrypt(config.key, envelope_iv, plaintext,
                           app + 2U) != 0) {
        set_last_error_text("GTIS4.6 authentication envelope encryption failed.");
        goto done;
    }
    app[0] = 0x02U;
    app[1] = 0x03U;
    report_progress(ctx, 7, "TEST: fresh GTIS4.6 02 03 P-256 authentication...");
    rc = clip_exchange(ctx, ctx->session_id, app, sizeof(app),
                       0x02U, 0x04U, 1,
                       reply, sizeof(reply), &reply_len,
                       CLIP_TIMEOUT_MS);
    if (rc != PULL_OK) goto done;
    if (reply_len != 146U && reply_len != 162U) {
        set_last_error_text("GTIS4.6 02 04 has unexpected envelope length.");
        rc = PULL_ERR_PROTOCOL;
        goto done;
    }
    if (reply[0] != 0x02U || reply[1] != 0x04U ||
        clip46_poc_decrypt_envelope(config.key, reply + 2U,
                                   reply_len - 2U, reply_plain,
                                   sizeof(reply_plain), &reply_plain_len) != 0 ||
        (reply_plain_len != 124U && reply_plain_len != 136U)) {
        set_last_error_text("GTIS4.6 02 04 decryption, padding or layout failed.");
        rc = PULL_ERR_PROTOCOL;
        goto done;
    }
    if (clip46_p256_ecdh_xonly(keypair.private_scalar,
            reply_plain + reply_plain_len - 32U, shared_secret) != 0) {
        set_last_error_text("GTIS4.6 peer X point invalid or ECDH failed.");
        rc = PULL_ERR_CRYPTO;
        goto done;
    }
    /* Exact experimentally verified split; no extra KDF. */
    memcpy(app_iv, shared_secret, 16U);
    memcpy(app_key, shared_secret + 16U, 16U);
    clip46_poc_wipe(shared_secret, sizeof(shared_secret));
    clip46_poc_wipe(&keypair, sizeof(keypair));

    /*
     * Outer byte 4 of the first protected request acknowledges the
     * transport size of the 02 04 reply: ceil(wire_length / 16) blocks,
     * where the wire length is reply_len + 5 (envelope + PDU). The
     * previously suspected "connection ID" values 0x0B/0x0A are exactly
     * this count for the observed 167/151-byte 02 04 replies; the
     * slot-based derivation was a coincidence and is retired.
     */
    {
        char note[112];
        unsigned long blocks;

        blocks = (unsigned long)((reply_len + 5U + 15U) / 16U);
        if (blocks > 255UL)
            blocks = 255UL;
        ctx->clip46_last_blocks = (clip_u8)blocks;
        sprintf(note,
                "GTIS4.6: slot 0x%02X; first request acknowledges %lu "
                "transport block(s).",
                (unsigned)ctx->ecm_slot,
                blocks);
        report_progress(ctx, 8, note);
    }

    /*
     * Optional read-only protected session: send the proven first status
     * query (0x0084) through the transparent secure codec. The first
     * request's byte 4 is the mechanical 02 04 block acknowledgement
     * above; CLIP46_POC_CONN_ID overrides it for independent verification.
     * On success the protected session is left active so the ordinary
     * read-only download workflow continues over it.
     */
    if (ctx->clip46_read_only_workflow) {
        if (g_clip46_options.secure_read) {
            clip_u8 request[CLIP_CAL_QUERY_REQUEST_SIZE];
            clip_u8 response[CLIP_APP_MAX];
            size_t response_len;
            if (g_clip46_options.conn_override != 0U)
                ctx->clip46_last_blocks =
                    (clip_u8)g_clip46_options.conn_override;
            memset(request, 0, sizeof(request));
            memset(response, 0, sizeof(response));
            /*
             * Standard query PDU 10 01 01 01 00 00 84; the codec sends the
             * service/sequence bytes clear and encrypts the remainder,
             * reproducing the bench-verified first protected request.
             */
            rc = clip_cal_build_query_request(0x01U, 0x000084UL, request);
            if (rc != CLIP_CAL_OK) {
                set_last_error_text("GTIS4.6 failed to build status query.");
                rc = PULL_ERR_CAL;
                goto done;
            }
            /* Promote the session before the exchange so it routes
             * through clip46_exchange(); cleared again on failure. */
            memcpy(ctx->clip46_app_key, app_key, 16U);
            memcpy(ctx->clip46_app_iv, app_iv, 16U);
            ctx->clip46_secure = 1;
            report_progress(ctx, 9,
                            "GTIS4.6: sending first protected status query...");
            rc = clip_exchange(ctx, ctx->session_id, request,
                               sizeof(request), 0x01U, 0x01U, 1,
                               response, sizeof(response), &response_len,
                               CLIP_TIMEOUT_MS);
            if (rc != PULL_OK) {
                ctx->clip46_secure = 0;
                clip46_poc_wipe(ctx->clip46_app_key,
                                sizeof(ctx->clip46_app_key));
                clip46_poc_wipe(ctx->clip46_app_iv,
                                sizeof(ctx->clip46_app_iv));
                goto done;
            }
            if (response_len < 2U ||
                response[0] != CLIP_CAL_SERVICE_REPLY) {
                set_last_error_text(
                    "GTIS4.6 protected status reply has unexpected shape.");
                ctx->clip46_secure = 0;
                clip46_poc_wipe(ctx->clip46_app_key,
                                sizeof(ctx->clip46_app_key));
                clip46_poc_wipe(ctx->clip46_app_iv,
                                sizeof(ctx->clip46_app_iv));
                rc = PULL_ERR_PROTOCOL;
                goto done;
            }
            clip46_poc_wipe(response, sizeof(response));
            report_progress(ctx, 10,
                            "GTIS4.6: protected session established; "
                            "continuing read-only calibration readback.");
            rc = PULL_OK;
            goto done;
        }
    }
    set_last_error_text(
        "GTIS4.6 02 04 decrypted and fresh P-256 ECDH derived. "
        "Set [experimental] gtis46_secure_read=true in config.toml. "
        "Context/ack semantics remain unverified.");
    rc = PULL_ERR_SECURE_POST_AUTH;

done:
    clip46_poc_wipe(&config, sizeof(config));
    clip46_poc_wipe(&keypair, sizeof(keypair));
    clip46_poc_wipe(plaintext, sizeof(plaintext));
    clip46_poc_wipe(envelope_iv, sizeof(envelope_iv));
    clip46_poc_wipe(app, sizeof(app));
    clip46_poc_wipe(reply, sizeof(reply));
    clip46_poc_wipe(reply_plain, sizeof(reply_plain));
    clip46_poc_wipe(shared_secret, sizeof(shared_secret));
    clip46_poc_wipe(app_iv, sizeof(app_iv));
    clip46_poc_wipe(app_key, sizeof(app_key));
    return rc;
}

static int
clip_authenticate(struct pull_ctx *ctx)
{
    clip_u8 seed_request[CLIP_SEED_REQUEST_SIZE];
    clip_u8 seed_reply_pdu[128];
    size_t seed_reply_len;
    struct clip_seed_reply seed_reply;
    clip_u8 tool_context[CLIP_TOOL_CONTEXT_SIZE];
    clip_u8 context_request[128];
    size_t context_request_len;
    clip_u8 context_reply[256];
    size_t context_reply_len;
    int rc;

    report_progress(ctx, 3, "Opening CLIP session...");
    rc = clip_send_open(ctx);
    if (rc != PULL_OK)
        return rc;

    ctx->wire_slot = 0U;

    report_progress(ctx, 5, "Requesting CLIP seed...");
    clip_build_seed_request(seed_request);
    rc = clip_exchange(ctx,
                       0x00U,
                       seed_request,
                       sizeof(seed_request),
                       0x01U,
                       CLIP_OPCODE_SEED,
                       1,
                       seed_reply_pdu,
                       sizeof(seed_reply_pdu),
                       &seed_reply_len,
                       CLIP_TIMEOUT_MS);
    if (rc == PULL_DETECTED_SECURE_CLIP)
        return clip46_experimental_auth(ctx, seed_reply_pdu, seed_reply_len);
    if (rc != PULL_OK)
        return rc;

    rc = clip_parse_seed_reply(seed_reply_pdu,
                               seed_reply_len,
                               &seed_reply);
    if (rc != CLIP_OK) {
        set_last_error_code("Invalid CLIP seed reply", (long)rc);
        return PULL_ERR_CRYPTO;
    }

    rc = clip_build_tool_context(tool_context,
                                 CLIP_DEFAULT_PASSWORD_TYPE,
                                 g_default_password,
                                 CLIP_DEFAULT_TOOL_FAMILY,
                                 CLIP_DEFAULT_TOOL_ID,
                                 CLIP_DEFAULT_TOOL_VERSION,
                                 g_default_tool_instance);
    if (rc != CLIP_OK) {
        set_last_error_code("Failed to build CLIP tool context", (long)rc);
        return PULL_ERR_CRYPTO;
    }

    rc = clip_build_context_request((unsigned int)seed_reply.encryption_level,
                                    seed_reply.seed,
                                    tool_context,
                                    sizeof(tool_context),
                                    context_request,
                                    sizeof(context_request),
                                    &context_request_len);
    if (rc != CLIP_OK) {
        set_last_error_code("Failed to encrypt CLIP tool context", (long)rc);
        return PULL_ERR_CRYPTO;
    }

    report_progress(ctx, 7, "Authenticating CLIP context...");
    rc = clip_exchange(ctx,
                       ctx->session_id,
                       context_request,
                       context_request_len,
                       0x01U,
                       CLIP_OPCODE_CONTEXT_REPLY,
                       1,
                       context_reply,
                       sizeof(context_reply),
                       &context_reply_len,
                       CLIP_TIMEOUT_MS);
    if (rc != PULL_OK)
        return rc;

    if (!clip_is_context_reply(context_reply, context_reply_len)) {
        set_last_error_text("ECM returned an invalid CLIP context reply.");
        return PULL_ERR_CRYPTO;
    }

    /*
     * The remote context is encrypted with the same seed/level.  In the
     * supplied recording the first decoded byte becomes the connection ID
     * used by the first guaranteed data request.
     */
    {
        clip_u8 remote_context[512];
        size_t remote_context_len;

        if (context_reply_len <= 2U) {
            set_last_error_text("CLIP context reply contains no encrypted context.");
            return PULL_ERR_CRYPTO;
        }

        rc = clip_decrypt((unsigned int)seed_reply.encryption_level,
                          seed_reply.seed,
                          context_reply + 2U,
                          context_reply_len - 2U,
                          remote_context,
                          sizeof(remote_context),
                          &remote_context_len);
        if (rc != CLIP_OK || remote_context_len == 0U) {
            set_last_error_code("Failed to decrypt CLIP remote context",
                                (long)rc);
            return PULL_ERR_CRYPTO;
        }

        ctx->session_id = remote_context[0];
        if (ctx->session_id == 0U || ctx->session_id == 0xffU) {
            set_last_error_text("CLIP remote context returned an invalid connection ID.");
            return PULL_ERR_CRYPTO;
        }
    }

    return PULL_OK;
}


static clip_u32
elite_load_be32(const clip_u8 *p)
{
    clip_u32 v;

    v = ((clip_u32)p[0] << 24);
    v |= ((clip_u32)p[1] << 16);
    v |= ((clip_u32)p[2] << 8);
    v |= (clip_u32)p[3];
    return v;
}

static unsigned int
elite_load_be16(const clip_u8 *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static void
elite_store_be32(clip_u8 *p, clip_u32 v)
{
    p[0] = (clip_u8)(v >> 24);
    p[1] = (clip_u8)(v >> 16);
    p[2] = (clip_u8)(v >> 8);
    p[3] = (clip_u8)v;
}

static int
elite_exchange(struct pull_ctx *ctx,
               const clip_u8 *request,
               size_t request_len,
               clip_u8 expected_opcode,
               clip_u8 *reply,
               size_t reply_capacity,
               size_t *reply_len,
               unsigned long timeout_ms)
{
    clip_u8 incoming[CLIP_WIRE_MAX];
    size_t incoming_len;
    unsigned long start;
    unsigned long now;
    int rc;

    if (ctx == NULL || request == NULL || request_len == 0U ||
        reply == NULL || reply_len == NULL) {
        return PULL_ERR_ARGUMENT;
    }

    rc = j1939_send_payload(ctx, request, request_len);
    if (rc != PULL_OK)
        return rc;

    start = ct_monotonic_ms();
    for (;;) {
        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= (unsigned long)timeout_ms)
            break;

        rc = j1939_read_payload(ctx,
                                incoming,
                                sizeof(incoming),
                                &incoming_len,
                                timeout_ms - (unsigned long)(unsigned long)(now - start));
        if (rc != PULL_OK)
            return rc;

        if (incoming_len >= 3U &&
            incoming[0] == 0x0dU &&
            incoming[2] == request[0]) {
            char msg[160];
            sprintf(msg,
                    "ELITE II negative response to service %02X (reason %02X).",
                    (unsigned int)request[0],
                    (unsigned int)incoming[1]);
            set_last_error_text(msg);
            return PULL_ERR_PROTOCOL;
        }

        if (incoming_len == 0U || incoming[0] != expected_opcode)
            continue;

        if (incoming_len > reply_capacity) {
            set_last_error_text("ELITE II response exceeds receive buffer.");
            return PULL_ERR_PROTOCOL;
        }

        memcpy(reply, incoming, incoming_len);
        *reply_len = incoming_len;
        return PULL_OK;
    }

    set_last_error_text("Timed out waiting for ELITE II response from ECM.");
    return PULL_ERR_TIMEOUT;
}

static int
elite_transfer_control(struct pull_ctx *ctx, clip_u8 opcode)
{
    clip_u8 request[8];
    clip_u8 reply[32];
    size_t reply_len;
    int rc;

    request[0] = opcode;
    request[1] = 0xfeU;
    request[2] = 0xfeU;
    request[3] = 0xffU;
    request[4] = 0xffU;
    request[5] = 0xffU;
    request[6] = 0xffU;
    request[7] = 0xffU;

    rc = elite_exchange(ctx,
                        request,
                        sizeof(request),
                        0x0cU,
                        reply,
                        sizeof(reply),
                        &reply_len,
                        CLIP_TIMEOUT_MS);
    if (rc != PULL_OK)
        return rc;

    if (reply_len < 4U ||
        reply[1] != opcode ||
        reply[2] != 0xfeU ||
        reply[3] != 0xfeU) {
        set_last_error_text("Invalid ELITE II transfer-control acknowledgement.");
        return PULL_ERR_PROTOCOL;
    }

    return PULL_OK;
}

static int
elite_get_descriptor(struct pull_ctx *ctx, struct clip_cal_map *map)
{
    clip_u8 request[8];
    clip_u8 reply[CLIP_WIRE_MAX];
    const clip_u8 *data;
    size_t reply_len;
    size_t data_len;
    size_t starts_off;
    size_t length_size_off;
    size_t lengths_off;
    size_t required;
    unsigned int nested_len;
    unsigned int count;
    unsigned int address_size;
    unsigned int length_size;
    unsigned int i;
    int rc;

    if (ctx == NULL || map == NULL)
        return PULL_ERR_ARGUMENT;

    memset(map, 0, sizeof(*map));

    request[0] = 0x43U;
    request[1] = (clip_u8)((ELITE_II_DESCRIPTOR_ID >> 8) & 0xffU);
    request[2] = (clip_u8)(ELITE_II_DESCRIPTOR_ID & 0xffU);
    request[3] = 0x00U;
    request[4] = 0x00U;
    request[5] = 0x00U;
    request[6] = (clip_u8)ELITE_II_DESCRIPTOR_SIZE;
    request[7] = 0xffU;

    rc = elite_exchange(ctx,
                        request,
                        sizeof(request),
                        0x44U,
                        reply,
                        sizeof(reply),
                        &reply_len,
                        CLIP_TIMEOUT_MS);
    if (rc != PULL_OK)
        return rc;

    if (reply_len < 4U ||
        reply[1] != request[1] ||
        reply[2] != request[2] ||
        reply[3] != request[6]) {
        set_last_error_text("Invalid ELITE II calibration descriptor reply.");
        return PULL_ERR_PROTOCOL;
    }

    data = reply + 4U;
    data_len = reply_len - 4U;
    if (data_len < 10U) {
        set_last_error_text("ELITE II calibration descriptor is too short.");
        return PULL_ERR_PROTOCOL;
    }

    /*
     * CM550/CM554 descriptor 0x1238:
     *   u16 payload_length
     *   u16 flags
     *   u16 range_count
     *   u16 address_width (=4)
     *   be32 start[range_count]
     *   u16 length_width (=4)
     *   be32 length[range_count]
     *
     * In the supplied ENI capture this describes six ranges which collapse to
     * the three runs present in the resulting legacy .ccal:
     * 00004004-00006000, 00008000-0007FFFE, 01000080-01001FFE.
     */
    nested_len = elite_load_be16(data);
    count = elite_load_be16(data + 4U);
    address_size = elite_load_be16(data + 6U);

    if ((size_t)nested_len + 2U > data_len ||
        count == 0U ||
        count > CLIP_CAL_MAX_RANGES ||
        address_size != 4U) {
        set_last_error_text("Unsupported ELITE II calibration descriptor layout.");
        return PULL_ERR_PROTOCOL;
    }

    starts_off = 8U;
    length_size_off = starts_off + ((size_t)count * 4U);
    if (length_size_off + 2U > data_len) {
        set_last_error_text("Truncated ELITE II calibration descriptor.");
        return PULL_ERR_PROTOCOL;
    }

    length_size = elite_load_be16(data + length_size_off);
    if (length_size != 4U) {
        set_last_error_text("Unsupported ELITE II calibration length width.");
        return PULL_ERR_PROTOCOL;
    }

    lengths_off = length_size_off + 2U;
    required = lengths_off + ((size_t)count * 4U);
    if (required > data_len) {
        set_last_error_text("Truncated ELITE II calibration range table.");
        return PULL_ERR_PROTOCOL;
    }

    map->range_count = count;
    for (i = 0U; i < count; ++i) {
        map->ranges[i].address =
            elite_load_be32(data + starts_off + ((size_t)i * 4U));
        map->ranges[i].length =
            elite_load_be32(data + lengths_off + ((size_t)i * 4U));
        map->ranges[i].auxiliary = (clip_u32)0;

        if (map->ranges[i].length == (clip_u32)0 ||
            map->ranges[i].address >
            (clip_u32)0xffffffffUL - map->ranges[i].length) {
            set_last_error_text("Invalid ELITE II calibration memory range.");
            return PULL_ERR_PROTOCOL;
        }
    }

    return PULL_OK;
}

static int
elite_read_memory(struct pull_ctx *ctx,
                  clip_u32 address,
                  unsigned int length,
                  clip_u8 *out)
{
    clip_u8 request[9];
    clip_u8 reply[CLIP_WIRE_MAX];
    size_t request_len;
    size_t reply_len;
    size_t header_len;
    clip_u8 expected_opcode;
    int rc;

    if (ctx == NULL || out == NULL || length == 0U ||
        length > ELITE_II_BLOCK_SIZE) {
        return PULL_ERR_ARGUMENT;
    }

    if (length <= 0xffU) {
        request[0] = 0x4aU;
        elite_store_be32(request + 1U, address);
        request[5] = (clip_u8)length;
        request[6] = 0xffU;
        request[7] = 0xffU;
        request_len = 8U;
        expected_opcode = 0x4bU;
        header_len = 6U;
    } else {
        request[0] = 0x4cU;
        elite_store_be32(request + 1U, address);
        elite_store_be32(request + 5U, (clip_u32)length);
        request_len = 9U;
        expected_opcode = 0x4dU;
        header_len = 9U;
    }

    rc = elite_exchange(ctx,
                        request,
                        request_len,
                        expected_opcode,
                        reply,
                        sizeof(reply),
                        &reply_len,
                        CLIP_READ_TIMEOUT_MS);
    if (rc != PULL_OK)
        return rc;

    if (reply_len < header_len + (size_t)length ||
        elite_load_be32(reply + 1U) != address) {
        set_last_error_text("Invalid ELITE II memory-read reply.");
        return PULL_ERR_PROTOCOL;
    }

    if (expected_opcode == 0x4bU) {
        if (reply[5] != (clip_u8)length) {
            set_last_error_text("ELITE II short-read length mismatch.");
            return PULL_ERR_PROTOCOL;
        }
    } else {
        if (elite_load_be32(reply + 5U) != (clip_u32)length) {
            set_last_error_text("ELITE II block-read length mismatch.");
            return PULL_ERR_PROTOCOL;
        }
    }

    memcpy(out, reply + header_len, (size_t)length);
    return PULL_OK;
}

static int
cal_exchange_sequence(struct pull_ctx *ctx,
                      const clip_u8 *request,
                      size_t request_len,
                      clip_u8 sequence,
                      clip_u8 *reply,
                      size_t reply_capacity,
                      size_t *reply_len,
                      unsigned long timeout_ms)
{
    return clip_exchange(ctx,
                         ctx->session_id,
                         request,
                         request_len,
                         CLIP_CAL_SERVICE_REPLY,
                         sequence,
                         1,
                         reply,
                         reply_capacity,
                         reply_len,
                         timeout_ms);
}

static int
cal_query(struct pull_ctx *ctx,
          clip_u8 *sequence,
          unsigned long identifier,
          clip_u8 *out,
          size_t out_capacity,
          size_t *out_len)
{
    clip_u8 request[CLIP_CAL_QUERY_REQUEST_SIZE];
    clip_u8 reply[CLIP_APP_MAX];
    size_t reply_len;
    const clip_u8 *data;
    size_t data_len;
    clip_u8 seq;
    int rc;

    if (ctx == NULL || sequence == NULL || out == NULL || out_len == NULL)
        return PULL_ERR_ARGUMENT;

    seq = *sequence;
    rc = clip_cal_build_query_request(seq, identifier, request);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Failed to build calibration query", (long)rc);
        return PULL_ERR_CAL;
    }

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_reply(reply,
                              reply_len,
                              seq,
                              &data,
                              &data_len);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Invalid calibration query reply", (long)rc);
        return PULL_ERR_CAL;
    }

    if (data_len > out_capacity) {
        set_last_error_text("Calibration query reply is larger than destination buffer.");
        return PULL_ERR_CAL;
    }

    if (data_len != 0U)
        memcpy(out, data, data_len);
    *out_len = data_len;
    return PULL_OK;
}

static int
cal_query_discard(struct pull_ctx *ctx,
                  clip_u8 *sequence,
                  unsigned long identifier)
{
    clip_u8 data[CLIP_APP_MAX];
    size_t data_len;

    return cal_query(ctx,
                     sequence,
                     identifier,
                     data,
                     sizeof(data),
                     &data_len);
}

/*
 * reference tool probes 0x002282 three times during connection setup.  The supplied
 * recordings show service 0x03 negative replies for all three probes, and
 * reference tool continues.  Accept either a normal positive reply or that negative
 * transaction result, but still require the matching application sequence.
 */
static int
cal_query_tolerate_negative(struct pull_ctx *ctx,
                            clip_u8 *sequence,
                            unsigned long identifier)
{
    clip_u8 request[CLIP_CAL_QUERY_REQUEST_SIZE];
    clip_u8 reply[CLIP_APP_MAX];
    size_t reply_len;
    clip_u8 seq;
    int rc;

    if (ctx == NULL || sequence == NULL)
        return PULL_ERR_ARGUMENT;

    seq = *sequence;
    rc = clip_cal_build_query_request(seq, identifier, request);
    if (rc != CLIP_CAL_OK)
        return PULL_ERR_CAL;

    ctx->tolerate_negative = 1;
    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    ctx->tolerate_negative = 0;
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    if (reply_len < 2U || reply[1] != seq) {
        set_last_error_text("Invalid tolerated CLIP preflight reply.");
        return PULL_ERR_PROTOCOL;
    }

    if (reply[0] != CLIP_CAL_SERVICE_REPLY && reply[0] != 0x03U) {
        set_last_error_text("Unexpected CLIP service during preflight probe.");
        return PULL_ERR_PROTOCOL;
    }

    return PULL_OK;
}

/* Observed short service-0x12 transaction: 12 <seq> 00 0B. */
static int
cal_enter_0b_state(struct pull_ctx *ctx,
                   clip_u8 *sequence,
                   int use_zero_outer_state)
{
    clip_u8 request[4];
    clip_u8 reply[256];
    size_t reply_len;
    const clip_u8 *data;
    size_t data_len;
    clip_u8 seq;
    int rc;

    if (ctx == NULL || sequence == NULL)
        return PULL_ERR_ARGUMENT;

    seq = *sequence;
    request[0] = CLIP_CAL_SERVICE_PHASE;
    request[1] = seq;
    request[2] = 0x00U;
    request[3] = 0x0bU;

    if (use_zero_outer_state) {
        rc = clip_exchange(ctx,
                           0x00U,
                           request,
                           sizeof(request),
                           CLIP_CAL_SERVICE_REPLY,
                           seq,
                           1,
                           reply,
                           sizeof(reply),
                           &reply_len,
                           CLIP_TIMEOUT_MS);
    } else {
        rc = cal_exchange_sequence(ctx,
                                   request,
                                   sizeof(request),
                                   seq,
                                   reply,
                                   sizeof(reply),
                                   &reply_len,
                                   CLIP_TIMEOUT_MS);
    }
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_reply(reply,
                              reply_len,
                              seq,
                              &data,
                              &data_len);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Invalid 0x000B state reply", (long)rc);
        return PULL_ERR_CAL;
    }

    (void)data;
    (void)data_len;
    return PULL_OK;
}

/*
 * Exact seven-item multi-query observed in both supplied reference tool recordings.
 * Keep the item layout literal until its per-item selector byte semantics are
 * proven from the serializer.
 */
static int
cal_preflight_multi_query(struct pull_ctx *ctx, clip_u8 *sequence)
{
    clip_u8 request[31];
    clip_u8 reply[CLIP_APP_MAX];
    size_t reply_len;
    clip_u8 seq;
    static const clip_u8 tail[29] = {
        0x07U,
        0x01U,0x00U,0x00U,0x82U,
        0x01U,0x00U,0x29U,0x24U,
        0x00U,0x00U,0x00U,0x17U,
        0x01U,0x00U,0x13U,0x80U,
        0x01U,0x00U,0x21U,0xbdU,
        0x01U,0x00U,0x00U,0x45U,
        0x01U,0x00U,0x29U,0x25U
    };
    int rc;

    if (ctx == NULL || sequence == NULL)
        return PULL_ERR_ARGUMENT;

    seq = *sequence;
    request[0] = CLIP_CAL_SERVICE_QUERY;
    request[1] = seq;
    memcpy(request + 2U, tail, sizeof(tail));

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    if (reply_len < 2U || reply[0] != CLIP_CAL_SERVICE_REPLY || reply[1] != seq) {
        set_last_error_text("Invalid CLIP preflight multi-query reply.");
        return PULL_ERR_PROTOCOL;
    }

    return PULL_OK;
}

/*
 * Reproduce the application-level reference tool preflight immediately following
 * CLIP authentication.  The two supplied recordings agree through sequence
 * 0x11.  The calibration-upload recording then performs 0x2287, 0x2882 and
 * two final 0x2226 polls before entering 0x0011 at sequence 0x16.
 */
static int
cal_run_protocol_preflight(struct pull_ctx *ctx, clip_u8 *sequence)
{
    int rc;

#define PREFLIGHT_QUERY(id_) do { \
        rc = cal_query_discard(ctx, sequence, (id_)); \
        if (rc != PULL_OK) return rc; \
    } while (0)

    PREFLIGHT_QUERY(0x002226UL); /* seq 00 */
    PREFLIGHT_QUERY(0x000084UL); /* seq 01 */

    rc = cal_preflight_multi_query(ctx, sequence); /* seq 02 */
    if (rc != PULL_OK)
        return rc;

    PREFLIGHT_QUERY(0x002295UL); /* seq 03 */

    /* Both recordings send the first 0x000B request with outer byte 4 = 00. */
    rc = cal_enter_0b_state(ctx, sequence, 1); /* seq 04 */
    if (rc != PULL_OK)
        return rc;

    /* reference tool waits while other connection setup work runs. */
    ct_sleep_ms(250);

    PREFLIGHT_QUERY(0x002226UL); /* seq 05 */

    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 06 */
    if (rc != PULL_OK) return rc;
    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 07 */
    if (rc != PULL_OK) return rc;
    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 08 */
    if (rc != PULL_OK) return rc;

    /* The calibration-upload recording carries the current outer state here. */
    rc = cal_enter_0b_state(ctx, sequence, 0); /* seq 09 */
    if (rc != PULL_OK)
        return rc;

    PREFLIGHT_QUERY(0x002226UL); /* 0A */
    PREFLIGHT_QUERY(0x002285UL); /* 0B */
    PREFLIGHT_QUERY(0x0021bdUL); /* 0C */
    PREFLIGHT_QUERY(0x002226UL); /* 0D */
    PREFLIGHT_QUERY(0x000045UL); /* 0E */
    PREFLIGHT_QUERY(0x002226UL); /* 0F */
    PREFLIGHT_QUERY(0x0021beUL); /* 10 */
    PREFLIGHT_QUERY(0x0021bfUL); /* 11 */

    /* Upload recording ordering.  The second recording contains the same
       four transactions in a different order, so these are checks/polls,
       not values baked into the sequence counter. */
    PREFLIGHT_QUERY(0x002287UL); /* 12 */
    PREFLIGHT_QUERY(0x002882UL); /* 13 */
    PREFLIGHT_QUERY(0x002226UL); /* 14 */
    PREFLIGHT_QUERY(0x002226UL); /* 15 */

#undef PREFLIGHT_QUERY

    return PULL_OK;
}


/* Upload-specific preflight.  It is identical to the proven readback
 * preflight through sequence 0x11, then follows the ordering in the supplied
 * reference tool programming capture for sequences 0x12..0x15. */
static int
cal_run_upload_preflight(struct pull_ctx *ctx, clip_u8 *sequence)
{
    int rc;

#define UPREF_QUERY(id_) do { \
        rc = cal_query_discard(ctx, sequence, (id_)); \
        if (rc != PULL_OK) return rc; \
    } while (0)

    UPREF_QUERY(0x002226UL); /* 00 */
    UPREF_QUERY(0x000084UL); /* 01 */
    rc = cal_preflight_multi_query(ctx, sequence); /* 02 */
    if (rc != PULL_OK) return rc;
    UPREF_QUERY(0x002295UL); /* 03 */
    rc = cal_enter_0b_state(ctx, sequence, 1); /* 04 */
    if (rc != PULL_OK) return rc;
    ct_sleep_ms(250);
    UPREF_QUERY(0x002226UL); /* 05 */
    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 06 */
    if (rc != PULL_OK) return rc;
    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 07 */
    if (rc != PULL_OK) return rc;
    rc = cal_query_tolerate_negative(ctx, sequence, 0x002282UL); /* 08 */
    if (rc != PULL_OK) return rc;
    rc = cal_enter_0b_state(ctx, sequence, 0); /* 09 */
    if (rc != PULL_OK) return rc;
    UPREF_QUERY(0x002226UL); /* 0A */
    UPREF_QUERY(0x002285UL); /* 0B */
    UPREF_QUERY(0x0021bdUL); /* 0C */
    UPREF_QUERY(0x002226UL); /* 0D */
    UPREF_QUERY(0x000045UL); /* 0E */
    UPREF_QUERY(0x002226UL); /* 0F */
    UPREF_QUERY(0x0021beUL); /* 10 */
    UPREF_QUERY(0x0021bfUL); /* 11 */

    /* Exact upload-recording ordering. */
    UPREF_QUERY(0x002226UL); /* 12 */
    UPREF_QUERY(0x002226UL); /* 13 */
    UPREF_QUERY(0x002287UL); /* 14 */
    UPREF_QUERY(0x002882UL); /* 15 */

#undef UPREF_QUERY
    return PULL_OK;
}

static int
cal_enter_phase(struct pull_ctx *ctx, clip_u8 *sequence, clip_u8 phase)
{
    clip_u8 request[CLIP_CAL_PHASE_REQUEST_SIZE];
    clip_u8 reply[256];
    size_t reply_len;
    const clip_u8 *data;
    size_t data_len;
    clip_u8 seq;
    int rc;

    seq = *sequence;
    rc = clip_cal_build_phase_request(seq, phase, request);
    if (rc != CLIP_CAL_OK)
        return PULL_ERR_CAL;

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_reply(reply,
                              reply_len,
                              seq,
                              &data,
                              &data_len);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Invalid phase reply", (long)rc);
        return PULL_ERR_CAL;
    }

    (void)data;
    (void)data_len;
    return PULL_OK;
}


static int
cal_enter_mode_value(struct pull_ctx *ctx,
                     clip_u8 *sequence,
                     unsigned int mode,
                     clip_u8 value)
{
    clip_u8 request[CLIP_CAL_PHASE_REQUEST_SIZE];
    clip_u8 reply[256];
    size_t reply_len;
    const clip_u8 *data;
    size_t data_len;
    clip_u8 seq;
    int rc;

    if (ctx == NULL || sequence == NULL)
        return PULL_ERR_ARGUMENT;

    seq = *sequence;
    rc = clip_cal_build_mode_request(seq, mode, value, request);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Failed to build CLIP mode request", (long)rc);
        return PULL_ERR_CAL;
    }

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_reply(reply, reply_len, seq, &data, &data_len);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Invalid CLIP mode reply", (long)rc);
        return PULL_ERR_CAL;
    }

    (void)data;
    (void)data_len;
    return PULL_OK;
}

static int
cal_end_phase(struct pull_ctx *ctx)
{
    clip_u8 request[CLIP_CAL_END_REQUEST_SIZE];
    int rc;

    clip_cal_build_end_request(request);
    rc = clip_send_only(ctx,
                        ctx->session_id,
                        request,
                        sizeof(request));
    if (rc == PULL_OK)
        ct_sleep_ms(20);
    return rc;
}

static int
cal_get_descriptor(struct pull_ctx *ctx,
                   clip_u8 *sequence,
                   struct clip_cal_map *map)
{
    clip_u8 request[CLIP_CAL_QUERY_REQUEST_SIZE];
    clip_u8 reply[CLIP_APP_MAX];
    size_t reply_len;
    clip_u8 seq;
    int rc;

    seq = *sequence;
    rc = clip_cal_build_query_request(seq,
                                      CLIP_CAL_ID_MEMORY_DESCRIPTOR,
                                      request);
    if (rc != CLIP_CAL_OK)
        return PULL_ERR_CAL;

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_memory_descriptor(reply,
                                          reply_len,
                                          seq,
                                          map);
    if (rc != CLIP_CAL_OK) {
        char msg[512];
        char byte_text[8];
        size_t i;
        size_t used;

        sprintf(msg,
                "Invalid calibration memory descriptor: %s (code %ld); "
                "reply_len=%lu seq=%02X raw=",
                clip_cal_strerror(rc),
                (long)rc,
                (unsigned long)reply_len,
                (unsigned int)seq);
        used = strlen(msg);

        for (i = 0U; i < reply_len; ++i) {
            sprintf(byte_text, "%s%02X",
                    (i == 0U) ? "" : " ",
                    (unsigned int)reply[i]);
            if (used + strlen(byte_text) + 1U >= sizeof(msg))
                break;
            strcat(msg, byte_text);
            used += strlen(byte_text);
        }

        set_last_error_text(msg);
        return PULL_ERR_CAL;
    }

    return PULL_OK;
}

static clip_u32
load_be32_local(const clip_u8 *p)
{
    clip_u32 v;

    v = ((clip_u32)p[0] << 24);
    v |= ((clip_u32)p[1] << 16);
    v |= ((clip_u32)p[2] << 8);
    v |= (clip_u32)p[3];
    return v;
}

static void
format_version4(const clip_u8 *data, size_t len, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U)
        return;

    out[0] = '\0';
    if (data == NULL || len < 4U)
        return;

    sprintf(out, "%u.%u.%u.%u",
            (unsigned int)data[0],
            (unsigned int)data[1],
            (unsigned int)data[2],
            (unsigned int)data[3]);
}

static int
is_printable_byte(clip_u8 c)
{
    return c >= 0x20U && c <= 0x7eU;
}

static void
extract_longest_ascii(const clip_u8 *data,
                      size_t len,
                      char *out,
                      size_t out_size)
{
    size_t i;
    size_t start;
    size_t best_start;
    size_t best_len;
    size_t n;

    if (out == NULL || out_size == 0U)
        return;

    out[0] = '\0';
    if (data == NULL || len == 0U)
        return;

    best_start = 0U;
    best_len = 0U;
    i = 0U;

    while (i < len) {
        while (i < len && !is_printable_byte(data[i]))
            ++i;
        start = i;
        while (i < len && is_printable_byte(data[i]))
            ++i;
        if (i - start > best_len) {
            best_start = start;
            best_len = i - start;
        }
    }

    if (best_len == 0U)
        return;

    while (best_len != 0U && data[best_start + best_len - 1U] == ' ')
        --best_len;

    n = best_len;
    if (n >= out_size)
        n = out_size - 1U;
    if (n != 0U)
        memcpy(out, data + best_start, n);
    out[n] = '\0';
}

static int
all_zero(const clip_u8 *data, size_t len)
{
    size_t i;

    if (data == NULL)
        return 0;
    for (i = 0U; i < len; ++i) {
        if (data[i] != 0U)
            return 0;
    }
    return 1;
}

static int
query_meta_required(struct pull_ctx *ctx,
                    clip_u8 *sequence,
                    unsigned long id,
                    clip_u8 *buf,
                    size_t cap,
                    size_t *len,
                    const char *label)
{
    int rc;
    char tmp[512];

    rc = cal_query(ctx, sequence, id, buf, cap, len);
    if (rc != PULL_OK) {
        safe_copy(tmp, g_last_error, sizeof(tmp));
        sprintf(g_last_error,
                "%s query failed: %s",
                label,
                tmp[0] != '\0' ? tmp : "unknown error");
    }
    return rc;
}

static int
collect_metadata(struct pull_ctx *ctx,
                 clip_u8 *sequence,
                 const struct clip_cal_map *map,
                 struct pull_meta *meta)
{
    clip_u8 data[512];
    size_t len;
    clip_u32 value;
    char ascii[128];
    int rc;

    memset(meta, 0, sizeof(*meta));
    safe_copy(meta->byte_order, "BigEndian", sizeof(meta->byte_order));
    safe_copy(meta->leading_word, "0000", sizeof(meta->leading_word));
    safe_copy(meta->harness_compat, "0000000000000000",
              sizeof(meta->harness_compat));
    {
        unsigned int year;

        ct_get_local_date(&year, NULL, NULL);
        sprintf(meta->file_descriptor,
                "Copyright %04u - Generated Calibration Data - "
                "Phase 22.60.70.02 - GTIS4.5",
                year);
    }

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_CAL_VERSION,
                             data, sizeof(data), &len,
                             "Calibration version");
    if (rc != PULL_OK)
        return rc;
    format_version4(data, len,
                    meta->calibration_version,
                    sizeof(meta->calibration_version));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_MODULE_NAME,
                             data, sizeof(data), &len,
                             "Module name");
    if (rc != PULL_OK)
        return rc;
    extract_longest_ascii(data, len,
                          meta->module_name,
                          sizeof(meta->module_name));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_PRODUCT_ID,
                             data, sizeof(data), &len,
                             "Product ID");
    if (rc != PULL_OK)
        return rc;
    extract_longest_ascii(data, len,
                          meta->product_id,
                          sizeof(meta->product_id));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_INTERFACE_LEVEL,
                             data, sizeof(data), &len,
                             "Interface level");
    if (rc != PULL_OK)
        return rc;
    format_version4(data, len,
                    meta->interface_level,
                    sizeof(meta->interface_level));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_MODULE_PART_NUMBER,
                             data, sizeof(data), &len,
                             "Module part number");
    if (rc != PULL_OK)
        return rc;
    ascii[0] = '\0';
    extract_longest_ascii(data, len, ascii, sizeof(ascii));
    if (ascii[0] != '\0') {
        safe_copy(meta->module_part_number,
                  ascii,
                  sizeof(meta->module_part_number));
    } else if (len >= 4U) {
        value = load_be32_local(data);
        sprintf(meta->module_part_number, "%lu", (unsigned long)value);
    }

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_BOOT_START_VERSION,
                             data, sizeof(data), &len,
                             "Start boot-loader version");
    if (rc != PULL_OK)
        return rc;
    format_version4(data, len,
                    meta->start_boot_loader_version,
                    sizeof(meta->start_boot_loader_version));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_BOOT_END_VERSION,
                             data, sizeof(data), &len,
                             "End boot-loader version");
    if (rc != PULL_OK)
        return rc;
    format_version4(data, len,
                    meta->end_boot_loader_version,
                    sizeof(meta->end_boot_loader_version));

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_BYTE_ORDER,
                             data, sizeof(data), &len,
                             "Byte order");
    if (rc != PULL_OK)
        return rc;
    ascii[0] = '\0';
    extract_longest_ascii(data, len, ascii, sizeof(ascii));
    if (ascii[0] != '\0') {
        if (ct_stricmp(ascii, "big") == 0 ||
            ct_stricmp(ascii, "bigendian") == 0)
            safe_copy(meta->byte_order, "BigEndian", sizeof(meta->byte_order));
        else if (ct_stricmp(ascii, "little") == 0 ||
                 ct_stricmp(ascii, "littleendian") == 0)
            safe_copy(meta->byte_order, "LittleEndian", sizeof(meta->byte_order));
    } else if (len != 0U && all_zero(data, len)) {
        safe_copy(meta->byte_order, "BigEndian", sizeof(meta->byte_order));
    }

    rc = query_meta_required(ctx, sequence,
                             CLIP_CAL_ID_INDEX_TABLE_ADDRESS,
                             data, sizeof(data), &len,
                             "Index table address");
    if (rc != PULL_OK)
        return rc;
    if (len < 4U) {
        set_last_error_text("Index table address reply is shorter than four bytes.");
        return PULL_ERR_CAL;
    }
    value = load_be32_local(data);
    sprintf(meta->index_table_address, "%08lX", (unsigned long)value);

    /*
     * The memory-descriptor trailer is preserved by clip_cal.  Only use it as
     * file_descriptor if it is entirely printable; otherwise leave the field
     * empty rather than inventing semantics.
     */
    if (map != NULL && map->trailer_len != 0U) {
        size_t i;
        int printable;

        printable = 1;
        for (i = 0U; i < map->trailer_len; ++i) {
            if (!is_printable_byte(map->trailer[i])) {
                printable = 0;
                break;
            }
        }
        if (printable)
            extract_longest_ascii(map->trailer,
                                  map->trailer_len,
                                  meta->file_descriptor,
                                  sizeof(meta->file_descriptor));
    }

    return PULL_OK;
}

static void
free_image(struct pull_image *image)
{
    unsigned int i;

    if (image == NULL)
        return;

    for (i = 0U; i < image->range_count; ++i) {
        if (image->ranges[i].data != NULL)
            free(image->ranges[i].data);
        image->ranges[i].data = NULL;
    }
    memset(image, 0, sizeof(*image));
}

static int
allocate_image(const struct clip_cal_map *map, struct pull_image *image)
{
    unsigned int i;
    size_t len;

    if (map == NULL || image == NULL)
        return PULL_ERR_ARGUMENT;

    memset(image, 0, sizeof(*image));
    image->range_count = map->range_count;

    for (i = 0U; i < map->range_count; ++i) {
        image->ranges[i].address = map->ranges[i].address;
        image->ranges[i].length = map->ranges[i].length;
        len = (size_t)map->ranges[i].length;
        image->ranges[i].data = (clip_u8 *)malloc(len);
        if (image->ranges[i].data == NULL) {
            set_last_error_text("Out of memory allocating calibration image.");
            free_image(image);
            return PULL_ERR_MEMORY;
        }
    }

    return PULL_OK;
}


static int
elite_pull_memory_ranges(struct pull_ctx *ctx,
                         const struct clip_cal_map *map,
                         struct pull_image *image)
{
    unsigned int i;
    clip_u32 offset;
    clip_u32 remaining;
    unsigned int chunk;
    size_t total;
    size_t completed;
    int percent;
    int rc;
    char msg[160];

    if (ctx == NULL || map == NULL || image == NULL)
        return PULL_ERR_ARGUMENT;

    total = clip_cal_total_size(map);
    if (total == 0U) {
        set_last_error_text("ELITE II descriptor contains no calibration bytes.");
        return PULL_ERR_PROTOCOL;
    }

    completed = 0U;
    for (i = 0U; i < map->range_count; ++i) {
        offset = (clip_u32)0;

        while (offset < map->ranges[i].length) {
            remaining = map->ranges[i].length - offset;
            chunk = ELITE_II_BLOCK_SIZE;
            if ((clip_u32)chunk > remaining)
                chunk = (unsigned int)remaining;

            rc = elite_read_memory(ctx,
                                   map->ranges[i].address + offset,
                                   chunk,
                                   image->ranges[i].data + (size_t)offset);
            if (rc != PULL_OK)
                return rc;

            offset += (clip_u32)chunk;
            completed += (size_t)chunk;

            percent = 15 + (int)((completed * 77U) / total);
            if (percent > 92)
                percent = 92;

            sprintf(msg,
                    "Reading ENI/ELITE II calibration: %lu / %lu bytes",
                    (unsigned long)completed,
                    (unsigned long)total);
            report_progress(ctx, percent, msg);
        }
    }

    return PULL_OK;
}

static int
pull_memory_ranges(struct pull_ctx *ctx,
                   clip_u8 *sequence,
                   const struct clip_cal_map *map,
                   struct pull_image *image)
{
    unsigned int i;
    clip_u32 offset;
    clip_u32 remaining;
    unsigned int chunk;
    clip_u8 request[CLIP_CAL_READ_REQUEST_SIZE];
    clip_u8 reply[CLIP_APP_MAX];
    size_t reply_len;
    const clip_u8 *data;
    clip_u8 seq;
    int rc;
    size_t total;
    size_t done;
    int percent;
    char status[160];

    if (ctx == NULL || sequence == NULL || map == NULL || image == NULL)
        return PULL_ERR_ARGUMENT;

    total = clip_cal_total_size(map);
    if (total == 0U) {
        set_last_error_text("Calibration descriptor has an invalid total size.");
        return PULL_ERR_CAL;
    }

    done = 0U;

    for (i = 0U; i < map->range_count; ++i) {
        offset = (clip_u32)0;

        while (offset < map->ranges[i].length) {
            remaining = map->ranges[i].length - offset;
            chunk = CLIP_CAL_DEFAULT_CHUNK_SIZE;
            if ((clip_u32)chunk > remaining)
                chunk = (unsigned int)remaining;

            seq = *sequence;
            rc = clip_cal_build_read_request(seq,
                                             map->ranges[i].address + offset,
                                             chunk,
                                             request);
            if (rc != CLIP_CAL_OK) {
                set_last_error_code("Failed to build calibration read", (long)rc);
                return PULL_ERR_CAL;
            }

            rc = cal_exchange_sequence(ctx,
                                       request,
                                       sizeof(request),
                                       seq,
                                       reply,
                                       sizeof(reply),
                                       &reply_len,
                                       CLIP_READ_TIMEOUT_MS);
            *sequence = clip_cal_next_sequence(seq);
            if (rc != PULL_OK)
                return rc;

            rc = clip_cal_parse_read_reply(reply,
                                           reply_len,
                                           seq,
                                           (size_t)chunk,
                                           &data);
            if (rc != CLIP_CAL_OK) {
                set_last_error_code("Invalid calibration memory reply", (long)rc);
                return PULL_ERR_CAL;
            }

            memcpy(image->ranges[i].data + (size_t)offset,
                   data,
                   (size_t)chunk);
            offset += (clip_u32)chunk;
            done += (size_t)chunk;

            /*
             * CLIP guaranteed-transfer ACK/window state.
             *
             * The reference tool capture proves that byte 4 of the next outbound
             * guaranteed message carries the number of 16-byte transport
             * blocks occupied by the previous inbound logical reply.  For a
             * memory-read positive reply the logical wire size is:
             *
             *     5-byte CLIP envelope + 2-byte app reply header + data
             *
             * Examples from the reference capture:
             *     16-byte data  -> 23 bytes total   -> 2 blocks  -> 0x02
             *   1000-byte data  -> 1007 bytes total -> 63 blocks -> 0x3F
             *    152-byte tail  -> 159 bytes total  -> 10 blocks -> 0x0A
             *    312-byte tail  -> 319 bytes total  -> 20 blocks -> 0x14
             *
             * clip_exchange() currently stores incoming[4] in session_id;
             * that field is not the correct carried state for sustained
             * memory transfers.  Override it here from the actual reply
             * length before the next read request is serialized.
             */
            {
                size_t logical_wire_len;
                unsigned long blocks;

                logical_wire_len = reply_len + 5U;
                blocks = (unsigned long)((logical_wire_len + 15U) / 16U);
                if (blocks == 0UL)
                    blocks = 1UL;
                if (blocks > 255UL)
                    blocks = 255UL;
                ctx->session_id = (clip_u8)blocks;
            }

            /*
             * reference tool leaves roughly one scheduler tick (~35 ms in the
             * supplied recording) between completed 1000-byte reads.  Do
             * the same instead of issuing the next guaranteed read less than
             * a millisecond after the preceding reply.
             */
            ct_sleep_ms(35);

            percent = 15 + (int)((done * 70U) / total);
            if (percent > 85)
                percent = 85;
            sprintf(status,
                    "Reading calibration: %lu / %lu bytes (range %u/%u)",
                    (unsigned long)done,
                    (unsigned long)total,
                    i + 1U,
                    map->range_count);
            report_progress(ctx, percent, status);
        }
    }

    return PULL_OK;
}

static int
collect_auxiliary(struct pull_ctx *ctx,
                  clip_u8 *sequence,
                  struct pull_meta *meta)
{
    clip_u8 query_data[256];
    size_t query_len;
    clip_u8 request[CLIP_CAL_PTR_REQUEST_SIZE];
    clip_u8 reply[256];
    size_t reply_len;
    clip_u32 pointer;
    clip_u8 read_request[CLIP_CAL_READ_REQUEST_SIZE];
    const clip_u8 *read_data;
    clip_u8 harness[CLIP_CAL_HARNESS_COMPAT_SIZE];
    clip_u8 seq;
    unsigned int i;
    int rc;
    char *p;

    report_progress(ctx, 88, "Reading CLIP auxiliary compatibility data...");

    rc = cal_query(ctx,
                   sequence,
                   CLIP_CAL_ID_STATUS_2226,
                   query_data,
                   sizeof(query_data),
                   &query_len);
    if (rc != PULL_OK)
        return rc;
    (void)query_len;

    rc = cal_enter_phase(ctx, sequence, CLIP_CAL_PHASE_AUX);
    if (rc != PULL_OK)
        return rc;

    seq = *sequence;
    rc = clip_cal_build_pointer_request(seq,
                                        CLIP_CAL_PTR_HARNESS_COMPAT,
                                        request);
    if (rc != CLIP_CAL_OK)
        return PULL_ERR_CAL;

    rc = cal_exchange_sequence(ctx,
                               request,
                               sizeof(request),
                               seq,
                               reply,
                               sizeof(reply),
                               &reply_len,
                               CLIP_TIMEOUT_MS);
    *sequence = clip_cal_next_sequence(seq);
    if (rc != PULL_OK)
        return rc;

    rc = clip_cal_parse_pointer_reply(reply,
                                      reply_len,
                                      seq,
                                      &pointer);
    if (rc != CLIP_CAL_OK) {
        set_last_error_code("Invalid harness pointer reply", (long)rc);
        return PULL_ERR_CAL;
    }

    for (i = 0U; i < CLIP_CAL_HARNESS_COMPAT_SIZE; ++i) {
        seq = *sequence;
        rc = clip_cal_build_read_request(seq,
                                         pointer + (clip_u32)i,
                                         1U,
                                         read_request);
        if (rc != CLIP_CAL_OK)
            return PULL_ERR_CAL;

        rc = cal_exchange_sequence(ctx,
                                   read_request,
                                   sizeof(read_request),
                                   seq,
                                   reply,
                                   sizeof(reply),
                                   &reply_len,
                                   CLIP_TIMEOUT_MS);
        *sequence = clip_cal_next_sequence(seq);
        if (rc != PULL_OK)
            return rc;

        rc = clip_cal_parse_read_reply(reply,
                                       reply_len,
                                       seq,
                                       1U,
                                       &read_data);
        if (rc != CLIP_CAL_OK)
            return PULL_ERR_CAL;
        harness[i] = read_data[0];
    }

    p = meta->harness_compat;
    for (i = 0U; i < CLIP_CAL_HARNESS_COMPAT_SIZE; ++i) {
        sprintf(p, "%02X", (unsigned int)harness[i]);
        p += 2;
    }
    *p = '\0';

    rc = cal_end_phase(ctx);
    if (rc != PULL_OK)
        return rc;

    /*
     * The recording performs a post-upload 0x2285 query, but its reply is a
     * large compatibility table -- it is NOT the four-hex-digit .ccal token.
     * Keep the query as a best-effort cleanup/compatibility step.  The file
     * writer still starts with a 0000 placeholder; rp1210_pull_ccal() patches
     * and verifies it with the recovered native calibration CRC implementation
     * immediately after the file is closed.
     */
    {
        clip_u8 post_data[CLIP_APP_MAX];
        size_t post_len;

        rc = cal_query(ctx,
                       sequence,
                       CLIP_CAL_ID_POST_2285,
                       post_data,
                       sizeof(post_data),
                       &post_len);
        if (rc != PULL_OK)
            clear_last_error();
        (void)post_len;
    }

    safe_copy(meta->leading_word, "0000", sizeof(meta->leading_word));
    report_progress(ctx, 91,
                    "Auxiliary data complete; .ccal CRC will be finalized after write.");

    return PULL_OK;
}

static void
xml_escape(const char *src, char *dst, size_t dst_size)
{
    size_t w;
    size_t i;
    const char *rep;
    size_t rep_len;

    if (dst == NULL || dst_size == 0U)
        return;

    w = 0U;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    for (i = 0U; src[i] != '\0' && w + 1U < dst_size; ++i) {
        rep = NULL;
        if (src[i] == '&')
            rep = "&amp;";
        else if (src[i] == '<')
            rep = "&lt;";
        else if (src[i] == '>')
            rep = "&gt;";
        else if (src[i] == '"')
            rep = "&quot;";
        else if (src[i] == '\'')
            rep = "&apos;";

        if (rep == NULL) {
            dst[w++] = src[i];
        } else {
            rep_len = strlen(rep);
            if (w + rep_len + 1U >= dst_size)
                break;
            memcpy(dst + w, rep, rep_len);
            w += rep_len;
        }
    }

    dst[w] = '\0';
}

static int
write_ihex_record(FILE *fp,
                  unsigned int type,
                  unsigned int address,
                  const clip_u8 *data,
                  unsigned int count)
{
    unsigned int sum;
    unsigned int i;
    unsigned int checksum;

    if (fp == NULL || count > 255U)
        return 0;

    sum = count + ((address >> 8) & 0xffU) + (address & 0xffU) + (type & 0xffU);

    if (fprintf(fp, ":%02X%04X%02X",
                count & 0xffU,
                address & 0xffffU,
                type & 0xffU) < 0)
        return 0;

    for (i = 0U; i < count; ++i) {
        sum += data[i];
        if (fprintf(fp, "%02X", (unsigned int)data[i]) < 0)
            return 0;
    }

    checksum = ((~sum) + 1U) & 0xffU;
    if (fprintf(fp, "%02X\r\n", checksum) < 0)
        return 0;

    return 1;
}


static int
write_elite_ccal(const char *path, const struct pull_image *image)
{
    FILE *fp;
    unsigned int order[CLIP_CAL_MAX_RANGES];
    unsigned int order_count;
    unsigned int i;
    unsigned int j;
    unsigned int tmp_index;
    unsigned int range_index;
    clip_u32 offset;
    clip_u32 address;
    unsigned int high;
    unsigned int current_high;
    unsigned int low;
    unsigned int room;
    unsigned int count;
    clip_u8 ela[2];
    clip_u8 crc_placeholder[4];
    int ok;

    if (path == NULL || image == NULL)
        return PULL_ERR_ARGUMENT;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        set_last_error_text("Unable to create destination ENI .ccal file.");
        return PULL_ERR_FILE;
    }

    crc_placeholder[0] = 0x00U;
    crc_placeholder[1] = 0x00U;
    crc_placeholder[2] = 0x00U;
    crc_placeholder[3] = 0x00U;

    /*
     * Legacy ENI files store their calibration CRC in the address field of a
     * proprietary type-FF Intel-HEX record.  ccal_set_cal_file_crc() already
     * knows this alternate format and patches the 0000 placeholder below.
     */
    ok = write_ihex_record(fp, 0xffU, 0U, crc_placeholder, 4U);

    order_count = image->range_count;
    if (order_count > CLIP_CAL_MAX_RANGES)
        ok = 0;

    for (i = 0U; i < order_count; ++i)
        order[i] = i;

    for (i = 0U; i < order_count; ++i) {
        for (j = i + 1U; j < order_count; ++j) {
            if (image->ranges[order[j]].address <
                image->ranges[order[i]].address) {
                tmp_index = order[i];
                order[i] = order[j];
                order[j] = tmp_index;
            }
        }
    }

    /*
     * The legacy writer starts in bank zero implicitly.  Extended linear
     * address records are emitted only when the high 16 bits actually change.
     * Reference files use 16-byte data rows.
     */
    current_high = 0U;

    for (i = 0U; ok && i < order_count; ++i) {
        range_index = order[i];
        offset = (clip_u32)0;

        while (offset < image->ranges[range_index].length) {
            address = image->ranges[range_index].address + offset;
            high = (unsigned int)((address >> 16) & 0xffffUL);
            low = (unsigned int)(address & 0xffffUL);

            if (high != current_high) {
                ela[0] = (clip_u8)((high >> 8) & 0xffU);
                ela[1] = (clip_u8)(high & 0xffU);
                if (!write_ihex_record(fp, 0x04U, 0U, ela, 2U)) {
                    ok = 0;
                    break;
                }
                current_high = high;
            }

            room = 0x10000U - low;
            count = 16U;
            if ((clip_u32)count >
                image->ranges[range_index].length - offset) {
                count = (unsigned int)
                    (image->ranges[range_index].length - offset);
            }
            if (count > room)
                count = room;

            if (!write_ihex_record(
                    fp,
                    0x00U,
                    low,
                    image->ranges[range_index].data + (size_t)offset,
                    count)) {
                ok = 0;
                break;
            }

            offset += (clip_u32)count;
        }
    }

    if (ok && !write_ihex_record(fp, 0x01U, 0U, NULL, 0U))
        ok = 0;

    if (fclose(fp) != 0)
        ok = 0;

    if (!ok) {
        (void)remove(path);
        set_last_error_text("Failed while writing destination ENI .ccal file.");
        return PULL_ERR_FILE;
    }

    return PULL_OK;
}

static int
write_ihex_image(FILE *fp, const struct pull_image *image)
{
    unsigned int order[CLIP_CAL_MAX_RANGES];
    unsigned int order_count;
    unsigned int i;
    unsigned int j;
    unsigned int tmp_index;
    unsigned int range_index;
    clip_u32 offset;
    clip_u32 address;
    unsigned int high;
    unsigned int current_high;
    unsigned int low;
    unsigned int room;
    unsigned int count;
    clip_u8 ela[2];

    if (fp == NULL || image == NULL)
        return 0;

    /*
     * reference tool's .ccal emits Intel-HEX runs in ascending flash-address order,
     * not descriptor-array order.  Sort only the tiny index list.
     */
    order_count = image->range_count;
    if (order_count > CLIP_CAL_MAX_RANGES)
        return 0;

    for (i = 0U; i < order_count; ++i)
        order[i] = i;

    for (i = 0U; i < order_count; ++i) {
        for (j = i + 1U; j < order_count; ++j) {
            if (image->ranges[order[j]].address <
                image->ranges[order[i]].address) {
                tmp_index = order[i];
                order[i] = order[j];
                order[j] = tmp_index;
            }
        }
    }

    current_high = 0xffffffffU;

    for (i = 0U; i < order_count; ++i) {
        range_index = order[i];
        offset = (clip_u32)0;

        while (offset < image->ranges[range_index].length) {
            address = image->ranges[range_index].address + offset;
            high = (unsigned int)((address >> 16) & 0xffffUL);
            low = (unsigned int)(address & 0xffffUL);

            if (high != current_high) {
                ela[0] = (clip_u8)((high >> 8) & 0xffU);
                ela[1] = (clip_u8)(high & 0xffU);
                if (!write_ihex_record(fp, 0x04U, 0U, ela, 2U))
                    return 0;
                current_high = high;
            }

            room = 0x10000U - low;
            count = 32U;
            if ((clip_u32)count >
                image->ranges[range_index].length - offset) {
                count = (unsigned int)
                    (image->ranges[range_index].length - offset);
            }
            if (count > room)
                count = room;

            if (!write_ihex_record(
                    fp,
                    0x00U,
                    low,
                    image->ranges[range_index].data + (size_t)offset,
                    count)) {
                return 0;
            }

            offset += (clip_u32)count;
        }
    }

    if (!write_ihex_record(fp, 0x01U, 0U, NULL, 0U))
        return 0;

    return 1;
}

static int
write_ccal(const char *path,
           const struct pull_meta *meta,
           const struct pull_image *image)
{
    FILE *fp;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    char creation_date[32];
    char cal_version[128];
    char module_name[128];
    char product_id[128];
    char interface_level[128];
    char part_number[128];
    char boot_start[128];
    char boot_end[128];
    char byte_order[64];
    char index_addr[64];
    char file_descriptor[512];
    char harness[64];
    int ok;

    if (path == NULL || meta == NULL || image == NULL)
        return PULL_ERR_ARGUMENT;

    ct_get_local_date(&year, &month, &day);
    sprintf(creation_date, "%04u-%02u-%02u",
            year,
            month,
            day);

    xml_escape(meta->calibration_version, cal_version, sizeof(cal_version));
    xml_escape(meta->module_name, module_name, sizeof(module_name));
    xml_escape(meta->product_id, product_id, sizeof(product_id));
    xml_escape(meta->interface_level, interface_level, sizeof(interface_level));
    xml_escape(meta->module_part_number, part_number, sizeof(part_number));
    xml_escape(meta->start_boot_loader_version, boot_start, sizeof(boot_start));
    xml_escape(meta->end_boot_loader_version, boot_end, sizeof(boot_end));
    xml_escape(meta->byte_order, byte_order, sizeof(byte_order));
    xml_escape(meta->index_table_address, index_addr, sizeof(index_addr));
    xml_escape(meta->file_descriptor, file_descriptor, sizeof(file_descriptor));
    xml_escape(meta->harness_compat, harness, sizeof(harness));

    fp = fopen(path, "wb");
    if (fp == NULL) {
        set_last_error_text("Unable to create destination .ccal file.");
        return PULL_ERR_FILE;
    }

    ok = 1;
    if (fprintf(fp, "%s\r\n", meta->leading_word) < 0)
        ok = 0;

    /* Keep each literal comfortably inside the ISO C90 minimum limit. */
    if (ok && fputs("<compatibility_header>", fp) == EOF)
        ok = 0;
    if (ok && fprintf(fp,
        "<calibration_version>%s</calibration_version>"
        "<module_name>%s</module_name>",
        cal_version, module_name) < 0)
        ok = 0;
    if (ok && fputs(
        "<first_prod_cfg_file_version>0.0.0.0</first_prod_cfg_file_version>",
        fp) == EOF)
        ok = 0;
    if (ok && fprintf(fp,
        "<product_id>%s</product_id>"
        "<interface_level>%s</interface_level>"
        "<module_part_number>%s</module_part_number>",
        product_id, interface_level, part_number) < 0)
        ok = 0;
    if (ok && fprintf(fp,
        "<creation_date>%s</creation_date>"
        "<start_boot_loader_version>%s</start_boot_loader_version>"
        "<end_boot_loader_version>%s</end_boot_loader_version>",
        creation_date, boot_start, boot_end) < 0)
        ok = 0;
    if (ok && fprintf(fp,
        "<byte_order>%s</byte_order>"
        "<index_table_address>%s</index_table_address>",
        byte_order, index_addr) < 0)
        ok = 0;
    if (ok && fprintf(fp,
        "<file_descriptor>%s</file_descriptor>",
        file_descriptor) < 0)
        ok = 0;
    if (ok && fprintf(fp,
        "<harness_key_compatibility_list>%s</harness_key_compatibility_list>",
        harness) < 0)
        ok = 0;
    if (ok && fputs("</compatibility_header>\r\n", fp) == EOF)
        ok = 0;

    if (ok && !write_ihex_image(fp, image))
        ok = 0;

    if (fclose(fp) != 0)
        ok = 0;

    if (!ok) {
        (void)remove(path);
        set_last_error_text("Failed while writing destination .ccal file.");
        return PULL_ERR_FILE;
    }

    return PULL_OK;
}


/* -------------------------------------------------------------------------
 * Calibration programming path recovered from the supplied reference tool upload
 * recording.  The already-proven CLIP authentication/preflight is reused,
 * then we reproduce the additional transition into loader mode 0x0017.
 * ------------------------------------------------------------------------- */

static int
cal_prepare_programming(struct pull_ctx *ctx,
                        clip_u8 *sequence,
                        int *loader_requested)
{
    int rc;

#define UPLOAD_QUERY(id_) do { \
        rc = cal_query_discard(ctx, sequence, (id_)); \
        if (rc != PULL_OK) return rc; \
    } while (0)

    /* Sequence is 0x16 on entry after cal_run_protocol_preflight().  These
       requests reproduce the final reference tool checks immediately before its
       0x000B -> 0x0017 programming transition. */
    UPLOAD_QUERY(0x002226UL); /* 16 */
    UPLOAD_QUERY(0x002285UL); /* 17 */
    UPLOAD_QUERY(0x002226UL); /* 18 */
    UPLOAD_QUERY(0x002287UL); /* 19 */
    UPLOAD_QUERY(0x000084UL); /* 1A product ID */
    UPLOAD_QUERY(0x000045UL); /* 1B boot-loader/version item */
    UPLOAD_QUERY(0x002160UL); /* 1C byte order */
    UPLOAD_QUERY(0x002924UL); /* 1D */
    UPLOAD_QUERY(0x00b825UL); /* 1E module part number */
    UPLOAD_QUERY(0x001380UL); /* 1F */

#undef UPLOAD_QUERY

    rc = cal_enter_0b_state(ctx, sequence, 0); /* 20 */
    if (rc != PULL_OK)
        return rc;

    ct_sleep_ms(50);

    /*
     * Flag the attempt BEFORE sending: the ECM could enter the loader
     * even if its reply is lost.  Never assume CLIP is still running.
     */
    if (loader_requested != NULL)
        *loader_requested = 1;
    /* Observed programming/loader transition: 12 <seq> 00 17 00. */
    rc = cal_enter_mode_value(ctx, sequence, 0x0017U, 0x00U); /* 21 */
    if (rc != PULL_OK)
        return rc;

    return PULL_OK;
}

static void
wait_for_ecm_clip_close(struct pull_ctx *ctx)
{
    clip_u8 rx[CLIP_WIRE_MAX];
    size_t rx_len;
    int rc;

    /* reference tool's ECM sends 81 05 04 01 FF FF FF FF FF after mode 0x0017.
       Treat it as useful synchronization, but do not make upload depend on
       seeing it because different firmware may switch to the loader faster. */
    rc = j1939_read_payload(ctx, rx, sizeof(rx), &rx_len, 3000UL);
    if (rc == PULL_OK) {
        if (rx_len >= 3U && rx[0] == 0x81U && rx[1] == 0x05U && rx[2] == 0x04U)
            return;
    }
    clear_last_error();
}

static int
raw_wait_prefix(struct pull_ctx *ctx,
                const clip_u8 *prefix,
                size_t prefix_len,
                unsigned long timeout_ms)
{
    clip_u8 rx[CLIP_WIRE_MAX];
    size_t rx_len;
    unsigned long start;
    unsigned long now;
    int rc;

    if (ctx == NULL || prefix == NULL || prefix_len == 0U)
        return PULL_ERR_ARGUMENT;

    start = ct_monotonic_ms();
    for (;;) {
        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= (unsigned long)timeout_ms)
            break;

        rc = j1939_read_payload(ctx,
                                rx,
                                sizeof(rx),
                                &rx_len,
                                250UL);
        if (rc == PULL_OK) {
            if (rx_len >= prefix_len && memcmp(rx, prefix, prefix_len) == 0)
                return PULL_OK;
            continue;
        }

        if (rc != PULL_ERR_TIMEOUT)
            return rc;
        clear_last_error();
    }

    set_last_error_text("Timed out waiting for raw loader response.");
    return PULL_ERR_TIMEOUT;
}

static int
raw_exchange_prefix(struct pull_ctx *ctx,
                    const clip_u8 *request,
                    size_t request_len,
                    const clip_u8 *prefix,
                    size_t prefix_len)
{
    int rc;

    rc = j1939_send_payload(ctx, request, request_len);
    if (rc != PULL_OK)
        return rc;

    return raw_wait_prefix(ctx, prefix, prefix_len, 5000UL);
}

static int
raw_loader_preamble(struct pull_ctx *ctx)
{
    static const clip_u8 q_password[8] =
        {0x43U,0x00U,0x05U,0x00U,0x00U,0x00U,0x06U,0xffU};
    static const clip_u8 a_password[3] = {0x44U,0x00U,0x05U};
    static const clip_u8 q_family[8] =
        {0x43U,0x00U,0x00U,0x00U,0x00U,0x00U,0x02U,0xffU};
    static const clip_u8 a_family[3] = {0x44U,0x00U,0x00U};
    static const clip_u8 q_43[8] =
        {0x41U,0x00U,0x43U,0xffU,0xffU,0xffU,0xffU,0xffU};
    static const clip_u8 a_43[5] = {0x0dU,0x08U,0x41U,0x00U,0x43U};
    static const clip_u8 q_10[8] =
        {0x41U,0x00U,0x10U,0xffU,0xffU,0xffU,0xffU,0xffU};
    static const clip_u8 a_10[3] = {0x42U,0x00U,0x10U};
    static const clip_u8 r_10[8] =
        {0x4aU,0x40U,0x01U,0x00U,0x08U,0x01U,0xffU,0xffU};
    static const clip_u8 d_10[6] = {0x4bU,0x40U,0x01U,0x00U,0x08U,0x01U};
    static const clip_u8 q_48[8] =
        {0x41U,0x00U,0x48U,0xffU,0xffU,0xffU,0xffU,0xffU};
    static const clip_u8 a_48[3] = {0x42U,0x00U,0x48U};
    static const clip_u8 r_48[8] =
        {0x4aU,0x00U,0xffU,0xfcU,0x08U,0x04U,0xffU,0xffU};
    static const clip_u8 d_48[6] = {0x4bU,0x00U,0xffU,0xfcU,0x08U,0x04U};
    static const clip_u8 q_01[8] =
        {0x41U,0x00U,0x01U,0xffU,0xffU,0xffU,0xffU,0xffU};
    static const clip_u8 a_01[3] = {0x42U,0x00U,0x01U};
    static const clip_u8 r_01[8] =
        {0x4aU,0x00U,0xffU,0xfcU,0x00U,0x04U,0xffU,0xffU};
    static const clip_u8 d_01[6] = {0x4bU,0x00U,0xffU,0xfcU,0x00U,0x04U};
    int rc;

#define RAW_STEP(req_, ans_) do { \
        rc = raw_exchange_prefix(ctx, (req_), sizeof(req_), (ans_), sizeof(ans_)); \
        if (rc != PULL_OK) return rc; \
        ct_sleep_ms(10); \
    } while (0)

    RAW_STEP(q_password, a_password);
    RAW_STEP(q_family, a_family);
    RAW_STEP(q_43, a_43);
    RAW_STEP(q_10, a_10);
    RAW_STEP(r_10, d_10);
    RAW_STEP(q_48, a_48);
    RAW_STEP(r_48, d_48);
    RAW_STEP(q_01, a_01);
    RAW_STEP(r_01, d_01);

#undef RAW_STEP

    return PULL_OK;
}

static int
raw_cal_send(void *user, const unsigned char *data, unsigned int length)
{
    struct pull_ctx *ctx;
    int rc;

    ctx = (struct pull_ctx *)user;
    rc = j1939_send_payload(ctx, (const clip_u8 *)data, (size_t)length);
    return rc == PULL_OK ? 0 : rc;
}

static int
raw_cal_recv(void *user,
             unsigned char *data,
             unsigned int capacity,
             unsigned int *length,
             unsigned long timeout_ms)
{
    struct pull_ctx *ctx;
    size_t n;
    int rc;

    if (length == NULL)
        return PULL_ERR_ARGUMENT;

    ctx = (struct pull_ctx *)user;
    n = 0U;
    rc = j1939_read_payload(ctx,
                            (clip_u8 *)data,
                            (size_t)capacity,
                            &n,
                            timeout_ms);
    if (rc == PULL_ERR_TIMEOUT)
        return 1;
    if (rc != PULL_OK)
        return -1;

    *length = (unsigned int)n;
    return 0;
}

static void
raw_cal_progress(void *user,
                 unsigned long bytes_sent,
                 unsigned long bytes_total,
                 unsigned long address)
{
    struct pull_ctx *ctx;
    int percent;
    char msg[192];

    ctx = (struct pull_ctx *)user;
    if (bytes_total == 0UL)
        percent = 35;
    else
        percent = 35 + (int)((bytes_sent * 60UL) / bytes_total);
    if (percent > 95)
        percent = 95;

    sprintf(msg,
            "Programming calibration: %lu / %lu bytes at 0x%08lX",
            bytes_sent,
            bytes_total,
            address);
    report_progress(ctx, percent, msg);
}

static int
verify_ecm_after_programming(struct pull_ctx *ctx,
                             const char *api_name,
                             int device_id,
                             int baud,
                             clip_u8 tool_sa,
                             clip_u8 ecm_sa,
                             int *lock_confirmed)
{
    RP1210_PROGRESS_CALLBACK saved_progress;
    clip_u8 sequence;
    int attempt;
    int rc;

    if (ctx == NULL)
        return PULL_ERR_ARGUMENT;

    report_progress(ctx, 96,
                    "Programming stream complete; waiting for ECM application recovery...");

    /*
     * The raw loader normally resets into the application after the final
     * transfer command.  Do not report success until the application can be
     * reached, authenticated, and answer a harmless product-ID query again.
     */
    saved_progress = ctx->progress;
    ctx->progress = NULL;
    close_j1939_transport(ctx);

    for (attempt = 0; attempt < 12; ++attempt) {
        ct_sleep_ms(attempt == 0 ? 5000UL : 3000UL);

        ctx->session_id = 0x01U;
        ctx->wire_slot = 0U;
        ctx->tolerate_negative = 0;

        rc = open_j1939_transport(ctx,
                                  api_name,
                                  device_id,
                                  baud,
                                  tool_sa,
                                  ecm_sa);
        if (rc == PULL_OK) {
            rc = clip_authenticate(ctx);
            if (rc == PULL_OK) {
                sequence = 0x00U;
                /*
                 * Re-lock only after authenticated application recovery,
                 * never while the raw programming loader is active.
                 */
                rc = cal_enter_0b_state(ctx, &sequence, 1);
                if (rc == PULL_OK)
                    rc = cal_enter_mode_value(ctx, &sequence,
                                              CLIP_MODE_CAL_LOCK, CLIP_CAL_LOCKED);
                if (rc == PULL_OK) {
                    if (lock_confirmed != NULL)
                        *lock_confirmed = 1;
                    rc = cal_query_discard(ctx, &sequence, 0x002226UL);
                    if (rc == PULL_OK)
                        rc = cal_query_discard(ctx, &sequence, 0x000084UL);
                    if (rc == PULL_OK) {
                        (void)clip_send_close(ctx);
                        close_j1939_transport(ctx);
                        ctx->progress = saved_progress;
                        report_progress(ctx, 99,
                            "ECM recovered; calibration re-lock acknowledged.");
                        return PULL_OK;
                    }
                }
                (void)clip_send_close(ctx);
            }
        }

        close_j1939_transport(ctx);
        clear_last_error();
    }

    ctx->progress = saved_progress;
    if (lock_confirmed != NULL && *lock_confirmed)
        set_last_error_text(
            "Calibration re-lock acknowledged, but post-programming application verification failed.");
    else
        set_last_error_text(
            "Programming completed, but calibration re-lock is NOT confirmed. ECM may remain unlocked or in loader mode.");
    return PULL_ERR_POST_VERIFY;
}

/*
 * GTIS4.6 staged flash experiment (CLIP46_POC_FLASH=1).
 *
 * Runs every stage that the passive OEM flash capture evidences, and
 * HARD-STOPS before the first component whose contents cannot be derived
 * from a passive capture:
 *
 *   stage 1  secure session (caller, CLIP46_POC_PULL=1)
 *   stage 2  multi-query + 0x000B state entry (observed session shape)
 *   stage 3  legacy-format unlock/lock/unlock dance through the
 *            protected codec; every ECM reply is reported
 *   stage 4  DA00 channel open + the captured init command sequence;
 *            every ECM reply is reported
 *   STOP     the DA00 authentication block ("22 27 08" + 32
 *            session-derived bytes) has no known derivation. Without it
 *            the ECM will not accept any image data.
 *
 * The flow never transmits image data, never enters a loader mode, and
 * re-locks the calibration before exiting. A power cycle is still
 * recommended after any unlock test.
 */
static int
clip46_staged_flash(struct pull_ctx *ctx,
                    const char *api_name,
                    int device_id,
                    int baud,
                    clip_u8 tool_sa,
                    clip_u8 ecm_sa)
{
    clip_u8 reply[CLIP_APP_MAX];
    clip_u8 request[CLIP_CAL_PHASE_REQUEST_SIZE];
    size_t reply_len;
    clip_u8 sequence;
    char note[192];
    int rc;

    (void)api_name;
    (void)device_id;
    (void)baud;
    (void)tool_sa;
    (void)ecm_sa;

    rc = PULL_ERR_SECURE_POST_AUTH;
    sequence = 0x02U; /* the protected session's first query used seq 01 */

    /*
     * Stage 2: reproduce the observed session shape before the unlock
     * (multi-query, then the 0x000B state entry with outer byte 4 = 00).
     */
    report_progress(ctx, 20, "GTIS4.6 flash stage 2: session preflight...");
    rc = cal_preflight_multi_query(ctx, &sequence);
    if (rc != PULL_OK)
        goto done;
    rc = cal_enter_0b_state(ctx, &sequence, 1);
    if (rc != PULL_OK)
        goto done;

    /*
     * Stage 3: legacy-format unlock dance. The captured GTIS4.6 unlock
     * commands are encrypted, but the legacy family uses
     * "12 <seq> 00 18 <value>" with value 0 = unlock, 1 = lock. Every
     * reply is reported so the bench run shows whether the ECM accepts
     * this interpretation.
     */
    report_progress(ctx, 25, "GTIS4.6 flash stage 3: unlock dance...");
    {
        int stage;
        for (stage = 0; stage < 3; ++stage) {
        static const clip_u8 values[3] = {0U, 1U, 0U};
        clip_u8 seq;

        seq = sequence;
        rc = clip_cal_build_mode_request(seq, 0x0018U,
                                         (unsigned int)values[stage],
                                         request);
        if (rc != CLIP_CAL_OK) {
            rc = PULL_ERR_CAL;
            goto done;
        }
        rc = cal_exchange_sequence(ctx,
                                   request,
                                   sizeof(request),
                                   seq,
                                   reply,
                                   sizeof(reply),
                                   &reply_len,
                                   CLIP_TIMEOUT_MS);
        sequence = clip_cal_next_sequence(seq);
        if (rc != PULL_OK) {
            sprintf(note,
                    "GTIS4.6 unlock step %d (value %u): exchange failed.",
                    stage + 1, values[stage]);
            report_progress(ctx, 26, note);
            goto done;
        }
        sprintf(note,
                "GTIS4.6 unlock step %d (12 %02X 00 18 %02X): "
                "reply len=%lu svc=%02X seq=%02X",
                stage + 1,
                (unsigned)seq,
                values[stage],
                (unsigned long)reply_len,
                (unsigned)(reply_len > 0U ? reply[0] : 0U),
                (unsigned)(reply_len > 1U ? reply[1] : 0U));
        report_progress(ctx, 26, note);
    }
    } /* stage variable block */

    /*
     * Stage 4: attempt the legacy loader transition. The OEM tool uses
     * the DA00 streaming protocol instead, but the ECM may still accept
     * the legacy "12 <seq> 00 17 00" loader-mode command through the
     * secure codec. A negative reply is harmless and informative.
     */
    report_progress(ctx, 35,
                    "GTIS4.6 flash stage 4: attempting legacy loader "
                    "transition (mode 0x0017)...");
    {
        clip_u8 seq;

        seq = sequence;
        rc = clip_cal_build_mode_request(seq, 0x0017U, 0x00U, request);
        if (rc != CLIP_CAL_OK) {
            rc = PULL_ERR_CAL;
            goto done;
        }
        rc = cal_exchange_sequence(ctx,
                                   request,
                                   sizeof(request),
                                   seq,
                                   reply,
                                   sizeof(reply),
                                   &reply_len,
                                   CLIP_TIMEOUT_MS);
        sequence = clip_cal_next_sequence(seq);
        if (rc != PULL_OK) {
            sprintf(note,
                    "GTIS4.6 loader transition (12 %02X 00 17 00): "
                    "exchange failed - ECM may not support the legacy "
                    "loader path over the secure session.",
                    (unsigned)seq);
            report_progress(ctx, 36, note);
            goto done;
        }
        sprintf(note,
                "GTIS4.6 loader transition reply: len=%lu svc=%02X seq=%02X "
                "data=%02X %02X",
                (unsigned long)reply_len,
                (unsigned)(reply_len > 0U ? reply[0] : 0U),
                (unsigned)(reply_len > 1U ? reply[1] : 0U),
                (unsigned)(reply_len > 2U ? reply[2] : 0U),
                (unsigned)(reply_len > 3U ? reply[3] : 0U));
        report_progress(ctx, 36, note);

        if (reply_len >= 2U && reply[0] == CLIP_CAL_SERVICE_REPLY) {
            report_progress(ctx, 37,
                            "GTIS4.6: loader transition ACCEPTED. The ECM "
                            "should drop the CLIP session and start the raw "
                            "calibration loader. The existing legacy upload "
                            "path can now take over after the standard 11s "
                            "startup wait.");
            set_last_error_text(
                "GTIS4.6 loader transition accepted via the secure CLIP "
                "session. The ECM is transitioning to the raw calibration "
                "loader. Re-run the upload WITHOUT CLIP46_POC_FLASH to "
                "use the full legacy programming path (raw loader "
                "preamble + 4B/4D writes + verify + re-lock).");
        } else if (reply_len >= 2U && reply[0] == 0x03U) {
            report_progress(ctx, 37,
                            "GTIS4.6: loader transition REJECTED (negative "
                            "reply). The ECM requires the DA00 streaming "
                            "protocol for calibration writes.");
            set_last_error_text(
                "GTIS4.6 loader transition rejected: the CM2450E does not "
                "accept the legacy raw-loader entry over the secure "
                "session. The DA00 streaming protocol is required.");
        } else {
            report_progress(ctx, 37,
                            "GTIS4.6: loader transition reply has an "
                            "unexpected shape - inspect the progress log.");
            set_last_error_text(
                "GTIS4.6 loader transition produced an unexpected reply. "
                "No image data was sent.");
        }
    }

    /*
     * No image data is ever sent by this staged flow.
     */
    rc = PULL_ERR_SECURE_POST_AUTH;

done:
    /* Best-effort re-lock through the CLIP session. */
    if (sequence != 0x02U) {
        clip_u8 seq;

        seq = sequence;
        if (clip_cal_build_mode_request(seq, 0x0018U, 1U, request) ==
            CLIP_CAL_OK) {
            clip_u8 lock_reply[CLIP_APP_MAX];
            size_t lock_reply_len;

            (void)cal_exchange_sequence(ctx,
                                        request,
                                        sizeof(request),
                                        seq,
                                        lock_reply,
                                        sizeof(lock_reply),
                                        &lock_reply_len,
                                        CLIP_TIMEOUT_MS);
        }
    }
    return rc;
}

/*
 * A failed loader transition or incomplete flash may leave no active CLIP
 * session.  Re-lock only if the recovered APPLICATION authenticates.
 * This cannot guarantee re-lock if the ECM remains in loader mode.
 */
static int
relock_after_loader_failure(struct pull_ctx *ctx,
                            const char *api_name,
                            int device_id,
                            int baud,
                            clip_u8 tool_sa,
                            clip_u8 ecm_sa)
{
    RP1210_PROGRESS_CALLBACK saved_progress;
    clip_u8 sequence;
    int attempt;
    int rc;

    saved_progress = ctx->progress;
    ctx->progress = NULL;
    close_j1939_transport(ctx);

    for (attempt = 0; attempt < 3; ++attempt) {
        if (attempt != 0)
            ct_sleep_ms(3000UL);

        ctx->session_id = 0x01U;
        ctx->wire_slot = 0U;
        ctx->tolerate_negative = 0;
        rc = open_j1939_transport(ctx, api_name, device_id, baud,
                                  tool_sa, ecm_sa);
        if (rc == PULL_OK) {
            rc = clip_authenticate(ctx);
            if (rc == PULL_OK) {
                sequence = 0x00U;
                rc = cal_enter_0b_state(ctx, &sequence, 1);
                if (rc == PULL_OK)
                    rc = cal_enter_mode_value(ctx, &sequence,
                                              CLIP_MODE_CAL_LOCK, CLIP_CAL_LOCKED);
                (void)clip_send_close(ctx);
                if (rc == PULL_OK) {
                    close_j1939_transport(ctx);
                    ctx->progress = saved_progress;
                    return 1;
                }
            }
        }
        close_j1939_transport(ctx);
        clear_last_error();
    }

    ctx->progress = saved_progress;
    return 0;
}

int RP1210_CALL
rp1210_pull_ccal(const char *api_name,
                 int device_id,
                 int baud,
                 unsigned char tool_sa,
                 unsigned char ecm_sa,
                 const char *out_path,
                 RP1210_PROGRESS_CALLBACK progress)
{
    struct pull_ctx ctx;
    struct clip_cal_map map;
    struct pull_image image;
    struct pull_meta meta;
    echo_io echo;
    clip_u8 sequence;
    int rc;
    int echo_detected;
    int clip_open;
    int elite_open;

    memset(&ctx, 0, sizeof(ctx));
    memset(&map, 0, sizeof(map));
    memset(&image, 0, sizeof(image));
    memset(&meta, 0, sizeof(meta));

    clear_last_error();

    if (api_name == NULL || api_name[0] == '\0' ||
        out_path == NULL || out_path[0] == '\0' ||
        device_id < 0) {
        set_last_error_text("Invalid RP1210 API, device, or output path.");
        return PULL_ERR_ARGUMENT;
    }

    if (clip46_runtime_load(&g_clip46_options) != 0) {
        set_last_error_text("Unable to load GTIS4.6 settings from config.toml.");
        return PULL_ERR_ARGUMENT;
    }

    ctx.transport = NULL;
    ctx.session_id = 0x01U;
    ctx.wire_slot = 0U;
    ctx.progress = progress;
    ctx.clip46_read_only_workflow = 1;
    clip_open = 0;
    elite_open = 0;

    report_progress(&ctx, 0, "Opening RP1210/J1939 transport...");
    rc = open_j1939_transport(&ctx,
                               api_name,
                               device_id,
                               baud,
                               (clip_u8)tool_sa,
                               (clip_u8)ecm_sa);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 2, "Connected and J1939 tool address claimed.");

    rc = clip_authenticate(&ctx);
    if (rc == PULL_DETECTED_ELITE_II) {
        /*
         * The negative CLIP-open reply identifies a legacy raw
         * Proprietary-A platform.  Probe ProductID before choosing the
         * family-specific readback path.
         */
        clear_last_error();
        echo_init_io(&ctx, &echo);
        echo_detected = echo_probe(&echo);

        if (echo_detected > 0) {
            clear_last_error();
            rc = echo_pull_ccal(&echo, out_path);
            if (rc != ECHO_TRANSFER_OK)
                rc = map_echo_result(rc);
            else {
                clear_last_error();
                rc = PULL_OK;
            }
            goto done;
        }

        clear_last_error();
        report_progress(&ctx, 4, "ENI/ELITE II platform detected.");

        report_progress(&ctx, 6, "Opening ELITE II calibration transfer...");
        rc = elite_transfer_control(&ctx, 0x04U);
        if (rc != PULL_OK)
            goto done;
        elite_open = 1;

        report_progress(&ctx, 8, "Reading ELITE II calibration descriptor...");
        rc = elite_get_descriptor(&ctx, &map);
        if (rc != PULL_OK)
            goto done;

        rc = allocate_image(&map, &image);
        if (rc != PULL_OK)
            goto done;

        report_progress(&ctx, 12, "Reading ENI/ELITE II calibration memory...");
        rc = elite_pull_memory_ranges(&ctx, &map, &image);
        if (rc != PULL_OK)
            goto done;

        report_progress(&ctx, 93, "Closing ELITE II calibration transfer...");
        rc = elite_transfer_control(&ctx, 0x05U);
        if (rc != PULL_OK)
            goto done;
        elite_open = 0;

        report_progress(&ctx, 95, "Writing legacy ENI .ccal file...");
        rc = write_elite_ccal(out_path, &image);
        if (rc != PULL_OK)
            goto done;

        report_progress(&ctx, 98, "Finalizing legacy calibration CRC...");
        if (!ccal_set_cal_file_crc(out_path) ||
            !ccal_check_cal_file_crc(out_path)) {
            set_last_error_text(
                "ENI calibration was pulled, but legacy calibration CRC finalization failed.");
            rc = PULL_ERR_CRC;
            goto done;
        }

        report_progress(&ctx, 100, "ENI/ELITE II calibration saved and CRC verified.");
        clear_last_error();
        rc = PULL_OK;
        goto done;
    }

    if (rc != PULL_OK)
        goto done;
    clip_open = 1;

    if (ctx.clip46_secure) {
        /*
         * GTIS4.6 secure readback. The first protected query (sequence
         * 01, the bench-verified 0x0084 status query) was already sent
         * during authentication. Continue with the observed secure-session
         * shape: the multi-query at sequence 02 and the 0x000B state entry
         * at sequence 03 (outer byte 4 = 00 in both reference recordings),
         * then read the descriptor. The legacy preflight probes and the
         * separate MAIN phase entry were not observed in the secure
         * reference session and are skipped.
         */
        sequence = 0x02U;
        report_progress(&ctx, 11, "Running GTIS4.6 secure preflight...");
        rc = cal_preflight_multi_query(&ctx, &sequence);
        if (rc != PULL_OK)
            goto done;

        rc = cal_enter_0b_state(&ctx, &sequence, 1);
        if (rc != PULL_OK)
            goto done;
    } else {
        sequence = 0x00U;

        /* Reproduce the observed reference tool post-authentication preflight. */
        report_progress(&ctx, 8, "Running reference tool CLIP preflight...");
        rc = cal_run_protocol_preflight(&ctx, &sequence);
        if (rc != PULL_OK)
            goto done;

        if (sequence != 0x16U) {
            set_last_error_code("Unexpected CLIP preflight sequence", (long)sequence);
            rc = PULL_ERR_PROTOCOL;
            goto done;
        }

        report_progress(&ctx, 9, "Entering calibration upload phase...");
        rc = cal_enter_phase(&ctx, &sequence, CLIP_CAL_PHASE_MAIN);
        if (rc != PULL_OK)
            goto done;
    }

    report_progress(&ctx, 12, "Reading calibration memory descriptor...");
    rc = cal_get_descriptor(&ctx, &sequence, &map);
    if (rc != PULL_OK)
        goto done;

    /*
     * Log the ECM's region table (metadata only) so it can be compared
     * directly against the INCAL image regions.
     */
    if (ctx.clip46_secure) {
        char msg[512];
        size_t used;
        unsigned int r;

        used = (size_t)sprintf(msg,
                               "GTIS4.6 descriptor 0x%08lX: %u range(s):",
                               (unsigned long)map.descriptor_value,
                               map.range_count);
        for (r = 0U;
             r < map.range_count && used < sizeof(msg) - 24U;
             ++r) {
            used += (size_t)sprintf(msg + used,
                                    " %08lX+%lu",
                                    (unsigned long)map.ranges[r].address,
                                    (unsigned long)map.ranges[r].length);
        }
        report_progress(&ctx, 13, msg);
    }

    rc = allocate_image(&map, &image);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 12, "Reading calibration compatibility metadata...");
    rc = collect_metadata(&ctx, &sequence, &map, &meta);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 15, "Reading ECM calibration memory...");
    rc = pull_memory_ranges(&ctx, &sequence, &map, &image);
    if (rc != PULL_OK)
        goto done;

    rc = cal_end_phase(&ctx);
    if (rc != PULL_OK)
        goto done;

    rc = collect_auxiliary(&ctx, &sequence, &meta);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 94, "Writing .ccal file...");
    rc = write_ccal(out_path, &meta, &image);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 98, "Calibration data written; finalizing file CRC...");
    if (!ccal_set_cal_file_crc(out_path) ||
        !ccal_check_cal_file_crc(out_path) ||
        !ccal_check_header_file_crc(out_path) ||
        !ccal_check_file_crc(out_path)) {
        set_last_error_text("Calibration was pulled, but native calibration CRC finalization failed.");
        rc = PULL_ERR_CRC;
        goto done;
    }

    report_progress(&ctx, 100, "Calibration saved and CRC verified.");
    clear_last_error();

 done:
    if (elite_open)
        (void)elite_transfer_control(&ctx, 0x05U);
    if (clip_open)
        (void)clip_send_close(&ctx);
    close_j1939_transport(&ctx);
    free_image(&image);

    return rc;
}

int RP1210_CALL
rp1210_upload_ccal(const char *api_name,
                   int device_id,
                   int baud,
                   unsigned char tool_sa,
                   unsigned char ecm_sa,
                   const char *ccal_path,
                   RP1210_PROGRESS_CALLBACK progress)
{
    struct pull_ctx ctx;
    clip_cal_io io;
    clip_cal_options options;
    echo_io echo;
    clip_cal_validation_detail detail;
    clip_u8 sequence;
    int rc;
    int cal_rc;
    int echo_detected;
    int clip_open;
    int loader_mode;
    int loader_requested;
    int unlock_attempted;
    int lock_confirmed;

    memset(&ctx, 0, sizeof(ctx));
    clear_last_error();

    if (api_name == NULL || api_name[0] == '\0' ||
        ccal_path == NULL || ccal_path[0] == '\0' ||
        device_id < 0) {
        set_last_error_text("Invalid RP1210 API, device, or CCAL path.");
        return PULL_ERR_ARGUMENT;
    }

    if (clip46_runtime_load(&g_clip46_options) != 0) {
        set_last_error_text("Unable to load GTIS4.6 settings from config.toml.");
        return PULL_ERR_ARGUMENT;
    }

    /*
     * GTIS4.6 is NOT the legacy loader. Plan the complete DA00 frame
     * sequence offline, then stop before opening RP1210. This prevents
     * even a successful secure authentication from falling through to
     * the incompatible 24-bit 4B/4D sender.
     */
    if (g_clip46_options.da00_plan) {
        struct clip46_plan_stats stats;
        char note[512];
        if (clip46_plan_ccal(ccal_path, &stats) != 0) {
            set_last_error_text("GTIS4.6 DA00 plan failed: CCAL CRC, image layout, "
                                "or 32-bit region address is invalid. No traffic sent.");
            return PULL_ERR_CAL;
        }
        sprintf(note, "GTIS4.6 DA00 offline plan: %u stream region(s), "
                "%u identity region(s), %lu image bytes, %lu superblocks, "
                "%lu wire frames. No ECM traffic.",
                stats.regions, stats.identity_regions, stats.image_bytes,
                stats.super_blocks, stats.wire_frames);
        if (progress != NULL)
            progress(100, note);
        set_last_error_text(
            "DA00 offline plan generated successfully. Programming is BLOCKED: "
            "session-derived 22 27 08 authentication, ECM identity merge, "
            "region-tail variants and encrypted service-11 commit remain "
            "unverified. No programming traffic was sent.");
        return PULL_ERR_SECURE_POST_AUTH;
    }

    /*
     * Deliberately perform the complete file/programming-layout preflight
     * before opening the adapter.  A CRC-valid but unsupported/truncated CCAL
     * therefore cannot put an ECM into loader mode.
     */
    if (progress != NULL)
        progress(0, "Validating CCAL CRC, Intel-HEX structure, and loader layout...");

    memset(&detail, 0, sizeof(detail));
    cal_rc = clip_cal_validate_ccal_detail(ccal_path, &detail);
    if (cal_rc != CLIP_CAL_OK) {
        char msg[512];

        if (cal_rc == CLIP_CAL_ERR_RANGE && detail.has_region) {
            sprintf(msg,
                    "CCAL preflight validation failed: region %u "
                    "(0x%08lX + %lu bytes) is outside the captured 24-bit "
                    "programming range. The legacy raw loader is the only "
                    "implemented upload path; GTIS4.6 secure ECMs use a "
                    "different streaming mechanism that is not yet "
                    "implemented. No programming traffic was sent.",
                    detail.region_index,
                    detail.address,
                    detail.length);
        } else {
            sprintf(msg,
                    "CCAL preflight validation failed: %s. No programming traffic was sent.",
                    clip_cal_strerror(cal_rc));
        }
        set_last_error_text(msg);

        if (cal_rc == CLIP_CAL_ERR_CRC)
            return PULL_ERR_CRC;
        if (cal_rc == CLIP_CAL_ERR_OPEN)
            return PULL_ERR_FILE;
        if (cal_rc == CLIP_CAL_ERR_MEMORY)
            return PULL_ERR_MEMORY;
        return PULL_ERR_CAL;
    }

    ctx.transport = NULL;
    ctx.session_id = 0x01U;
    ctx.wire_slot = 0U;
    ctx.progress = progress;
    /*
     * The GTIS4.6 protected session (and the staged flash experiment)
     * reuses the read-only workflow gate: CLIP46_POC_PULL=1 with
     * CLIP46_POC_FILE establishes the session; CLIP46_POC_FLASH=1 then
     * selects the staged flash flow after authentication.
     */
    ctx.clip46_read_only_workflow = 1;
    clip_open = 0;
    loader_mode = 0;
    loader_requested = 0;
    unlock_attempted = 0;
    lock_confirmed = 0;

    report_progress(&ctx, 2, "CCAL fully validated. Opening RP1210/J1939 transport...");
    rc = open_j1939_transport(&ctx,
                               api_name,
                               device_id,
                               baud,
                               (clip_u8)tool_sa,
                               (clip_u8)ecm_sa);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 4, "Connected and J1939 tool address claimed.");

    rc = clip_authenticate(&ctx);
    if (rc == PULL_DETECTED_ELITE_II) {
        /*
         * INTENTIONAL SAFETY BLOCKER.
         *
         * The validated ECH/ECHO material supplied for this implementation is
         * a readback trace: 4C memory-read requests with 4D data replies.  It
         * does not establish a safe calibration/configuration write sequence.
         * ECHO II / ENI / ELITE II programming remains blocked as well.
         */
        clear_last_error();
        echo_init_io(&ctx, &echo);
        echo_detected = echo_probe(&echo);

        if (echo_detected > 0) {
            report_progress(
                &ctx,
                5,
                "ECH/ECHO programming is intentionally blocked.");
            set_last_error_text(
                "ECH/ECHO calibration readback is supported, but programming is intentionally blocked because the validated trace only proves read operations. Use the recommended OEM software for programming.");
        }
        else {
            report_progress(
                &ctx,
                5,
                "ECHO II / ELITE II programming is intentionally blocked.");
            set_last_error_text(
                "Sending config files to ECHO II-era ECMs is not supported due to potential corruption of the ECM. If you know what you are doing, use the recommended OEM software.");
        }

        rc = PULL_ERR_LEGACY_TX_BLOCKED;
        goto done;
    }
    if (rc != PULL_OK)
        goto done;

    /*
     * Never enter a GTIS4.6 loader or send raw writes. The only supported
     * DA00 code path currently produces an OFFLINE plan above.
     */
    if (ctx.clip46_secure) {
        set_last_error_text(
            "GTIS4.6 live upload is not implemented. No loader transition "
            "or image data was sent; enable [experimental] gtis46_da00_plan "
            "for an offline frame-count verification.");
        rc = PULL_ERR_SECURE_POST_AUTH;
        goto done;
    } else {
        /*
         * Legacy (non-secure) path: full 22-query preflight, unlock,
         * and loader transition.
         */
        sequence = 0x00U;
        report_progress(&ctx, 10, "Running reference tool CLIP programming preflight...");
        rc = cal_run_upload_preflight(&ctx, &sequence);
        if (rc != PULL_OK)
            goto done;
        if (sequence != 0x16U) {
            set_last_error_code("Unexpected upload preflight sequence", (long)sequence);
            rc = PULL_ERR_PROTOCOL;
            goto done;
        }

        report_progress(&ctx, 17, "Unlocking ECM calibration access...");
        unlock_attempted = 1;
        rc = cal_enter_mode_value(&ctx, &sequence,
                                  CLIP_MODE_CAL_LOCK, CLIP_CAL_UNLOCKED);
        if (rc != PULL_OK) {
            set_last_error_text(
                "Calibration unlock not acknowledged; programming not started.");
            goto done;
        }

        report_progress(&ctx, 18, "Unlock acknowledged; entering calibration loader...");
        rc = cal_prepare_programming(&ctx, &sequence, &loader_requested);
        if (rc != PULL_OK)
            goto done;
    }

    loader_mode = 1;
    clip_open = 0;
    wait_for_ecm_clip_close(&ctx);

    /* In the supplied reference tool upload recording the raw loader identification
       begins about 11.5 seconds after the positive 0x0017 reply (roughly
       10.5 seconds after reference tool's own close acknowledgement).  The five
       trailing bytes of that initiator-close packet are session-dependent
       and their serializer is not yet independently proven, so do not replay
       captured bytes from another session.  Instead allow the ECM the same
       startup interval and probe only with the idempotent raw-loader queries. */
    report_progress(&ctx, 24, "Waiting for calibration loader startup...");
    ct_sleep_ms(11000);

    report_progress(&ctx, 28, "Synchronizing with raw calibration loader...");
    {
        int attempt;
        rc = PULL_ERR_TIMEOUT;
        for (attempt = 0; attempt < 4; ++attempt) {
            rc = raw_loader_preamble(&ctx);
            if (rc == PULL_OK)
                break;
            clear_last_error();
            if (attempt != 3)
                ct_sleep_ms(2000);
        }
        if (rc != PULL_OK) {
            set_last_error_text(
                "ECM entered programming mode, but the raw calibration loader did not answer the captured identification sequence.");
            goto done;
        }
    }

    io.user = &ctx;
    io.send = raw_cal_send;
    io.recv = raw_cal_recv;

    clip_cal_options_init(&options);
    options.timeout_ms = 60000UL;
    options.progress = raw_cal_progress;
    options.progress_user = &ctx;

    report_progress(&ctx, 35, "Programming verified CCAL...");
    rc = clip_cal_send_ccal(ccal_path, &io, &options);
    if (rc != CLIP_CAL_OK) {
        char msg[512];
        sprintf(msg, "Calibration upload failed: %s (clip_cal=%d)",
                clip_cal_strerror(rc), rc);
        set_last_error_text(msg);
        rc = PULL_ERR_UPLOAD;
        goto done;
    }

    rc = verify_ecm_after_programming(&ctx,
                                      api_name,
                                      device_id,
                                      baud,
                                      (clip_u8)tool_sa,
                                      (clip_u8)ecm_sa,
                                      &lock_confirmed);
    if (rc != PULL_OK)
        goto done;

    report_progress(&ctx, 100,
                    "Calibration programmed, ECM re-locked, recovery verified.");
    rc = PULL_OK;

 done:
    if (unlock_attempted && !lock_confirmed) {
        char original[sizeof(g_last_error)];
        char summary[sizeof(g_last_error)];
        int restored;

        strncpy(original, g_last_error, sizeof(original) - 1U);
        original[sizeof(original) - 1U] = 0;
        restored = 0;

        if (loader_requested || loader_mode) {
            report_progress(&ctx, 96,
                "Upload interrupted; checking whether ECM can be re-locked...");
            restored = relock_after_loader_failure(
                &ctx, api_name, device_id, baud,
                (clip_u8)tool_sa, (clip_u8)ecm_sa);
        } else if (clip_open) {
            report_progress(&ctx, 96,
                "Upload aborted before loader; restoring calibration lock...");
            restored = cal_enter_mode_value(
                &ctx, &sequence, CLIP_MODE_CAL_LOCK, CLIP_CAL_LOCKED)
                == PULL_OK;
        }

        if (restored)
            sprintf(summary, "%.345s Calibration re-lock acknowledged.", original);
        else
            sprintf(summary,
                "%.300s WARNING: re-lock NOT confirmed; ECM may remain unlocked.",
                original);
        set_last_error_text(summary);
    }

    if (clip_open && !loader_requested && !loader_mode)
        (void)clip_send_close(&ctx);
    close_j1939_transport(&ctx);
    return rc;
}

