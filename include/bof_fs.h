/*
 * bof_fs.h - the files backend of bof (Birds Of Feather), for backends
 * made of it.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The operations of bof_storage_files (bof/fs/): the calls of bof.h of
 * the same names (without bof_fs_), with the context of the backend
 * first. A backend that keeps the files of the boards as its cache (as
 * post.svc's does, bof/ps/ in pttbbs.cmd) calls them to keep the cache in
 * step, or uses them as its own operations where the files are what it
 * has. Lower than these, the primitives of the files themselves are in
 * bof_files.h.
 */

#ifndef INCLUDE_BOF_FS_H
#define INCLUDE_BOF_FS_H

#include "bof.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOF_FS_WRITE(name, req_t, out_t) \
    bof_error_t bof_fs_##name(void *ctx, const bof_actor_t *actor, \
                              const req_t *req, out_t *out)
BOF_FS_WRITE(create_post, bof_create_post_t, bof_post_response_t);
BOF_FS_WRITE(edit_post, bof_edit_post_t, bof_post_response_t);
BOF_FS_WRITE(delete_post, bof_delete_post_t, bof_post_result_t);
BOF_FS_WRITE(remove_post, bof_remove_post_t, bof_post_result_t);
BOF_FS_WRITE(lock_post, bof_lock_post_t, bof_post_response_t);
BOF_FS_WRITE(feature_post, bof_feature_post_t, bof_post_response_t);
BOF_FS_WRITE(bbs_set_post_modes, bof_bbs_set_post_modes_t,
             bof_post_response_t);
BOF_FS_WRITE(bbs_reset_post_score, bof_bbs_reset_post_score_t,
             bof_post_response_t);
BOF_FS_WRITE(create_comment, bof_create_comment_t, bof_comment_response_t);
#undef BOF_FS_WRITE

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_FS_H */
