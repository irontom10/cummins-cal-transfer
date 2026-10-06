#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "j1939_transport.h"
#include "rp1210_transport.h"

struct rp1210_transport {
    char last_error[128];
};

static int g_fail_connect;
static int g_open_count;
static char g_protocols[4][64];
static int g_command_count;
static int g_commands[16];
static unsigned char g_command_data[16];
static size_t g_command_len[16];
static unsigned char g_last_send[128];
static size_t g_last_send_len;
static unsigned char g_next_rx[128];
static size_t g_next_rx_len;

static void
reset_fake(void)
{
    g_fail_connect = 0;
    g_open_count = 0;
    memset(g_protocols, 0, sizeof(g_protocols));
    g_command_count = 0;
    memset(g_commands, 0, sizeof(g_commands));
    memset(g_command_data, 0, sizeof(g_command_data));
    memset(g_command_len, 0, sizeof(g_command_len));
    memset(g_last_send, 0, sizeof(g_last_send));
    g_last_send_len = 0U;
    memset(g_next_rx, 0, sizeof(g_next_rx));
    g_next_rx_len = 0U;
}

struct rp1210_transport *
rp1210_transport_create(void)
{
    return (struct rp1210_transport *)calloc(
        1U, sizeof(struct rp1210_transport));
}

void
rp1210_transport_destroy(struct rp1210_transport *transport)
{
    free(transport);
}

int
rp1210_transport_open(struct rp1210_transport *transport,
                      const char *api_name,
                      int device_id,
                      const char *protocol)
{
    (void)api_name;
    (void)device_id;

    if (g_open_count < 4) {
        strncpy(g_protocols[g_open_count], protocol,
                sizeof(g_protocols[g_open_count]) - 1U);
        g_protocols[g_open_count][sizeof(g_protocols[g_open_count]) - 1U] =
            '\0';
    }
    ++g_open_count;

    if (g_fail_connect) {
        strcpy(transport->last_error, "synthetic connect failure");
        return RP1210_TRANSPORT_ERR_CONNECT;
    }

    return RP1210_TRANSPORT_OK;
}

void
rp1210_transport_close(struct rp1210_transport *transport)
{
    (void)transport;
}

int
rp1210_transport_send(struct rp1210_transport *transport,
                      const unsigned char *message,
                      size_t message_len)
{
    (void)transport;

    assert(message_len <= sizeof(g_last_send));
    memcpy(g_last_send, message, message_len);
    g_last_send_len = message_len;
    return RP1210_TRANSPORT_OK;
}

int
rp1210_transport_receive(struct rp1210_transport *transport,
                         unsigned char *message,
                         size_t message_capacity,
                         size_t *message_len,
                         unsigned long timeout_ms)
{
    (void)transport;
    (void)timeout_ms;

    if (g_next_rx_len == 0U)
        return RP1210_TRANSPORT_ERR_TIMEOUT;

    assert(g_next_rx_len <= message_capacity);
    memcpy(message, g_next_rx, g_next_rx_len);
    *message_len = g_next_rx_len;
    g_next_rx_len = 0U;
    return RP1210_TRANSPORT_OK;
}

int
rp1210_transport_command(struct rp1210_transport *transport,
                         int command,
                         const unsigned char *data,
                         size_t data_len)
{
    (void)transport;

    assert(g_command_count < 16);
    g_commands[g_command_count] = command;
    g_command_len[g_command_count] = data_len;
    g_command_data[g_command_count] =
        (data != NULL && data_len != 0U) ? data[0] : 0xffU;
    ++g_command_count;
    return RP1210_TRANSPORT_OK;
}

const char *
rp1210_transport_error(const struct rp1210_transport *transport)
{
    return transport != NULL ? transport->last_error : "";
}

static void
test_fixed_baud_never_falls_back(void)
{
    struct j1939_transport *transport;
    int rc;

    reset_fake();
    g_fail_connect = 1;

    transport = j1939_transport_create();
    assert(transport != NULL);

    rc = j1939_transport_open(
        transport, "FAKE", 1, 500000, 0x00ef00UL,
        6U, 0xfaU, 0x00U);

    assert(rc == J1939_TRANSPORT_ERR_CONNECT);
    assert(g_open_count == 2);
    assert(strcmp(g_protocols[0], "J1939:Baud=500") == 0);
    assert(strcmp(g_protocols[1], "J1939:Baud=500000") == 0);

    j1939_transport_destroy(transport);
}

static void
test_auto_baud_is_explicit(void)
{
    struct j1939_transport *transport;

    reset_fake();

    transport = j1939_transport_create();
    assert(transport != NULL);

    assert(j1939_transport_open(
               transport, "FAKE", 1, 0, 0x00ef00UL,
               6U, 0xfaU, 0x00U) == J1939_TRANSPORT_OK);
    assert(g_open_count == 1);
    assert(strcmp(g_protocols[0], "J1939:Baud=Auto") == 0);

    j1939_transport_destroy(transport);
}

static void
test_echo_off_and_framing(void)
{
    struct j1939_transport *transport;
    unsigned char payload[8];
    size_t payload_len;
    static const unsigned char tx_payload[] = {0xaaU, 0x55U};

    reset_fake();

    transport = j1939_transport_create();
    assert(transport != NULL);

    assert(j1939_transport_open(
               transport, "FAKE", 1, 250000, 0x00ef00UL,
               6U, 0xfaU, 0x00U) == J1939_TRANSPORT_OK);

    assert(g_command_count >= 1);
    assert(g_commands[0] == 16);
    assert(g_command_len[0] == 1U);
    assert(g_command_data[0] == 0x00U);

    assert(j1939_transport_send(
               transport, tx_payload, sizeof(tx_payload)) ==
           J1939_TRANSPORT_OK);
    assert(g_last_send_len == 8U);
    assert(g_last_send[0] == 0x00U);
    assert(g_last_send[1] == 0xefU);
    assert(g_last_send[2] == 0x00U);
    assert(g_last_send[3] == 6U);
    assert(g_last_send[4] == 0xfaU);
    assert(g_last_send[5] == 0x00U);
    assert(g_last_send[6] == 0xaaU);
    assert(g_last_send[7] == 0x55U);

    g_next_rx[0] = 0x00U;
    g_next_rx[1] = 0x00U;
    g_next_rx[2] = 0x00U;
    g_next_rx[3] = 0x01U;
    g_next_rx[4] = 0x00U;
    g_next_rx[5] = 0xefU;
    g_next_rx[6] = 0x00U;
    g_next_rx[7] = 6U;
    g_next_rx[8] = 0x00U;
    g_next_rx[9] = 0xfaU;
    g_next_rx[10] = 0x12U;
    g_next_rx[11] = 0x34U;
    g_next_rx_len = 12U;

    payload_len = 0U;
    assert(j1939_transport_receive(
               transport, payload, sizeof(payload), &payload_len, 100UL) ==
           J1939_TRANSPORT_OK);
    assert(payload_len == 2U);
    assert(payload[0] == 0x12U);
    assert(payload[1] == 0x34U);

    j1939_transport_destroy(transport);
}

int
main(void)
{
    test_fixed_baud_never_falls_back();
    test_auto_baud_is_explicit();
    test_echo_off_and_framing();

    puts("j1939 transport tests passed");
    return 0;
}
