/*
 * Calibration Transfer - native Windows front end.
 *
 * ISO C89 application code; Win32 controls and common dialogs only.
 * The RP1210/J1939/calibration implementation is linked directly into
 * the executable.  No CLR, WinForms, or companion application DLLs.
 */
#define WIN32_LEAN_AND_MEAN
#define CT_CONFIG_EXPORTS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <process.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rp1210clip.h"
#include "config_store.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_API         1001
#define ID_DEVICE      1002
#define ID_PROTOCOL    1003
#define ID_BAUD        1004
#define ID_TOOL_SA     1005
#define ID_ECM_SA      1006
#define ID_REFRESH     1007
#define ID_PULL        1008
#define ID_UPLOAD      1009

#define WM_TRANSFER_PROGRESS (WM_APP + 1)
#define WM_TRANSFER_FINISHED (WM_APP + 2)
#define PATH_CAP 1024

struct transfer_request {
    char api[64];
    char path[PATH_CAP];
    int device_id;
    int baud;
    unsigned char tool_sa;
    unsigned char ecm_sa;
    int upload;
    int result;
    char error[512];
};

struct progress_packet {
    int percent;
    char message[512];
};

struct app_state {
    HWND window;
    HWND api;
    HWND device;
    HWND protocol;
    HWND baud;
    HWND tool_sa;
    HWND ecm_sa;
    HWND refresh;
    HWND pull;
    HWND upload;
    HWND progress;
    HWND status;
    RP1210_DEVICE devices[RP1210_MAX_DEVICES];
    int device_count;
    int config_loaded;
    int busy;
    char config_api[64];
    int config_device;
    int config_baud;
    int config_tool_sa;
    int config_ecm_sa;
};

static struct app_state g_ui;

static void
copy_text(char *dest, const char *src, size_t capacity)
{
    size_t n;

    if (capacity == 0U)
        return;
    if (src == NULL)
        src = "";

    n = strlen(src);
    if (n >= capacity)
        n = capacity - 1U;
    if (n != 0U)
        memcpy(dest, src, n);
    dest[n] = '\0';
}

static void
set_status(const char *message)
{
    SetWindowTextA(g_ui.status, message);
}

static int
selected_index(HWND combo)
{
    LRESULT n;

    n = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    return n == CB_ERR ? -1 : (int)n;
}

static int
selected_data(HWND combo)
{
    int index;
    LRESULT data;

    index = selected_index(combo);
    if (index < 0)
        return -1;

    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)index, 0);
    if (data == CB_ERR)
        return -1;
    return (int)data;
}

static int
select_by_data(HWND combo, int wanted)
{
    int i;
    int count;

    count = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; ++i) {
        if ((int)SendMessageA(combo, CB_GETITEMDATA,
                             (WPARAM)i, 0) == wanted) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)i, 0);
            return 1;
        }
    }
    return 0;
}

static const char *
selected_api(void)
{
    int index;

    index = selected_data(g_ui.api);
    if (index < 0 || index >= g_ui.device_count)
        return NULL;
    return g_ui.devices[index].api;
}

static void
populate_devices(int wanted)
{
    const char *api;
    char name[384];
    int i;
    int item;

    SendMessageA(g_ui.device, CB_RESETCONTENT, 0, 0);
    item = (int)SendMessageA(g_ui.device, CB_ADDSTRING, 0,
                              (LPARAM)"Auto - first device for this API");
    if (item >= 0)
        SendMessageA(g_ui.device, CB_SETITEMDATA,
                     (WPARAM)item, (LPARAM)-1);

    api = selected_api();
    if (api != NULL) {
        for (i = 0; i < g_ui.device_count; ++i) {
            if (_stricmp(api, g_ui.devices[i].api) != 0)
                continue;
            sprintf(name, "%d - %.255s", g_ui.devices[i].device_id,
                    g_ui.devices[i].description);
            item = (int)SendMessageA(g_ui.device, CB_ADDSTRING,
                                     0, (LPARAM)name);
            if (item >= 0)
                SendMessageA(g_ui.device, CB_SETITEMDATA,
                             (WPARAM)item,
                             (LPARAM)g_ui.devices[i].device_id);
        }
    }

    if (!select_by_data(g_ui.device, wanted))
        SendMessageA(g_ui.device, CB_SETCURSEL, 0, 0);
}

