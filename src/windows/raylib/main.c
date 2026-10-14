/*
 * Calibration Transfer - raylib front end (Windows RP1210).
 *
 * ISO C89 UI; links the SAME native core and Windows RP1210 backend as
 * src/windows/win-api/main.c. No C#/.NET or protocol implementation here.
 *
 * This target is x86 because current Windows RP1210 vendor DLLs are x86.
 */
#define CT_CONFIG_EXPORTS
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "rp1210clip.h"
#include "config_store.h"
#include "ct_platform.h"
#include "native_bridge.h"

#define WIDTH 1000
#define HEIGHT 760
#define PATH_CAP 1024
#define STATUS_CAP 512
#define MAX_APIS RP1210_MAX_DEVICES

enum { COMBO_NONE, COMBO_API, COMBO_DEVICE, COMBO_BAUD };
enum { MODAL_NONE, MODAL_CONFIRM, MODAL_RESULT, MODAL_WARNING };
enum { FOCUS_NONE, FOCUS_TOOL, FOCUS_ECM };

struct app_state {
    RP1210_DEVICE devices[RP1210_MAX_DEVICES];
    int device_count;
    int api_indices[MAX_APIS];
    int api_count;
    int selected_api;
    int device_indices[RP1210_MAX_DEVICES];
    int available_devices;
    int selected_device;   /* zero means automatic (first matching device) */
    int selected_baud;
    char tool_sa[3];
    char ecm_sa[3];
    int focus;
    int open_combo;
    int scroll;
    int config_loaded;
    char saved_api[64];
    int saved_device;
    int saved_baud;
    int saved_tool;
    int saved_ecm;
    int busy;
    int modal;
    int result_upload;
    int result_ok;
    char modal_title[96];
    char modal_text[1600];
    char status[STATUS_CAP];
    struct ct_transfer_request request;
};

static struct app_state g;
static Font g_ui_font;
static int g_custom_font;
static char g_font_path[PATH_CAP];

/* Prefer the true mono Nerd Font variant; both carry patched icon glyphs. */
static const char *g_font_names[] = {
    "JetBrainsMonoNerdFontMono-Regular.ttf",
    "JetBrainsMonoNerdFont-Regular.ttf",
    "JetBrainsMonoNerdFontPropo-Regular.ttf"
};

#define FONT_SPACING 0.5f


static const int baud_values[] = { 0, 125000, 250000, 500000, 1000000 };
static const char *baud_names[] = { "Auto", "125000", "250000", "500000", "1000000" };

