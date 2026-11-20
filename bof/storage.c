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

// The backend when none was set: BOF_DEFAULT_STORAGE (config.h), the
// files backend unless pttbbs.conf names another (a bof_storage_ops_t
// declared in bof_storage.h).

static const bof_storage_ops_t *
get_backend(void)
{
    if (!backend)
        bof_storage_set(&BOF_DEFAULT_STORAGE, NULL);
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
    bof_post_result_t *res = out ? out : &local;
    bof_error_t err = WRITE(remove_post, res, actor, req);

    if (err == BOF_OK && req->bbs_modlog) {
        // Not as asked when the content could not be archived.
        const bof_modlog_t *m = req->bbs_modlog;
        if (req->bbs_modlog_unarchived &&
            (res->bbs_content_result == DELETE_FILE_CONTENT_FAILED ||
             !res->bbs_archived_path[0]))
            m = req->bbs_modlog_unarchived;
        modlog_done(req->post.community, actor, req->reason, m);
    }
    return err;
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
bof_restore_post(const bof_actor_t *actor, const bof_restore_post_t *req,
                 bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    bof_error_t err = WRITE(restore_post, out ? out : &local, actor, req);
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

bof_error_t
bof_create_post_like(const bof_actor_t *actor,
                     const bof_create_post_like_t *req,
                     bof_post_like_response_t *out)
{
    bof_post_like_response_t local = { 0 };
    return WRITE(create_post_like, out ? out : &local, actor, req);
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
bof_edit_comment(const bof_actor_t *actor, const bof_edit_comment_t *req,
                 bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    return WRITE(edit_comment, out ? out : &local, actor, req);
}

bof_error_t
bof_delete_comment(const bof_actor_t *actor, const bof_delete_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    if (!req->post.bbs_header || req->bbs_floor <= 0 || !req->bbs_creator)
        return BOF_ERR_INVALID_BODY_FIELD;
    return WRITE(delete_comment, out ? out : &local, actor, req);
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
 * Reports and the modlog
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_report(const bof_actor_t *actor, const bof_create_report_t *req,
                  bof_report_t *out)
{
    bof_report_t local = { 0 };
    return WRITE(create_report, out ? out : &local, actor, req);
}

bof_error_t
bof_resolve_report(const bof_actor_t *actor, const bof_resolve_report_t *req,
                   bof_report_t *out)
{
    bof_report_t local = { 0 };
    return WRITE(resolve_report, out ? out : &local, actor, req);
}

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
bof_get_posts(const bof_get_posts_t *req, bof_get_posts_response_t *out)
{
    return READ(get_posts, req, out);
}

bof_error_t
bof_get_post(const bof_get_post_t *req, bof_get_post_response_t *out)
{
    return READ(get_post, req, out);
}

bof_error_t
bof_bbs_get_post_body(const bof_post_ref_t *post, const char *dest_path,
                      bof_post_version_t *ver)
{
    return READ(get_post_body, post, dest_path, ver);
}

bof_error_t
bof_get_comments(const bof_get_comments_t *req,
                 bof_get_comments_response_t *out)
{
    return READ(get_comments, req, out);
}

bof_error_t
bof_list_reports(const bof_list_reports_t *req,
                 bof_list_reports_response_t *out)
{
    return READ(list_reports, req, out);
}

int
bof_bbs_post_version_matches(const bof_post_ref_t *post,
                             const bof_post_version_t *ver)
{
    const bof_storage_ops_t *b = get_backend();
    return b->post_version_matches ?
        b->post_version_matches(backend_ctx, post, ver) : 0;
}

bof_error_t
bof_bbs_get_post_file(const bof_post_ref_t *post, char *path, size_t sz)
{
    char buf[PATHLEN];
    bof_error_t err;

    if (!post || !post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || !path || !sz)
        return BOF_ERR_INVALID_BODY_FIELD;
    // The files of a board are the posts, or the cache of the backend.
    if ((err = READ(refresh_post, post)) != BOF_OK)
        return err;
    setdirpath(buf, post->bbs_dir, post->bbs_header->filename);
    if (strlcpy(path, buf, sz) >= sz)
        return BOF_ERR_INVALID_BODY_FIELD;
    return BOF_OK;
}
