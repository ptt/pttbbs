/*
 * bof_storage.h - the storage virtualization of bof (Birds Of Feather).
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 *   mbbsd -> bof API (bof.h) -> storage virtualization -> storage backends
 *                                                          (files, post.svc)
 *
 * One backend keeps everything (bof_storage_set()): every call of the bof
 * API goes to it, after the API has checked the actor and the request. An
 * operation the backend does not have (NULL) gives
 * bbs_storage_not_supported. A backend may be made of others: post.svc's
 * keeps posts in post.svc and the files of the boards as its cache, by
 * calling the files backend itself, so the two stay in step in one place.
 * There is no fan-out of writes to several backends: copies kept apart
 * (e.g. a search index) follow the one backend, by its log or a resync.
 *
 * Posts are named by their board and file name (bof_post_ref_t.community
 * and the file name of bbs_header); the position in a list (bbs_dir,
 * bbs_index) is for the files backend. Records for the modlog given with
 * an action are written (by write_modlog) by the bof API once the backend
 * did it.
 */

#ifndef INCLUDE_BOF_STORAGE_H
#define INCLUDE_BOF_STORAGE_H

#include "bof.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where a backend made of others logs what it could not keep in step
 * (e.g. the cache of post.svc's), to be synchronized again. */
#define BOF_STORAGE_LOG "log/bof_storage.log"

/* The operations of a backend; ctx is what it was set with. They
 * are the calls of bof.h of the same names. */
typedef struct bof_storage_ops_t {
    const char *name;

    /* Posts. */
    bof_error_t (*create_post)(void *ctx, const bof_actor_t *actor,
                               const bof_create_post_t *req,
                               bof_post_response_t *out);
    bof_error_t (*edit_post)(void *ctx, const bof_actor_t *actor,
                             const bof_edit_post_t *req,
                             bof_post_response_t *out);
    bof_error_t (*delete_post)(void *ctx, const bof_actor_t *actor,
                               const bof_delete_post_t *req,
                               bof_post_result_t *out);
    bof_error_t (*remove_post)(void *ctx, const bof_actor_t *actor,
                               const bof_remove_post_t *req,
                               bof_post_result_t *out);
    bof_error_t (*remove_posts)(void *ctx, const bof_actor_t *actor,
                                const bof_remove_posts_t *req, int *count);
    bof_error_t (*lock_post)(void *ctx, const bof_actor_t *actor,
                             const bof_lock_post_t *req,
                             bof_post_response_t *out);
    bof_error_t (*feature_post)(void *ctx, const bof_actor_t *actor,
                                const bof_feature_post_t *req,
                                bof_post_response_t *out);
    bof_error_t (*set_post_modes)(void *ctx, const bof_actor_t *actor,
                                  const bof_bbs_set_post_modes_t *req,
                                  bof_post_response_t *out);
    bof_error_t (*reset_post_score)(void *ctx, const bof_actor_t *actor,
                                    const bof_bbs_reset_post_score_t *req,
                                    bof_post_response_t *out);
    bof_error_t (*mask_post)(void *ctx, const bof_actor_t *actor,
                             const bof_bbs_mask_post_t *req,
                             bof_post_response_t *out);
    bof_error_t (*remove_crosspost)(void *ctx, const bof_actor_t *actor,
                                    const bof_bbs_remove_crosspost_t *req,
                                    bof_post_result_t *out);

    /* Comments. */
    bof_error_t (*create_comment)(void *ctx, const bof_actor_t *actor,
                                  const bof_create_comment_t *req,
                                  bof_comment_response_t *out);
    bof_error_t (*remove_comment)(void *ctx, const bof_actor_t *actor,
                                  const bof_remove_comment_t *req,
                                  bof_comment_response_t *out);

    /* The modlog: 0 or -1. */
    int (*write_modlog)(void *ctx, const char *community,
                        const bof_actor_t *actor, const char *reason,
                        int reason_audit_only, const bof_modlog_t *m);
    int (*note_modlog_edit)(void *ctx, const char *community,
                            const bof_actor_t *actor);

    /* Reads. */
    bof_error_t (*get_posts)(void *ctx, const bof_get_posts_t *req,
                             bof_get_posts_response_t *out);
    bof_error_t (*get_post)(void *ctx, const bof_get_post_t *req,
                            bof_get_post_response_t *out);
    bof_error_t (*get_post_body)(void *ctx, const bof_post_ref_t *post,
                                 const char *dest_path,
                                 bof_post_version_t *ver);
} bof_storage_ops_t;

/* The backend of the files of a classic BBS (.DIR, boards/, the time
 * capsule). It is used when no other was set. */
extern const bof_storage_ops_t bof_storage_files;

/* Sets the backend, with the context its operations get; NULL: the files
 * backend again, when something is called. */
void bof_storage_set(const bof_storage_ops_t *ops, void *ctx);

/* As bof_storage_set(NULL, NULL). */
void bof_storage_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_STORAGE_H */
