/*
 * clip_cal.c
 *
 * C89 implementation of the CLIP calibration-upload helpers declared in
 * clip_cal.h.  The packet layouts are derived from the supplied reference tool
 * recording and cross-checked against the generated .ccal Intel-HEX payload.
 */

#include "clip_cal.h"
#include "ccal_crc.h"
#include "ct_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static clip_u32
clip_cal_load_be32(const clip_u8 *p)
{
    clip_u32 v;

    v = ((clip_u32)p[0] << 24);
    v |= ((clip_u32)p[1] << 16);
    v |= ((clip_u32)p[2] << 8);
    v |= (clip_u32)p[3];
    return v;
}

static unsigned int
clip_cal_load_be16(const clip_u8 *p)
{
    unsigned int v;

    v = ((unsigned int)p[0] << 8);
    v |= (unsigned int)p[1];
    return v;
}

static clip_u32
clip_cal_load_be_width(const clip_u8 *p, unsigned int width)
{
    clip_u32 v;
    unsigned int i;

    v = (clip_u32)0;
    for (i = 0U; i < width; ++i)
        v = (v << 8) | (clip_u32)p[i];
    return v;
}

static void
clip_cal_store_be32(clip_u8 *p, clip_u32 v)
{
    p[0] = (clip_u8)(v >> 24);
    p[1] = (clip_u8)(v >> 16);
    p[2] = (clip_u8)(v >> 8);
    p[3] = (clip_u8)v;
}

static void
clip_cal_store_be16(clip_u8 *p, unsigned int v)
{
    p[0] = (clip_u8)((v >> 8) & 0xffU);
    p[1] = (clip_u8)(v & 0xffU);
}

int
clip_cal_build_mode_request(clip_u8 sequence,
                            unsigned int mode,
                            clip_u8 value,
                            clip_u8 out[CLIP_CAL_PHASE_REQUEST_SIZE])
{
    if (out == NULL)
        return CLIP_CAL_ERR_ARGUMENT;
    if (mode > 0xffffU)
        return CLIP_CAL_ERR_LENGTH;

    out[0] = CLIP_CAL_SERVICE_PHASE;
    out[1] = sequence;
    out[2] = (clip_u8)((mode >> 8) & 0xffU);
    out[3] = (clip_u8)(mode & 0xffU);
    out[4] = value;
    return CLIP_CAL_OK;
}

int
clip_cal_build_phase_request(clip_u8 sequence,
                             clip_u8 phase,
                             clip_u8 out[CLIP_CAL_PHASE_REQUEST_SIZE])
{
    return clip_cal_build_mode_request(sequence, 0x0011U, phase, out);
}

