/*
 * ct_platform.c
 *
 * Minimal operating-system compatibility helpers used by the protocol core.
 * C89 source.
 */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#else

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <errno.h>
#include <time.h>
#include <strings.h>

#endif

#include "ct_platform.h"

unsigned long
ct_monotonic_ms(void)
{
#ifdef _WIN32
    return (unsigned long)GetTickCount();
#else
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0UL;

    return (unsigned long)now.tv_sec * 1000UL +
           (unsigned long)(now.tv_nsec / 1000000L);
#endif
}

void
ct_sleep_ms(unsigned long ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec request;
    struct timespec remaining;

    request.tv_sec = (time_t)(ms / 1000UL);
    request.tv_nsec = (long)((ms % 1000UL) * 1000000UL);

    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR)
            break;
        request = remaining;
    }
#endif
}

int
ct_stricmp(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}
