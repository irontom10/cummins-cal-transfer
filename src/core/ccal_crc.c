#include "ccal_crc.h"
#include <stdio.h>
#include <string.h>

static unsigned int parity(unsigned int v)
{
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return v & 1U;
}

static unsigned short step(unsigned short crc, unsigned int c)
{
    unsigned int i, t;
    i = (c ^ crc) & 0xFFU;
    t = (((i << 1) ^ i) << 6) & 0xFFFFU;
    if (parity(i)) t ^= 0x8002U;
    return (unsigned short)((((unsigned int)crc >> 7) & 0x01FEU) ^ t);
}

unsigned short ccal_crc(unsigned short crc, const unsigned char *s)
{
    while (*s) {
        if (*s >= 0x20U) crc = step(crc, *s);
        ++s;
    }
    return crc;
}

static int hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int hex4(const char *s, unsigned short *v)
{
    unsigned int n, i;
    int h;
    n = 0U;
    for (i = 0U; i < 4U; ++i) {
        h = hex((unsigned char)s[i]);
        if (h < 0) return 0;
        n = (n << 4) | (unsigned int)h;
    }
    *v = (unsigned short)n;
    return 1;
}

static int ws(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static void trim(char *s)
{
    char *p, *e;
    p = s;
    while (*p && ws((unsigned char)*p)) ++p;
    if (p != s) memmove(s, p, strlen(p) + 1U);
    e = s + strlen(s);
    while (e > s && ws((unsigned char)e[-1])) --e;
    *e = '\0';
}

static unsigned short swap16(unsigned short v)
{
    return (unsigned short)((v << 8) | (v >> 8));
}

static int calc(const char *file, int alt, int check,
                unsigned short *stored, unsigned short *crc, char suffix[16])
{
    FILE *f;
    char first[64];
    unsigned short v;
    int c, skip;
    size_t n;

    f = fopen(file, "rb");
    if (!f) return 0;
    if (!fgets(first, sizeof(first), f)) { fclose(f); return 0; }
    if (!strchr(first, '\n') && !feof(f)) { fclose(f); return 0; }
    trim(first);
    n = strlen(first);
    *crc = 0U;

    if (!alt) {
        if (n != 4U || !hex4(first, stored)) { fclose(f); return 0; }
        suffix[0] = '\0';
    } else {
        if (n != 19U || !hex4(first + 3, &v)) { fclose(f); return 0; }
        *stored = swap16(v);
        memcpy(suffix, first + 7, 10U);
        suffix[10] = '\0';
        *crc = ccal_crc(*crc, (const unsigned char *)suffix);
    }

    skip = 0;
    while ((c = fgetc(f)) != EOF) {
        if (check && (unsigned char)c == 0x1AU) break;
        if (c == '\n') { skip = 0; continue; }
        if (skip) continue;
        if (c == 0) { skip = 1; continue; }
        if ((unsigned char)c >= 0x20U) *crc = step(*crc, (unsigned char)c);
    }
    if (ferror(f)) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static int write_first(const char *file, const char *s, size_t n)
{
    FILE *f;
    int ok;
    f = fopen(file, "r+b");
    if (!f) return 0;
    ok = fwrite(s, 1U, n, f) == n && fflush(f) == 0;
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static int line_checksum(char *s)
{
    unsigned int sum, b;
    size_t i, n;
    int hi, lo;
    sum = 0U;
    n = strlen(s);
    for (i = 1U; i + 1U < n; i += 2U) {
        hi = hex((unsigned char)s[i]); lo = hex((unsigned char)s[i + 1U]);
        if (hi < 0 || lo < 0) return 0;
        sum = (sum + (unsigned int)((hi << 4) | lo)) & 0xFFU;
    }
    b = (0U - sum) & 0xFFU;
    sprintf(s + n, "%02X", b);
    return 1;
}

static int mode(const char *file, int alt, int check, int set)
{
    unsigned short stored, crc;
    char suffix[16], out[64];
    if (!calc(file, alt, check, &stored, &crc, suffix)) return 0;
    if (!set || stored == crc) return stored == crc;
    if (!alt) {
        sprintf(out, "%04X", (unsigned int)crc);
        return write_first(file, out, 4U);
    }
    sprintf(out, ":04%04X%s", (unsigned int)swap16(crc), suffix);
    if (!line_checksum(out)) return 0;
    return write_first(file, out, strlen(out));
}

int ccal_check_cal_file_crc(const char *f)
{
    return mode(f, 0, 1, 0) || mode(f, 1, 1, 0);
}

int ccal_check_header_file_crc(const char *f)
{
    return mode(f, 0, 1, 0);
}

int ccal_set_cal_file_crc(const char *f)
{
    return mode(f, 0, 0, 1) || mode(f, 1, 0, 1);
}

int ccal_check_file_crc(const char *f)
{
    return mode(f, 0, 1, 0);
}

int ccal_set_file_crc(const char *f)
{
    return mode(f, 0, 0, 1);
}