static void
refresh_devices(int preserve_ui_selection)
{
    char previous_api[64];
    const char *current;
    char label[256];
    char status[128];
    int wanted_device;
    int count;
    int i;
    int j;
    int duplicate;
    int item;
    int wanted_api_index;

    current = preserve_ui_selection ? selected_api() : NULL;
    copy_text(previous_api, current != NULL ? current :
              g_ui.config_api, sizeof(previous_api));
    wanted_device = preserve_ui_selection ?
                    selected_data(g_ui.device) : g_ui.config_device;

    SendMessageA(g_ui.api, CB_RESETCONTENT, 0, 0);
    SendMessageA(g_ui.device, CB_RESETCONTENT, 0, 0);
    g_ui.device_count = 0;
    count = rp1210_refresh();
    if (count < 0) {
        set_status("RP1210 discovery failed.");
        return;
    }
    if (count > RP1210_MAX_DEVICES)
        count = RP1210_MAX_DEVICES;

    for (i = 0; i < count; ++i) {
        if (!rp1210_get(i, &g_ui.devices[g_ui.device_count]))
            continue;
        ++g_ui.device_count;
    }

    wanted_api_index = -1;
    for (i = 0; i < g_ui.device_count; ++i) {
        duplicate = 0;
        for (j = 0; j < i; ++j) {
            if (_stricmp(g_ui.devices[j].api, g_ui.devices[i].api) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate)
            continue;

        if (_stricmp(g_ui.devices[i].vendor,
                     g_ui.devices[i].api) == 0) {
            copy_text(label, g_ui.devices[i].api, sizeof(label));
        } else {
            sprintf(label, "%.127s (%.63s)",
                    g_ui.devices[i].vendor, g_ui.devices[i].api);
        }
        item = (int)SendMessageA(g_ui.api, CB_ADDSTRING,
                                 0, (LPARAM)label);
        if (item < 0)
            continue;
        SendMessageA(g_ui.api, CB_SETITEMDATA,
                     (WPARAM)item, (LPARAM)i);
        if (_stricmp(previous_api, g_ui.devices[i].api) == 0)
            wanted_api_index = item;
    }

    if (wanted_api_index < 0 &&
        (int)SendMessageA(g_ui.api, CB_GETCOUNT, 0, 0) > 0)
        wanted_api_index = 0;
    if (wanted_api_index >= 0)
        SendMessageA(g_ui.api, CB_SETCURSEL,
                     (WPARAM)wanted_api_index, 0);

    populate_devices(wanted_device);
    if (g_ui.device_count == 0) {
        set_status("No RP1210 devices found.");
    } else {
        sprintf(status, "%d RP1210 device(s) found.", g_ui.device_count);
        set_status(status);
    }
}

static int
parse_hex_edit(HWND edit, unsigned char *out)
{
    char text[16];
    size_t i;
    unsigned long value;

    GetWindowTextA(edit, text, sizeof(text));
    if (text[0] == '\0' || strlen(text) > 2U)
        return 0;
    for (i = 0U; text[i] != '\0'; ++i) {
        if (!isxdigit((unsigned char)text[i]))
            return 0;
    }

    value = strtoul(text, NULL, 16);
    if (value > 255UL)
        return 0;
    *out = (unsigned char)value;
    return 1;
}

static int
get_baud(void)
{
    int value;

    value = selected_data(g_ui.baud);
    return value < 0 ? 250000 : value;
}

static void
read_config(void)
{
    long value;
    char error[512];

    g_ui.config_api[0] = '\0';
    g_ui.config_device = -1;
    g_ui.config_baud = 250000;
    g_ui.config_tool_sa = 0xFA;
    g_ui.config_ecm_sa = 0x00;

    g_ui.config_loaded = ct_config_load_default() == 0;
    if (!g_ui.config_loaded) {
        ct_config_get_last_error(error, sizeof(error));
        if (error[0] != '\0')
            MessageBoxA(g_ui.window, error, "Config Warning",
                        MB_OK | MB_ICONWARNING);
        return;
    }

    ct_config_get_string("adapter", "api", "", g_ui.config_api,
                         sizeof(g_ui.config_api));
    value = -1;
    ct_config_get_int("adapter", "device", -1, &value);
    g_ui.config_device = (int)value;

    value = 250000;
    ct_config_get_int("adapter", "baud", 250000, &value);
    g_ui.config_baud = (int)value;

    value = 0xFA;
    ct_config_get_int("j1939", "tool_sa", 0xFA, &value);
    if (value >= 0 && value <= 255)
        g_ui.config_tool_sa = (int)value;

    value = 0;
    ct_config_get_int("j1939", "ecm_sa", 0, &value);
    if (value >= 0 && value <= 255)
        g_ui.config_ecm_sa = (int)value;
}

static void
apply_config(void)
{
    char hex[8];

    if (!select_by_data(g_ui.baud, g_ui.config_baud))
        select_by_data(g_ui.baud, 250000);

    sprintf(hex, "%02X", g_ui.config_tool_sa);
    SetWindowTextA(g_ui.tool_sa, hex);
    sprintf(hex, "%02X", g_ui.config_ecm_sa);
    SetWindowTextA(g_ui.ecm_sa, hex);
}

static void
save_config(int show_error)
{
    const char *api;
    char raw[16];
    char error[512];
    unsigned char tool_sa;
    unsigned char ecm_sa;
    int device_id;
    int rc;

    if (!g_ui.config_loaded)
        return;

    api = selected_api();
    device_id = selected_data(g_ui.device);
    if (api == NULL ||
        !parse_hex_edit(g_ui.tool_sa, &tool_sa) ||
        !parse_hex_edit(g_ui.ecm_sa, &ecm_sa))
        return;

    rc = ct_config_set_string("adapter", "api", api);
    if (rc == 0)
        rc = ct_config_set_int("adapter", "device", device_id);
    if (rc == 0)
        rc = ct_config_set_int("adapter", "baud", get_baud());
    if (rc == 0) {
        sprintf(raw, "0x%02X", (unsigned int)tool_sa);
        rc = ct_config_set_raw("j1939", "tool_sa", raw);
    }
    if (rc == 0) {
        sprintf(raw, "0x%02X", (unsigned int)ecm_sa);
        rc = ct_config_set_raw("j1939", "ecm_sa", raw);
    }
    if (rc == 0)
        rc = ct_config_save();
    if (rc != 0 && show_error) {
        ct_config_get_last_error(error, sizeof(error));
        MessageBoxA(g_ui.window, error, "Configuration Save Failed",
                    MB_OK | MB_ICONWARNING);
    }
}

static int
resolve_device_id(const char *api, int requested)
{
    int i;

    if (requested >= 0)
        return requested;
    for (i = 0; i < g_ui.device_count; ++i) {
        if (_stricmp(g_ui.devices[i].api, api) == 0)
            return g_ui.devices[i].device_id;
    }
    return -1;
}

static void
set_busy(int busy)
{
    g_ui.busy = busy;
    EnableWindow(g_ui.api, !busy);
    EnableWindow(g_ui.device, !busy);
    EnableWindow(g_ui.baud, !busy);
    EnableWindow(g_ui.tool_sa, !busy);
    EnableWindow(g_ui.ecm_sa, !busy);
    EnableWindow(g_ui.refresh, !busy);
    EnableWindow(g_ui.pull, !busy);
    EnableWindow(g_ui.upload, !busy);
}

static void RP1210_CALL
progress_callback(int percent, const char *message)
{
    struct progress_packet *packet;

    packet = (struct progress_packet *)malloc(sizeof(*packet));
    if (packet == NULL)
        return;

    packet->percent = percent;
    copy_text(packet->message, message, sizeof(packet->message));
    if (!PostMessageA(g_ui.window, WM_TRANSFER_PROGRESS,
                      0, (LPARAM)packet))
        free(packet);
}

static unsigned __stdcall
transfer_worker(void *argument)
{
    struct transfer_request *request;

    request = (struct transfer_request *)argument;
    if (request->upload) {
        request->result = rp1210_upload_ccal(
            request->api, request->device_id, request->baud,
            request->tool_sa, request->ecm_sa, request->path,
            progress_callback);
    } else {
        request->result = rp1210_pull_ccal(
            request->api, request->device_id, request->baud,
            request->tool_sa, request->ecm_sa, request->path,
            progress_callback);
    }

    request->error[0] = '\0';
    if (request->result != 0)
        rp1210_get_last_error(request->error, sizeof(request->error));
    if (!PostMessageA(g_ui.window, WM_TRANSFER_FINISHED,
                      0, (LPARAM)request))
        free(request);
    return 0U;
}

static int
choose_path(int upload, char *path, DWORD size)
{
    OPENFILENAMEA dialog;
    static const char filter[] =
        "Calibration (*.ccal)\0*.ccal\0All files (*.*)\0*.*\0";

    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_ui.window;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = size;
    dialog.lpstrDefExt = "ccal";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (upload) {
        path[0] = '\0';
        dialog.Flags |= OFN_FILEMUSTEXIST;
        return GetOpenFileNameA(&dialog) != 0;
    }

    copy_text(path, "ecm-upload.ccal", size);
    dialog.Flags |= OFN_OVERWRITEPROMPT;
    return GetSaveFileNameA(&dialog) != 0;
}

static void
start_transfer(int upload)
{
    const char *api;
    struct transfer_request *request;
    HANDLE thread;
    unsigned thread_id;
    char confirm[1600];

    if (g_ui.busy)
        return;

    api = selected_api();
    if (api == NULL) {
        MessageBoxA(g_ui.window, "Select an RP1210 API/device.",
                    "Calibration Transfer", MB_OK | MB_ICONWARNING);
        return;
    }

    request = (struct transfer_request *)calloc(1U, sizeof(*request));
    if (request == NULL) {
        MessageBoxA(g_ui.window, "Not enough memory.",
                    "Calibration Transfer", MB_OK | MB_ICONERROR);
        return;
    }

    copy_text(request->api, api, sizeof(request->api));
    request->device_id = resolve_device_id(
        api, selected_data(g_ui.device));
    request->baud = get_baud();
    request->upload = upload;

    if (request->device_id < 0) {
        MessageBoxA(g_ui.window,
                    "No physical device is available for this RP1210 API.",
                    "Calibration Transfer", MB_OK | MB_ICONWARNING);
        free(request);
        return;
    }
    if (!parse_hex_edit(g_ui.tool_sa, &request->tool_sa) ||
        !parse_hex_edit(g_ui.ecm_sa, &request->ecm_sa)) {
        MessageBoxA(g_ui.window,
                    "Tool SA and ECM SA must be hex bytes (e.g. FA, 00).",
                    "Calibration Transfer", MB_OK | MB_ICONWARNING);
        free(request);
        return;
    }

    if (!choose_path(upload, request->path, sizeof(request->path))) {
        free(request);
        return;
    }

    if (upload) {
        sprintf(confirm,
                "Program this calibration into the ECM?\r\n\r\n"
                "%.1023s\r\n\r\n"
                "The native uploader validates the CCAL CRC before "
                "opening the RP1210 adapter. Invalid files send no "
                "programming traffic.",
                request->path);
        if (MessageBoxA(g_ui.window, confirm,
                        "Confirm Calibration Upload",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            free(request);
            return;
        }
    }

    save_config(0);
    SendMessageA(g_ui.progress, PBM_SETPOS, 0, 0);
    set_status(upload ? "Verifying calibration CRC..." : "Starting...");
    set_busy(1);

    thread = (HANDLE)_beginthreadex(NULL, 0, transfer_worker,
                                     request, 0, &thread_id);
    if (thread == NULL) {
        set_busy(0);
        set_status("Failed to start native transfer worker.");
        MessageBoxA(g_ui.window, "Could not start transfer thread.",
                    "Calibration Transfer", MB_OK | MB_ICONERROR);
        free(request);
        return;
    }
    CloseHandle(thread);
}

static HWND
make_control(const char *class_name, const char *label,
             DWORD style, DWORD ex_style, int id,
             int x, int y, int width, int height)
{
    HWND control;
    HFONT font;

    control = CreateWindowExA(ex_style, class_name, label,
                              WS_CHILD | WS_VISIBLE | style,
                              x, y, width, height,
                              g_ui.window, (HMENU)(INT_PTR)id,
                              GetModuleHandleA(NULL), NULL);
    if (control != NULL) {
        font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
    }
    return control;
}

static HWND
make_combo(int id, int top)
{
    return make_control("COMBOBOX", "",
                        CBS_DROPDOWNLIST | CBS_HASSTRINGS |
                        WS_VSCROLL | WS_TABSTOP,
                        0, id, 175, top, 360, 220);
}

static HWND
make_edit(int id, int top, const char *initial)
{
    HWND edit;

    edit = make_control("EDIT", initial,
                        WS_BORDER | WS_TABSTOP | ES_UPPERCASE |
                        ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
                        id, 175, top, 90, 24);
    if (edit != NULL)
        SendMessageA(edit, EM_LIMITTEXT, 2, 0);
    return edit;
}

static void
make_label(const char *text, int top)
{
    make_control("STATIC", text, 0, 0,
                 0, 28, top + 4, 135, 22);
}

static int
create_controls(void)
{
    static const char *bauds[] =
        {"Auto", "125000", "250000", "500000", "1000000"};
    static const int baud_values[] =
        {0, 125000, 250000, 500000, 1000000};
    int i;
    int item;

    make_label("Vendor:", 15);
    g_ui.api = make_combo(ID_API, 15);
    make_label("Device:", 55);
    g_ui.device = make_combo(ID_DEVICE, 55);
    make_label("Protocol:", 105);
    g_ui.protocol = make_combo(ID_PROTOCOL, 105);
    SendMessageA(g_ui.protocol, CB_ADDSTRING, 0, (LPARAM)"J1939");
    SendMessageA(g_ui.protocol, CB_SETCURSEL, 0, 0);
    EnableWindow(g_ui.protocol, FALSE);

    make_label("Baud:", 145);
    g_ui.baud = make_combo(ID_BAUD, 145);
    for (i = 0; i < 5; ++i) {
        item = (int)SendMessageA(g_ui.baud, CB_ADDSTRING,
                                 0, (LPARAM)bauds[i]);
        if (item >= 0)
            SendMessageA(g_ui.baud, CB_SETITEMDATA,
                         (WPARAM)item, (LPARAM)baud_values[i]);
    }
    select_by_data(g_ui.baud, 250000);

    make_label("Tool SA (hex):", 195);
    g_ui.tool_sa = make_edit(ID_TOOL_SA, 195, "FA");
    make_label("ECM SA (hex):", 235);
    g_ui.ecm_sa = make_edit(ID_ECM_SA, 235, "00");

    g_ui.refresh = make_control("BUTTON", "Refresh",
                                BS_PUSHBUTTON | WS_TABSTOP,
                                0, ID_REFRESH, 175, 295, 125, 32);
    g_ui.pull = make_control("BUTTON", "Pull ECM CAL",
                             BS_PUSHBUTTON | WS_TABSTOP,
                             0, ID_PULL, 310, 295, 130, 32);
    g_ui.upload = make_control("BUTTON", "Upload CCAL",
                               BS_PUSHBUTTON | WS_TABSTOP,
                               0, ID_UPLOAD, 455, 295, 130, 32);
    g_ui.progress = make_control(PROGRESS_CLASSA, "",
                                 PBS_SMOOTH, 0,
                                 0, 28, 360, 615, 22);
    if (g_ui.progress != NULL) {
        SendMessageA(g_ui.progress, PBM_SETRANGE32, 0, 100);
        SendMessageA(g_ui.progress, PBM_SETPOS, 0, 0);
    }
    g_ui.status = make_control("STATIC", "Ready.",
                               SS_LEFT, 0, 0, 28, 395, 615, 45);

    return g_ui.api != NULL && g_ui.device != NULL &&
           g_ui.protocol != NULL && g_ui.baud != NULL &&
           g_ui.tool_sa != NULL && g_ui.ecm_sa != NULL &&
           g_ui.refresh != NULL && g_ui.pull != NULL &&
           g_ui.upload != NULL && g_ui.progress != NULL &&
           g_ui.status != NULL;
}

static LRESULT CALLBACK
window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct progress_packet *progress;
    struct transfer_request *request;
    char result[1800];

    switch (message) {
    case WM_CREATE:
        g_ui.window = window;
        if (!create_controls())
            return -1;
        return 0;

    case WM_COMMAND:
        if (LOWORD(wparam) == ID_API &&
            HIWORD(wparam) == CBN_SELCHANGE) {
            populate_devices(-1);
        } else if (HIWORD(wparam) == BN_CLICKED) {
            switch (LOWORD(wparam)) {
            case ID_REFRESH:
                refresh_devices(1);
                break;
            case ID_PULL:
                start_transfer(0);
                break;
            case ID_UPLOAD:
                start_transfer(1);
                break;
            }
        }
        return 0;

    case WM_TRANSFER_PROGRESS:
        progress = (struct progress_packet *)lparam;
        if (progress != NULL) {
            if (progress->percent < 0)
                progress->percent = 0;
            if (progress->percent > 100)
                progress->percent = 100;
            SendMessageA(g_ui.progress, PBM_SETPOS,
                         (WPARAM)progress->percent, 0);
            set_status(progress->message);
            free(progress);
        }
        return 0;

    case WM_TRANSFER_FINISHED:
        request = (struct transfer_request *)lparam;
        if (request != NULL) {
            set_busy(0);
            if (request->result == 0) {
                SendMessageA(g_ui.progress, PBM_SETPOS, 100, 0);
                set_status(request->upload ?
                           "Calibration upload completed." :
                           "Saved and native CRC verified.");
                sprintf(result, "%s\r\n\r\n%.1023s",
                        request->upload ?
                        "Calibration upload completed successfully." :
                        "ECM calibration saved and CRC verified.",
                        request->path);
                MessageBoxA(window, result,
                            request->upload ? "Calibration Upload" :
                            "Calibration Download", MB_OK | MB_ICONINFORMATION);
            } else {
                if (request->error[0] == '\0')
                    copy_text(request->error, "Unknown native transfer error.",
                              sizeof(request->error));
                set_status(request->error);
                sprintf(result, "%.511s\r\n\r\nNative return code: %d",
                        request->error, request->result);
                MessageBoxA(window, result,
                            request->upload ?
                            "Calibration Upload Failed" :
                            "Calibration Download Failed",
                            MB_OK | MB_ICONERROR);
            }
            free(request);
        }
        return 0;

    case WM_CLOSE:
        if (g_ui.busy) {
            MessageBoxA(window,
                "A calibration transfer is in progress. Do not close "
                "the application or disconnect the adapter until it finishes.",
                "Calibration Transfer In Progress",
                MB_OK | MB_ICONWARNING);
            return 0;
        }
        save_config(0);
        if (g_ui.config_loaded) {
            ct_config_close();
            g_ui.config_loaded = 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(window, message, wparam, lparam);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous,
        LPSTR command_line, int show_command)
{
    WNDCLASSEXA cls;
    INITCOMMONCONTROLSEX controls;
    MSG message;
    HWND window;
    int result;

    (void)previous;
    (void)command_line;
    memset(&g_ui, 0, sizeof(g_ui));

    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&controls)) {
        MessageBoxA(NULL, "Common controls initialization failed.",
                    "Calibration Transfer", MB_OK | MB_ICONERROR);
        return 1;
    }

    memset(&cls, 0, sizeof(cls));
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(101));
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    cls.lpszClassName = "CalibrationTransferWinAPI";
    cls.hIconSm = cls.hIcon;

    if (!RegisterClassExA(&cls))
        return 1;

    window = CreateWindowExA(WS_EX_DLGMODALFRAME |
                            WS_EX_CONTROLPARENT,
                            cls.lpszClassName, "ECM Calibration Transfer",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                            WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 700, 500,
                            NULL, NULL, instance, NULL);
    if (window == NULL) {
        MessageBoxA(NULL, "Could not create the native UI.",
                    "Calibration Transfer", MB_OK | MB_ICONERROR);
        return 1;
    }

    read_config();
    apply_config();
    refresh_devices(0);
    ShowWindow(window, show_command);
    UpdateWindow(window);

    for (;;) {
        result = (int)GetMessageA(&message, NULL, 0, 0);
        if (result <= 0)
            break;
        if (!IsDialogMessageA(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }

    return result == -1 ? 1 : (int)message.wParam;
}
