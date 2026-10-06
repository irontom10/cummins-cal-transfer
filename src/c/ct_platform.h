#ifndef CT_PLATFORM_H
#define CT_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Small platform shim for protocol code.
 *
 * Keep operating-system details here so the calibration/J1939 modules do not
 * need to include Win32 or POSIX headers directly.
 */
unsigned long ct_monotonic_ms(void);
void ct_sleep_ms(unsigned long ms);
int ct_stricmp(const char *a, const char *b);
void ct_get_local_date(unsigned int *year,
                       unsigned int *month,
                       unsigned int *day);

#ifdef __cplusplus
}
#endif

#endif /* CT_PLATFORM_H */
