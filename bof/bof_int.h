/*
 * bof_int.h - what the sources of bof (Birds Of Feather) include.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * bof is below the user interface: it uses the common libraries, the
 * structures and texts of the BBS, and its own headers, but not those of
 * mbbsd (proto.h, var.h, the UI toolkits), so that it cannot reach the
 * globals of a session (the current user, board, SHM) by accident.
 */

#ifndef BOF_BOF_INT_H
#define BOF_BOF_INT_H

#include "osdep.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "config.h"
#include "cmsys.h"
#include "cmbbs.h"
#include "ansi.h"
#include "pttstruct.h"
#include "common.h"
#include "bof.h"
#include "bof_files.h"
#include "fnv_hash.h"

#endif /* BOF_BOF_INT_H */
