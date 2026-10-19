#include "j_string.h"

#include <stdlib.h>
#include <string.h>

char *
j_strdup(const char *text)
{
    size_t n;
    char *copy;

    if (text == NULL)
        text = "";

    n = strlen(text);
    if (n == (size_t)-1)
        return NULL;

    copy = (char *)malloc(n + 1U);
    if (copy != NULL)
        memcpy(copy, text, n + 1U);
    return copy;
}

int
j_str_copy(char *out, size_t capacity, const char *text)
{
    size_t n;

    if (out == NULL || capacity == 0U)
        return 0;

    if (text == NULL)
        text = "";

    n = strlen(text);
    if (n >= capacity)
        n = capacity - 1U;

    if (n != 0U)
        memcpy(out, text, n);
    out[n] = '\0';
    return 1;
}

static unsigned int
j_ascii_tolower(unsigned int ch)
{
    if (ch >= 0x41U && ch <= 0x5AU)
        return ch + 0x20U;
    return ch;
}

int
j_ascii_stricmp(const char *a, const char *b)
{
    unsigned int ca;
    unsigned int cb;

    if (a == NULL)
        a = "";
    if (b == NULL)
        b = "";

    for (;;) {
        ca = j_ascii_tolower((unsigned char)*a);
        cb = j_ascii_tolower((unsigned char)*b);

        if (ca != cb || ca == 0U)
            return (int)ca - (int)cb;

        ++a;
        ++b;
    }
}
