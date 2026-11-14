/*
 * storage.c - bof (Birds Of Feather): the storage virtualization, and the
 * calls of the bof API that go through it.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * See bof_storage.h: every call goes to the one backend. The work of the
 * bof API that is not about storage (the checks of the actor) is done
 * here, once, for any backend.
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

/* ------------------------------------------------------------------------
 * Posts
 * ------------------------------------------------------------------------ */

bof_error_t
bof_edit_post(const bof_actor_t *actor, const bof_edit_post_t *req,
              bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    return WRITE(edit_post, out ? out : &local, actor, req);
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
bof_bbs_set_post_modes(const bof_actor_t *actor,
                       const bof_bbs_set_post_modes_t *req,
                       bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    return WRITE(set_post_modes, out ? out : &local, actor, req);
}
