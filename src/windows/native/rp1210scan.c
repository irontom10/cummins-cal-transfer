#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rp1210scan.h"

static RP1210_DEVICE g_devices[RP1210_MAX_DEVICES];
static int g_device_count = 0;

static void safe_copy(char *dst, const char *src, size_t size)
{
    if (dst == NULL || size == 0)
        return;

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static char *trim(char *text)
{
    char *end;

    if (text == NULL)
        return NULL;

    while (*text != '\0' &&
           isspace((unsigned char)*text))
    {
        ++text;
    }

    if (*text == '\0')
        return text;

    end = text + strlen(text) - 1;

    while (end > text &&
           isspace((unsigned char)*end))
    {
        *end = '\0';
        --end;
    }

    return text;
}

static void build_ini_path(
    char *out,
    size_t out_size,
    const char *windows_dir,
    const char *api
)
{
    size_t len;

    if (out == NULL || out_size == 0)
        return;

    out[0] = '\0';

    safe_copy(out, windows_dir, out_size);

    len = strlen(out);

    if (len != 0 &&
        out[len - 1] != '\\')
    {
        if (len + 1 < out_size)
        {
            out[len] = '\\';
            out[len + 1] = '\0';
        }
    }

    strncat(
        out,
        api,
        out_size - strlen(out) - 1
    );

    len = strlen(out);

    if (len < 4 ||
        _stricmp(out + len - 4, ".ini") != 0)
    {
        strncat(
            out,
            ".ini",
            out_size - strlen(out) - 1
        );
    }
}

static void scan_api(
    const char *windows_dir,
    const char *api
)
{
    char ini_path[MAX_PATH];
    char vendor[128];

    char section[64];
    char description[256];
    char device_name[256];

    int section_index;
    int device_id;

    build_ini_path(
        ini_path,
        sizeof(ini_path),
        windows_dir,
        api
    );

    vendor[0] = '\0';

    GetPrivateProfileStringA(
        "VendorInformation",
        "Name",
        "",
        vendor,
        sizeof(vendor),
        ini_path
    );

    if (vendor[0] == '\0')
    {
        GetPrivateProfileStringA(
            "VendorInformation",
            "VendorName",
            "",
            vendor,
            sizeof(vendor),
            ini_path
        );
    }

    if (vendor[0] == '\0')
        safe_copy(vendor, api, sizeof(vendor));
    for (section_index = 0;
         section_index < 256;
         ++section_index)
    {
        RP1210_DEVICE *device;

        if (g_device_count >= RP1210_MAX_DEVICES)
            return;

        sprintf(
            section,
            "DeviceInformation%d",
            section_index
        );

        device_id = GetPrivateProfileIntA(
            section,
            "DeviceID",
            -1,
            ini_path
        );

        if (device_id < 0)
            continue;

        description[0] = '\0';
        device_name[0] = '\0';

        GetPrivateProfileStringA(
            section,
            "DeviceDescription",
            "",
            description,
            sizeof(description),
            ini_path
        );

        GetPrivateProfileStringA(
            section,
            "DeviceName",
            "",
            device_name,
            sizeof(device_name),
            ini_path
        );

        if (description[0] == '\0')
            safe_copy(
                description,
                device_name,
                sizeof(description)
            );

        if (description[0] == '\0')
        {
            sprintf(
                description,
                "RP1210 Device %d",
                device_id
            );
        }

        device = &g_devices[g_device_count];

        memset(
            device,
            0,
            sizeof(*device)
        );

        device->device_id = device_id;

        safe_copy(
            device->api,
            api,
            sizeof(device->api)
        );

        safe_copy(
            device->vendor,
            vendor,
            sizeof(device->vendor)
        );

        safe_copy(
            device->description,
            description,
            sizeof(device->description)
        );

        ++g_device_count;
    }
}

int RP1210_CALL rp1210_refresh(void)
{
    char windows_dir[MAX_PATH];
    char master_ini[MAX_PATH];

    char api_list[4096];
    char parse_buffer[4096];

    char *token;
    char *api;

    size_t len;

    g_device_count = 0;

    if (GetWindowsDirectoryA(
            windows_dir,
            sizeof(windows_dir)) == 0)
    {
        return -1;
    }

    safe_copy(
        master_ini,
        windows_dir,
        sizeof(master_ini)
    );

    len = strlen(master_ini);

    if (len != 0 &&
        master_ini[len - 1] != '\\')
    {
        if (len + 1 < sizeof(master_ini))
        {
            master_ini[len] = '\\';
            master_ini[len + 1] = '\0';
        }
    }

    strncat(
        master_ini,
        "RP121032.ini",
        sizeof(master_ini) -
        strlen(master_ini) - 1
    );

    api_list[0] = '\0';

    GetPrivateProfileStringA(
        "RP1210Support",
        "APIImplementations",
        "",
        api_list,
        sizeof(api_list),
        master_ini
    );

    if (api_list[0] == '\0')
        return 0;

    safe_copy(
        parse_buffer,
        api_list,
        sizeof(parse_buffer)
    );

    token = strtok(parse_buffer, ",");

    while (token != NULL)
    {
        api = trim(token);

        if (api != NULL &&
            api[0] != '\0')
        {
            scan_api(
                windows_dir,
                api
            );
        }

        token = strtok(NULL, ",");
    }

    return g_device_count;
}

int RP1210_CALL rp1210_count(void)
{
    return g_device_count;
}

int RP1210_CALL rp1210_get(
    int index,
    RP1210_DEVICE *device
)
{
    if (device == NULL)
        return 0;

    if (index < 0 ||
        index >= g_device_count)
    {
        return 0;
    }

    memcpy(
        device,
        &g_devices[index],
        sizeof(*device)
    );

    return 1;
}