static Rectangle
rect(float x, float y, float w, float h)
{
    Rectangle r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

static void
copy_text(char *dst, const char *src, size_t cap)
{
    size_t n;
    if (cap == 0U)
        return;
    if (src == NULL)
        src = "";
    n = strlen(src);
    if (n >= cap)
        n = cap - 1U;
    if (n > 0U)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static int
clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}


/*
 * raylib's default DrawText/MeasureText always use its bitmap font.
 * All text in this frontend instead goes through DrawTextEx/MeasureTextEx
 * so layout and display use exactly the same JetBrains Mono font.
 */
static void
ui_draw_text(const char *text, int x, int y, int size, Color tint)
{
    Vector2 position;
    position.x = (float)x;
    position.y = (float)y;
    DrawTextEx(g_ui_font, text, position, (float)size, FONT_SPACING, tint);
}

static int
ui_measure_text(const char *text, int size)
{
    Vector2 dimensions;
    dimensions = MeasureTextEx(g_ui_font, text, (float)size, FONT_SPACING);
    return (int)(dimensions.x + 0.5f);
}

static int
try_load_font(const char *path)
{
    Font candidate;
    int codepoints[98];
    int i;
    if (path == NULL || path[0] == '\0' || !FileExists(path))
        return 0;
    for (i = 0; i < 95; ++i)
        codepoints[i] = 32 + i;
    /* Nerd Font / Font Awesome glyphs: download, upload, refresh. */
    codepoints[95] = 0xF019;
    codepoints[96] = 0xF093;
    codepoints[97] = 0xF021;
    candidate = LoadFontEx(path, 40, codepoints, 98);
    if (!IsFontValid(candidate) || candidate.texture.id == 0)
        return 0;
    g_ui_font = candidate;
    g_custom_font = 1;
    copy_text(g_font_path, path, sizeof(g_font_path));
    SetTextureFilter(g_ui_font.texture, TEXTURE_FILTER_BILINEAR);
    return 1;
}

static int
try_font_directory(const char *directory, const char *subdirectory)
{
    char root[PATH_CAP];
    char candidate[PATH_CAP];
    size_t length;
    size_t extra;
    int i;
    if (directory == NULL || directory[0] == '\0')
        return 0;
    extra = (subdirectory != NULL) ? strlen(subdirectory) : 0U;
    length = strlen(directory);
    if (length + extra + 3U >= sizeof(root))
        return 0;
    copy_text(root, directory, sizeof(root));
    if (length > 0U && root[length - 1U] != '\\' &&
        root[length - 1U] != '/')
        strcat(root, "\\");
    if (subdirectory != NULL)
        strcat(root, subdirectory);
    for (i = 0; i < (int)(sizeof(g_font_names) / sizeof(g_font_names[0])); ++i) {
        if (strlen(root) + strlen(g_font_names[i]) >= sizeof(candidate))
            continue;
        copy_text(candidate, root, sizeof(candidate));
        strcat(candidate, g_font_names[i]);
        if (try_load_font(candidate))
            return 1;
    }
    return 0;
}

static void
load_ui_font(void)
{
    const char *environment_font;
    char config_font[PATH_CAP];
    g_ui_font = GetFontDefault();
    g_custom_font = 0;
    g_font_path[0] = '\0';

    environment_font = getenv("CALTRANSFER_FONT");
    if (try_load_font(environment_font))
        return;
    config_font[0] = '\0';
    if (g.config_loaded) {
        ct_config_get_string("ui", "font_path", "", config_font,
                             sizeof(config_font));
        if (try_load_font(config_font))
            return;
    }
    if (try_font_directory(GetApplicationDirectory(), "fonts\\") ||
        try_font_directory(GetApplicationDirectory(), "") ||
        try_font_directory(getenv("LOCALAPPDATA"), "Microsoft\\Windows\\Fonts\\") ||
        try_font_directory(getenv("WINDIR"), "Fonts\\"))
        return;
    /* If not installed, keep rendering with raylib's bundled font. */
}

static const char *
current_api(void)
{
    if (g.selected_api < 0 || g.selected_api >= g.api_count)
        return NULL;
    return g.devices[g.api_indices[g.selected_api]].api;
}

static int
current_device_id(void)
{
    if (g.selected_device > 0 && g.selected_device <= g.available_devices)
        return g.devices[g.device_indices[g.selected_device - 1]].device_id;
    if (g.available_devices > 0)
        return g.devices[g.device_indices[0]].device_id;
    return -1;
}

static int
current_baud(void)
{
    return baud_values[clamp_int(g.selected_baud, 0, 4)];
}

static int
parse_sa(const char *s, unsigned char *value)
{
    unsigned long result;
    int i;
    int n;
    n = (int)strlen(s);
    if (n < 1 || n > 2)
        return 0;
    for (i = 0; i < n; ++i) {
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    }
    result = strtoul(s, NULL, 16);
    if (result > 255UL)
        return 0;
    *value = (unsigned char)result;
    return 1;
}

static void
open_message(int kind, const char *title, const char *message)
{
    g.modal = kind;
    copy_text(g.modal_title, title, sizeof(g.modal_title));
    copy_text(g.modal_text, message, sizeof(g.modal_text));
}

static void
read_config(void)
{
    long v;
    char error[STATUS_CAP];
    g.saved_api[0] = '\0';
    g.saved_device = -1;
    g.saved_baud = 250000;
    g.saved_tool = 0xFA;
    g.saved_ecm = 0;
    g.config_loaded = ct_config_load_default() == 0;
    if (!g.config_loaded) {
        error[0] = '\0';
        ct_config_get_last_error(error, sizeof(error));
        if (error[0] != '\0')
            copy_text(g.status, error, sizeof(g.status));
    } else {
        ct_config_get_string("adapter", "api", "", g.saved_api,
                             sizeof(g.saved_api));
        v = -1;
        ct_config_get_int("adapter", "device", -1, &v);
        g.saved_device = (int)v;
        v = 250000;
        ct_config_get_int("adapter", "baud", 250000, &v);
        g.saved_baud = (int)v;
        v = 0xFA;
        ct_config_get_int("j1939", "tool_sa", 0xFA, &v);
        if (v >= 0 && v <= 255)
            g.saved_tool = (int)v;
        v = 0;
        ct_config_get_int("j1939", "ecm_sa", 0, &v);
        if (v >= 0 && v <= 255)
            g.saved_ecm = (int)v;
    }
    sprintf(g.tool_sa, "%02X", g.saved_tool);
    sprintf(g.ecm_sa, "%02X", g.saved_ecm);
    g.selected_baud = 2;
    {
        int i;
        for (i = 0; i < 5; ++i) {
            if (baud_values[i] == g.saved_baud)
                g.selected_baud = i;
        }
    }
}

static void
save_config(void)
{
    unsigned char sa_tool;
    unsigned char sa_ecm;
    const char *api;
    char raw[16];
    int rc;
    if (!g.config_loaded)
        return;
    api = current_api();
    if (api == NULL || !parse_sa(g.tool_sa, &sa_tool) ||
        !parse_sa(g.ecm_sa, &sa_ecm))
        return;
    rc = ct_config_set_string("adapter", "api", api);
    if (rc == 0)
        rc = ct_config_set_int("adapter", "device",
                               g.selected_device == 0 ? -1 : current_device_id());
    if (rc == 0)
        rc = ct_config_set_int("adapter", "baud", current_baud());
    if (rc == 0) {
        sprintf(raw, "0x%02X", (unsigned int)sa_tool);
        rc = ct_config_set_raw("j1939", "tool_sa", raw);
    }
    if (rc == 0) {
        sprintf(raw, "0x%02X", (unsigned int)sa_ecm);
        rc = ct_config_set_raw("j1939", "ecm_sa", raw);
    }
    if (rc == 0)
        rc = ct_config_save();
    if (rc != 0) {
        ct_config_get_last_error(g.status, sizeof(g.status));
    }
}

static void
populate_devices(int wanted_id)
{
    const char *api;
    int i;
    g.available_devices = 0;
    g.selected_device = 0;
    api = current_api();
    if (api == NULL)
        return;
    for (i = 0; i < g.device_count; ++i) {
        if (ct_stricmp(g.devices[i].api, api) == 0)
            g.device_indices[g.available_devices++] = i;
    }
    if (wanted_id >= 0) {
        for (i = 0; i < g.available_devices; ++i) {
            if (g.devices[g.device_indices[i]].device_id == wanted_id) {
                g.selected_device = i + 1;
                break;
            }
        }
    }
}

static void
refresh_devices(int preserve)
{
    char old_api[64];
    int old_device;
    int count;
    int i;
    int j;
    int duplicate;
    int wanted_api;
    const char *api;
    char message[STATUS_CAP];

    api = preserve ? current_api() : NULL;
    copy_text(old_api, api != NULL ? api : g.saved_api, sizeof(old_api));
    old_device = preserve ?
        (g.selected_device == 0 ? -1 : current_device_id()) : g.saved_device;
    g.device_count = 0;
    g.api_count = 0;
    g.selected_api = -1;
    g.open_combo = COMBO_NONE;
    g.scroll = 0;
    count = rp1210_refresh();
    if (count < 0) {
        copy_text(g.status, "RP1210 adapter discovery failed.", sizeof(g.status));
        populate_devices(-1);
        return;
    }
    if (count > RP1210_MAX_DEVICES)
        count = RP1210_MAX_DEVICES;
    for (i = 0; i < count; ++i) {
        if (rp1210_get(i, &g.devices[g.device_count]))
            ++g.device_count;
    }
    wanted_api = -1;
    for (i = 0; i < g.device_count; ++i) {
        duplicate = 0;
        for (j = 0; j < g.api_count; ++j) {
            if (ct_stricmp(g.devices[i].api,
                           g.devices[g.api_indices[j]].api) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            g.api_indices[g.api_count] = i;
            if (ct_stricmp(old_api, g.devices[i].api) == 0)
                wanted_api = g.api_count;
            ++g.api_count;
        }
    }
    if (wanted_api < 0 && g.api_count > 0)
        wanted_api = 0;
    g.selected_api = wanted_api;
    populate_devices(old_device);
    if (g.device_count == 0)
        copy_text(g.status, "No RP1210 devices detected. Check drivers and adapter.",
                  sizeof(g.status));
    else {
        sprintf(message, "%d RP1210 device(s) detected.", g.device_count);
        copy_text(g.status, message, sizeof(g.status));
    }
}

static void
start_worker(void)
{
    g.busy = 1;
    g.modal = MODAL_NONE;
    g.open_combo = COMBO_NONE;
    if (ct_native_begin(&g.request) != 0) {
        g.busy = 0;
        open_message(MODAL_WARNING, "Transfer failed",
                     "Could not create the native transfer worker.");
    }
}

static void
prepare_transfer(int upload)
{
    const char *api;
    unsigned char tool_sa;
    unsigned char ecm_sa;
    if (g.busy)
        return;
    api = current_api();
    if (api == NULL || current_device_id() < 0) {
        open_message(MODAL_WARNING, "Adapter required",
                     "Select an available RP1210 vendor and device.");
        return;
    }
    if (!parse_sa(g.tool_sa, &tool_sa) || !parse_sa(g.ecm_sa, &ecm_sa)) {
        open_message(MODAL_WARNING, "Invalid source address",
                     "Tool SA and ECM SA must be one or two hexadecimal digits (00 to FF).");
        return;
    }
    memset(&g.request, 0, sizeof(g.request));
    copy_text(g.request.api, api, sizeof(g.request.api));
    g.request.device_id = current_device_id();
    g.request.baud = current_baud();
    g.request.tool_sa = tool_sa;
    g.request.ecm_sa = ecm_sa;
    g.request.upload = upload;
    if (!ct_native_choose_path(upload, g.request.path,
                               (unsigned long)sizeof(g.request.path),
                               GetWindowHandle()))
        return;
    save_config();
    if (upload) {
        open_message(MODAL_CONFIRM, "Confirm ECM programming",
                     "This will WRITE to the ECM. Confirm that the XCAL is compatible, "
                     "power is stable and the RP1210 connection will not be interrupted. "
                     "The native uploader checks XCAL CRC before opening the adapter.");
    } else
        start_worker();
}

static void
poll_worker(void)
{
    struct ct_transfer_status snapshot;
    char message[1600];
    if (!g.busy)
        return;
    ct_native_snapshot(&snapshot);
    if (!snapshot.done)
        return;
    ct_native_join();
    g.busy = 0;
    g.result_upload = g.request.upload;
    g.result_ok = snapshot.result == 0;
    if (g.result_ok) {
        copy_text(g.status,
                  g.request.upload ? "Calibration upload completed." :
                                     "ECM calibration saved and CRC verified.",
                  sizeof(g.status));
        sprintf(message, "%s\n\n%.1023s",
                g.request.upload ? "Calibration upload completed." :
                                   "Calibration saved and native CRC verified.",
                g.request.path);
        open_message(MODAL_RESULT, "Transfer completed", message);
    } else {
        if (snapshot.error[0] == '\0')
            copy_text(snapshot.error, "Unknown native transfer error.",
                      sizeof(snapshot.error));
        copy_text(g.status, snapshot.error, sizeof(g.status));
        sprintf(message, "%.511s\n\nNative return code: %d",
                snapshot.error, snapshot.result);
        open_message(MODAL_WARNING, "Transfer failed", message);
    }
}

static Color
paint(unsigned int hex)
{
    return GetColor(hex);
}

static int
hit(Rectangle r)
{
    return CheckCollisionPointRec(GetMousePosition(), r);
}

static void
draw_trimmed(const char *s, int x, int y, int max_width, int size, Color color)
{
    char short_text[512];
    size_t n;
    copy_text(short_text, s, sizeof(short_text));
    n = strlen(short_text);
    while (n > 0U && ui_measure_text(short_text, size) > max_width) {
        --n;
        short_text[n] = '\0';
    }
    ui_draw_text(short_text, x, y, size, color);
}

static void
draw_wrapped(const char *text, int x, int y, int max_width,
             int size, int max_lines, Color color)
{
    char line[256];
    char word[256];
    int line_len;
    int word_len;
    int lines;
    const char *p;
    line[0] = '\0';
    line_len = 0;
    word_len = 0;
    lines = 0;
    p = text;
    for (;;) {
        int newline;
        int whitespace;
        char ch;
        ch = *p;
        newline = ch == '\n' || ch == '\r';
        whitespace = ch == ' ' || ch == '\t';
        if (whitespace || newline || ch == '\0') {
            char candidate[512];
            word[word_len] = '\0';
            if (word_len > 0) {
                if (line_len == 0)
                    copy_text(candidate, word, sizeof(candidate));
                else
                    sprintf(candidate, "%.240s %.240s", line, word);
                if (line_len > 0 && ui_measure_text(candidate, size) > max_width) {
                    ui_draw_text(line, x, y + lines * (size + 7), size, color);
                    ++lines;
                    if (lines >= max_lines)
                        return;
                    copy_text(line, word, sizeof(line));
                } else
                    copy_text(line, candidate, sizeof(line));
                line_len = (int)strlen(line);
                word_len = 0;
            }
            if (newline || ch == '\0') {
                if (line_len > 0 || newline) {
                    ui_draw_text(line, x, y + lines * (size + 7), size, color);
                    ++lines;
                    if (lines >= max_lines)
                        return;
                    line[0] = '\0';
                    line_len = 0;
                }
            }
            if (ch == '\0')
                return;
        } else if (word_len < 250) {
            word[word_len++] = ch;
        }
        ++p;
    }
}

static void
box(Rectangle r, unsigned int fill, unsigned int outline)
{
    DrawRectangleRounded(r, 0.10f, 8, paint(fill));
    DrawRectangleRoundedLinesEx(r, 0.10f, 8, 1.0f, paint(outline));
}

static int
button(Rectangle r, const char *title, int active, int accent)
{
    int hovered;
    int size;
    Color background;
    hovered = active && hit(r);
    background = active ?
        paint(accent ? (hovered ? 0x4E94EFFF : 0x3878D8FF) :
                       (hovered ? 0x293951FF : 0x202D41FF)) :
        paint(0x1B2332FF);
    DrawRectangleRounded(r, 0.17f, 8, background);
    DrawRectangleRoundedLinesEx(r, 0.17f, 8, 1.0f,
                                 paint(active ? 0x59718FFF : 0x343E4DFF));
    size = 19;
    ui_draw_text(title, (int)(r.x + (r.width - ui_measure_text(title, size)) / 2),
             (int)(r.y + (r.height - size) / 2), size,
             paint(active ? 0xF1F6FFFF : 0x79869AFF));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static Rectangle
combo_rect(int id)
{
    if (id == COMBO_API)
        return rect(405, 178, 520, 43);
    if (id == COMBO_DEVICE)
        return rect(405, 245, 520, 43);
    return rect(735, 326, 190, 43);
}

static int
combo_count(int id)
{
    if (id == COMBO_API)
        return g.api_count;
    if (id == COMBO_DEVICE)
        return current_api() != NULL ? g.available_devices + 1 : 0;
    return 5;
}

static int
combo_selected(int id)
{
    if (id == COMBO_API)
        return g.selected_api;
    if (id == COMBO_DEVICE)
        return g.selected_device;
    return g.selected_baud;
}

static void
combo_label(int id, int idx, char *label, size_t cap)
{
    int device_idx;
    const RP1210_DEVICE *device;
    if (id == COMBO_BAUD) {
        copy_text(label, baud_names[clamp_int(idx, 0, 4)], cap);
        return;
    }
    if (id == COMBO_API) {
        if (idx < 0 || idx >= g.api_count) {
            copy_text(label, "No RP1210 APIs detected", cap);
            return;
        }
        device = &g.devices[g.api_indices[idx]];
        if (ct_stricmp(device->vendor, device->api) == 0)
            copy_text(label, device->api, cap);
        else
            sprintf(label, "%.127s (%.63s)", device->vendor, device->api);
        return;
    }
    if (idx == 0) {
        copy_text(label, "Auto - first device for this API", cap);
        return;
    }
    if (idx < 1 || idx > g.available_devices) {
        copy_text(label, "No devices detected", cap);
        return;
    }
    device_idx = g.device_indices[idx - 1];
    device = &g.devices[device_idx];
    sprintf(label, "%d - %.255s", device->device_id, device->description);
}

static void
draw_combo(int id)
{
    Rectangle r;
    char label[400];
    int enabled;
    int hovered;
    r = combo_rect(id);
    enabled = !g.busy && g.modal == MODAL_NONE && combo_count(id) > 0;
    hovered = enabled && hit(r);
    box(r, hovered ? 0x25344BFF : 0x182338FF,
        g.open_combo == id ? 0x61A6FFFF : 0x3B4B64FF);
    combo_label(id, combo_selected(id), label, sizeof(label));
    draw_trimmed(label, (int)r.x + 16, (int)r.y + 12,
                 (int)r.width - 55, 18,
                 paint(enabled ? 0xE7EFFAFF : 0x8490A4FF));
    ui_draw_text("v", (int)(r.x + r.width - 29), (int)r.y + 12, 19,
             paint(0x8BA8CBFF));
    if (hovered && g.open_combo == COMBO_NONE &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g.open_combo = id;
        g.scroll = 0;
        g.focus = FOCUS_NONE;
    }
}

static void
draw_dropdown(void)
{
    Rectangle owner;
    Rectangle menu;
    Rectangle item;
    int id;
    int count;
    int shown;
    int row;
    int idx;
    int wheel;
    char label[400];
    if (g.open_combo == COMBO_NONE || g.modal != MODAL_NONE || g.busy)
        return;
    id = g.open_combo;
    count = combo_count(id);
    if (count <= 0) {
        g.open_combo = COMBO_NONE;
        return;
    }
    owner = combo_rect(id);
    shown = count < 8 ? count : 8;
    menu = rect(owner.x, owner.y + owner.height + 3, owner.width,
                (float)(shown * 37 + 8));
    /* Baud selector is lower: open upwards to keep all options visible. */
    if (menu.y + menu.height > HEIGHT - 20)
        menu.y = owner.y - menu.height - 3;
    wheel = (int)GetMouseWheelMove();
    if (wheel != 0 && hit(menu))
        g.scroll = clamp_int(g.scroll - wheel, 0, count - shown);
    box(menu, 0x172438FF, 0x537295FF);
    for (row = 0; row < shown; ++row) {
        idx = row + g.scroll;
        item = rect(menu.x + 4, menu.y + 4 + row * 37,
                    menu.width - 8, 36);
        if (hit(item))
            DrawRectangleRec(item, paint(0x314A70FF));
        else if (idx == combo_selected(id))
            DrawRectangleRec(item, paint(0x243652FF));
        combo_label(id, idx, label, sizeof(label));
        draw_trimmed(label, (int)item.x + 12, (int)item.y + 9,
                     (int)item.width - 23, 17, paint(0xE5ECF5FF));
        if (hit(item) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (id == COMBO_API) {
                if (g.selected_api != idx) {
                    g.selected_api = idx;
                    populate_devices(-1);
                }
            } else if (id == COMBO_DEVICE)
                g.selected_device = idx;
            else
                g.selected_baud = idx;
            g.open_combo = COMBO_NONE;
        }
    }
    if (count > shown)
        ui_draw_text("Scroll for more", (int)menu.x + (int)menu.width - 155,
                 (int)menu.y + (int)menu.height + 3, 12, paint(0x8398B6FF));
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !hit(menu) && !hit(owner))
        g.open_combo = COMBO_NONE;
}

static void
edit_sa(Rectangle r, char *value, int focus_id)
{
    int active;
    int ch;
    size_t n;
    int key;
    active = !g.busy && g.modal == MODAL_NONE;
    if (active && g.open_combo == COMBO_NONE &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit(r))
        g.focus = focus_id;
    box(r, g.focus == focus_id ? 0x253651FF : 0x182338FF,
        g.focus == focus_id ? 0x61A6FFFF : 0x3B4B64FF);
    ui_draw_text(value, (int)r.x + 17, (int)r.y + 12, 20, paint(0xE7EFFAFF));
    if (g.focus == focus_id && active && g.open_combo == COMBO_NONE) {
        if (IsKeyPressed(KEY_BACKSPACE)) {
            n = strlen(value);
            if (n != 0U)
                value[n - 1U] = '\0';
        }
        for (;;) {
            ch = GetCharPressed();
            if (ch == 0)
                break;
            key = toupper((unsigned char)ch);
            n = strlen(value);
            if (n < 2U && isxdigit((unsigned char)key)) {
                value[n] = (char)key;
                value[n + 1U] = '\0';
            }
        }
        DrawRectangle((int)r.x + 18 + ui_measure_text(value, 20),
                      (int)r.y + 11, 2, 23, paint(0x7EB3FFFF));
    }
}

static void
draw_modal(void)
{
    Rectangle panel;
    int yes;
    int no;
    if (g.modal == MODAL_NONE)
        return;
    DrawRectangle(0, 0, WIDTH, HEIGHT, paint(0x040913D9));
    panel = rect(175, 206, 650, 340);
    box(panel, 0x1D2A3FFF, 0x55708FFF);
    ui_draw_text(g.modal_title, 207, 237, 26, paint(0xF6F9FFFF));
    DrawRectangle(207, 280, 584, 1, paint(0x44546BFF));
    draw_wrapped(g.modal_text, 207, 304, 575, 17, 6,
                 paint(0xCFDBEAFF));
    if (g.modal == MODAL_CONFIRM) {
        draw_trimmed(g.request.path, 207, 452, 576, 16,
                     paint(0x83B6FFFF));
        no = button(rect(490, 487, 135, 43), "Cancel", 1, 0);
        yes = button(rect(639, 487, 152, 43), "Program ECM", 1, 1);
        if (no)
            g.modal = MODAL_NONE;
        if (yes)
            start_worker();
    } else {
        if (button(rect(652, 487, 139, 43), "Close", 1, 1))
            g.modal = MODAL_NONE;
    }
}

static void
draw_screen(void)
{
    struct ct_transfer_status snapshot;
    int interactive;
    int allow_actions;
    char info[160];
    int progress;
    BeginDrawing();
    ClearBackground(paint(0x0B1322FF));
    DrawRectangle(0, 0, WIDTH, 92, paint(0x121F32FF));
    DrawRectangle(0, 90, WIDTH, 2, paint(0x33557BFF));
    ui_draw_text("CALIBRATION TRANSFER", 43, 27, 28, paint(0xF1F6FFFF));
    ui_draw_text("RAYLIB  /  NATIVE RP1210  /  J1939", 45, 64, 13,
             paint(0x80A6D1FF));
    ui_draw_text(g_custom_font ? "FONT  JetBrains Mono Nerd" :
                                  "FONT  default (Nerd Font not found)",
                 695, 64, 13, paint(0x80A6D1FF));

    box(rect(33, 117, 934, 345), 0x152136FF, 0x2F4159FF);
    ui_draw_text("ADAPTER & BUS", 58, 137, 20, paint(0xEAF2FFFF));
    ui_draw_text("RP1210 vendor", 61, 190, 18, paint(0xB7CBE5FF));
    draw_combo(COMBO_API);
    ui_draw_text("RP1210 device", 61, 257, 18, paint(0xB7CBE5FF));
    draw_combo(COMBO_DEVICE);
    ui_draw_text("Protocol", 61, 338, 18, paint(0xB7CBE5FF));
    box(rect(165, 326, 198, 43), 0x1B293FFF, 0x394960FF);
    ui_draw_text("J1939", 185, 338, 18, paint(0xDFE9F5FF));
    ui_draw_text("Baud", 668, 338, 18, paint(0xB7CBE5FF));
    draw_combo(COMBO_BAUD);

    ui_draw_text("Tool SA", 61, 405, 18, paint(0xB7CBE5FF));
    edit_sa(rect(167, 393, 100, 43), g.tool_sa, FOCUS_TOOL);
    ui_draw_text("hex", 278, 405, 15, paint(0x7186A5FF));
    ui_draw_text("ECM SA", 361, 405, 18, paint(0xB7CBE5FF));
    edit_sa(rect(466, 393, 100, 43), g.ecm_sa, FOCUS_ECM);
    ui_draw_text("hex", 579, 405, 15, paint(0x7186A5FF));
    interactive = !g.busy && g.open_combo == COMBO_NONE &&
                  g.modal == MODAL_NONE;
    if (button(rect(769, 393, 156, 43),
               g_custom_font ? "\xEF\x80\xA1  Refresh" : "Refresh",
               interactive, 0)) {
        refresh_devices(1);
        save_config();
    }

    box(rect(33, 481, 934, 119), 0x152136FF, 0x2F4159FF);
    ui_draw_text("CALIBRATION ACTIONS", 58, 500, 20,
             paint(0xEAF2FFFF));
    allow_actions = interactive && g.available_devices > 0;
    if (button(rect(59, 540, 426, 46),
               g_custom_font ? "\xEF\x80\x99  Pull ECM CAL > XCAL" :
                               "Pull ECM CAL  >  XCAL",
               allow_actions, 1))
        prepare_transfer(0);
    if (button(rect(507, 540, 426, 46),
               g_custom_font ? "\xEF\x82\x93  Upload XCAL > ECM" :
                               "Upload XCAL  >  ECM",
               allow_actions, 0))
        prepare_transfer(1);

    box(rect(33, 619, 934, 111), 0x152136FF, 0x2F4159FF);
    ui_draw_text("TRANSFER STATUS", 58, 637, 18, paint(0xB8D0F0FF));
    ct_native_snapshot(&snapshot);
    progress = g.busy ? snapshot.percent : (g.result_ok ? 100 : 0);
    progress = clamp_int(progress, 0, 100);
    DrawRectangleRounded(rect(58, 676, 870, 14), 0.4f, 8,
                         paint(0x23354BFF));
    if (progress > 0)
        DrawRectangleRounded(rect(58, 676, 870.0f * progress / 100.0f, 14),
                             0.4f, 8, paint(0x428CDBFF));
    sprintf(info, "%d%%", progress);
    ui_draw_text(info, 877, 637, 20, paint(0xE8F4FFFF));
    draw_trimmed(g.busy ? snapshot.status : g.status,
                 59, 699, 871, 16, paint(0xB9CBDEFF));
    if (g.busy)
        ui_draw_text("TRANSFER RUNNING - DO NOT DISCONNECT OR POWER OFF",
                 368, 640, 13, paint(0xF8C778FF));
    draw_dropdown();
    draw_modal();
    EndDrawing();
}

int
ct_raylib_main(void)
{
    int should_close;
    memset(&g, 0, sizeof(g));
    g.selected_api = -1;
    g.selected_baud = 2;
    copy_text(g.status, "Ready.", sizeof(g.status));
    ct_native_init();
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WIDTH, HEIGHT, "ECM Calibration Transfer - raylib");
    if (!IsWindowReady()) {
        ct_native_shutdown();
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    read_config();
    load_ui_font();
    refresh_devices(0);
    should_close = 0;
    while (!should_close) {
        poll_worker();
        if (WindowShouldClose()) {
            if (!g.busy)
                should_close = 1;
            else
                copy_text(g.status,
                          "Transfer active: closing deferred until worker finishes.",
                          sizeof(g.status));
        }
        if (!should_close)
            draw_screen();
    }
    save_config();
    if (g.config_loaded)
        ct_config_close();
    if (g_custom_font)
        UnloadFont(g_ui_font);
    CloseWindow();
    ct_native_shutdown();
    return 0;
}
