#ifndef J_STRING_H
#define J_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * General-purpose, ISO C89 string helpers.
 *
 * Unlike standard strdup(), j_strdup() accepts NULL as an empty string.
 * The returned buffer is allocated with malloc() and must be freed by
 * the caller.
 */
char *j_strdup(const char *text);

/*
 * Copy a NUL-terminated string into out with room for the terminator.
 * NULL text is treated as "". Overlapping source and destination are
 * not supported. The result is always NUL-terminated when capacity > 0.
 * Returns 0 for an invalid/zero-capacity output, 1 otherwise; a copy
 * that was truncated still returns 1.
 */
int j_str_copy(char *out, size_t capacity, const char *text);

/*
 * Strip leading and trailing ASCII whitespace in place.
 * Whitespace is space, tab, carriage return, line feed, vertical tab,
 * or form feed. NULL is ignored. Interior whitespace is unchanged.
 */
void j_str_trim(char *text);

/*
 * Locale-independent, ASCII-only case-insensitive comparison.
 * NULL inputs are treated as empty strings. Non-ASCII bytes are
 * compared unchanged. Returns negative, zero, or positive like strcmp().
 */
int j_ascii_stricmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* J_STRING_H */
