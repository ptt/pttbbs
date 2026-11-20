/*
 * A small test framework for bof: EXPECT*() record failures and go on;
 * bof_test_report() prints the result and returns the exit status.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#ifndef BOF_TEST_H
#define BOF_TEST_H

#include <stdio.h>
#include <string.h>

#include "bof.h"
#include "bof_storage.h"

/* The tests are of the files backend, even where pttbbs.conf names another
 * default backend (BOF_DEFAULT_STORAGE). */
__attribute__((constructor)) static void
bof_test_use_files(void)
{
    bof_storage_set(&bof_storage_files, NULL);
}

extern int bof_test_failures;

#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__,    \
                    #cond);                                                \
            bof_test_failures++;                                           \
        }                                                                  \
    } while (0)

#define EXPECT_INT(got, want)                                              \
    do {                                                                   \
        long long g_ = (long long)(got), w_ = (long long)(want);           \
        if (g_ != w_) {                                                    \
            fprintf(stderr, "%s:%d: %s = %lld, want %lld\n", __FILE__,     \
                    __LINE__, #got, g_, w_);                               \
            bof_test_failures++;                                           \
        }                                                                  \
    } while (0)

#define EXPECT_STR(got, want)                                              \
    do {                                                                   \
        const char *g_ = (got), *w_ = (want);                              \
        if (!g_ || strcmp(g_, w_) != 0) {                                  \
            fprintf(stderr, "%s:%d: %s = \"%s\", want \"%s\"\n", __FILE__, \
                    __LINE__, #got, g_ ? g_ : "(null)", w_);               \
            bof_test_failures++;                                           \
        }                                                                  \
    } while (0)

#define EXPECT_OK(call) EXPECT_STR(bof_error_name(call), "ok")

#define RUN(test)                                                          \
    do {                                                                   \
        int before_ = bof_test_failures;                                   \
        test();                                                            \
        printf("%s %s\n", bof_test_failures == before_ ? "PASS" : "FAIL",  \
               #test);                                                     \
    } while (0)

static inline int
bof_test_report(void)
{
    if (bof_test_failures) {
        printf("FAILED: %d failure(s)\n", bof_test_failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}

#endif /* BOF_TEST_H */
