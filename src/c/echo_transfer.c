/*
 * echo_transfer.c
 *
 * ECH / ECHO-series calibration readback over the legacy raw Proprietary-A
 * services observed in the supplied trace.
 *
 * Transport is injected through echo_io.  This module has no RP1210
 * dependency and deliberately contains no programming/write implementation.
 *
 * C89 source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "echo_transfer.h"
#include "ccal_crc.h"
#include "ct_platform.h"

#define ECHO_MAX_WIRE          4096U
#define ECHO_TIMEOUT_MS        5000UL
#define ECHO_READ_TIMEOUT_MS   10000UL
#define ECHO_BLOCK_SIZE        1000U
#define ECHO_MAX_RANGES        16U

#define ECHO_PRODUCT_ID        0x0043U
#define ECHO_DESCRIPTOR_ID     0x002eU
#define ECHO_DESCRIPTOR_SIZE   0x0030U

struct echo_range {
    unsigned long address;
    unsigned long length;
    unsigned char *data;
};

struct echo_image {
    unsigned int range_count;
    struct echo_range ranges[ECHO_MAX_RANGES];
};

struct echo_meta {
    char calibration_version[32];
    char module_id[32];
    char product_id[32];
    char module_part_number[32];
    char market_id[64];
    char interface_level[32];
    char start_boot_loader_version[32];
    char end_boot_loader_version[32];
    char engine_id[32];
    char fuel_system_id[32];
    char byte_order[32];
    char address_length[16];
    char cpp_data_link[32];
};

static char g_echo_error[512];

static void
set_error(const char *text)
{
    size_t n;

    if (text == NULL) {
        g_echo_error[0] = '\0';
        return;
    }

    n = strlen(text);
    if (n >= sizeof(g_echo_error))
        n = sizeof(g_echo_error) - 1U;

    if (n != 0U)
        memcpy(g_echo_error, text, n);
    g_echo_error[n] = '\0';
}

static void
progress(const echo_io *io, int percent, const char *message)
{
    if (io != NULL && io->progress != NULL)
        io->progress(io->user, percent, message);
}

static unsigned int
load_be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static unsigned long
load_be32(const unsigned char *p)
{
    unsigned long v;

    v = ((unsigned long)p[0] << 24);
    v |= ((unsigned long)p[1] << 16);
    v |= ((unsigned long)p[2] << 8);
    v |= (unsigned long)p[3];
    return v;
}

static void
store_be32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int
exchange(const echo_io *io,
         const unsigned char *request,
         unsigned int request_len,
         unsigned char expected_opcode,
         unsigned char *reply,
         unsigned int reply_capacity,
         unsigned int *reply_len,
         unsigned long timeout_ms)
{
    unsigned char incoming[ECHO_MAX_WIRE];
    unsigned int incoming_len;
    unsigned long start;
    unsigned long now;
    int rc;

    if (io == NULL || io->send == NULL || io->recv == NULL ||
        request == NULL || request_len == 0U ||
        reply == NULL || reply_len == NULL) {
        return ECHO_TRANSFER_ERR_ARGUMENT;
    }

    rc = io->send(io->user, request, request_len);
    if (rc != 0) {
        set_error("ECH/ECHO transport send failed.");
        return ECHO_TRANSFER_ERR_TRANSPORT;
    }

    start = ct_monotonic_ms();
    for (;;) {
        now = ct_monotonic_ms();
        if ((unsigned long)(now - start) >= (unsigned long)timeout_ms)
            break;

        incoming_len = 0U;
        rc = io->recv(io->user,
                      incoming,
                      (unsigned int)sizeof(incoming),
                      &incoming_len,
                      timeout_ms - (unsigned long)(unsigned long)(now - start));
        if (rc != 0) {
            set_error("ECH/ECHO transport receive failed.");
            return ECHO_TRANSFER_ERR_TRANSPORT;
        }

        if (incoming_len >= 3U &&
            incoming[0] == 0x0dU &&
            incoming[2] == request[0]) {
            char msg[160];
            sprintf(msg,
                    "ECH/ECHO negative response to service %02X (reason %02X).",
                    (unsigned int)request[0],
                    (unsigned int)incoming[1]);
            set_error(msg);
            return ECHO_TRANSFER_ERR_PROTOCOL;
        }

        if (incoming_len == 0U || incoming[0] != expected_opcode)
            continue;

        if (incoming_len > reply_capacity) {
            set_error("ECH/ECHO response exceeds receive buffer.");
            return ECHO_TRANSFER_ERR_PROTOCOL;
        }

        memcpy(reply, incoming, incoming_len);
        *reply_len = incoming_len;
        return ECHO_TRANSFER_OK;
    }

    set_error("Timed out waiting for ECH/ECHO response.");
    return ECHO_TRANSFER_ERR_TRANSPORT;
}

static int
read_parameter(const echo_io *io,
               unsigned int identifier,
               unsigned int length,
               unsigned char *out,
               unsigned int out_capacity)
{
    unsigned char request[8];
    unsigned char reply[ECHO_MAX_WIRE];
    unsigned int reply_len;
    int rc;

    if (io == NULL || out == NULL ||
        length == 0U || length > 0xffU ||
        out_capacity < length) {
        return ECHO_TRANSFER_ERR_ARGUMENT;
    }

    request[0] = 0x43U;
    request[1] = (unsigned char)((identifier >> 8) & 0xffU);
    request[2] = (unsigned char)(identifier & 0xffU);
    request[3] = 0x00U;
    request[4] = 0x00U;
    request[5] = 0x00U;
    request[6] = (unsigned char)length;
    request[7] = 0xffU;

    rc = exchange(io,
                  request,
                  (unsigned int)sizeof(request),
                  0x44U,
                  reply,
                  (unsigned int)sizeof(reply),
                  &reply_len,
                  ECHO_TIMEOUT_MS);
    if (rc != ECHO_TRANSFER_OK)
        return rc;

    if (reply_len < 4U + length ||
        reply[1] != request[1] ||
        reply[2] != request[2] ||
        reply[3] != (unsigned char)length) {
        set_error("Invalid ECH/ECHO parameter reply.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    memcpy(out, reply + 4U, length);
    return ECHO_TRANSFER_OK;
}

static int
transfer_control(const echo_io *io, unsigned char opcode)
{
    unsigned char request[8];
    unsigned char reply[32];
    unsigned int reply_len;
    int rc;

    request[0] = opcode;
    request[1] = 0xfeU;
    request[2] = 0xfeU;
    request[3] = 0xffU;
    request[4] = 0xffU;
    request[5] = 0xffU;
    request[6] = 0xffU;
    request[7] = 0xffU;

    rc = exchange(io,
                  request,
                  (unsigned int)sizeof(request),
                  0x0cU,
                  reply,
                  (unsigned int)sizeof(reply),
                  &reply_len,
                  ECHO_TIMEOUT_MS);
    if (rc != ECHO_TRANSFER_OK)
        return rc;

    if (reply_len < 4U ||
        reply[1] != opcode ||
        reply[2] != 0xfeU ||
        reply[3] != 0xfeU) {
        set_error("Invalid ECH/ECHO transfer-control acknowledgement.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    return ECHO_TRANSFER_OK;
}

static int
read_descriptor(const echo_io *io, struct echo_image *image)
{
    unsigned char data[ECHO_DESCRIPTOR_SIZE];
    unsigned int nested_len;
    unsigned int count;
    unsigned int address_size;
    unsigned int length_size;
    unsigned int i;
    size_t starts_off;
    size_t length_size_off;
    size_t lengths_off;
    size_t required;
    int rc;

    if (io == NULL || image == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    memset(image, 0, sizeof(*image));

    rc = read_parameter(io,
                        ECHO_DESCRIPTOR_ID,
                        ECHO_DESCRIPTOR_SIZE,
                        data,
                        (unsigned int)sizeof(data));
    if (rc != ECHO_TRANSFER_OK)
        return rc;

    /*
     * ECH descriptor 0x002E from the supplied trace:
     *   u16 payload_length
     *   u16 flags
     *   u16 range_count
     *   u16 address_width (=4)
     *   be32 start[range_count]
     *   u16 length_width (=4)
     *   be32 length[range_count]
     *
     * The validated trace contains three ranges:
     *   00008000 + 00038000
     *   00040000 + 00024D86
     *   01000080 + 00001AB8
     */
    nested_len = load_be16(data);
    count = load_be16(data + 4U);
    address_size = load_be16(data + 6U);

    if ((size_t)nested_len + 2U > sizeof(data) ||
        count == 0U ||
        count > ECHO_MAX_RANGES ||
        address_size != 4U) {
        set_error("Unsupported ECH/ECHO calibration descriptor layout.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    starts_off = 8U;
    length_size_off = starts_off + ((size_t)count * 4U);
    if (length_size_off + 2U > sizeof(data)) {
        set_error("Truncated ECH/ECHO calibration descriptor.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    length_size = load_be16(data + length_size_off);
    if (length_size != 4U) {
        set_error("Unsupported ECH/ECHO calibration length width.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    lengths_off = length_size_off + 2U;
    required = lengths_off + ((size_t)count * 4U);
    if (required > sizeof(data)) {
        set_error("Truncated ECH/ECHO calibration range table.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    image->range_count = count;
    for (i = 0U; i < count; ++i) {
        image->ranges[i].address =
            load_be32(data + starts_off + ((size_t)i * 4U));
        image->ranges[i].length =
            load_be32(data + lengths_off + ((size_t)i * 4U));

        if (image->ranges[i].length == 0UL ||
            image->ranges[i].address >
            0xffffffffUL - image->ranges[i].length) {
            set_error("Invalid ECH/ECHO calibration memory range.");
            return ECHO_TRANSFER_ERR_PROTOCOL;
        }
    }

    return ECHO_TRANSFER_OK;
}

static void
trim_ascii(char *dst,
           size_t dst_size,
           const unsigned char *src,
           unsigned int src_len)
{
    unsigned int n;

    if (dst == NULL || dst_size == 0U)
        return;

    n = src_len;
    while (n > 0U &&
           (src[n - 1U] == 0x00U || src[n - 1U] == 0x20U))
        --n;

    if ((size_t)n >= dst_size)
        n = (unsigned int)(dst_size - 1U);

    if (n != 0U)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static void
format_hex_bytes(char *dst,
                 size_t dst_size,
                 const unsigned char *src,
                 unsigned int src_len)
{
    unsigned int i;
    size_t used;

    if (dst == NULL || dst_size == 0U)
        return;

    used = 0U;
    dst[0] = '\0';

    for (i = 0U; i < src_len; ++i) {
        if (used + 2U >= dst_size)
            break;
        sprintf(dst + used, "%02X", (unsigned int)src[i]);
        used += 2U;
    }
}

static int
collect_metadata(const echo_io *io, struct echo_meta *meta)
{
    unsigned char data[64];
    int rc;

    if (io == NULL || meta == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    memset(meta, 0, sizeof(*meta));

#define ECHO_PARAM(id_, len_) \
    do { \
        rc = read_parameter(io, (id_), (len_), data, (unsigned int)sizeof(data)); \
        if (rc != ECHO_TRANSFER_OK) return rc; \
    } while (0)

    ECHO_PARAM(0x0019U, 4U);
    format_hex_bytes(meta->calibration_version,
                     sizeof(meta->calibration_version),
                     data,
                     4U);

    ECHO_PARAM(0x0000U, 2U);
    trim_ascii(meta->module_id, sizeof(meta->module_id), data, 2U);

    ECHO_PARAM(0x0043U, 3U);
    trim_ascii(meta->product_id, sizeof(meta->product_id), data, 3U);

    ECHO_PARAM(0x0001U, 4U);
    sprintf(meta->module_part_number, "%lu", load_be32(data));

    ECHO_PARAM(0x0044U, 16U);
    trim_ascii(meta->market_id, sizeof(meta->market_id), data, 16U);

    ECHO_PARAM(0x002dU, 2U);
    sprintf(meta->interface_level, "%u", load_be16(data));

    ECHO_PARAM(0x0046U, 4U);
    format_hex_bytes(meta->start_boot_loader_version,
                     sizeof(meta->start_boot_loader_version),
                     data,
                     4U);

    ECHO_PARAM(0x0047U, 4U);
    format_hex_bytes(meta->end_boot_loader_version,
                     sizeof(meta->end_boot_loader_version),
                     data,
                     4U);

    ECHO_PARAM(0x0028U, 8U);
    trim_ascii(meta->engine_id, sizeof(meta->engine_id), data, 8U);

    ECHO_PARAM(0x0029U, 8U);
    trim_ascii(meta->fuel_system_id, sizeof(meta->fuel_system_id), data, 8U);

    ECHO_PARAM(0x002aU, 12U);
    trim_ascii(meta->byte_order, sizeof(meta->byte_order), data, 12U);

    ECHO_PARAM(0x002bU, 1U);
    sprintf(meta->address_length, "%u", (unsigned int)data[0]);

    ECHO_PARAM(0x002cU, 5U);
    trim_ascii(meta->cpp_data_link, sizeof(meta->cpp_data_link), data, 5U);

#undef ECHO_PARAM

    if (strcmp(meta->product_id, "ECH") != 0) {
        set_error("Legacy controller did not identify as ECH.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    return ECHO_TRANSFER_OK;
}

static int
allocate_image(struct echo_image *image)
{
    unsigned int i;
    size_t len;

    if (image == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    for (i = 0U; i < image->range_count; ++i) {
        len = (size_t)image->ranges[i].length;
        image->ranges[i].data = (unsigned char *)malloc(len);
        if (image->ranges[i].data == NULL) {
            set_error("Out of memory allocating ECH/ECHO calibration image.");
            return ECHO_TRANSFER_ERR_MEMORY;
        }
    }

    return ECHO_TRANSFER_OK;
}

static void
free_image(struct echo_image *image)
{
    unsigned int i;

    if (image == NULL)
        return;

    for (i = 0U; i < image->range_count; ++i) {
        free(image->ranges[i].data);
        image->ranges[i].data = NULL;
    }
}

static int
read_memory(const echo_io *io,
            unsigned long address,
            unsigned int length,
            unsigned char *out)
{
    unsigned char request[9];
    unsigned char reply[ECHO_MAX_WIRE];
    unsigned int reply_len;
    int rc;

    if (io == NULL || out == NULL ||
        length == 0U || length > ECHO_BLOCK_SIZE) {
        return ECHO_TRANSFER_ERR_ARGUMENT;
    }

    request[0] = 0x4cU;
    store_be32(request + 1U, address);
    store_be32(request + 5U, (unsigned long)length);

    rc = exchange(io,
                  request,
                  (unsigned int)sizeof(request),
                  0x4dU,
                  reply,
                  (unsigned int)sizeof(reply),
                  &reply_len,
                  ECHO_READ_TIMEOUT_MS);
    if (rc != ECHO_TRANSFER_OK)
        return rc;

    if (reply_len < 9U + length ||
        load_be32(reply + 1U) != address ||
        load_be32(reply + 5U) != (unsigned long)length) {
        set_error("Invalid ECH/ECHO memory-read reply.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    memcpy(out, reply + 9U, length);
    return ECHO_TRANSFER_OK;
}

static int
read_image(const echo_io *io, struct echo_image *image)
{
    unsigned int i;
    unsigned long offset;
    unsigned long remaining;
    unsigned int chunk;
    unsigned long total;
    unsigned long done;
    int rc;
    int percent;
    char msg[160];

    if (io == NULL || image == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    total = 0UL;
    for (i = 0U; i < image->range_count; ++i)
        total += image->ranges[i].length;

    if (total == 0UL) {
        set_error("ECH/ECHO descriptor contains no calibration bytes.");
        return ECHO_TRANSFER_ERR_PROTOCOL;
    }

    done = 0UL;
    for (i = 0U; i < image->range_count; ++i) {
        offset = 0UL;
        while (offset < image->ranges[i].length) {
            remaining = image->ranges[i].length - offset;
            chunk = ECHO_BLOCK_SIZE;
            if ((unsigned long)chunk > remaining)
                chunk = (unsigned int)remaining;

            rc = read_memory(io,
                             image->ranges[i].address + offset,
                             chunk,
                             image->ranges[i].data + (size_t)offset);
            if (rc != ECHO_TRANSFER_OK)
                return rc;

            offset += (unsigned long)chunk;
            done += (unsigned long)chunk;

            percent = 15 + (int)((done * 77UL) / total);
            if (percent > 92)
                percent = 92;

            sprintf(msg,
                    "Reading ECH/ECHO calibration: %lu / %lu bytes",
                    done,
                    total);
            progress(io, percent, msg);

            ct_sleep_ms(35);
        }
    }

    return ECHO_TRANSFER_OK;
}

static int
write_ihex_record(FILE *fp,
                  unsigned int type,
                  unsigned int address,
                  const unsigned char *data,
                  unsigned int count)
{
    unsigned int sum;
    unsigned int checksum;
    unsigned int i;

    if (fp == NULL || count > 255U)
        return 0;

    sum = (count & 0xffU) +
          ((address >> 8) & 0xffU) +
          (address & 0xffU) +
          (type & 0xffU);

    if (fprintf(fp, ":%02X%04X%02X",
                count & 0xffU,
                address & 0xffffU,
                type & 0xffU) < 0) {
        return 0;
    }

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
write_image(FILE *fp, const struct echo_image *image)
{
    unsigned int order[ECHO_MAX_RANGES];
    unsigned int order_count;
    unsigned int i;
    unsigned int j;
    unsigned int tmp;
    unsigned int index;
    unsigned long offset;
    unsigned long address;
    unsigned int high;
    unsigned int current_high;
    unsigned int low;
    unsigned int room;
    unsigned int count;
    unsigned char ela[2];

    if (fp == NULL || image == NULL ||
        image->range_count > ECHO_MAX_RANGES) {
        return 0;
    }

    order_count = image->range_count;
    for (i = 0U; i < order_count; ++i)
        order[i] = i;

    for (i = 0U; i < order_count; ++i) {
        for (j = i + 1U; j < order_count; ++j) {
            if (image->ranges[order[j]].address <
                image->ranges[order[i]].address) {
                tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    current_high = 0xffffffffU;

    for (i = 0U; i < order_count; ++i) {
        index = order[i];
        offset = 0UL;

        while (offset < image->ranges[index].length) {
            address = image->ranges[index].address + offset;
            high = (unsigned int)((address >> 16) & 0xffffUL);
            low = (unsigned int)(address & 0xffffUL);

            if (high != current_high) {
                ela[0] = (unsigned char)((high >> 8) & 0xffU);
                ela[1] = (unsigned char)(high & 0xffU);
                if (!write_ihex_record(fp, 0x04U, 0U, ela, 2U))
                    return 0;
                current_high = high;
            }

            room = 0x10000U - low;
            count = 16U;
            if ((unsigned long)count >
                image->ranges[index].length - offset) {
                count = (unsigned int)
                    (image->ranges[index].length - offset);
            }
            if (count > room)
                count = room;

            if (!write_ihex_record(
                    fp,
                    0x00U,
                    low,
                    image->ranges[index].data + (size_t)offset,
                    count)) {
                return 0;
            }

            offset += (unsigned long)count;
        }
    }

    return write_ihex_record(fp, 0x01U, 0U, NULL, 0U);
}

static int
write_ccal(const char *path,
           const struct echo_meta *meta,
           const struct echo_image *image)
{
    FILE *fp;
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned char zero4[4];
    int ok;

    if (path == NULL || meta == NULL || image == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        set_error("Unable to create destination ECH/ECHO .ccal file.");
        return ECHO_TRANSFER_ERR_FILE;
    }

    ct_get_local_date(&year, &month, &day);
    memset(zero4, 0, sizeof(zero4));

    ok = 1;
    if (fputs("0000\r\n", fp) == EOF)
        ok = 0;
    if (ok && fputs("[Header Records]\r\n", fp) == EOF)
        ok = 0;
    if (ok && fprintf(fp, "CalibrationVersion=%s\r\n",
                      meta->calibration_version) < 0)
        ok = 0;
    if (ok && fprintf(fp, "ModuleID=%s\r\n", meta->module_id) < 0)
        ok = 0;
    if (ok && fprintf(fp, "ProductID=%s\r\n", meta->product_id) < 0)
        ok = 0;
    if (ok && fprintf(fp, "ModulePN=%s\r\n",
                      meta->module_part_number) < 0)
        ok = 0;
    if (ok && fprintf(fp, "MarketID=%s\r\n", meta->market_id) < 0)
        ok = 0;
    if (ok && fprintf(fp, "InterfaceLevel=%s\r\n",
                      meta->interface_level) < 0)
        ok = 0;
    if (ok && fprintf(fp, "CreationDate=%02u%02u%02u\r\n",
                      month,
                      day,
                      year % 100U) < 0)
        ok = 0;
    if (ok && fprintf(fp, "StartBootLoaderVersion=%s\r\n",
                      meta->start_boot_loader_version) < 0)
        ok = 0;
    if (ok && fprintf(fp, "EndBootLoaderVersion=%s\r\n",
                      meta->end_boot_loader_version) < 0)
        ok = 0;
    if (ok && fprintf(fp, "EngineID=%s\r\n", meta->engine_id) < 0)
        ok = 0;
    if (ok && fprintf(fp, "FuelSystemID=%s\r\n",
                      meta->fuel_system_id) < 0)
        ok = 0;
    if (ok && fprintf(fp, "ByteOrder=%s\r\n", meta->byte_order) < 0)
        ok = 0;
    if (ok && fprintf(fp, "AddressLength=%s\r\n",
                      meta->address_length) < 0)
        ok = 0;
    if (ok && fprintf(fp, "CPPDataLink=%s\r\n",
                      meta->cpp_data_link) < 0)
        ok = 0;
    if (ok && fputs(
            "FileDescriptor=ECH/ECHO calibration readback\r\n",
            fp) == EOF)
        ok = 0;
    if (ok && fputs("[Data Records]\r\n", fp) == EOF)
        ok = 0;

    /*
     * The supplied ECH file begins its data section with a proprietary
     * type-FF four-byte zero record, followed by ordinary Intel-HEX.
     */
    if (ok && !write_ihex_record(fp, 0xffU, 0U, zero4, 4U))
        ok = 0;
    if (ok && !write_image(fp, image))
        ok = 0;

    if (fclose(fp) != 0)
        ok = 0;

    if (!ok) {
        (void)remove(path);
        set_error("Failed while writing destination ECH/ECHO .ccal file.");
        return ECHO_TRANSFER_ERR_FILE;
    }

    if (!ccal_set_cal_file_crc(path) ||
        !ccal_check_cal_file_crc(path) ||
        !ccal_check_header_file_crc(path) ||
        !ccal_check_file_crc(path)) {
        set_error("ECH/ECHO .ccal CRC finalization failed.");
        return ECHO_TRANSFER_ERR_CRC;
    }

    return ECHO_TRANSFER_OK;
}

int
echo_probe(const echo_io *io)
{
    unsigned char data[3];
    int rc;

    if (io == NULL || io->send == NULL || io->recv == NULL)
        return ECHO_TRANSFER_ERR_ARGUMENT;

    set_error(NULL);

    rc = read_parameter(io,
                        ECHO_PRODUCT_ID,
                        3U,
                        data,
                        (unsigned int)sizeof(data));
    if (rc != ECHO_TRANSFER_OK)
        return 0;

    return data[0] == 'E' && data[1] == 'C' && data[2] == 'H';
}

int
echo_pull_ccal(const echo_io *io, const char *path)
{
    struct echo_meta meta;
    struct echo_image image;
    int rc;
    int opened;

    if (io == NULL || io->send == NULL || io->recv == NULL ||
        path == NULL || path[0] == '\0') {
        return ECHO_TRANSFER_ERR_ARGUMENT;
    }

    memset(&image, 0, sizeof(image));
    set_error(NULL);
    opened = 0;

    progress(io, 4, "ECH/ECHO platform detected.");

    rc = transfer_control(io, 0x04U);
    if (rc != ECHO_TRANSFER_OK)
        goto done;
    opened = 1;

    progress(io, 8, "Reading ECH/ECHO compatibility metadata...");
    rc = collect_metadata(io, &meta);
    if (rc != ECHO_TRANSFER_OK)
        goto done;

    progress(io, 10, "Reading ECH/ECHO calibration descriptor...");
    rc = read_descriptor(io, &image);
    if (rc != ECHO_TRANSFER_OK)
        goto done;

    rc = allocate_image(&image);
    if (rc != ECHO_TRANSFER_OK)
        goto done;

    progress(io, 15, "Reading ECH/ECHO calibration memory...");
    rc = read_image(io, &image);
    if (rc != ECHO_TRANSFER_OK)
        goto done;

    progress(io, 93, "Closing ECH/ECHO calibration transfer...");
    rc = transfer_control(io, 0x05U);
    if (rc != ECHO_TRANSFER_OK)
        goto done;
    opened = 0;

    progress(io, 95, "Writing ECH/ECHO .ccal file...");
    rc = write_ccal(path, &meta, &image);
    if (rc != ECHO_TRANSFER_OK)
        goto done;

    progress(io, 100, "ECH/ECHO calibration saved and CRC verified.");

 done:
    if (opened)
        (void)transfer_control(io, 0x05U);

    free_image(&image);
    return rc;
}

const char *
echo_get_last_error(void)
{
    return g_echo_error;
}
