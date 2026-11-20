/*
 * gateway.c - bof (Birds Of Feather) for programs other than the BBS (in
 * libbof_gateway.a only; the programs of the BBS set themselves up).
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include "bof_int.h"
#include "bof_gateway.h"

extern SHM_t *SHM;

bof_error_t
bof_gateway_open(void)
{
    if (chdir(BBSHOME) != 0)
        return BOF_ERR_UNKNOWN;
    // The board cache of the running BBS (not one made here).
    if (!SHM && !attach_check_SHM())
        return BOF_ERR_UNKNOWN;
    return BOF_OK;
}
