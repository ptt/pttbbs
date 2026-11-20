/*
 * Tests of bof.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>

#include "bof_test.h"

int bof_test_failures;

static void
test_error_names(void)
{
    EXPECT_STR(bof_error_name(BOF_OK), "ok");
    EXPECT_STR(bof_error_name(BOF_ERR_COULDNT_FIND_POST), "couldnt_find_post");
    EXPECT_STR(bof_error_name(BOF_ERR_NOT_A_MODERATOR), "not_a_moderator");
    EXPECT_STR(bof_error_name(BOF_ERR_NOT_AN_ADMIN), "not_an_admin");
    EXPECT_STR(bof_error_name(BOF_ERR_MAX), "unknown");
    EXPECT_STR(bof_error_name((bof_error_t)-1), "unknown");
    for (int e = 0; e < BOF_ERR_MAX; e++)
        EXPECT(strcmp(bof_error_name((bof_error_t)e), "unknown") != 0 ||
               e == BOF_ERR_UNKNOWN);
}

int
main(void)
{
    RUN(test_error_names);
    return bof_test_report();
}
