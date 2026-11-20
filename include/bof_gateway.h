/*
 * bof_gateway.h - bof (Birds Of Feather) for programs other than the BBS,
 * e.g. a Lemmy API server in front of it.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Link bof/libbof_gateway.a (BoF with the globals of the BBS that the
 * programs of the BBS define themselves), common/bbs/libcmbbs.a,
 * common/sys/libcmsys.a and common/osdep/libosdep.a, and call
 * bof_gateway_open() once. See bof/README.md, "Gateways".
 */

#ifndef INCLUDE_BOF_GATEWAY_H
#define INCLUDE_BOF_GATEWAY_H

#include "bof.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Makes the process ready to call BoF as the programs of the BBS do: in
 * BBSHOME (the files of BoF are relative to it), with the board cache
 * (shared memory) of the running BBS attached. Errors: unknown (the BBS
 * is not running, or BBSHOME cannot be entered). */
bof_error_t bof_gateway_open(void);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_GATEWAY_H */
