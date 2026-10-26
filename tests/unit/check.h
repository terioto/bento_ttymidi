/* Minimal checks for the unit tests: count failures, print where they happened. */
#ifndef BENTO_TTYMIDI_CHECK_H
#define BENTO_TTYMIDI_CHECK_H

#include <stdio.h>

static int check_failures;
static int check_count;

#define CHECK(cond) do { \
        check_count++; \
        if (!(cond)) { \
            check_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static int check_report(const char *name)
{
    if (check_failures) {
        fprintf(stderr, "%s: %d of %d checks failed\n", name, check_failures, check_count);
        return 1;
    }
    printf("%s: %d checks passed\n", name, check_count);
    return 0;
}

#endif
