#ifndef RP1210SCAN_H
#define RP1210SCAN_H

#ifdef _WIN32
/* Native frontends link the protocol statically, not as exported DLL APIs. */
#ifdef RP1210_STATIC_LINK
#define RP1210_EXPORT
#else
#define RP1210_EXPORT __declspec(dllexport)
#endif
#define RP1210_CALL __cdecl
#else
#define RP1210_EXPORT
#define RP1210_CALL
#endif

#define RP1210_MAX_DEVICES 256

typedef struct rp1210_device
{
    int device_id;

    char api[64];
    char vendor[128];
    char description[256];
} RP1210_DEVICE;

RP1210_EXPORT int RP1210_CALL rp1210_refresh(void);
RP1210_EXPORT int RP1210_CALL rp1210_count(void);
RP1210_EXPORT int RP1210_CALL rp1210_get(
    int index,
    RP1210_DEVICE *device
);

#endif
