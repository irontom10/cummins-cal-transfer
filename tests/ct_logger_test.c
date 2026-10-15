/* C89 smoke test for persistent transfer events and opt-in bus traces. */
#include "ct_logger.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int contains(const char *file_name, const char *needle)
{
    char line[1024];
    FILE *fp;
    int found;
    fp = fopen(file_name, "r");
    if (fp == NULL) return 0;
    found = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, needle) != NULL) found = 1;
    }
    fclose(fp);
    return found;
}

int main(void)
{
    unsigned char data[3];
    FILE *fp;
    const char *events;
    const char *bus;

    events = "ct-logger-test-events.log";
    bus = "ct-logger-test-can.log";
    remove(events);
    remove(bus);
    data[0] = 0x12U;
    data[1] = 0x34U;
    data[2] = 0xefU;

    ct_log_configure(events, bus, 0);
    assert(ct_log_begin("PULL", "TEST", 1, 250000, 250U, 0U) == 0);
    ct_log_event("ERROR", "TEST", "adapter disconnected");
    ct_log_can_frame("TX", 101UL, 0xef00UL, 6U, 250U, 0U, data, 3U);
    ct_log_end("PULL", -5, "test");
    assert(contains(events, "adapter disconnected"));
    assert(contains(events, "rc=-5"));
    fp = fopen(bus, "r");
    assert(fp == NULL);

    ct_log_configure(events, bus, 1);
    assert(ct_log_begin("UPLOAD", "TEST", 1, 250000, 250U, 0U) == 0);
    ct_log_can_frame("TX", 102UL, 0xef00UL, 6U, 250U, 0U, data, 3U);
    ct_log_can_raw("RX-RAW", 103UL, data, 3U);
    ct_log_end("UPLOAD", 0, "");
    assert(contains(bus, "PGN=00EF00"));
    assert(contains(bus, "12 34 EF"));
    assert(contains(bus, "RX-RAW"));
    assert(contains(events, "END UPLOAD rc=0"));
    remove(events);
    remove(bus);
    return 0;
}
