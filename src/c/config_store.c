/*
 * config_store.c
 *
 * Standalone C89 TOML-backed configuration store for the WinForms UI.
 *
 * Deliberately isolated from all ECM/RP1210 code.  The file lives at:
 *
 *     %USERPROFILE%\\.config\\CalibrationTransfer\\config.toml
 *
 * This is a small, conservative TOML editor rather than an application-specific
 * settings struct.  It understands normal [table] headers and scalar values,
 * preserves unrecognized TOML/comments verbatim, and exposes raw get/set calls
 * so future UI features can use arrays/inline tables without coupling config
 * parsing to the protocol core.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>

#define CT_CONFIG_EXPORTS
#include "config_store.h"

#define CT_PATH_CAP 4096
#define CT_ERROR_CAP 512

struct ct_line_store {
    char **line;
    size_t count;
    size_t capacity;
};

static struct ct_line_store g_store;
static wchar_t g_path[CT_PATH_CAP];
static char g_last_error[CT_ERROR_CAP];
static int g_loaded = 0;

static void
ct_set_error(const char *text)
{
    size_t n;

    if (text == NULL)
        text = "";

    n = strlen(text);
    if (n >= sizeof(g_last_error))
        n = sizeof(g_last_error) - 1U;

    if (n != 0U)
        memcpy(g_last_error, text, n);
    g_last_error[n] = '\0';
}

static char *
ct_strdup(const char *text)
{
    size_t n;
    char *copy;

    if (text == NULL)
        text = "";

    n = strlen(text) + 1U;
    copy = (char *)malloc(n);
    if (copy != NULL)
        memcpy(copy, text, n);
    return copy;
}

static void
ct_store_clear(void)
{
    size_t i;

    for (i = 0U; i < g_store.count; ++i)
        free(g_store.line[i]);

    free(g_store.line);
    g_store.line = NULL;
    g_store.count = 0U;
    g_store.capacity = 0U;
}

static int
ct_store_reserve(size_t required)
{
    size_t capacity;
    char **new_lines;

    if (required <= g_store.capacity)
        return 1;

    capacity = g_store.capacity == 0U ? 32U : g_store.capacity;
    while (capacity < required) {
        if (capacity > ((size_t)-1) / 2U)
            return 0;
        capacity *= 2U;
    }

    new_lines = (char **)realloc(g_store.line, capacity * sizeof(char *));
    if (new_lines == NULL)
        return 0;

    g_store.line = new_lines;
    g_store.capacity = capacity;
    return 1;
}

static int
ct_store_insert(size_t index, const char *text)
{
    char *copy;

    if (index > g_store.count)
        return 0;

    if (!ct_store_reserve(g_store.count + 1U))
        return 0;

    copy = ct_strdup(text);
    if (copy == NULL)
        return 0;

    if (index < g_store.count) {
        memmove(&g_store.line[index + 1U],
                &g_store.line[index],
                (g_store.count - index) * sizeof(char *));
    }

    g_store.line[index] = copy;
    ++g_store.count;
    return 1;
}

static int
ct_store_append(const char *text)
{
    return ct_store_insert(g_store.count, text);
}

static int
ct_store_replace(size_t index, const char *text)
{
    char *copy;

    if (index >= g_store.count)
        return 0;

    copy = ct_strdup(text);
    if (copy == NULL)
        return 0;

    free(g_store.line[index]);
    g_store.line[index] = copy;
    return 1;
}

static char *
ct_read_line(FILE *fp)
{
    char *buffer;
    size_t length;
    size_t capacity;
    int ch;
    char *grown;

    capacity = 128U;
    length = 0U;
    buffer = (char *)malloc(capacity);
    if (buffer == NULL)
        return NULL;

    for (;;) {
        ch = fgetc(fp);
        if (ch == EOF)
            break;

        if (ch == '\n')
            break;

        if (ch == '\r') {
            ch = fgetc(fp);
            if (ch != '\n' && ch != EOF)
                ungetc(ch, fp);
            break;
        }

        if (length + 1U >= capacity) {
            if (capacity > ((size_t)-1) / 2U) {
                free(buffer);
                return NULL;
            }
            capacity *= 2U;
            grown = (char *)realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = grown;
        }

        buffer[length++] = (char)ch;
    }

    if (ch == EOF && length == 0U) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

static char *
ct_ltrim(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
        ++text;
    return text;
}

static void
ct_rtrim(char *text)
{
    size_t n;

    n = strlen(text);
    while (n != 0U && isspace((unsigned char)text[n - 1U])) {
        --n;
        text[n] = '\0';
    }
}

static int
ct_copy_text(char *out, int out_size, const char *text)
{
    size_t n;

    if (out == NULL || out_size <= 0)
        return 0;

    if (text == NULL)
        text = "";

    n = strlen(text);
    if (n >= (size_t)out_size)
        n = (size_t)out_size - 1U;

    if (n != 0U)
        memcpy(out, text, n);
    out[n] = '\0';
    return 1;
}

static int
ct_is_name_char(int ch)
{
    return isalnum((unsigned char)ch) ||
           ch == '_' ||
           ch == '-' ||
           ch == '.';
}

static int
ct_is_array_table_header(const char *line)
{
    const char *p;

    if (line == NULL)
        return 0;

    p = line;
    while (*p != '\0' && isspace((unsigned char)*p))
        ++p;

    return p[0] == '[' && p[1] == '[';
}

static int
ct_parse_table_header(const char *line, char *section, size_t section_size)
{
    const char *p;
    const char *end;
    size_t n;

    if (line == NULL || section == NULL || section_size == 0U)
        return 0;

    p = line;
    while (*p != '\0' && isspace((unsigned char)*p))
        ++p;

    if (*p != '[' || p[1] == '[')
        return 0;

    ++p;
    end = strchr(p, ']');
    if (end == NULL)
        return 0;

    while (end > p && isspace((unsigned char)end[-1]))
        --end;
    while (p < end && isspace((unsigned char)*p))
        ++p;

    if (p == end)
        return 0;

    n = (size_t)(end - p);
    if (n >= section_size)
        n = section_size - 1U;

    memcpy(section, p, n);
    section[n] = '\0';
    return 1;
}

static int
ct_extract_key(const char *line, char *key, size_t key_size)
{
    const char *p;
    const char *eq;
    const char *end;
    size_t n;
    size_t i;

    if (line == NULL || key == NULL || key_size == 0U)
        return 0;

    p = line;
    while (*p != '\0' && isspace((unsigned char)*p))
        ++p;

    if (*p == '\0' || *p == '#' || *p == '[')
        return 0;

    eq = strchr(p, '=');
    if (eq == NULL)
        return 0;

    end = eq;
    while (end > p && isspace((unsigned char)end[-1]))
        --end;

    if (end == p)
        return 0;

    n = (size_t)(end - p);
    if (n >= key_size)
        return 0;

    for (i = 0U; i < n; ++i) {
        if (!ct_is_name_char((unsigned char)p[i]))
            return 0;
    }

    memcpy(key, p, n);
    key[n] = '\0';
    return 1;
}

static const char *
ct_value_start(const char *line)
{
    const char *eq;

    if (line == NULL)
        return NULL;

    eq = strchr(line, '=');
    if (eq == NULL)
        return NULL;

    ++eq;
    while (*eq != '\0' && isspace((unsigned char)*eq))
        ++eq;
    return eq;
}

static size_t
ct_value_length_without_comment(const char *value)
{
    size_t i;
    size_t end;
    int quote;
    int escaped;
    int ch;

    if (value == NULL)
        return 0U;

    quote = 0;
    escaped = 0;
    end = strlen(value);

    for (i = 0U; value[i] != '\0'; ++i) {
        ch = (unsigned char)value[i];

        if (quote == '"') {
            if (escaped) {
                escaped = 0;
                continue;
            }
            if (ch == '\\') {
                escaped = 1;
                continue;
            }
            if (ch == '"')
                quote = 0;
            continue;
        }

        if (quote == '\'') {
            if (ch == '\'')
                quote = 0;
            continue;
        }

        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }

        if (ch == '#') {
            end = i;
            break;
        }
    }

    while (end != 0U && isspace((unsigned char)value[end - 1U]))
        --end;

    return end;
}

static int
ct_find_key(const char *section,
            const char *key,
            size_t *line_index,
            size_t *section_end)
{
    char current[256];
    char parsed_section[256];
    char parsed_key[256];
    size_t i;
    int in_section;
    int saw_section;

    if (section == NULL)
        section = "";

    current[0] = '\0';
    in_section = section[0] == '\0';
    saw_section = in_section;

    if (section_end != NULL)
        *section_end = g_store.count;

    for (i = 0U; i < g_store.count; ++i) {
        /*
         * An array-of-tables is still a TOML table boundary.  We preserve it
         * verbatim, but scalar section/key access deliberately does not treat
         * [[profiles]] as the same thing as [profiles].
         */
        if (ct_is_array_table_header(g_store.line[i])) {
            if (in_section && section_end != NULL)
                *section_end = i;
            current[0] = '\0';
            in_section = 0;
            continue;
        }

        if (ct_parse_table_header(g_store.line[i],
                                  parsed_section,
                                  sizeof(parsed_section))) {
            if (in_section && section_end != NULL)
                *section_end = i;

            ct_copy_text(current, (int)sizeof(current), parsed_section);
            in_section = strcmp(current, section) == 0;
            if (in_section)
                saw_section = 1;
            continue;
        }

        if (!in_section)
            continue;

        if (ct_extract_key(g_store.line[i], parsed_key, sizeof(parsed_key)) &&
            strcmp(parsed_key, key) == 0) {
            if (line_index != NULL)
                *line_index = i;
            return 1;
        }
    }

    if (in_section && section_end != NULL)
        *section_end = g_store.count;

    return saw_section ? -1 : 0;
}

