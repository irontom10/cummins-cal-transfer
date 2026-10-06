#ifndef CCAL_CRC_H
#define CCAL_CRC_H

#include <limits.h>
#if USHRT_MAX != 0xFFFFU
#error "16-bit unsigned short required"
#endif

unsigned short ccal_crc(unsigned short crc, const unsigned char *s);
int ccal_check_cal_file_crc(const char *file);
int ccal_check_header_file_crc(const char *file);
int ccal_set_cal_file_crc(const char *file);
int ccal_check_file_crc(const char *file);
int ccal_set_file_crc(const char *file);

#endif
