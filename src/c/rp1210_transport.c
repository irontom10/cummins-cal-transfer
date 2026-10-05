/*
 * rp1210_transport.c
 *
 * Generic 32-bit Windows RP1210 transport.
 *
 * This module owns only vendor DLL loading, RP1210 client lifetime, raw
 * RP1210 send/receive, commands, and error reporting.  It intentionally has
 * no knowledge of J1939, PGNs, source addresses, CLIP, calibration files, or
 * ECM session state.
 *
 * C89 source.
 */

#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rp1210_transport.h"

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
                      const char *protocol)
{
    int rc;
    short client;

    if (transport == NULL ||
        api_name == NULL || api_name[0] == '\0' ||
        protocol == NULL || protocol[0] == '\0' ||
        device_id < 0) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    rp1210_transport_close(transport);
    transport->last_error[0] = '\0';

    rc = load_api(transport, api_name);
    if (rc != RP1210_TRANSPORT_OK)
        return rc;

    client = transport->api.client_connect(NULL,
                                            (short)device_id,
                                            (char *)protocol,
                                            0L,
                                            0L,
                                            0);
    if (client < 0 || client > 127) {
        set_rp1210_error(transport, "RP1210_ClientConnect failed", client);
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    transport->client_id = client;
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
                      const unsigned char *message,
                      size_t message_len)
{
    short rc;

    if (transport == NULL ||
        (message == NULL && message_len != 0U) ||
        message_len > (size_t)SHRT_MAX) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    if (transport->client_id < 0) {
        set_error_text(transport, "RP1210 transport is not connected.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    rc = transport->api.send_message(
        transport->client_id,
        (char *)message,
        (short)message_len,
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
                         unsigned char *message,
                         size_t message_capacity,
                         size_t *message_len,
                         unsigned long timeout_ms)
{
    DWORD start;
    DWORD now;
    short n;
    short read_capacity;

    if (transport == NULL ||
        message == NULL ||
        message_len == NULL ||
        message_capacity == 0U) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    if (transport->client_id < 0) {
        set_error_text(transport, "RP1210 transport is not connected.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    if (message_capacity > (size_t)SHRT_MAX)
        read_capacity = SHRT_MAX;
    else
        read_capacity = (short)message_capacity;

    *message_len = 0U;
    start = GetTickCount();

    for (;;) {
        n = transport->api.read_message(
            transport->client_id,
            (char *)message,
            read_capacity,
            0);

        if (n > 0) {
            *message_len = (size_t)n;
            return RP1210_TRANSPORT_OK;
        }

        if (n < 0) {
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

    set_error_text(transport, "Timed out waiting for RP1210 message.");
    return RP1210_TRANSPORT_ERR_TIMEOUT;
}

int
rp1210_transport_command(struct rp1210_transport *transport,
                         int command,
                         const unsigned char *data,
                         size_t data_len)
{
    short rc;

    if (transport == NULL ||
        command < 0 || command > SHRT_MAX ||
        (data == NULL && data_len != 0U) ||
        data_len > (size_t)SHRT_MAX) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    if (transport->client_id < 0) {
        set_error_text(transport, "RP1210 transport is not connected.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    rc = transport->api.send_command(
        (short)command,
        transport->client_id,
        (char *)data,
        (short)data_len);
    if (rc != 0) {
        set_rp1210_error(transport, "RP1210_SendCommand failed", rc);
        return RP1210_TRANSPORT_ERR_COMMAND;
    }

    return RP1210_TRANSPORT_OK;
}

const char *
rp1210_transport_error(const struct rp1210_transport *transport)
{
    if (transport == NULL)
        return "RP1210 transport is not initialized.";

    return transport->last_error;
}
