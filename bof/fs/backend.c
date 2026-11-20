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
    .create_post = bof_fs_create_post,
    .edit_post = bof_fs_edit_post,
    .delete_post = bof_fs_delete_post,
    .remove_post = bof_fs_remove_post,
    .remove_posts = bof_fs_remove_posts,
    .purge_posts = bof_fs_purge_posts,
    .restore_post = bof_fs_restore_post,
    .lock_post = bof_fs_lock_post,
    .feature_post = bof_fs_feature_post,
    .set_post_modes = bof_fs_bbs_set_post_modes,
    .reset_post_score = bof_fs_bbs_reset_post_score,
    .mask_post = bof_fs_bbs_mask_post,
    .remove_crosspost = bof_fs_bbs_remove_crosspost,
    // Votes are kept only with comments in files.
    .create_post_like = NULL,
    .create_comment = bof_fs_create_comment,
    .edit_comment = bof_fs_edit_comment,
    // A comment taken out of a file could neither be restored (deleted:
    // false) nor keep its history: see bof_delete_comment_t.
    .delete_comment = NULL,
    .remove_comment = bof_fs_remove_comment,
    .create_report = bof_fs_create_report,
    .resolve_report = bof_fs_resolve_report,
    .write_modlog = bof_fs_write_modlog,
    .note_modlog_edit = bof_fs_note_modlog_edit,
    .get_posts = bof_fs_get_posts,
    .get_post = bof_fs_get_post,
    .get_post_body = bof_fs_bbs_get_post_body,
    .get_comments = bof_fs_get_comments,
    // Comments in files keep no revisions of their own.
    .get_comment_revisions = NULL,
    .list_reports = bof_fs_list_reports,
    .post_version_matches = bof_fs_post_version_matches,
    .refresh_post = bof_fs_refresh_post,
    // A record of the files has no room for an ID.
    .get_post_id = NULL,
    .get_post_key = NULL,
    .find_post = bof_fs_find_post,
};