int
clip_cal_build_query_request(clip_u8 sequence,
                             unsigned long identifier,
                             clip_u8 out[CLIP_CAL_QUERY_REQUEST_SIZE])
{
    if (out == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    if (identifier > 0x00ffffffUL)
        return CLIP_CAL_ERR_LENGTH;

    out[0] = CLIP_CAL_SERVICE_QUERY;
    out[1] = sequence;
    out[2] = 0x01U;
    out[3] = 0x01U;
    out[4] = (clip_u8)((identifier >> 16) & 0xffUL);
    out[5] = (clip_u8)((identifier >> 8) & 0xffUL);
    out[6] = (clip_u8)(identifier & 0xffUL);
    return CLIP_CAL_OK;
}

int
clip_cal_parse_reply(const clip_u8 *pdu,
                     size_t pdu_len,
                     clip_u8 expected_sequence,
                     const clip_u8 **data,
                     size_t *data_len)
{
    if (pdu == NULL || data == NULL || data_len == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    if (pdu_len < 2U)
        return CLIP_CAL_ERR_FORMAT;

    if (pdu[0] != CLIP_CAL_SERVICE_REPLY)
        return CLIP_CAL_ERR_FORMAT;

    if (pdu[1] != expected_sequence)
        return CLIP_CAL_ERR_SEQUENCE;

    *data = pdu + 2U;
    *data_len = pdu_len - 2U;
    return CLIP_CAL_OK;
}

int
clip_cal_parse_memory_descriptor(const clip_u8 *pdu,
                                 size_t pdu_len,
                                 clip_u8 expected_sequence,
                                 struct clip_cal_map *map)
{
    const clip_u8 *data;
    size_t data_len;
    size_t offset;
    size_t bytes;
    size_t trailer_len;
    unsigned int count;
    unsigned int address_width;
    unsigned int auxiliary_width;
    unsigned int length_width;
    unsigned int i;
    int rc;

    if (map == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    rc = clip_cal_parse_reply(pdu,
                              pdu_len,
                              expected_sequence,
                              &data,
                              &data_len);
    if (rc != CLIP_CAL_OK)
        return rc;

    /*
     * Observed width-tagged descriptor layout:
     *
     *   u32 descriptor_value
     *   u16 range_count
     *   u16 address_width
     *   address[range_count][address_width]
     *   u16 auxiliary_width
     *   auxiliary[range_count][auxiliary_width]
     *   u16 length_width
     *   length[range_count][length_width]
     *   optional trailer
     *
     * The earlier CM2350A recording happened to contain 0x0004 for both the
     * range count and element widths.  Treating those equal values as duplicate
     * counts was incorrect and rejected valid descriptors such as a five-range,
     * four-byte-width map.
     */
    if (data_len < 8U)
        return CLIP_CAL_ERR_FORMAT;

    memset(map, 0, sizeof(*map));

    map->descriptor_value = clip_cal_load_be32(data);
    count = clip_cal_load_be16(data + 4U);
    address_width = clip_cal_load_be16(data + 6U);

    if (count == 0U || count > CLIP_CAL_MAX_RANGES)
        return CLIP_CAL_ERR_RANGE_COUNT;
    if (address_width == 0U || address_width > 4U)
        return CLIP_CAL_ERR_FORMAT;

    offset = 8U;
    bytes = (size_t)count * (size_t)address_width;
    if (bytes > data_len - offset)
        return CLIP_CAL_ERR_FORMAT;

    for (i = 0U; i < count; ++i) {
        map->ranges[i].address =
            clip_cal_load_be_width(data + offset +
                                   ((size_t)i * address_width),
                                   address_width);
    }
    offset += bytes;

    if (data_len - offset < 2U)
        return CLIP_CAL_ERR_FORMAT;
    auxiliary_width = clip_cal_load_be16(data + offset);
    offset += 2U;

    if (auxiliary_width == 0U || auxiliary_width > 4U)
        return CLIP_CAL_ERR_FORMAT;

    bytes = (size_t)count * (size_t)auxiliary_width;
    if (bytes > data_len - offset)
        return CLIP_CAL_ERR_FORMAT;

    for (i = 0U; i < count; ++i) {
        map->ranges[i].auxiliary =
            clip_cal_load_be_width(data + offset +
                                   ((size_t)i * auxiliary_width),
                                   auxiliary_width);
    }
    offset += bytes;

    if (data_len - offset < 2U)
        return CLIP_CAL_ERR_FORMAT;
    length_width = clip_cal_load_be16(data + offset);
    offset += 2U;

    if (length_width == 0U || length_width > 4U)
        return CLIP_CAL_ERR_FORMAT;

    bytes = (size_t)count * (size_t)length_width;
    if (bytes > data_len - offset)
        return CLIP_CAL_ERR_FORMAT;

    for (i = 0U; i < count; ++i) {
        map->ranges[i].length =
            clip_cal_load_be_width(data + offset +
                                   ((size_t)i * length_width),
                                   length_width);

        if (map->ranges[i].length == (clip_u32)0)
            return CLIP_CAL_ERR_FORMAT;

        if (map->ranges[i].address >
            (clip_u32)0xffffffffUL - map->ranges[i].length)
            return CLIP_CAL_ERR_LENGTH;
    }
    offset += bytes;

    map->range_count = count;

    trailer_len = data_len - offset;
    if (trailer_len > CLIP_CAL_MAX_TRAILER)
        return CLIP_CAL_ERR_BUFFER;

    map->trailer_len = trailer_len;
    if (trailer_len != 0U)
        memcpy(map->trailer, data + offset, trailer_len);
    if (trailer_len < CLIP_CAL_MAX_TRAILER)
        memset(map->trailer + trailer_len,
               0,
               CLIP_CAL_MAX_TRAILER - trailer_len);

    return CLIP_CAL_OK;
}

int
clip_cal_build_read_request(clip_u8 sequence,
                            clip_u32 address,
                            unsigned int length,
                            clip_u8 out[CLIP_CAL_READ_REQUEST_SIZE])
{
    if (out == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    if (length == 0U || length > 0xffffU)
        return CLIP_CAL_ERR_LENGTH;

    out[0] = CLIP_CAL_SERVICE_READ_MEMORY;
    out[1] = sequence;
    clip_cal_store_be32(out + 2U, address);
    clip_cal_store_be16(out + 6U, length);
    return CLIP_CAL_OK;
}

int
clip_cal_parse_read_reply(const clip_u8 *pdu,
                          size_t pdu_len,
                          clip_u8 expected_sequence,
                          size_t expected_length,
                          const clip_u8 **data)
{
    const clip_u8 *reply_data;
    size_t reply_len;
    int rc;

    if (data == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    rc = clip_cal_parse_reply(pdu,
                              pdu_len,
                              expected_sequence,
                              &reply_data,
                              &reply_len);
    if (rc != CLIP_CAL_OK)
        return rc;

    if (reply_len < expected_length)
        return CLIP_CAL_ERR_FORMAT;

    *data = reply_data;
    return CLIP_CAL_OK;
}

int
clip_cal_build_pointer_request(clip_u8 sequence,
                               unsigned int pointer_id,
                               clip_u8 out[CLIP_CAL_PTR_REQUEST_SIZE])
{
    if (out == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    if (pointer_id > 0xffffU)
        return CLIP_CAL_ERR_LENGTH;

    out[0] = CLIP_CAL_SERVICE_RESOLVE_PTR;
    out[1] = sequence;
    out[2] = 0x00U;
    out[3] = 0x00U;
    clip_cal_store_be16(out + 4U, pointer_id);
    return CLIP_CAL_OK;
}

int
clip_cal_parse_pointer_reply(const clip_u8 *pdu,
                             size_t pdu_len,
                             clip_u8 expected_sequence,
                             clip_u32 *address)
{
    const clip_u8 *data;
    size_t data_len;
    int rc;

    if (address == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    rc = clip_cal_parse_reply(pdu,
                              pdu_len,
                              expected_sequence,
                              &data,
                              &data_len);
    if (rc != CLIP_CAL_OK)
        return rc;

    if (data_len < 4U)
        return CLIP_CAL_ERR_FORMAT;

    *address = clip_cal_load_be32(data);
    return CLIP_CAL_OK;
}

void
clip_cal_build_end_request(clip_u8 out[CLIP_CAL_END_REQUEST_SIZE])
{
    if (out == NULL)
        return;

    out[0] = CLIP_CAL_SERVICE_END_PHASE;
    out[1] = 0x00U;
    out[2] = 0x00U;
    out[3] = 0x00U;
}

clip_u8
clip_cal_next_sequence(clip_u8 sequence)
{
    if (sequence >= 0xfeU)
        return 0x00U;

    return (clip_u8)(sequence + 1U);
}

void
clip_cal_cursor_init(struct clip_cal_cursor *cursor)
{
    if (cursor == NULL)
        return;

    cursor->range_index = 0U;
    cursor->range_offset = (clip_u32)0;
}

int
clip_cal_next_read(const struct clip_cal_map *map,
                   struct clip_cal_cursor *cursor,
                   clip_u8 sequence,
                   unsigned int max_chunk,
                   clip_u8 out[CLIP_CAL_READ_REQUEST_SIZE],
                   struct clip_cal_read_info *info)
{
    const struct clip_cal_range *range;
    clip_u32 remaining;
    clip_u32 address;
    unsigned int chunk;
    int rc;

    if (map == NULL || cursor == NULL || out == NULL || info == NULL)
        return CLIP_CAL_ERR_ARGUMENT;

    if (max_chunk == 0U || max_chunk > 0xffffU)
        return CLIP_CAL_ERR_LENGTH;

    while (cursor->range_index < map->range_count) {
        range = &map->ranges[cursor->range_index];

        if (cursor->range_offset < range->length)
            break;

        cursor->range_index++;
        cursor->range_offset = (clip_u32)0;
    }

    if (cursor->range_index >= map->range_count)
        return 0;

    range = &map->ranges[cursor->range_index];
    remaining = range->length - cursor->range_offset;

    chunk = max_chunk;
    if ((clip_u32)chunk > remaining)
        chunk = (unsigned int)remaining;

    address = range->address + cursor->range_offset;

    rc = clip_cal_build_read_request(sequence, address, chunk, out);
    if (rc != CLIP_CAL_OK)
        return rc;

    info->range_index = cursor->range_index;
    info->address = address;
    info->length = chunk;

    cursor->range_offset += (clip_u32)chunk;
    return 1;
}

size_t
clip_cal_total_size(const struct clip_cal_map *map)
{
    size_t total;
    size_t part;
    unsigned int i;

    if (map == NULL)
        return 0U;

    total = 0U;
    for (i = 0U; i < map->range_count; ++i) {
        part = (size_t)map->ranges[i].length;
        if (part > ((size_t)-1) - total)
            return 0U;
        total += part;
    }

    return total;
}

/* ===================== CCAL PROGRAMMING SUPPORT ===================== */

#define CLIP_CAL_LINE_MAX 2048
#define CLIP_CAL_SHORT_ADDR 0x00A00000UL
#define CLIP_CAL_RX_MAX 4096U
#define CLIP_CAL_MAX_SKIPPED_RX 64U

typedef struct clip_cal_region_tag {
    unsigned long address;
    unsigned long length;
    unsigned long capacity;
    unsigned char *data;
} clip_cal_region;

typedef struct clip_cal_image_tag {
    clip_cal_region *region;
    unsigned int count;
    unsigned int capacity;
} clip_cal_image;

static int clip_cal_hex_nibble(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static int clip_cal_hex_byte(const char *s, unsigned char *value)
{
    int hi;
    int lo;

    hi = clip_cal_hex_nibble((unsigned char)s[0]);
    lo = clip_cal_hex_nibble((unsigned char)s[1]);
    if (hi < 0 || lo < 0) {
        return 0;
    }

    *value = (unsigned char)((hi << 4) | lo);
    return 1;
}

static void clip_cal_image_free(clip_cal_image *image)
{
    unsigned int i;

    if (image == NULL) {
        return;
    }

    for (i = 0U; i < image->count; ++i) {
        free(image->region[i].data);
        image->region[i].data = NULL;
    }
    free(image->region);
    image->region = NULL;
    image->count = 0U;
    image->capacity = 0U;
}

static int clip_cal_region_reserve(clip_cal_region *region,
                                   unsigned long required)
{
    unsigned long new_capacity;
    unsigned char *new_data;

    if (required <= region->capacity) {
        return CLIP_CAL_OK;
    }

    new_capacity = region->capacity;
    if (new_capacity == 0UL) {
        new_capacity = 256UL;
    }

    while (new_capacity < required) {
        if (new_capacity > (ULONG_MAX / 2UL)) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2UL;
    }

    if ((unsigned long)(size_t)new_capacity != new_capacity) {
        return CLIP_CAL_ERR_MEMORY;
    }

    new_data = (unsigned char *)realloc(region->data, (size_t)new_capacity);
    if (new_data == NULL) {
        return CLIP_CAL_ERR_MEMORY;
    }

    region->data = new_data;
    region->capacity = new_capacity;
    return CLIP_CAL_OK;
}

static int clip_cal_image_new_region(clip_cal_image *image,
                                     unsigned long address,
                                     clip_cal_region **out_region)
{
    unsigned int new_capacity;
    clip_cal_region *new_regions;
    clip_cal_region *region;

    if (image->count == image->capacity) {
        new_capacity = image->capacity == 0U ? 8U : image->capacity * 2U;
        if (new_capacity < image->capacity) {
            return CLIP_CAL_ERR_MEMORY;
        }
        new_regions = (clip_cal_region *)realloc(
            image->region,
            (size_t)new_capacity * sizeof(clip_cal_region));
        if (new_regions == NULL) {
            return CLIP_CAL_ERR_MEMORY;
        }
        image->region = new_regions;
        image->capacity = new_capacity;
    }

    region = &image->region[image->count];
    region->address = address;
    region->length = 0UL;
    region->capacity = 0UL;
    region->data = NULL;
    ++image->count;

    *out_region = region;
    return CLIP_CAL_OK;
}

static int clip_cal_image_append(clip_cal_image *image,
                                 unsigned long address,
                                 const unsigned char *data,
                                 unsigned int length)
{
    clip_cal_region *region;
    unsigned long end_address;
    int rc;

    if (length == 0U) {
        return CLIP_CAL_OK;
    }

    if (image->count == 0U) {
        rc = clip_cal_image_new_region(image, address, &region);
        if (rc != CLIP_CAL_OK) {
            return rc;
        }
    } else {
        region = &image->region[image->count - 1U];
        end_address = region->address + region->length;
        if (address < end_address) {
            return CLIP_CAL_ERR_ORDER;
        }
        if (address != end_address) {
            rc = clip_cal_image_new_region(image, address, &region);
            if (rc != CLIP_CAL_OK) {
                return rc;
            }
        }
    }

    if (region->length > ULONG_MAX - (unsigned long)length) {
        return CLIP_CAL_ERR_RANGE;
    }

    rc = clip_cal_region_reserve(region,
                                 region->length + (unsigned long)length);
    if (rc != CLIP_CAL_OK) {
        return rc;
    }

    memcpy(region->data + (size_t)region->length, data, (size_t)length);
    region->length += (unsigned long)length;
    return CLIP_CAL_OK;
}

static int clip_cal_parse_ccal(const char *filename, clip_cal_image *image)
{
    FILE *fp;
    char line[CLIP_CAL_LINE_MAX];
    unsigned long base;
    int rc;
    int saw_data;

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        return CLIP_CAL_ERR_OPEN;
    }

    base = 0UL;
    rc = CLIP_CAL_OK;
    saw_data = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t text_len;
        unsigned char count;
        unsigned char addr_hi;
        unsigned char addr_lo;
        unsigned char type;
        unsigned char checksum;
        unsigned char record[255];
        unsigned int i;
        unsigned int sum;
        unsigned long address;
        unsigned long value;
        const char *p;

        if (line[0] != ':') {
            continue;
        }

        text_len = strlen(line);
        while (text_len > 0U &&
               (line[text_len - 1U] == '\r' || line[text_len - 1U] == '\n')) {
            line[text_len - 1U] = '\0';
            --text_len;
        }

        if (text_len < 11U) {
            rc = CLIP_CAL_ERR_FORMAT;
            break;
        }

        p = line + 1;
        if (!clip_cal_hex_byte(p, &count) ||
            !clip_cal_hex_byte(p + 2, &addr_hi) ||
            !clip_cal_hex_byte(p + 4, &addr_lo) ||
            !clip_cal_hex_byte(p + 6, &type)) {
            rc = CLIP_CAL_ERR_FORMAT;
            break;
        }

        if (text_len != (size_t)(11U + ((unsigned int)count * 2U))) {
            rc = CLIP_CAL_ERR_FORMAT;
            break;
        }

        sum = (unsigned int)count + (unsigned int)addr_hi +
              (unsigned int)addr_lo + (unsigned int)type;
        p += 8;
        for (i = 0U; i < (unsigned int)count; ++i) {
            if (!clip_cal_hex_byte(p + (i * 2U), &record[i])) {
                rc = CLIP_CAL_ERR_FORMAT;
                break;
            }
            sum += (unsigned int)record[i];
        }
        if (rc != CLIP_CAL_OK) {
            break;
        }
        if (!clip_cal_hex_byte(p + ((unsigned int)count * 2U), &checksum)) {
            rc = CLIP_CAL_ERR_FORMAT;
            break;
        }
        sum += (unsigned int)checksum;
        if ((sum & 0xFFU) != 0U) {
            rc = CLIP_CAL_ERR_HEX_CHECKSUM;
            break;
        }

        address = ((unsigned long)addr_hi << 8) | (unsigned long)addr_lo;

        if (type == 0x00U) {
            if (base > ULONG_MAX - address) {
                rc = CLIP_CAL_ERR_RANGE;
                break;
            }
            address += base;
            rc = clip_cal_image_append(image, address, record,
                                       (unsigned int)count);
            if (rc != CLIP_CAL_OK) {
                break;
            }
            saw_data = 1;
        } else if (type == 0x01U) {
            break;
        } else if (type == 0x02U) {
            if (count != 2U) {
                rc = CLIP_CAL_ERR_FORMAT;
                break;
            }
            value = ((unsigned long)record[0] << 8) |
                    (unsigned long)record[1];
            base = value << 4;
        } else if (type == 0x04U) {
            if (count != 2U) {
                rc = CLIP_CAL_ERR_FORMAT;
                break;
            }
            value = ((unsigned long)record[0] << 8) |
                    (unsigned long)record[1];
            base = value << 16;
        } else if (type == 0x03U || type == 0x05U) {
            /* Start-address records do not contain calibration payload. */
        } else {
            rc = CLIP_CAL_ERR_FORMAT;
            break;
        }
    }

    if (ferror(fp) && rc == CLIP_CAL_OK) {
        rc = CLIP_CAL_ERR_FORMAT;
    }
    fclose(fp);

    if (rc == CLIP_CAL_OK && !saw_data) {
        rc = CLIP_CAL_ERR_FORMAT;
    }
    return rc;
}

static int clip_cal_validate_image(const clip_cal_image *image,
                                   unsigned int *special_index,
                                   unsigned long *total_out)
{
    const clip_cal_region *region;
    unsigned long total;
    unsigned int found_special;
    unsigned int i;

    if (image == NULL || image->count == 0U) {
        return CLIP_CAL_ERR_LAYOUT;
    }

    total = 0UL;
    found_special = 0U;

    for (i = 0U; i < image->count; ++i) {
        region = &image->region[i];

        if (region->length == 0UL) {
            return CLIP_CAL_ERR_LAYOUT;
        }

        if (region->address == CLIP_CAL_SHORT_ADDR) {
            if (found_special != 0U) {
                return CLIP_CAL_ERR_LAYOUT;
            }
            if (region->length > 253UL) {
                return CLIP_CAL_ERR_RANGE;
            }
            found_special = 1U;
            if (special_index != NULL) {
                *special_index = i;
            }
        } else {
            if (region->address > 0xFFFFFFUL ||
                region->length > 0xFFFFFFUL ||
                region->length > 0xFFFFFEUL) {
                return CLIP_CAL_ERR_RANGE;
            }

            if (region->address >
                0x1000000UL - region->length - 2UL) {
                return CLIP_CAL_ERR_RANGE;
            }
        }

        if (total > ULONG_MAX - 2UL ||
            region->length > ULONG_MAX - total - 2UL) {
            return CLIP_CAL_ERR_RANGE;
        }
        total += region->length + 2UL;
    }

    if (found_special == 0U) {
        return CLIP_CAL_ERR_LAYOUT;
    }

    if (total_out != NULL) {
        *total_out = total;
    }

    return CLIP_CAL_OK;
}

unsigned int clip_cal_crc16_kermit(const unsigned char *data,
                                   unsigned long length)
{
    unsigned int crc;
    unsigned long i;
    unsigned int bit;

    crc = 0U;
    for (i = 0UL; i < length; ++i) {
        crc ^= (unsigned int)data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0x8408U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc & 0xFFFFU;
}

int clip_cal_verify_ccal_crc(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
        return 0;

    /* A normal CCAL uses the four-hex-character first-line token.  Require
       all public checks supplied by the recovered calibration CRC implementation
       to agree before a single programming packet is emitted. */
    if (!ccal_check_cal_file_crc(filename))
        return 0;
    if (!ccal_check_header_file_crc(filename))
        return 0;
    if (!ccal_check_file_crc(filename))
        return 0;
    return 1;
}

int clip_cal_validate_ccal(const char *filename)
{
    clip_cal_image image;
    int rc;

    if (filename == NULL || filename[0] == '\0') {
        return CLIP_CAL_ERR_ARGUMENT;
    }

    if (!clip_cal_verify_ccal_crc(filename)) {
        return CLIP_CAL_ERR_CRC;
    }

    image.region = NULL;
    image.count = 0U;
    image.capacity = 0U;

    rc = clip_cal_parse_ccal(filename, &image);
    if (rc == CLIP_CAL_OK) {
        rc = clip_cal_validate_image(&image, NULL, NULL);
    }

    clip_cal_image_free(&image);
    return rc;
}

void clip_cal_options_init(clip_cal_options *options)
{
    if (options == NULL) {
        return;
    }

    options->timeout_ms = CLIP_CAL_DEFAULT_TIMEOUT;
    options->block_size = CLIP_CAL_BLOCK_SIZE;
    options->finish_value = CLIP_CAL_CAPTURE_FINISH;
    options->send_start_command = 1;
    options->send_end_command = 1;
    options->progress = NULL;
    options->progress_user = NULL;
}

static int clip_cal_complete_matches(const unsigned char *rx,
                                     unsigned int rx_len,
                                     const unsigned char *request,
                                     unsigned int request_len)
{
    if (rx_len < 2U || request_len == 0U || rx[0] != 0x0CU) {
        return 0;
    }

    if (rx[1] != request[0]) {
        return 0;
    }

    if (request[0] == 0x4DU || request[0] == 0x4BU) {
        if (request_len < 5U || rx_len < 6U) {
            return 0;
        }
        if (memcmp(rx + 1, request, 5U) != 0) {
            return 0;
        }
    } else if (request[0] == 0x44U) {
        if (request_len < 3U || rx_len < 4U) {
            return 0;
        }
        if (memcmp(rx + 1, request, 3U) != 0) {
            return 0;
        }
    }

    return 1;
}

static int clip_cal_send_and_wait(const clip_cal_io *io,
                                  const clip_cal_options *options,
                                  const unsigned char *request,
                                  unsigned int request_len)
{
    unsigned char rx[CLIP_CAL_RX_MAX];
    unsigned int rx_len;
    unsigned int skipped;
    unsigned long start;
    unsigned long now;
    unsigned long elapsed;
    unsigned long remaining;
    int trc;

    trc = io->send(io->user, request, request_len);
    if (trc != 0) {
        return CLIP_CAL_ERR_TRANSPORT;
    }

    skipped = 0U;
    start = ct_monotonic_ms();

    while (skipped < CLIP_CAL_MAX_SKIPPED_RX) {
        now = ct_monotonic_ms();
        elapsed = (unsigned long)(now - start);
        if (elapsed >= options->timeout_ms) {
            return CLIP_CAL_ERR_TIMEOUT;
        }
        remaining = options->timeout_ms - elapsed;

        rx_len = 0U;
        trc = io->recv(io->user, rx, sizeof(rx), &rx_len, remaining);
        if (trc > 0) {
            return CLIP_CAL_ERR_TIMEOUT;
        }
        if (trc < 0) {
            return CLIP_CAL_ERR_TRANSPORT;
        }
        if (rx_len > sizeof(rx)) {
            return CLIP_CAL_ERR_TRANSPORT;
        }
        if (clip_cal_complete_matches(rx, rx_len, request, request_len)) {
            return CLIP_CAL_OK;
        }
        ++skipped;
    }

    return CLIP_CAL_ERR_ACK;
}

static int clip_cal_send_simple(const clip_cal_io *io,
                                const clip_cal_options *options,
                                unsigned char command)
{
    unsigned char request[8];
    unsigned int i;

    request[0] = command;
    for (i = 1U; i < 8U; ++i) {
        request[i] = 0xFFU;
    }
    return clip_cal_send_and_wait(io, options, request, 8U);
}

static int clip_cal_set_region_length(const clip_cal_io *io,
                                      const clip_cal_options *options,
                                      unsigned long length)
{
    unsigned char request[8];

    if (length > 0xFFFFFFFFUL) {
        return CLIP_CAL_ERR_RANGE;
    }

    request[0] = 0x44U;
    request[1] = 0x00U;
    request[2] = 0x18U;
    request[3] = 0x04U;
    request[4] = (unsigned char)((length >> 24) & 0xFFUL);
    request[5] = (unsigned char)((length >> 16) & 0xFFUL);
    request[6] = (unsigned char)((length >> 8) & 0xFFUL);
    request[7] = (unsigned char)(length & 0xFFUL);

    return clip_cal_send_and_wait(io, options, request, 8U);
}

static int clip_cal_send_finish_value(const clip_cal_io *io,
                                      const clip_cal_options *options)
{
    unsigned char request[8];
    unsigned int value;

    value = options->finish_value & 0xFFFFU;
    request[0] = 0x44U;
    request[1] = 0x00U;
    request[2] = 0x0BU;
    request[3] = 0x02U;
    request[4] = (unsigned char)((value >> 8) & 0xFFU);
    request[5] = (unsigned char)(value & 0xFFU);
    request[6] = 0xFFU;
    request[7] = 0xFFU;

    return clip_cal_send_and_wait(io, options, request, 8U);
}

static unsigned char clip_cal_region_stream_byte(const clip_cal_region *region,
                                                 unsigned long offset,
                                                 unsigned int crc)
{
    if (offset < region->length) {
        return region->data[(size_t)offset];
    }
    if (offset == region->length) {
        return (unsigned char)((crc >> 8) & 0xFFU);
    }
    return (unsigned char)(crc & 0xFFU);
}

static int clip_cal_send_short_region(const clip_cal_region *region,
                                      const clip_cal_io *io,
                                      const clip_cal_options *options,
                                      unsigned long *sent,
                                      unsigned long total)
{
    unsigned char request[6U + 255U];
    unsigned long transfer_len;
    unsigned long i;
    unsigned int crc;
    int rc;

    if (region->length > 253UL) {
        return CLIP_CAL_ERR_RANGE;
    }
    if (region->address > 0xFFFFFFFFUL) {
        return CLIP_CAL_ERR_RANGE;
    }

    transfer_len = region->length + 2UL;
    crc = clip_cal_crc16_kermit(region->data, region->length);

    rc = clip_cal_set_region_length(io, options, transfer_len);
    if (rc != CLIP_CAL_OK) {
        return rc;
    }

    request[0] = 0x4BU;
    request[1] = (unsigned char)((region->address >> 24) & 0xFFUL);
    request[2] = (unsigned char)((region->address >> 16) & 0xFFUL);
    request[3] = (unsigned char)((region->address >> 8) & 0xFFUL);
    request[4] = (unsigned char)(region->address & 0xFFUL);
    request[5] = (unsigned char)transfer_len;
    for (i = 0UL; i < transfer_len; ++i) {
        request[6U + (unsigned int)i] =
            clip_cal_region_stream_byte(region, i, crc);
    }

    rc = clip_cal_send_and_wait(io, options, request,
                                6U + (unsigned int)transfer_len);
    if (rc != CLIP_CAL_OK) {
        return rc;
    }

    *sent += transfer_len;
    if (options->progress != NULL) {
        options->progress(options->progress_user, *sent, total,
                          region->address);
    }
    return CLIP_CAL_OK;
}

static int clip_cal_send_bulk_region(const clip_cal_region *region,
                                     const clip_cal_io *io,
                                     const clip_cal_options *options,
                                     unsigned long *sent,
                                     unsigned long total)
{
    unsigned char *request;
    unsigned long transfer_len;
    unsigned long offset;
    unsigned long current_address;
    unsigned int block_size;
    unsigned int chunk;
    unsigned int i;
    unsigned int crc;
    int rc;

    block_size = options->block_size;
    if (block_size == 0U || block_size > 0xFFFFU) {
        return CLIP_CAL_ERR_ARGUMENT;
    }
    if (region->address > 0xFFFFFFUL) {
        return CLIP_CAL_ERR_RANGE;
    }
    if (region->length > 0xFFFFFFUL) {
        return CLIP_CAL_ERR_RANGE;
    }
    if (region->address + region->length + 2UL > 0x1000000UL) {
        return CLIP_CAL_ERR_RANGE;
    }

    transfer_len = region->length + 2UL;
    crc = clip_cal_crc16_kermit(region->data, region->length);

    rc = clip_cal_set_region_length(io, options, transfer_len);
    if (rc != CLIP_CAL_OK) {
        return rc;
    }

    request = (unsigned char *)malloc((size_t)block_size + 9U);
    if (request == NULL) {
        return CLIP_CAL_ERR_MEMORY;
    }

    offset = 0UL;
    while (offset < transfer_len) {
        unsigned long remaining;

        remaining = transfer_len - offset;
        chunk = remaining > (unsigned long)block_size ?
                block_size : (unsigned int)remaining;
        current_address = region->address + offset;

        request[0] = 0x4DU;
        request[1] = 0x00U;
        request[2] = (unsigned char)((current_address >> 16) & 0xFFUL);
        request[3] = (unsigned char)((current_address >> 8) & 0xFFUL);
        request[4] = (unsigned char)(current_address & 0xFFUL);
        request[5] = 0x00U;
        request[6] = 0x00U;
        request[7] = (unsigned char)((chunk >> 8) & 0xFFU);
        request[8] = (unsigned char)(chunk & 0xFFU);

        for (i = 0U; i < chunk; ++i) {
            request[9U + i] =
                clip_cal_region_stream_byte(region,
                                            offset + (unsigned long)i,
                                            crc);
        }

        rc = clip_cal_send_and_wait(io, options, request, 9U + chunk);
        if (rc != CLIP_CAL_OK) {
            free(request);
            return rc;
        }

        offset += (unsigned long)chunk;
        *sent += (unsigned long)chunk;
        if (options->progress != NULL) {
            options->progress(options->progress_user, *sent, total,
                              current_address);
        }
    }

    free(request);
    return CLIP_CAL_OK;
}

int clip_cal_send_ccal(const char *filename,
                       const clip_cal_io *io,
                       const clip_cal_options *options)
{
    clip_cal_image image;
    clip_cal_options local_options;
    const clip_cal_options *opt;
    clip_cal_region *special;
    unsigned int special_index;
    unsigned int i;
    unsigned long total;
    unsigned long sent;
    int rc;

    if (filename == NULL || io == NULL || io->send == NULL || io->recv == NULL) {
        return CLIP_CAL_ERR_ARGUMENT;
    }

    /*
     * Keep the sender independently defensive even though the outer transfer
     * layer performs this same full preflight before opening the adapter.
     */
    rc = clip_cal_validate_ccal(filename);
    if (rc != CLIP_CAL_OK) {
        return rc;
    }

    if (options == NULL) {
        clip_cal_options_init(&local_options);
        opt = &local_options;
    } else {
        opt = options;
    }

    if (opt->block_size == 0U || opt->block_size > 0xFFFFU) {
        return CLIP_CAL_ERR_ARGUMENT;
    }

    image.region = NULL;
    image.count = 0U;
    image.capacity = 0U;

    rc = clip_cal_parse_ccal(filename, &image);
    if (rc != CLIP_CAL_OK) {
        clip_cal_image_free(&image);
        return rc;
    }

    special_index = 0U;
    total = 0UL;
    rc = clip_cal_validate_image(&image, &special_index, &total);
    if (rc != CLIP_CAL_OK) {
        clip_cal_image_free(&image);
        return rc;
    }

    special = &image.region[special_index];
    sent = 0UL;

    if (opt->send_start_command) {
        rc = clip_cal_send_simple(io, opt, 0x02U);
        if (rc != CLIP_CAL_OK) {
            clip_cal_image_free(&image);
            return rc;
        }
    }

    rc = clip_cal_send_short_region(special, io, opt, &sent, total);
    if (rc != CLIP_CAL_OK) {
        clip_cal_image_free(&image);
        return rc;
    }

    for (i = 0U; i < image.count; ++i) {
        if (i == special_index) {
            continue;
        }
        rc = clip_cal_send_bulk_region(&image.region[i], io, opt,
                                       &sent, total);
        if (rc != CLIP_CAL_OK) {
            clip_cal_image_free(&image);
            return rc;
        }
    }

    rc = clip_cal_send_finish_value(io, opt);
    if (rc != CLIP_CAL_OK) {
        clip_cal_image_free(&image);
        return rc;
    }

    if (opt->send_end_command) {
        rc = clip_cal_send_simple(io, opt, 0x07U);
        if (rc != CLIP_CAL_OK) {
            clip_cal_image_free(&image);
            return rc;
        }
    }

    clip_cal_image_free(&image);
    return CLIP_CAL_OK;
}

const char *clip_cal_strerror(int code)
{
    switch (code) {
    case CLIP_CAL_OK:
        return "ok";
    case CLIP_CAL_ERR_ARGUMENT:
        return "invalid argument";
    case CLIP_CAL_ERR_OPEN:
        return "could not open CCAL file";
    case CLIP_CAL_ERR_FORMAT:
        return "invalid or unsupported CCAL/Intel HEX format";
    case CLIP_CAL_ERR_HEX_CHECKSUM:
        return "Intel HEX checksum mismatch";
    case CLIP_CAL_ERR_ORDER:
        return "Intel HEX records are overlapping or out of order";
    case CLIP_CAL_ERR_MEMORY:
        return "out of memory";
    case CLIP_CAL_ERR_LAYOUT:
        return "required CCAL region layout was not found";
    case CLIP_CAL_ERR_RANGE:
        return "address or length is outside the captured protocol range";
    case CLIP_CAL_ERR_TRANSPORT:
        return "transport send/receive error";
    case CLIP_CAL_ERR_TIMEOUT:
        return "timed out waiting for ECM completion response";
    case CLIP_CAL_ERR_ACK:
        return "matching ECM completion response was not received";
    case CLIP_CAL_ERR_CRC:
        return "CCAL file CRC verification failed";
    default:
        return "unknown clip_cal error";
    }
}
