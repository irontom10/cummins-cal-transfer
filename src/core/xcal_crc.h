#ifndef XCAL_CRC_H
#define XCAL_CRC_H

#include <limits.h>
#if USHRT_MAX != 0xFFFFU
#error "16-bit unsigned short required"
#endif

unsigned short xcal_crc(unsigned short crc, const unsigned char *s);
int xcal_check_cal_file_crc(const char *file);
int xcal_check_header_file_crc(const char *file);
int xcal_set_cal_file_crc(const char *file);
int xcal_check_file_crc(const char *file);
int xcal_set_file_crc(const char *file);

#endif
