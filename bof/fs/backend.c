/*
 * backend.c - bof (Birds Of Feather), the files backend: its operations.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * bof_storage_files keeps everything in the files of a classic BBS (.DIR,
 * boards/, the time capsule). It is the backend when no other is set
 * (see bof_storage.h).
 */

#include "../bof_int.h"
#include "bof_fs.h"
#include "bof_storage.h"

const bof_storage_ops_t bof_storage_files = {
    .name = "files",
    .edit_post = bof_fs_edit_post,
    .delete_post = bof_fs_delete_post,
    .remove_post = bof_fs_remove_post,
    .set_post_modes = bof_fs_bbs_set_post_modes,
};
