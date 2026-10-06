#ifndef CAL_TRANSFER_ANDROID_WINDOWS_COMPAT_H
#define CAL_TRANSFER_ANDROID_WINDOWS_COMPAT_H

/*
 * Tiny compatibility shim for the handful of Win32 utility calls still used
 * by the protocol/file code.  The Windows RP1210 layer is NOT built on Android.
 */

#include <stdio.h>
#include <strings.h>
#include <time.h>

typedef unsigned int DWORD;

typedef struct _SYSTEMTIME {
    unsigned short wYear;
    unsigned short wMonth;
    unsigned short wDayOfWeek;
    unsigned short wDay;
    unsigned short wHour;
    unsigned short wMinute;
    unsigned short wSecond;
    unsigned short wMilliseconds;
} SYSTEMTIME;

static DWORD GetTickCount(void)
{
    struct timespec ts;
    unsigned long ms;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0U;

    ms = (unsigned long)ts.tv_sec * 1000UL;
    ms += (unsigned long)(ts.tv_nsec / 1000000L);
    return (DWORD)ms;
}

static void Sleep(DWORD milliseconds)
{
    struct timespec req;

    req.tv_sec = (time_t)(milliseconds / 1000U);
    req.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&req, &req) != 0)
        ;
}

static void GetLocalTime(SYSTEMTIME *st)
{
    time_t now;
    struct tm tmv;

    if (st == NULL)
        return;

    now = time(NULL);
    if (localtime_r(&now, &tmv) == NULL) {
        st->wYear = 1970U;
        st->wMonth = 1U;
        st->wDay = 1U;
        st->wDayOfWeek = 4U;
        st->wHour = 0U;
        st->wMinute = 0U;
        st->wSecond = 0U;
        st->wMilliseconds = 0U;
        return;
    }

    st->wYear = (unsigned short)(tmv.tm_year + 1900);
    st->wMonth = (unsigned short)(tmv.tm_mon + 1);
    st->wDay = (unsigned short)tmv.tm_mday;
    st->wDayOfWeek = (unsigned short)tmv.tm_wday;
    st->wHour = (unsigned short)tmv.tm_hour;
    st->wMinute = (unsigned short)tmv.tm_min;
    st->wSecond = (unsigned short)tmv.tm_sec;
    st->wMilliseconds = 0U;
}

static int DeleteFileA(const char *path)
{
    return path != NULL && remove(path) == 0;
}

#define _stricmp strcasecmp

#endif
