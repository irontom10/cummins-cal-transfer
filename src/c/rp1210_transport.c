/*
 * rp1210_transport.c
 *
 * Generic 32-bit Windows RP1210/J1939 transport.
 *
 * This module owns vendor DLL loading, RP1210 client lifetime, J1939
 * filtering, source-address protection, and payload send/receive.  It has no
 * knowledge of CLIP, calibration files, authentication, or ECM session state.
 *
 * C89 source.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rp1210_transport.h"

#define RP1210_TRANSPORT_MAX_PAYLOAD         4096U

#define RP1210_CMD_SET_J1939_FILTER             4
#define RP1210_CMD_SET_ALL_FILTERS_DISCARD     17
#define RP1210_CMD_PROTECT_J1939_ADDRESS       19
#define RP1210_CMD_SET_J1939_FILTER_TYPE       25
#define RP1210_CMD_FLUSH_TX_RX                 39

#define RP1210_FILTER_PGN          0x01U
#define RP1210_FILTER_SOURCE       0x04U
#define RP1210_FILTER_DESTINATION  0x08U
#define RP1210_FILTER_INCLUSIVE    0x00U

typedef short (WINAPI *PFN_RP1210_CLIENT_CONNECT)(
    HWND, short, char *, long, long, short);
typedef short (WINAPI *PFN_RP1210_CLIENT_DISCONNECT)(short);
typedef short (WINAPI *PFN_RP1210_SEND_MESSAGE)(
    short, char *, short, short, short);
typedef short (WINAPI *PFN_RP1210_READ_MESSAGE)(
    short, char *, short, short);
typedef short (WINAPI *PFN_RP1210_SEND_COMMAND)(
    short, short, char *, short);
typedef short (WINAPI *PFN_RP1210_GET_ERROR_MSG)(short, char *);

struct rp1210_api {
    HMODULE module;
    PFN_RP1210_CLIENT_CONNECT client_connect;
    PFN_RP1210_CLIENT_DISCONNECT client_disconnect;
    PFN_RP1210_SEND_MESSAGE send_message;
    PFN_RP1210_READ_MESSAGE read_message;
    PFN_RP1210_SEND_COMMAND send_command;
    PFN_RP1210_GET_ERROR_MSG get_error_msg;
};

struct rp1210_transport {
    struct rp1210_api api;
    short client_id;
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
set_error_text(struct rp1210_transport *transport, const char *text)
{
    if (transport == NULL)
        return;

    safe_copy(transport->last_error, text, sizeof(transport->last_error));
}

static void
set_error_code(struct rp1210_transport *transport,
               const char *text,
               long code)
{
    char tmp[512];

    if (transport == NULL)
        return;

    sprintf(tmp, "%s (code %ld)", text, code);
    set_error_text(transport, tmp);
}

static FARPROC
resolve_proc(HMODULE module, const char *name, const char *decorated)
{
    FARPROC p;

    p = GetProcAddress(module, name);
    if (p == NULL && decorated != NULL)
        p = GetProcAddress(module, decorated);
    return p;
}

static void
set_rp1210_error(struct rp1210_transport *transport,
                 const char *what,
                 short code)
{
    char message[256];
    char tmp[512];

    message[0] = '\0';
    if (transport != NULL && transport->api.get_error_msg != NULL)
        transport->api.get_error_msg(code, message);

    if (message[0] != '\0')
        sprintf(tmp, "%s: RP1210 %d - %s", what, (int)code, message);
    else
        sprintf(tmp, "%s: RP1210 error %d", what, (int)code);

    set_error_text(transport, tmp);
}

static int
load_api(struct rp1210_transport *transport, const char *api_name)
{
    char dll_name[MAX_PATH];
    size_t len;

    if (transport == NULL || api_name == NULL || api_name[0] == '\0')
        return RP1210_TRANSPORT_ERR_ARGUMENT;

    safe_copy(dll_name, api_name, sizeof(dll_name));
    len = strlen(dll_name);
    if (len < 4U || _stricmp(dll_name + len - 4U, ".dll") != 0) {
        if (len + 4U >= sizeof(dll_name)) {
            set_error_text(transport, "RP1210 API DLL name is too long.");
            return RP1210_TRANSPORT_ERR_LOAD_API;
        }
        strcat(dll_name, ".dll");
    }

    transport->api.module = LoadLibraryA(dll_name);
    if (transport->api.module == NULL) {
        set_error_code(transport,
                       "Unable to load RP1210 vendor DLL",
                       (long)GetLastError());
        return RP1210_TRANSPORT_ERR_LOAD_API;
    }

    transport->api.client_connect = (PFN_RP1210_CLIENT_CONNECT)
        resolve_proc(transport->api.module,
                     "RP1210_ClientConnect",
                     "_RP1210_ClientConnect@24");
    transport->api.client_disconnect = (PFN_RP1210_CLIENT_DISCONNECT)
        resolve_proc(transport->api.module,
                     "RP1210_ClientDisconnect",
                     "_RP1210_ClientDisconnect@4");
    transport->api.send_message = (PFN_RP1210_SEND_MESSAGE)
        resolve_proc(transport->api.module,
                     "RP1210_SendMessage",
                     "_RP1210_SendMessage@20");
    transport->api.read_message = (PFN_RP1210_READ_MESSAGE)
        resolve_proc(transport->api.module,
                     "RP1210_ReadMessage",
                     "_RP1210_ReadMessage@16");
    transport->api.send_command = (PFN_RP1210_SEND_COMMAND)
        resolve_proc(transport->api.module,
                     "RP1210_SendCommand",
                     "_RP1210_SendCommand@16");
    transport->api.get_error_msg = (PFN_RP1210_GET_ERROR_MSG)
        resolve_proc(transport->api.module,
                     "RP1210_GetErrorMsg",
                     "_RP1210_GetErrorMsg@8");

    if (transport->api.client_connect == NULL ||
        transport->api.client_disconnect == NULL ||
        transport->api.send_message == NULL ||
        transport->api.read_message == NULL ||
        transport->api.send_command == NULL) {
        set_error_text(
            transport,
            "RP1210 vendor DLL is missing a required API export.");
        FreeLibrary(transport->api.module);
        memset(&transport->api, 0, sizeof(transport->api));
        return RP1210_TRANSPORT_ERR_SYMBOL;
    }

    return RP1210_TRANSPORT_OK;
}

static int
connect_j1939(struct rp1210_transport *transport,
              int device_id,
              int baud)
{
    char protocol[64];
    char protocol_alt[64];
    short client;
    short rc;
    int kbps;

    if (transport == NULL)
        return RP1210_TRANSPORT_ERR_ARGUMENT;

    if (baud == 125000 || baud == 250000 ||
        baud == 500000 || baud == 1000000) {
        kbps = baud / 1000;
    } else {
        set_error_text(transport, "Unsupported J1939 baud rate.");
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    sprintf(protocol, "J1939:Baud=%d", kbps);
    sprintf(protocol_alt, "J1939:Baud=%d", baud);

    client = transport->api.client_connect(NULL,
                                            (short)device_id,
                                            protocol,
                                            0L,
                                            0L,
                                            0);
    if (client < 0 || client > 127) {
        client = transport->api.client_connect(NULL,
                                                (short)device_id,
                                                protocol_alt,
                                                0L,
                                                0L,
                                                0);
    }

    if (client < 0 || client > 127) {
        client = transport->api.client_connect(NULL,
                                                (short)device_id,
                                                "J1939",
                                                0L,
                                                0L,
                                                0);
    }

    if (client < 0 || client > 127) {
        set_rp1210_error(
            transport,
            "RP1210_ClientConnect failed",
            client);
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    transport->client_id = client;

    rc = transport->api.send_command(
        (short)RP1210_CMD_SET_ALL_FILTERS_DISCARD,
        transport->client_id,
        NULL,
        0);
    if (rc != 0) {
        set_rp1210_error(
            transport,
            "RP1210_Set_All_Filters_States_to_Discard failed",
            rc);
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    {
        unsigned char filter_type;
        unsigned char filter[7];

        filter_type = RP1210_FILTER_INCLUSIVE;
        rc = transport->api.send_command(
            (short)RP1210_CMD_SET_J1939_FILTER_TYPE,
            transport->client_id,
            (char *)&filter_type,
            1);
        if (rc != 0) {
            set_rp1210_error(
                transport,
                "RP1210_Set_J1939_Filter_Type failed",
                rc);
            return RP1210_TRANSPORT_ERR_CONNECT;
        }

        filter[0] = (unsigned char)(RP1210_FILTER_PGN |
                                    RP1210_FILTER_SOURCE |
                                    RP1210_FILTER_DESTINATION);
        filter[1] = (unsigned char)(transport->pgn & 0xffUL);
        filter[2] = (unsigned char)((transport->pgn >> 8) & 0xffUL);
        filter[3] = (unsigned char)((transport->pgn >> 16) & 0xffUL);
        filter[4] = 0x00U;
        filter[5] = transport->destination_address;
        filter[6] = transport->source_address;

        rc = transport->api.send_command(
            (short)RP1210_CMD_SET_J1939_FILTER,
            transport->client_id,
            (char *)filter,
            (short)sizeof(filter));
        if (rc != 0) {
            set_rp1210_error(
                transport,
                "RP1210_Set_Message_Filtering_For_J1939 failed",
                rc);
            return RP1210_TRANSPORT_ERR_CONNECT;
        }
    }

    rc = transport->api.send_command(
        (short)RP1210_CMD_FLUSH_TX_RX,
        transport->client_id,
        NULL,
        0);
    (void)rc;

    {
        unsigned char claim[10];

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

        rc = transport->api.send_command(
            (short)RP1210_CMD_PROTECT_J1939_ADDRESS,
            transport->client_id,
            (char *)claim,
            (short)sizeof(claim));

        if (rc != 0) {
            set_rp1210_error(
                transport,
                "RP1210_Protect_J1939_Address failed",
                rc);
            return RP1210_TRANSPORT_ERR_CONNECT;
        }
    }

    return RP1210_TRANSPORT_OK;
}

struct rp1210_transport *
rp1210_transport_create(void)
{
    struct rp1210_transport *transport;

    transport = (struct rp1210_transport *)calloc(
        1U,
        sizeof(struct rp1210_transport));
    if (transport == NULL)
        return NULL;

    transport->client_id = -1;
    return transport;
}

void
rp1210_transport_destroy(struct rp1210_transport *transport)
{
    if (transport == NULL)
        return;

    rp1210_transport_close(transport);
    free(transport);
}

int
rp1210_transport_open(struct rp1210_transport *transport,
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
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    rp1210_transport_close(transport);
    transport->last_error[0] = '\0';
    transport->pgn = pgn;
    transport->priority = priority;
    transport->source_address = source_address;
    transport->destination_address = destination_address;

    rc = load_api(transport, api_name);
    if (rc != RP1210_TRANSPORT_OK)
        return rc;

    rc = connect_j1939(transport, device_id, baud);
    if (rc != RP1210_TRANSPORT_OK)
        return rc;

    return RP1210_TRANSPORT_OK;
}

void
rp1210_transport_close(struct rp1210_transport *transport)
{
    if (transport == NULL)
        return;

    if (transport->client_id >= 0 &&
        transport->api.client_disconnect != NULL) {
        transport->api.client_disconnect(transport->client_id);
    }
    transport->client_id = -1;

    if (transport->api.module != NULL)
        FreeLibrary(transport->api.module);

    memset(&transport->api, 0, sizeof(transport->api));
}

int
rp1210_transport_send(struct rp1210_transport *transport,
                      const unsigned char *payload,
                      size_t payload_len)
{
    unsigned char buffer[RP1210_TRANSPORT_MAX_PAYLOAD + 6U];
    short rc;
    size_t total;

    if (transport == NULL || payload == NULL)
        return RP1210_TRANSPORT_ERR_ARGUMENT;

    if (transport->client_id < 0) {
        set_error_text(transport, "RP1210 transport is not connected.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    if (payload_len > RP1210_TRANSPORT_MAX_PAYLOAD) {
        set_error_text(
            transport,
            "J1939 payload is larger than the RP1210 send buffer.");
        return RP1210_TRANSPORT_ERR_PROTOCOL;
    }

    buffer[0] = (unsigned char)(transport->pgn & 0xffUL);
    buffer[1] = (unsigned char)((transport->pgn >> 8) & 0xffUL);
    buffer[2] = (unsigned char)((transport->pgn >> 16) & 0xffUL);
    buffer[3] = transport->priority;
    buffer[4] = transport->source_address;
    buffer[5] = transport->destination_address;
    memcpy(buffer + 6U, payload, payload_len);
    total = payload_len + 6U;

    rc = transport->api.send_message(
        transport->client_id,
        (char *)buffer,
        (short)total,
        0,
        0);
    if (rc != 0) {
        set_rp1210_error(transport, "RP1210_SendMessage failed", rc);
        return RP1210_TRANSPORT_ERR_SEND;
    }

    return RP1210_TRANSPORT_OK;
}

int
rp1210_transport_receive(struct rp1210_transport *transport,
                         unsigned char *payload,
                         size_t payload_capacity,
                         size_t *payload_len,
                         unsigned long timeout_ms)
{
    unsigned char buffer[RP1210_TRANSPORT_MAX_PAYLOAD + 32U];
    DWORD start;
    DWORD now;
    short n;
    unsigned long pgn;
    size_t n_payload;
    unsigned char source_address;
    unsigned char destination_address;

    if (transport == NULL ||
        payload == NULL ||
        payload_len == NULL) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    if (transport->client_id < 0) {
        set_error_text(transport, "RP1210 transport is not connected.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    start = GetTickCount();

    for (;;) {
        n = transport->api.read_message(
            transport->client_id,
            (char *)buffer,
            (short)sizeof(buffer),
            0);

        if (n > 0) {
            if (n >= 10) {
                pgn = (unsigned long)buffer[4] |
                      ((unsigned long)buffer[5] << 8) |
                      ((unsigned long)buffer[6] << 16);
                source_address = buffer[8];
                destination_address = buffer[9];

                if (pgn == transport->pgn &&
                    source_address == transport->destination_address &&
                    (destination_address == transport->source_address ||
                     destination_address == 0xffU)) {
                    n_payload = (size_t)n - 10U;
                    if (n_payload > payload_capacity) {
                        set_error_text(
                            transport,
                            "Incoming J1939 payload exceeds receive buffer.");
                        return RP1210_TRANSPORT_ERR_PROTOCOL;
                    }

                    memcpy(payload, buffer + 10U, n_payload);
                    *payload_len = n_payload;
                    return RP1210_TRANSPORT_OK;
                }
            }
        } else if (n < 0) {
            set_rp1210_error(
                transport,
                "RP1210_ReadMessage failed",
                (short)(-n));
            return RP1210_TRANSPORT_ERR_RECEIVE;
        }

        now = GetTickCount();
        if ((DWORD)(now - start) >= (DWORD)timeout_ms)
            break;

        Sleep(1);
    }

    set_error_text(
        transport,
        "Timed out waiting for matching J1939 response.");
    return RP1210_TRANSPORT_ERR_TIMEOUT;
}

const char *
rp1210_transport_error(const struct rp1210_transport *transport)
{
    if (transport == NULL)
        return "RP1210 transport is not initialized.";

    return transport->last_error;
}