static int
ct_get_raw_internal(const char *section,
                    const char *key,
                    char *out,
                    int out_size)
{
    size_t index;
    const char *value;
    size_t n;

    if (key == NULL || key[0] == '\0' || out == NULL || out_size <= 0)
        return 0;

    if (ct_find_key(section, key, &index, NULL) != 1)
        return 0;

    value = ct_value_start(g_store.line[index]);
    if (value == NULL)
        return 0;

    n = ct_value_length_without_comment(value);
    if (n >= (size_t)out_size)
        n = (size_t)out_size - 1U;

    if (n != 0U)
        memcpy(out, value, n);
    out[n] = '\0';
    return 1;
}

static int
ct_build_default_path(void)
{
    wchar_t home[CT_PATH_CAP];
    wchar_t config_dir[CT_PATH_CAP];
    DWORD n;
    int written;

    n = GetEnvironmentVariableW(L"USERPROFILE",
                                home,
                                (DWORD)(sizeof(home) / sizeof(home[0])));
    if (n == 0U || n >= (DWORD)(sizeof(home) / sizeof(home[0]))) {
        ct_set_error("USERPROFILE is unavailable; cannot resolve ~/.config.");
        return 0;
    }

    written = _snwprintf(config_dir,
                         sizeof(config_dir) / sizeof(config_dir[0]),
                         L"%ls\\.config",
                         home);
    if (written < 0 ||
        written >= (int)(sizeof(config_dir) / sizeof(config_dir[0]))) {
        ct_set_error("Configuration directory path is too long.");
        return 0;
    }

    if (!CreateDirectoryW(config_dir, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        ct_set_error("Unable to create ~/.config.");
        return 0;
    }

    written = _snwprintf(g_path,
                         sizeof(g_path) / sizeof(g_path[0]),
                         L"%ls\\CalibrationTransfer",
                         config_dir);
    if (written < 0 ||
        written >= (int)(sizeof(g_path) / sizeof(g_path[0]))) {
        ct_set_error("Configuration directory path is too long.");
        return 0;
    }

    if (!CreateDirectoryW(g_path, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        ct_set_error("Unable to create ~/.config/CalibrationTransfer.");
        return 0;
    }

    written = _snwprintf(g_path,
                         sizeof(g_path) / sizeof(g_path[0]),
                         L"%ls\\CalibrationTransfer\\config.toml",
                         config_dir);
    if (written < 0 ||
        written >= (int)(sizeof(g_path) / sizeof(g_path[0]))) {
        ct_set_error("Configuration file path is too long.");
        return 0;
    }

    return 1;
}

static int
ct_append_default_header(void)
{
    if (!ct_store_append("# CalibrationTransfer user configuration"))
        return 0;
    if (!ct_store_append("# ~/.config/CalibrationTransfer/config.toml"))
        return 0;
    return 1;
}

int CT_CONFIG_CALL
ct_config_load_default(void)
{
    FILE *fp;
    char *line;

    ct_store_clear();
    g_loaded = 0;
    g_path[0] = L'\0';
    ct_set_error("");

    if (!ct_build_default_path())
        return -1;

    fp = _wfopen(g_path, L"rb");
    if (fp == NULL) {
        if (!ct_append_default_header()) {
            ct_set_error("Out of memory while creating default configuration.");
            return -1;
        }
        g_loaded = 1;
        return 0;
    }

    for (;;) {
        line = ct_read_line(fp);
        if (line == NULL)
            break;

        if (!ct_store_append(line)) {
            free(line);
            fclose(fp);
            ct_store_clear();
            ct_set_error("Out of memory while loading configuration.");
            return -1;
        }
        free(line);
    }

    if (ferror(fp)) {
        fclose(fp);
        ct_store_clear();
        ct_set_error("Error while reading config.toml.");
        return -1;
    }

    fclose(fp);
    g_loaded = 1;
    return 0;
}

static int
ct_escape_toml_string(const char *value, char **out)
{
    size_t i;
    size_t need;
    size_t w;
    char *text;
    unsigned int ch;

    if (value == NULL)
        value = "";

    need = 3U;
    for (i = 0U; value[i] != '\0'; ++i) {
        ch = (unsigned char)value[i];
        if (ch == '"' || ch == '\\' || ch == '\n' ||
            ch == '\r' || ch == '\t')
            need += 2U;
        else if (ch < 0x20U)
            need += 6U;
        else
            ++need;
    }

    text = (char *)malloc(need);
    if (text == NULL)
        return 0;

    w = 0U;
    text[w++] = '"';

    for (i = 0U; value[i] != '\0'; ++i) {
        ch = (unsigned char)value[i];
        if (ch == '"' || ch == '\\') {
            text[w++] = '\\';
            text[w++] = (char)ch;
        } else if (ch == '\n') {
            text[w++] = '\\';
            text[w++] = 'n';
        } else if (ch == '\r') {
            text[w++] = '\\';
            text[w++] = 'r';
        } else if (ch == '\t') {
            text[w++] = '\\';
            text[w++] = 't';
        } else if (ch < 0x20U) {
            sprintf(text + w, "\\u%04X", ch);
            w += 6U;
        } else {
            text[w++] = (char)ch;
        }
    }

    text[w++] = '"';
    text[w] = '\0';
    *out = text;
    return 1;
}

static int
ct_unescape_toml_string(const char *raw, char *out, int out_size)
{
    size_t i;
    size_t w;
    size_t n;
    int quote;
    int ch;

    if (raw == NULL || out == NULL || out_size <= 0)
        return 0;

    n = strlen(raw);
    if (n < 2U)
        return 0;

    quote = (unsigned char)raw[0];
    if ((quote != '"' && quote != '\'') || raw[n - 1U] != quote)
        return 0;

    w = 0U;
    for (i = 1U; i + 1U < n; ++i) {
        ch = (unsigned char)raw[i];

        if (quote == '\'') {
            if (w + 1U >= (size_t)out_size)
                break;
            out[w++] = (char)ch;
            continue;
        }

        if (ch == '\\' && i + 2U < n) {
            ++i;
            ch = (unsigned char)raw[i];
            if (ch == 'n')
                ch = '\n';
            else if (ch == 'r')
                ch = '\r';
            else if (ch == 't')
                ch = '\t';
            else if (ch == '"' || ch == '\\') {
            } else {
                return 0;
            }
        }

        if (w + 1U >= (size_t)out_size)
            break;
        out[w++] = (char)ch;
    }

    out[w] = '\0';
    return 1;
}

static int
ct_valid_name(const char *name, int allow_empty)
{
    size_t i;

    if (name == NULL)
        return allow_empty;

    if (name[0] == '\0')
        return allow_empty;

    for (i = 0U; name[i] != '\0'; ++i) {
        if (!ct_is_name_char((unsigned char)name[i]))
            return 0;
    }
    return 1;
}

int CT_CONFIG_CALL
ct_config_get_raw(const char *section,
                  const char *key,
                  char *out,
                  int out_size)
{
    if (!g_loaded)
        return 0;
    return ct_get_raw_internal(section, key, out, out_size);
}

int CT_CONFIG_CALL
ct_config_get_string(const char *section,
                     const char *key,
                     const char *default_value,
                     char *out,
                     int out_size)
{
    char raw[4096];

    if (out == NULL || out_size <= 0)
        return 0;

    if (!g_loaded ||
        !ct_get_raw_internal(section, key, raw, sizeof(raw)) ||
        !ct_unescape_toml_string(raw, out, out_size)) {
        ct_copy_text(out, out_size, default_value == NULL ? "" : default_value);
        return 0;
    }

    return 1;
}

int CT_CONFIG_CALL
ct_config_get_int(const char *section,
                  const char *key,
                  long default_value,
                  long *out_value)
{
    char raw[256];
    char *end;
    long value;

    if (out_value == NULL)
        return 0;

    if (!g_loaded ||
        !ct_get_raw_internal(section, key, raw, sizeof(raw))) {
        *out_value = default_value;
        return 0;
    }

    value = strtol(raw, &end, 0);
    while (*end != '\0' && isspace((unsigned char)*end))
        ++end;

    if (end == raw || *end != '\0') {
        *out_value = default_value;
        return 0;
    }

    *out_value = value;
    return 1;
}

int CT_CONFIG_CALL
ct_config_get_bool(const char *section,
                   const char *key,
                   int default_value,
                   int *out_value)
{
    char raw[64];

    if (out_value == NULL)
        return 0;

    if (!g_loaded ||
        !ct_get_raw_internal(section, key, raw, sizeof(raw))) {
        *out_value = default_value ? 1 : 0;
        return 0;
    }

    if (_stricmp(raw, "true") == 0) {
        *out_value = 1;
        return 1;
    }
    if (_stricmp(raw, "false") == 0) {
        *out_value = 0;
        return 1;
    }

    *out_value = default_value ? 1 : 0;
    return 0;
}

static int
ct_set_raw_internal(const char *section,
                    const char *key,
                    const char *toml_value)
{
    size_t index;
    size_t section_end;
    int found;
    size_t need;
    char *line;
    char header[512];

    if (!g_loaded) {
        ct_set_error("Configuration has not been loaded.");
        return -1;
    }

    if (!ct_valid_name(section, 1) ||
        !ct_valid_name(key, 0) ||
        toml_value == NULL ||
        strchr(toml_value, '\r') != NULL ||
        strchr(toml_value, '\n') != NULL) {
        ct_set_error("Invalid TOML section, key, or value.");
        return -1;
    }

    need = strlen(key) + strlen(toml_value) + 4U;
    line = (char *)malloc(need);
    if (line == NULL) {
        ct_set_error("Out of memory while updating configuration.");
        return -1;
    }

    sprintf(line, "%s = %s", key, toml_value);

    found = ct_find_key(section, key, &index, &section_end);
    if (found == 1) {
        if (!ct_store_replace(index, line)) {
            free(line);
            ct_set_error("Out of memory while updating configuration.");
            return -1;
        }
        free(line);
        return 0;
    }

    if (found == -1) {
        if (!ct_store_insert(section_end, line)) {
            free(line);
            ct_set_error("Out of memory while updating configuration.");
            return -1;
        }
        free(line);
        return 0;
    }

    if (g_store.count != 0U &&
        g_store.line[g_store.count - 1U][0] != '\0') {
        if (!ct_store_append("")) {
            free(line);
            ct_set_error("Out of memory while updating configuration.");
            return -1;
        }
    }

    if (section != NULL && section[0] != '\0') {
        if (strlen(section) + 3U > sizeof(header)) {
            free(line);
            ct_set_error("TOML section name is too long.");
            return -1;
        }
        sprintf(header, "[%s]", section);
        if (!ct_store_append(header)) {
            free(line);
            ct_set_error("Out of memory while updating configuration.");
            return -1;
        }
    }

    if (!ct_store_append(line)) {
        free(line);
        ct_set_error("Out of memory while updating configuration.");
        return -1;
    }

    free(line);
    return 0;
}

int CT_CONFIG_CALL
ct_config_set_raw(const char *section,
                  const char *key,
                  const char *toml_value)
{
    return ct_set_raw_internal(section, key, toml_value);
}

int CT_CONFIG_CALL
ct_config_set_string(const char *section,
                     const char *key,
                     const char *value)
{
    char *escaped;
    int rc;

    escaped = NULL;
    if (!ct_escape_toml_string(value, &escaped)) {
        ct_set_error("Out of memory while escaping TOML string.");
        return -1;
    }

    rc = ct_set_raw_internal(section, key, escaped);
    free(escaped);
    return rc;
}

int CT_CONFIG_CALL
ct_config_set_int(const char *section,
                  const char *key,
                  long value)
{
    char raw[64];

    sprintf(raw, "%ld", value);
    return ct_set_raw_internal(section, key, raw);
}

int CT_CONFIG_CALL
ct_config_set_bool(const char *section,
                   const char *key,
                   int value)
{
    return ct_set_raw_internal(section,
                               key,
                               value ? "true" : "false");
}

int CT_CONFIG_CALL
ct_config_save(void)
{
    wchar_t temp_path[CT_PATH_CAP];
    FILE *fp;
    size_t i;
    int written;
    int ok;

    if (!g_loaded || g_path[0] == L'\0') {
        ct_set_error("Configuration has not been loaded.");
        return -1;
    }

    written = _snwprintf(temp_path,
                         sizeof(temp_path) / sizeof(temp_path[0]),
                         L"%ls.%lu.tmp",
                         g_path,
                         (unsigned long)GetCurrentProcessId());
    if (written < 0 ||
        written >= (int)(sizeof(temp_path) / sizeof(temp_path[0]))) {
        ct_set_error("Temporary configuration path is too long.");
        return -1;
    }

    fp = _wfopen(temp_path, L"wb");
    if (fp == NULL) {
        ct_set_error("Unable to open temporary config.toml for writing.");
        return -1;
    }

    ok = 1;
    for (i = 0U; i < g_store.count; ++i) {
        if (fputs(g_store.line[i], fp) == EOF ||
            fputs("\r\n", fp) == EOF) {
            ok = 0;
            break;
        }
    }

    if (fflush(fp) != 0)
        ok = 0;
    if (fclose(fp) != 0)
        ok = 0;

    if (!ok) {
        DeleteFileW(temp_path);
        ct_set_error("Failed while writing config.toml.");
        return -1;
    }

    if (!MoveFileExW(temp_path,
                     g_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_path);
        ct_set_error("Unable to atomically replace config.toml.");
        return -1;
    }

    ct_set_error("");
    return 0;
}

void CT_CONFIG_CALL
ct_config_close(void)
{
    ct_store_clear();
    g_loaded = 0;
    g_path[0] = L'\0';
    ct_set_error("");
}

int CT_CONFIG_CALL
ct_config_get_path(char *out, int out_size)
{
    int required;

    if (out == NULL || out_size <= 0 || g_path[0] == L'\0')
        return 0;

    required = WideCharToMultiByte(CP_UTF8,
                                   0,
                                   g_path,
                                   -1,
                                   out,
                                   out_size,
                                   NULL,
                                   NULL);
    if (required == 0) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

int CT_CONFIG_CALL
ct_config_get_last_error(char *out, int out_size)
{
    return ct_copy_text(out, out_size, g_last_error);
}
