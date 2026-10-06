/*
 * rp1210_transport_android.c
 *
 * Android implementation of the generic RP1210 transport interface.
 *
 * The Cummins/J1939 core knows nothing about Bluetooth or Android.  The Java
 * front end selects/pairs a device, passes its MAC address here, and this
 * backend loads the selected vendor RP1210 .so and exposes the same transport
 * contract used by the Windows implementation.
 *
 * C89 source.
 */

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ct_platform.h"
#include "rp1210_transport.h"
#include "rp1210_android.h"

#define IOCTL_SET_DATA_PATH   0x102UL
#define IOCTL_SELECT_DEVICE   0x103UL
#define NON_BLOCKING_IO       0

typedef short (*PFN_RP1210_CLIENT_CONNECT)(
    unsigned int, short, char *, long, long, short);
typedef short (*PFN_RP1210_CLIENT_DISCONNECT)(short);
typedef short (*PFN_RP1210_SEND_MESSAGE)(
    short, char *, short, short, short);
typedef short (*PFN_RP1210_READ_MESSAGE)(
    short, char *, short, short);
typedef short (*PFN_RP1210_SEND_COMMAND)(
    short, short, char *, short);
typedef short (*PFN_RP1210_GET_ERROR_MSG)(short, char *);
typedef short (*PFN_RP1210_IOCTL)(
    short, unsigned long, void *, void *);

struct rp1210_data_path {
    unsigned char *DataPath;
};

struct rp1210_device_name {
    unsigned char *DeviceName;
};

struct rp1210_api {
    void *module;
    PFN_RP1210_CLIENT_CONNECT client_connect;
    PFN_RP1210_CLIENT_DISCONNECT client_disconnect;
    PFN_RP1210_SEND_MESSAGE send_message;
    PFN_RP1210_READ_MESSAGE read_message;
    PFN_RP1210_SEND_COMMAND send_command;
    PFN_RP1210_GET_ERROR_MSG get_error_msg;
    PFN_RP1210_IOCTL ioctl_fn;
};

struct rp1210_transport {
    struct rp1210_api api;
    short client_id;
    char last_error[512];
};

static char g_data_path[1024];
static char g_mac_address[32];

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

static int
ascii_equal(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL)
        return 0;

    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;

        if (ca >= 'a' && ca <= 'z')
            ca = (unsigned char)(ca - ('a' - 'A'));
        if (cb >= 'a' && cb <= 'z')
            cb = (unsigned char)(cb - ('a' - 'A'));

        if (ca != cb)
            return 0;
    }

    return *a == '\0' && *b == '\0';
}

static const char *
library_for_api(const char *api_name)
{
    if (ascii_equal(api_name, "NULN2R32") ||
        ascii_equal(api_name, "USBL2"))
        return "libnuln2r32.so";

    if (ascii_equal(api_name, "NULN3R32") ||
        ascii_equal(api_name, "USBL3"))
        return "libnuln3r32.so";

    if (ascii_equal(api_name, "NBLR32") ||
        ascii_equal(api_name, "BLMINI"))
        return "libnblr32.so";

    if (ascii_equal(api_name, "NBL2R32") ||
        ascii_equal(api_name, "BL2"))
        return "libnbl2r32.so";

    if (ascii_equal(api_name, "CIL7R32") ||
        ascii_equal(api_name, "CIL7"))
        return "libcil7r32.so";

    if (ascii_equal(api_name, "CIMR32") ||
        ascii_equal(api_name, "CILMINI"))
        return "libcimr32.so";

    if (ascii_equal(api_name, "CIM16R32") ||
        ascii_equal(api_name, "CILMINI16"))
        return "libcim16r32.so";

    if (ascii_equal(api_name, "CULN3R32") ||
        ascii_equal(api_name, "CUL3"))
        return "libculn3r32.so";

    if (ascii_equal(api_name, "KULN3R32") ||
        ascii_equal(api_name, "KUL3"))
        return "libkuln3r32.so";

    return NULL;
}

static void
set_error_text(struct rp1210_transport *transport, const char *text)
{
    if (transport != NULL)
        safe_copy(transport->last_error, text, sizeof(transport->last_error));
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
        sprintf(tmp, "%s: RP1210 %d - %.300s", what, (int)code, message);
    else
        sprintf(tmp, "%s: RP1210 error %d", what, (int)code);

    set_error_text(transport, tmp);
}

