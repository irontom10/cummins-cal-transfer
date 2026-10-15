/* C89 diagnostic logger; no OS, GUI, RP1210 or j_string dependencies.
 * The caller must serialize transfers and logging configuration.
 * Event/error logs are enabled by default, full CAN traffic is opt-in. */
#include "ct_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CT_LOG_PATH_SIZE 1024

static char g_event_path[CT_LOG_PATH_SIZE] = "caltransfer.log";
static char g_can_path[CT_LOG_PATH_SIZE] = "caltransfer-can.log";
static FILE *g_event_file = NULL;
static FILE *g_can_file = NULL;
static int g_trace_can = -1;
static int g_last_percent = -1;
static char g_last_message[256];

static void log_copy(char *dst, const char *src, size_t cap)
{
    size_t i;
    if (dst == NULL || cap == 0U) return;
    if (src == NULL) src = "";
    for (i = 0U; i < cap - 1U && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static void stamp(FILE *f)
{
    char value[32];
    time_t now;
    struct tm *parts;
    now = time(NULL);
    parts = localtime(&now);
    if (parts == NULL ||
        strftime(value, sizeof(value), "%Y-%m-%d %H:%M:%S", parts) == 0U)
        log_copy(value, "0000-00-00 00:00:00", sizeof(value));
    fputs(value, f);
}

static void sanitized(FILE *f, const char *s)
{
    const unsigned char *p;
    if (s == NULL) return;
    for (p = (const unsigned char *)s; *p != 0U; ++p) {
        if (*p == '\n' || *p == '\r') fputc(' ', f);
        else if (*p < 0x20U && *p != '\t') fputc('?', f);
        else fputc(*p, f);
    }
}

static void hex_bytes(FILE *f, const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0U; i < n; ++i) {
        if (i != 0U) fputc(' ', f);
        fprintf(f, "%02X", (unsigned int)b[i]);
    }
}

void ct_log_configure(const char *events, const char *can, int trace_can)
{
    if (g_event_file != NULL || g_can_file != NULL) return;
    if (events != NULL && events[0] != '\0')
        log_copy(g_event_path, events, sizeof(g_event_path));
    if (can != NULL && can[0] != '\0')
        log_copy(g_can_path, can, sizeof(g_can_path));
    if (trace_can >= 0) g_trace_can = trace_can != 0;
}

int ct_log_begin(const char *operation, const char *api, int device,
                 int baud, unsigned int tool_sa, unsigned int ecm_sa)
{
    int trace;
    const char *env;
    if (g_event_file != NULL || g_can_file != NULL) return -1;
    env = getenv("CT_LOG_FILE");
    if (env != NULL && env[0] != '\0')
        log_copy(g_event_path, env, sizeof(g_event_path));
    env = getenv("CT_LOG_CAN_FILE");
    if (env != NULL && env[0] != '\0')
        log_copy(g_can_path, env, sizeof(g_can_path));
    trace = g_trace_can;
    if (trace < 0) {
        env = getenv("CT_LOG_CAN");
        trace = env != NULL && strcmp(env, "1") == 0;
    }
    g_last_percent = -1;
    g_last_message[0] = '\0';
    g_event_file = fopen(g_event_path, "a");
    if (g_event_file == NULL) return -1;
    if (trace) {
        g_can_file = fopen(g_can_path, "a");
        if (g_can_file == NULL)
            ct_log_event("ERROR", "LOGGER", "Unable to open CAN trace file");
        else {
            fputs("# CAN capture: frames may contain authentication/calibration data\n", g_can_file);
            fflush(g_can_file);
        }
    }
    fputc('\n', g_event_file);
    stamp(g_event_file);
    fputs(" | INFO | SESSION | START ", g_event_file);
    sanitized(g_event_file, operation);
    fputs(" API=", g_event_file);
    sanitized(g_event_file, api);
    fprintf(g_event_file, " device=%d baud=%d SA=%02X DA=%02X CAN=%s\n",
            device, baud, tool_sa & 255U, ecm_sa & 255U,
            g_can_file != NULL ? "on" : "off");
    fflush(g_event_file);
    return 0;
}

void ct_log_event(const char *level, const char *module, const char *message)
{
    if (g_event_file == NULL) return;
    stamp(g_event_file);
    fputs(" | ", g_event_file);
    sanitized(g_event_file, level);
    fputs(" | ", g_event_file);
    sanitized(g_event_file, module);
    fputs(" | ", g_event_file);
    sanitized(g_event_file, message);
    fputc('\n', g_event_file);
    if (level != NULL && strcmp(level, "ERROR") == 0) fflush(g_event_file);
}

void ct_log_progress(int percent, const char *message)
{
    char label[32];
    if (message == NULL) message = "";
    if (percent == g_last_percent && strcmp(message, g_last_message) == 0)
        return;
    g_last_percent = percent;
    log_copy(g_last_message, message, sizeof(g_last_message));
    sprintf(label, "PROGRESS %d%%", percent);
    ct_log_event("INFO", label, message);
}

void ct_log_can_frame(const char *direction, unsigned long tick_ms,
                      unsigned long pgn, unsigned int priority,
                      unsigned int sa, unsigned int da,
                      const unsigned char *payload, size_t length)
{
    if (g_can_file == NULL || (payload == NULL && length != 0U)) return;
    stamp(g_can_file);
    fprintf(g_can_file,
            " | %s | ms=%lu PGN=%06lX priority=%u SA=%02X DA=%02X len=%lu | ",
            direction, tick_ms, pgn, priority, sa, da, (unsigned long)length);
    hex_bytes(g_can_file, payload, length);
    fputc('\n', g_can_file);
    fflush(g_can_file);
}

void ct_log_can_raw(const char *direction, unsigned long tick_ms,
                    const unsigned char *buffer, size_t length)
{
    if (g_can_file == NULL || (buffer == NULL && length != 0U)) return;
    stamp(g_can_file);
    fprintf(g_can_file, " | %s | ms=%lu RP1210_bytes=%lu | ",
            direction, tick_ms, (unsigned long)length);
    hex_bytes(g_can_file, buffer, length);
    fputc('\n', g_can_file);
    fflush(g_can_file);
}

void ct_log_end(const char *operation, int rc, const char *error)
{
    if (g_event_file != NULL) {
        stamp(g_event_file);
        fprintf(g_event_file, " | %s | SESSION | END ",
                rc == 0 ? "INFO" : "ERROR");
        sanitized(g_event_file, operation);
        fprintf(g_event_file, " rc=%d ", rc);
        if (rc != 0) sanitized(g_event_file, error);
        fputc('\n', g_event_file);
        fclose(g_event_file);
        g_event_file = NULL;
    }
    if (g_can_file != NULL) {
        fflush(g_can_file);
        fclose(g_can_file);
        g_can_file = NULL;
    }
}
