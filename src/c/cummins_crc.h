#ifndef CUMMINS_CRC_H
#define CUMMINS_CRC_H

#include <limits.h>
#if USHRT_MAX != 0xFFFFU
#error "16-bit unsigned short required"
#endif

unsigned short cummins_crc(unsigned short crc, const unsigned char *s);
int cummins_check_cal_file_crc(const char *file);
int cummins_check_header_file_crc(const char *file);
int cummins_set_cal_file_crc(const char *file);
int cummins_check_file_crc(const char *file);
int cummins_set_file_crc(const char *file);

#endif
