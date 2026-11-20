/*
 * A program outside the BBS links BoF through libbof_gateway.a alone (with
 * the common libraries), without the globals of the BBS of its own.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>

#include "bof_gateway.h"

int
main(int argc, char **argv)
{
    (void)argv;
    // Linked, not run: opening attaches the board cache of a running BBS.
    if (argc > 1 && bof_gateway_open() != BOF_OK)
        return 1;
    if (strcmp(bof_error_name(BOF_ERR_BBS_NOT_ALLOWED), "bbs_not_allowed"))
        return 1;
    printf("OK\n");
    return 0;
}
