#include "xcal_crc.h"

#include <stdio.h>
#include <string.h>

/*
 * File format and CRC behavior are intentionally preserved here.
 * In particular, verification stops at DOS EOF (0x1A), while repair
 * continues to physical EOF. Keep that distinction unless the format
 * requirements and existing calibration files have been revalidated.
 */
enum xcal_header_format {
    XCAL_HEADER_PLAIN,
    XCAL_HEADER_EXTENDED
};

enum xcal_end_marker_policy {
    XCAL_READ_TO_PHYSICAL_EOF,
    XCAL_STOP_AT_DOS_EOF
};

enum xcal_crc_operation {
    XCAL_VERIFY_CRC,
    XCAL_REPAIR_CRC
};

/* Return 1 if the low eight bits contain an odd number of set bits. */
static unsigned int has_odd_byte_parity(unsigned int byte_value)
{
    byte_value ^= byte_value >> 4;
    byte_value ^= byte_value >> 2;
    byte_value ^= byte_value >> 1;

    return byte_value & 1U;
}

/* Update the XCAL CRC using one byte. The shifts/masks are protocol logic. */
static unsigned short update_crc_with_byte(unsigned short current_crc,
                                           unsigned int input_byte)
{
    unsigned int mixed_byte;
    unsigned int crc_transform;
    unsigned int shifted_crc;

    mixed_byte = (input_byte ^ current_crc) & 0xFFU;
    crc_transform = (((mixed_byte << 1) ^ mixed_byte) << 6) & 0xFFFFU;

    if (has_odd_byte_parity(mixed_byte)) {
        crc_transform ^= 0x8002U;
    }

    shifted_crc = ((unsigned int)current_crc >> 7) & 0x01FEU;
    return (unsigned short)(shifted_crc ^ crc_transform);
}

/*
 * Public API: extend an existing CRC over a NUL-terminated byte string.
 * Bytes below ASCII space are ignored; the first NUL ends the string.
 */
unsigned short xcal_crc(unsigned short current_crc, const unsigned char *text)
{
    while (*text != '\0') {
        if (*text >= 0x20U) {
            current_crc = update_crc_with_byte(current_crc, *text);
        }
        ++text;
    }

    return current_crc;
}

/* Convert one ASCII hexadecimal digit to 0..15, or -1 on failure. */
static int hex_digit_value(int character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }

    return -1;
}

/* Decode four ASCII hex digits into a 16-bit unsigned value. */
static int parse_hex_u16(const char *hex_digits, unsigned short *value)
{
    unsigned int result;
    unsigned int digit_index;
    int digit;

    result = 0U;
    for (digit_index = 0U; digit_index < 4U; ++digit_index) {
        digit = hex_digit_value((unsigned char)hex_digits[digit_index]);
        if (digit < 0) {
            return 0;
        }
        result = (result << 4) | (unsigned int)digit;
    }

    *value = (unsigned short)result;
    return 1;
}

static int is_ascii_whitespace(unsigned char character)
{
    return character == ' '  || character == '\t' ||
           character == '\r' || character == '\n' ||
           character == '\v' || character == '\f';
}

