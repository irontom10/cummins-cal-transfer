#ifndef RP1210SCAN_H
#define RP1210SCAN_H

#ifdef _WIN32
#define RP1210_EXPORT __declspec(dllexport)
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
