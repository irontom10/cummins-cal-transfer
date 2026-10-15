#ifndef CT_LOGGER_H
#define CT_LOGGER_H
#include <stddef.h>

/* Synchronous C89 logger for the single calibration-transfer worker.
 * Full CAN captures are opt-in: they may include authentication/payload bytes. */
void ct_log_configure(const char *event_path, const char *can_path, int trace_can);
int ct_log_begin(const char *operation, const char *api, int device,
                 int baud, unsigned int tool_sa, unsigned int ecm_sa);
void ct_log_end(const char *operation, int rc, const char *error);
void ct_log_event(const char *severity, const char *component, const char *message);
void ct_log_progress(int percent, const char *message);
void ct_log_can_frame(const char *direction, unsigned long tick_ms,
                      unsigned long pgn, unsigned int priority,
                      unsigned int sa, unsigned int da,
                      const unsigned char *payload, size_t length);
void ct_log_can_raw(const char *direction, unsigned long tick_ms,
                    const unsigned char *buffer, size_t length);
#endif