/* Strip leading and trailing whitespace in place. */
static void trim_ascii_whitespace(char *text)
{
    char *first_character;
    char *end;

    first_character = text;
    while (*first_character != '\0' &&
           is_ascii_whitespace((unsigned char)*first_character)) {
        ++first_character;
    }

    if (first_character != text) {
        memmove(text, first_character, strlen(first_character) + 1U);
    }

    end = text + strlen(text);
    while (end > text && is_ascii_whitespace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
}

static unsigned short swap_u16_bytes(unsigned short value)
{
    return (unsigned short)((value << 8) | (value >> 8));
}

/*
 * Decode the first file line.
 *
 * Plain:     four ASCII hexadecimal digits holding the stored CRC.
 * Extended: 19 characters; CRC at [3..6], suffix at [7..16].
 *
 * The extended suffix contributes to the CRC as ASCII text, NOT as
 * decoded hex bytes. The extended format also stores its CRC byte-swapped.
 * As in the original implementation, the prefix and final record checksum
 * are not validated while parsing.
 */
static int parse_crc_header(const char *header_line,
                            enum xcal_header_format header_format,
                            unsigned short *stored_crc,
                            unsigned short *calculated_crc,
                            char header_suffix[16])
{
    size_t header_length;
    unsigned short encoded_crc;

    header_length = strlen(header_line);
    *calculated_crc = 0U;

    if (header_format == XCAL_HEADER_PLAIN) {
        if (header_length != 4U || !parse_hex_u16(header_line, stored_crc)) {
            return 0;
        }
        header_suffix[0] = '\0';
        return 1;
    }

    if (header_length != 19U ||
        !parse_hex_u16(header_line + 3, &encoded_crc)) {
        return 0;
    }

    *stored_crc = swap_u16_bytes(encoded_crc);
    memcpy(header_suffix, header_line + 7, 10U);
    header_suffix[10] = '\0';
    *calculated_crc = xcal_crc(*calculated_crc,
                               (const unsigned char *)header_suffix);
    return 1;
}

/*
 * Scan bytes after the header, accumulating the existing format's CRC.
 * LF resets the NUL-line skip; a NUL ignores the rest of its line.
 * All bytes below 0x20 are excluded from the CRC, including CR and LF.
 */
static int update_crc_from_file_body(FILE *input,
                                     enum xcal_end_marker_policy end_policy,
                                     unsigned short *calculated_crc)
{
    int input_character;
    int skip_to_next_line;

    skip_to_next_line = 0;
    while ((input_character = fgetc(input)) != EOF) {
        if (end_policy == XCAL_STOP_AT_DOS_EOF &&
            (unsigned char)input_character == 0x1AU) {
            break;
        }

        if (input_character == '\n') {
            skip_to_next_line = 0;
            continue;
        }
        if (skip_to_next_line) {
            continue;
        }
        if (input_character == 0) {
            skip_to_next_line = 1;
            continue;
        }
        if ((unsigned char)input_character >= 0x20U) {
            *calculated_crc = update_crc_with_byte(
                *calculated_crc, (unsigned char)input_character);
        }
    }

    return !ferror(input);
}

/* Read the stored CRC and calculate the CRC of the remaining file bytes. */
static int calculate_file_crc(const char *filename,
                              enum xcal_header_format header_format,
                              enum xcal_end_marker_policy end_policy,
                              unsigned short *stored_crc,
                              unsigned short *calculated_crc,
                              char header_suffix[16])
{
    FILE *input;
    char header_line[64];
    int success;

    input = fopen(filename, "rb");
    if (input == NULL) {
        return 0;
    }

    if (fgets(header_line, sizeof(header_line), input) == NULL) {
        fclose(input);
        return 0;
    }

    /* Reject a header too long for the buffer, unless it ends at EOF. */
    if (strchr(header_line, '\n') == NULL && !feof(input)) {
        fclose(input);
        return 0;
    }

    trim_ascii_whitespace(header_line);
    if (!parse_crc_header(header_line, header_format, stored_crc,
                          calculated_crc, header_suffix)) {
        fclose(input);
        return 0;
    }

    success = update_crc_from_file_body(input, end_policy, calculated_crc);
    fclose(input);
    return success;
}

/* Replace exactly the first byte_count bytes; leave the file body intact. */
static int overwrite_file_header(const char *filename,
                                 const char *replacement_header,
                                 size_t byte_count)
{
    FILE *output;
    int success;

    output = fopen(filename, "r+b");
    if (output == NULL) {
        return 0;
    }

    success = fwrite(replacement_header, 1U, byte_count, output) == byte_count &&
              fflush(output) == 0;
    if (fclose(output) != 0) {
        success = 0;
    }

    return success;
}

/* Append the two-digit additive checksum to a colon-prefixed hex record. */
static int append_hex_record_checksum(char *record)
{
    unsigned int byte_sum;
    unsigned int check_byte;
    size_t index;
    size_t record_length;
    int high_digit;
    int low_digit;

    byte_sum = 0U;
    record_length = strlen(record);

    /* Index 0 is the ':'; the remaining characters are ASCII hex pairs. */
    for (index = 1U; index + 1U < record_length; index += 2U) {
        high_digit = hex_digit_value((unsigned char)record[index]);
        low_digit = hex_digit_value((unsigned char)record[index + 1U]);
        if (high_digit < 0 || low_digit < 0) {
            return 0;
        }
        byte_sum = (byte_sum +
                    (unsigned int)((high_digit << 4) | low_digit)) & 0xFFU;
    }

    check_byte = (0U - byte_sum) & 0xFFU;
    sprintf(record + record_length, "%02X", check_byte);
    return 1;
}

/* Verify the stored CRC or overwrite it with the calculated value. */
static int process_file_crc(const char *filename,
                            enum xcal_header_format header_format,
                            enum xcal_end_marker_policy end_policy,
                            enum xcal_crc_operation operation)
{
    unsigned short stored_crc;
    unsigned short calculated_crc;
    char header_suffix[16];
    char replacement_header[64];

    if (!calculate_file_crc(filename, header_format, end_policy,
                            &stored_crc, &calculated_crc, header_suffix)) {
        return 0;
    }

    if (operation == XCAL_VERIFY_CRC || stored_crc == calculated_crc) {
        return stored_crc == calculated_crc;
    }

    if (header_format == XCAL_HEADER_PLAIN) {
        sprintf(replacement_header, "%04X", (unsigned int)calculated_crc);
        return overwrite_file_header(filename, replacement_header, 4U);
    }

    sprintf(replacement_header, ":04%04X%s",
            (unsigned int)swap_u16_bytes(calculated_crc), header_suffix);
    if (!append_hex_record_checksum(replacement_header)) {
        return 0;
    }

    return overwrite_file_header(filename, replacement_header,
                                 strlen(replacement_header));
}

/* Public names and call semantics are retained for all existing callers. */
int xcal_check_cal_file_crc(const char *filename)
{
    return process_file_crc(filename, XCAL_HEADER_PLAIN,
                            XCAL_STOP_AT_DOS_EOF, XCAL_VERIFY_CRC) ||
           process_file_crc(filename, XCAL_HEADER_EXTENDED,
                            XCAL_STOP_AT_DOS_EOF, XCAL_VERIFY_CRC);
}

int xcal_check_header_file_crc(const char *filename)
{
    return process_file_crc(filename, XCAL_HEADER_PLAIN,
                            XCAL_STOP_AT_DOS_EOF, XCAL_VERIFY_CRC);
}

int xcal_set_cal_file_crc(const char *filename)
{
    return process_file_crc(filename, XCAL_HEADER_PLAIN,
                            XCAL_READ_TO_PHYSICAL_EOF, XCAL_REPAIR_CRC) ||
           process_file_crc(filename, XCAL_HEADER_EXTENDED,
                            XCAL_READ_TO_PHYSICAL_EOF, XCAL_REPAIR_CRC);
}

int xcal_check_file_crc(const char *filename)
{
    return process_file_crc(filename, XCAL_HEADER_PLAIN,
                            XCAL_STOP_AT_DOS_EOF, XCAL_VERIFY_CRC);
}

int xcal_set_file_crc(const char *filename)
{
    return process_file_crc(filename, XCAL_HEADER_PLAIN,
                            XCAL_READ_TO_PHYSICAL_EOF, XCAL_REPAIR_CRC);
}
