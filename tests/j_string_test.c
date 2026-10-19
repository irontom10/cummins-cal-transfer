#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "j_string.h"

static void
test_strdup(void)
{
    char *text;

    text = j_strdup("a test");
    assert(text != NULL);
    assert(strcmp(text, "a test") == 0);
    text[0] = 'A';
    assert(strcmp(text, "A test") == 0);
    free(text);

    text = j_strdup(NULL);
    assert(text != NULL);
    assert(strcmp(text, "") == 0);
    free(text);
}

static void
test_copy(void)
{
    char buffer[5];

    assert(j_str_copy(NULL, sizeof(buffer), "hi") == 0);
    assert(j_str_copy(buffer, 0U, "hi") == 0);
    assert(j_str_copy(buffer, sizeof(buffer), "hi") == 1);
    assert(strcmp(buffer, "hi") == 0);
    assert(j_str_copy(buffer, sizeof(buffer), "abcdef") == 1);
    assert(strcmp(buffer, "abcd") == 0);
    assert(j_str_copy(buffer, 1U, "something") == 1);
    assert(strcmp(buffer, "") == 0);
    assert(j_str_copy(buffer, sizeof(buffer), NULL) == 1);
    assert(strcmp(buffer, "") == 0);
}

static void
test_trim(void)
{
    char buffer[64];

    strcpy(buffer, " \t\r\n\v\fhello\t world \t\r\n\v\f");
    j_str_trim(buffer);
    assert(strcmp(buffer, "hello\t world") == 0);

    strcpy(buffer, "no change");
    j_str_trim(buffer);
    assert(strcmp(buffer, "no change") == 0);

    strcpy(buffer, " \t\r\n\v\f");
    j_str_trim(buffer);
    assert(strcmp(buffer, "") == 0);

    buffer[0] = '\0';
    j_str_trim(buffer);
    assert(strcmp(buffer, "") == 0);

    j_str_trim(NULL);
}

static void
test_ascii_comparison(void)
{
    assert(j_ascii_stricmp("TRUE", "true") == 0);
    assert(j_ascii_stricmp("FALSE", "false") == 0);
    assert(j_ascii_stricmp("True", "trues") < 0);
    assert(j_ascii_stricmp("ABC", "abb") > 0);
    assert(j_ascii_stricmp(NULL, "") == 0);
    assert(j_ascii_stricmp(NULL, "a") < 0);
    assert(j_ascii_stricmp("a", NULL) > 0);
    assert(j_ascii_stricmp("\xC0", "\xE0") < 0);
}

int
main(void)
{
    test_strdup();
    test_copy();
    test_trim();
    test_ascii_comparison();
    puts("j_string tests passed");
    return 0;
}
