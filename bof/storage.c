/*
 * storage.c - bof (Birds Of Feather): the storage virtualization, and the
 * calls of the bof API that go through it.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * See bof_storage.h: every call goes to the one backend. The work of the
 * bof API that is not about storage (the checks of the actor, the modlog
 * of moderation actions) is done here, once, for any backend.
 */

#include "bof_int.h"
#include "bof_storage.h"

// The one backend; NULL until set (or first used: the default one).
static const bof_storage_ops_t *backend;
static void *backend_ctx;

void
bof_storage_set(const bof_storage_ops_t *ops, void *ctx)
{
    backend = ops;
    backend_ctx = ops ? ctx : NULL;
}

void
bof_storage_reset(void)
{
    bof_storage_set(NULL, NULL);
}

// The backend when none was set: the files backend.
static const bof_storage_ops_t *
get_backend(void)
{
    if (!backend)
        bof_storage_set(&bof_storage_files, NULL);
    return backend;
}

// Calls op of the backend; bbs_storage_not_supported if it has none.
// Writes and reads go the same way; they are named apart to show which
// calls change things.
#define CALL(op, ...) ({                                                    \
    const bof_storage_ops_t *b_ = get_backend();                            \
    b_->op ? b_->op(backend_ctx, __VA_ARGS__) :                             \
        BOF_ERR_BBS_STORAGE_NOT_SUPPORTED; })
#define WRITE(op, out, ...) CALL(op, __VA_ARGS__, (out))
#define READ(op, ...) CALL(op, __VA_ARGS__)

// Writes a record of the modlog. Returns 0, or -1.
static int
modlog_write_all(const char *community, const bof_actor_t *actor,
                 const char *reason, int reason_audit_only,
                 const bof_modlog_t *m)
{
    const bof_storage_ops_t *b = get_backend();

    if (!b->write_modlog ||
        b->write_modlog(backend_ctx, community, actor, reason,
                        reason_audit_only, m) != 0)
        return -1;
    return 0;
}

// Writes the record of a moderation action, once it is done.
static void
modlog_done(const char *community, const bof_actor_t *actor,
            const char *reason, const bof_modlog_t *m)
{
    if (m && actor && community)
        modlog_write_all(community, actor, reason, 0, m);
}

/* ------------------------------------------------------------------------
 * Posts
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_post(const bof_actor_t *actor, const bof_create_post_t *req,
                bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    return WRITE(create_post, out ? out : &local, actor, req);
}

bof_error_t
bof_edit_post(const bof_actor_t *actor, const bof_edit_post_t *req,
              bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    bof_error_t err = WRITE(edit_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, NULL, req->bbs_modlog);
    return err;
}

bof_error_t
bof_delete_post(const bof_actor_t *actor, const bof_delete_post_t *req,
                bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    return WRITE(delete_post, out ? out : &local, actor, req);
}

bof_error_t
bof_remove_post(const bof_actor_t *actor, const bof_remove_post_t *req,
                bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    return WRITE(remove_post, out ? out : &local, actor, req);
}

bof_error_t
bof_remove_posts(const bof_actor_t *actor, const bof_remove_posts_t *req,
                 int *count)
{
    int local;
    bof_error_t err = WRITE(remove_posts, count ? count : &local, actor,
                            req);
    if (err == BOF_OK)
        modlog_done(req->community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_lock_post(const bof_actor_t *actor, const bof_lock_post_t *req,
              bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    bof_error_t err = WRITE(lock_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_feature_post(const bof_actor_t *actor, const bof_feature_post_t *req,
                 bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    return WRITE(feature_post, out ? out : &local, actor, req);
}

bof_error_t
bof_bbs_set_post_modes(const bof_actor_t *actor,
                       const bof_bbs_set_post_modes_t *req,
                       bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    return WRITE(set_post_modes, out ? out : &local, actor, req);
}

bof_error_t
bof_bbs_reset_post_score(const bof_actor_t *actor,
                         const bof_bbs_reset_post_score_t *req,
                         bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    bof_error_t err = WRITE(reset_post_score, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_bbs_mask_post(const bof_actor_t *actor, const bof_bbs_mask_post_t *req,
                  bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    bof_error_t err = WRITE(mask_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_bbs_remove_crosspost(const bof_actor_t *actor,
                         const bof_bbs_remove_crosspost_t *req,
                         bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    return WRITE(remove_crosspost, out ? out : &local, actor, req);
}

/* ------------------------------------------------------------------------
 * Comments
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_comment(const bof_actor_t *actor, const bof_create_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    return WRITE(create_comment, out ? out : &local, actor, req);
}

bof_error_t
bof_remove_comment(const bof_actor_t *actor, const bof_remove_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    bof_error_t err = WRITE(remove_comment, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

/* ------------------------------------------------------------------------
 * The modlog
 * ------------------------------------------------------------------------ */

int
bof_modlog_write(const char *community, const bof_actor_t *actor,
                 const char *reason, int reason_audit_only,
                 const bof_modlog_t *m)
{
    return modlog_write_all(community, actor, reason, reason_audit_only, m);
}

int
bof_modlog_note_edit(const char *community, const bof_actor_t *actor)
{
    const bof_storage_ops_t *b = get_backend();

    if (!b->note_modlog_edit ||
        b->note_modlog_edit(backend_ctx, community, actor) != 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------------
 * Reads
 * ------------------------------------------------------------------------ */

bof_error_t
bof_bbs_get_post_body(const bof_post_ref_t *post, const char *dest_path,
                      bof_post_version_t *ver)
{
    return READ(get_post_body, post, dest_path, ver);
}
