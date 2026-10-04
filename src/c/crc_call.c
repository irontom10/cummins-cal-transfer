#include "ccal_crc.h"
#include <ctype.h>
#include <stdio.h>


static const char *b(int v) { return v ? "True" : "False"; }
static int eq(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
    return *a == *b;
}

int main(int argc, char **argv)
{
    const char *m, *f;
    int r;
    if (argc != 3) {
        puts("usage:");
        puts("  crc_call.exe check file.ihex");
        puts("  crc_call.exe set   file.ihex");
        return 1;
    }
    m = argv[1]; f = argv[2];
    if (eq(m, "check")) {
        r = ccal_check_cal_file_crc(f);    printf("CheckCalFileCRC = %s\n", b(r));
        r = ccal_check_header_file_crc(f); printf("CheckHeaderFileCRC = %s\n", b(r));
        r = ccal_check_file_crc(f);        printf("CheckFileCRC = %s\n", b(r));
        return 0;
    }
    if (eq(m, "set")) {
        r = ccal_set_cal_file_crc(f); printf("SetCalFileCRC = %s\n", b(r));
        r = ccal_set_file_crc(f);     printf("SetFileCRC = %s\n", b(r));
        return 0;
    }
    printf("unknown mode: %s\n", m);
    return 1;
}
