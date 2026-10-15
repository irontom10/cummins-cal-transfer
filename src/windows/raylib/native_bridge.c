/*
 * Windows-only UI bridge. Do not include raylib.h here.
 * Keeps Windows common dialogs, Win32 threading and RP1210 callbacks
 * out of the raylib translation unit to avoid Win32/raylib API collisions.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <process.h>
#include <stdio.h>
#include <string.h>

#include "native_bridge.h"
#include "rp1210clip.h"

static CRITICAL_SECTION g_lock;
static HANDLE g_thread;
static struct ct_transfer_request g_request;
static struct ct_transfer_status g_status;

static void
safe_copy(char *dst, const char *src, size_t cap)
{
    size_t len;
    if (cap == 0U)
        return;
    if (src == NULL)
        src = "";
    len = strlen(src);
    if (len >= cap)
        len = cap - 1U;
    if (len != 0U)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

void
ct_native_init(void)
{
    InitializeCriticalSection(&g_lock);
    g_thread = NULL;
    memset(&g_status, 0, sizeof(g_status));
}

void
ct_native_shutdown(void)
{
    ct_native_join();
    DeleteCriticalSection(&g_lock);
}

int
ct_native_choose_path(int upload, char *path, unsigned long cap, void *owner)
{
    OPENFILENAMEA dialog;
    static const char filters[] =
        "XCAL calibration (*.xcal)\0*.xcal\0All files (*.*)\0*.*\0";
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = (HWND)owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)cap;
    dialog.lpstrFilter = filters;
    dialog.lpstrDefExt = "xcal";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (upload) {
        path[0] = '\0';
        dialog.Flags |= OFN_FILEMUSTEXIST;
        return GetOpenFileNameA(&dialog) != 0;
    }
    safe_copy(path, "ecm-calibration.xcal", (size_t)cap);
    dialog.Flags |= OFN_OVERWRITEPROMPT;
    return GetSaveFileNameA(&dialog) != 0;
}

static void RP1210_CALL
report_progress(int percent, const char *message)
{
    EnterCriticalSection(&g_lock);
    g_status.percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    safe_copy(g_status.status, message, sizeof(g_status.status));
    LeaveCriticalSection(&g_lock);
}

static unsigned __stdcall
transfer_thread(void *argument)
{
    int rc;
    char error[CT_RAYLIB_STATUS_CAP];
    (void)argument;
    if (g_request.upload) {
        rc = rp1210_upload_xcal(g_request.api, g_request.device_id,
                                g_request.baud, g_request.tool_sa,
                                g_request.ecm_sa, g_request.path,
                                report_progress);
    } else {
        rc = rp1210_pull_xcal(g_request.api, g_request.device_id,
                              g_request.baud, g_request.tool_sa,
                              g_request.ecm_sa, g_request.path,
                              report_progress);
    }
    error[0] = '\0';
    if (rc != 0)
        rp1210_get_last_error(error, sizeof(error));
    EnterCriticalSection(&g_lock);
    g_status.result = rc;
    safe_copy(g_status.error, error, sizeof(g_status.error));
    g_status.done = 1;
    LeaveCriticalSection(&g_lock);
    return 0U;
}

int
ct_native_begin(const struct ct_transfer_request *request)
{
    unsigned id;
    if (g_thread != NULL || request == NULL)
        return -1;
    EnterCriticalSection(&g_lock);
    g_request = *request;
    memset(&g_status, 0, sizeof(g_status));
    safe_copy(g_status.status,
              g_request.upload ? "Verifying calibration CRC..." :
                                 "Connecting to ECM...",
              sizeof(g_status.status));
    LeaveCriticalSection(&g_lock);
    g_thread = (HANDLE)_beginthreadex(NULL, 0, transfer_thread, NULL, 0, &id);
    return g_thread != NULL ? 0 : -1;
}

void
ct_native_snapshot(struct ct_transfer_status *snapshot)
{
    if (snapshot == NULL)
        return;
    EnterCriticalSection(&g_lock);
    *snapshot = g_status;
    LeaveCriticalSection(&g_lock);
}

void
ct_native_join(void)
{
    if (g_thread != NULL) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show;
    return ct_raylib_main();
}