static int
load_api(struct rp1210_transport *transport, const char *api_name)
{
    const char *library;
    const char *error_text;

    if (transport == NULL || api_name == NULL || api_name[0] == '\0')
        return RP1210_TRANSPORT_ERR_ARGUMENT;

    library = library_for_api(api_name);
    if (library == NULL) {
        set_error_text(transport, "Unsupported Android RP1210 implementation.");
        return RP1210_TRANSPORT_ERR_LOAD_API;
    }

    dlerror();
    transport->api.module = dlopen(library, RTLD_GLOBAL | RTLD_NOW);
    if (transport->api.module == NULL) {
        char tmp[512];

        error_text = dlerror();
        sprintf(tmp,
                "Unable to load Android RP1210 library %s: %.350s",
                library,
                error_text != NULL ? error_text : "unknown dlopen error");
        set_error_text(transport, tmp);
        return RP1210_TRANSPORT_ERR_LOAD_API;
    }

    transport->api.client_connect = (PFN_RP1210_CLIENT_CONNECT)
        dlsym(transport->api.module, "RP1210_ClientConnect");
    transport->api.client_disconnect = (PFN_RP1210_CLIENT_DISCONNECT)
        dlsym(transport->api.module, "RP1210_ClientDisconnect");
    transport->api.send_message = (PFN_RP1210_SEND_MESSAGE)
        dlsym(transport->api.module, "RP1210_SendMessage");
    transport->api.read_message = (PFN_RP1210_READ_MESSAGE)
        dlsym(transport->api.module, "RP1210_ReadMessage");
    transport->api.send_command = (PFN_RP1210_SEND_COMMAND)
        dlsym(transport->api.module, "RP1210_SendCommand");
    transport->api.get_error_msg = (PFN_RP1210_GET_ERROR_MSG)
        dlsym(transport->api.module, "RP1210_GetErrorMsg");
    transport->api.ioctl_fn = (PFN_RP1210_IOCTL)
        dlsym(transport->api.module, "RP1210_Ioctl");

    if (transport->api.client_connect == NULL ||
        transport->api.client_disconnect == NULL ||
        transport->api.send_message == NULL ||
        transport->api.read_message == NULL ||
        transport->api.send_command == NULL ||
        transport->api.ioctl_fn == NULL) {
        set_error_text(
            transport,
            "Android RP1210 library is missing a required export.");
        dlclose(transport->api.module);
        memset(&transport->api, 0, sizeof(transport->api));
        return RP1210_TRANSPORT_ERR_SYMBOL;
    }

    return RP1210_TRANSPORT_OK;
}

int
rp1210_android_configure(const char *data_path, const char *mac_address)
{
    if (data_path == NULL || data_path[0] == '\0' ||
        mac_address == NULL || strlen(mac_address) != 17U) {
        return 0;
    }

    safe_copy(g_data_path, data_path, sizeof(g_data_path));
    safe_copy(g_mac_address, mac_address, sizeof(g_mac_address));
    return 1;
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
    struct rp1210_data_path data_path;
    struct rp1210_device_name device_name;
    short rc;
    short client;
    int load_rc;

    if (transport == NULL ||
        api_name == NULL || api_name[0] == '\0' ||
        protocol == NULL || protocol[0] == '\0' ||
        device_id < 0) {
        return RP1210_TRANSPORT_ERR_ARGUMENT;
    }

    if (g_data_path[0] == '\0' || g_mac_address[0] == '\0') {
        set_error_text(
            transport,
            "Android RP1210 device was not configured with a data path and Bluetooth MAC.");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    rp1210_transport_close(transport);
    transport->last_error[0] = '\0';

    load_rc = load_api(transport, api_name);
    if (load_rc != RP1210_TRANSPORT_OK)
        return load_rc;

    data_path.DataPath = (unsigned char *)g_data_path;
    rc = transport->api.ioctl_fn(0, IOCTL_SET_DATA_PATH, &data_path, NULL);
    if (rc != 0) {
        set_rp1210_error(transport, "IOCTL_SET_DATA_PATH failed", rc);
        rp1210_transport_close(transport);
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    device_name.DeviceName = (unsigned char *)g_mac_address;
    rc = transport->api.ioctl_fn(0, IOCTL_SELECT_DEVICE, &device_name, NULL);
    if (rc != 0) {
        set_rp1210_error(transport, "IOCTL_SELECT_DEVICE failed", rc);
        rp1210_transport_close(transport);
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    /*
     * Android device ID 2 is the Bluetooth entry in the supplied mobile
     * RP1210 INI files.  The public core still passes the device ID through,
     * so this backend remains compatible if another transport is added later.
     */
    client = transport->api.client_connect(
        0U,
        (short)device_id,
        (char *)protocol,
        1024L * 1024L,
        1024L * 1024L,
        0);

    if (client < 0 || client > 127) {
        set_rp1210_error(transport, "RP1210_ClientConnect failed", client);
        rp1210_transport_close(transport);
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
        dlclose(transport->api.module);

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
        NON_BLOCKING_IO);

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
    unsigned long start;
    unsigned long now;
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

    read_capacity = message_capacity > (size_t)SHRT_MAX ?
        SHRT_MAX : (short)message_capacity;

    *message_len = 0U;
    start = ct_monotonic_ms();

    for (;;) {
        n = transport->api.read_message(
            transport->client_id,
            (char *)message,
            read_capacity,
            NON_BLOCKING_IO);

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

        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= timeout_ms)
            break;

        ct_sleep_ms(1UL);
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
