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
BOF_FS_WRITE(remove_posts, bof_remove_posts_t, int);
BOF_FS_WRITE(purge_posts, bof_purge_posts_t, int);
BOF_FS_WRITE(restore_post, bof_restore_post_t, bof_post_response_t);
BOF_FS_WRITE(lock_post, bof_lock_post_t, bof_post_response_t);
BOF_FS_WRITE(feature_post, bof_feature_post_t, bof_post_response_t);
BOF_FS_WRITE(bbs_set_post_modes, bof_bbs_set_post_modes_t,
             bof_post_response_t);
BOF_FS_WRITE(bbs_reset_post_score, bof_bbs_reset_post_score_t,
             bof_post_response_t);
BOF_FS_WRITE(bbs_mask_post, bof_bbs_mask_post_t, bof_post_response_t);
BOF_FS_WRITE(bbs_remove_crosspost, bof_bbs_remove_crosspost_t,
             bof_post_result_t);
BOF_FS_WRITE(create_comment, bof_create_comment_t, bof_comment_response_t);
BOF_FS_WRITE(edit_comment, bof_edit_comment_t, bof_comment_response_t);
BOF_FS_WRITE(remove_comment, bof_remove_comment_t, bof_comment_response_t);
BOF_FS_WRITE(create_report, bof_create_report_t, bof_report_t);
BOF_FS_WRITE(resolve_report, bof_resolve_report_t, bof_report_t);
#undef BOF_FS_WRITE
int bof_fs_write_modlog(void *ctx, const char *community,
                        const bof_actor_t *actor, const char *reason,
                        int reason_audit_only, const bof_modlog_t *m);
int bof_fs_note_modlog_edit(void *ctx, const char *community,
                            const bof_actor_t *actor);
bof_error_t bof_fs_get_posts(void *ctx, const bof_get_posts_t *req,
                             bof_get_posts_response_t *out);
bof_error_t bof_fs_get_post(void *ctx, const bof_get_post_t *req,
                            bof_get_post_response_t *out);
bof_error_t bof_fs_bbs_get_post_body(void *ctx, const bof_post_ref_t *post,
                                     const char *dest_path,
                                     bof_post_version_t *ver);
bof_error_t bof_fs_get_comments(void *ctx, const bof_get_comments_t *req,
                                bof_get_comments_response_t *out);
bof_error_t bof_fs_list_reports(void *ctx, const bof_list_reports_t *req,
                                bof_list_reports_response_t *out);
int bof_fs_post_version_matches(void *ctx, const bof_post_ref_t *post,
                                const bof_post_version_t *ver);
bof_error_t bof_fs_refresh_post(void *ctx, const bof_post_ref_t *post);
bof_error_t bof_fs_find_post(void *ctx, const bof_post_key_t *key,
                             bof_found_post_t *out);
bof_error_t bof_fs_get_post_revisions(void *ctx, const bof_post_ref_t *post,
                                      int *count);
bof_error_t bof_fs_get_post_revision_file(void *ctx,
                                          const bof_post_ref_t *post, int rev,
                                          char *path, size_t sz);
bof_error_t bof_fs_list_post_revisions(
    void *ctx, const bof_bbs_list_post_revisions_t *req,
    bof_bbs_list_post_revisions_response_t *out);
bof_error_t bof_fs_list_removed_posts(
    void *ctx, const bof_bbs_list_removed_posts_t *req,
    bof_bbs_list_removed_posts_response_t *out);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_FS_H */
