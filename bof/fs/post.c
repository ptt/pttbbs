/*
 * post.c - bof (Birds Of Feather), the files backend: posts kept in the
 * files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The calls of bof.h on posts, done with the records and files of a board
 * (files/record.c) and the time capsule.
 */

#include "../bof_int.h"
#include "bof_fs.h"

// What DeletePost and RemovePost share: the record is dropped (or replaced
// by a tombstone, titled by default after whether deleter is its owner),
// and the content archived in the same list.
static bof_error_t
drop_post(const bof_actor_t *actor, const bof_post_ref_t *post,
          int keep_record, const char *title, const char *reason,
          bof_post_result_t *out)
{
    const fileheader_t *fh = post->bbs_header;
    const char *deleter = actor ? actor->name : NULL;

    memset(out, 0, sizeof(*out));
    if (!post->bbs_dir || !fh || post->bbs_index <= 0 || !fh->filename[0])
        return BOF_ERR_INVALID_BODY_FIELD;

    if (!(keep_record &&
          bof_files_tombstone(post->bbs_dir, post->bbs_index, fh, title,
                              deleter) == 0) &&
        delete_fileheader(post->bbs_dir, fh, post->bbs_index) != 0)
        return BOF_ERR_COULDNT_FIND_POST;

    out->bbs_content_result = bof_files_delete_content(
            post->bbs_dir, fh, post->bbs_dir, out->bbs_archived_path,
            sizeof(out->bbs_archived_path), reason, deleter,
            actor ? actor->from : NULL);
    return BOF_OK;
}

bof_error_t
bof_fs_delete_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                   const bof_delete_post_t *req, bof_post_result_t *out)
{
    if (!req->deleted)
        return BOF_ERR_INVALID_BODY_FIELD;
    return drop_post(actor, &req->post, req->bbs_keep_record,
                     req->bbs_title, NULL, out);
}

bof_error_t
bof_fs_remove_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                   const bof_remove_post_t *req, bof_post_result_t *out)
{
    const char *title = req->bbs_title;
    const fileheader_t *fh = req->post.bbs_header;

    if (!req->removed)
        return BOF_ERR_INVALID_BODY_FIELD;
    // The tombstone of an anonymous post can not tell by its owner that it
    // was removed rather than deleted: say it.
    if ((!title || !*title) && fh && (fh->filemode & FILE_ANONYMOUS))
        title = STR_SAFEREMOVE_TITLE;
    return drop_post(actor, &req->post, req->bbs_keep_record,
                     title, req->reason, out);
}

// The record of post at its position, changed as asked; out gets it.
static bof_error_t
change_record(const bof_post_ref_t *post, int32_t modified,
              const char *title, const char *owner,
              const char *date, const int32_t *money,
              uint16_t enable, uint16_t disable,
              bof_post_response_t *out)
{
    const fileheader_t *fh = post->bbs_header;
    fileheader_t updated;
    int32_t multi;

    if (!post->bbs_dir || !fh || post->bbs_index <= 0 || !fh->filename[0])
        return BOF_ERR_INVALID_BODY_FIELD;
    if (money)
        multi = *money;
    if (bof_files_modify_record(post->bbs_dir, post->bbs_index, fh->filename,
                                modified, title, owner, date, 0,
                                money ? &multi : NULL, enable, disable,
                                &updated) != 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;
}

bof_error_t
bof_fs_edit_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                 const bof_edit_post_t *req, bof_post_response_t *out)
{
    return change_record(&req->post, req->bbs_modified, req->name,
                         req->bbs_owner, req->bbs_date,
                         req->bbs_money, 0, 0, out);
}

bof_error_t
bof_fs_bbs_set_post_modes(void *ctx GCC_UNUSED,
                          const bof_actor_t *actor GCC_UNUSED,
                          const bof_bbs_set_post_modes_t *req,
                          bof_post_response_t *out)
{
    if (!req->enable && !req->disable)
        return BOF_ERR_INVALID_BODY_FIELD;
    return change_record(&req->post, 0, NULL, NULL, NULL, NULL,
                         req->enable, req->disable, out);
}

static int
valid_ref(const bof_post_ref_t *post)
{
    const fileheader_t *fh = post->bbs_header;
    return post->bbs_dir && fh && post->bbs_index > 0 && fh->filename[0];
}

bof_error_t
bof_fs_lock_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                 const bof_lock_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    fileheader_t updated;
    char from[PATHLEN], to[PATHLEN];
    int moved = 0;

    if (!valid_ref(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // Already as asked.
    if ((req->locked && fh->filename[0] == 'L') ||
        (!req->locked && fh->filename[0] == 'M')) {
        if (out && out->bbs_header)
            *out->bbs_header = *fh;
        return BOF_OK;
    }
    if (fh->filename[0] != (req->locked ? 'M' : 'L'))
        return BOF_ERR_INVALID_BODY_FIELD;

    // The file of a locked post is X.<stamp>: code that doesn't know
    // locked posts must not read it as L.<stamp> (older locks did not
    // rename it, so unlocking leaves those alone).
    updated = *fh;
    updated.filename[0] = req->locked ? 'M' : 'X';
    setdirpath(from, post->bbs_dir, updated.filename);
    updated.filename[0] = req->locked ? 'X' : 'M';
    setdirpath(to, post->bbs_dir, updated.filename);
    if (access(from, F_OK) == 0) {
        if (Rename(from, to) != 0)
            return BOF_ERR_COULDNT_UPDATE_POST;
        moved = 1;
    }
    updated.filename[0] = req->locked ? 'L' : 'M';
    if (substitute_fileheader(post->bbs_dir, fh, &updated,
                              post->bbs_index) != 0) {
        if (moved)
            Rename(to, from);
        return BOF_ERR_COULDNT_FIND_POST;
    }
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;
}

bof_error_t
bof_fs_feature_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                    const bof_feature_post_t *req, bof_post_response_t *out)
{
    if (req->featured)
        return change_record(&req->post, 0, NULL, NULL, NULL, NULL,
                             FILE_BOTTOM | FILE_MARKED, 0, out);
    return change_record(&req->post, 0, NULL, NULL, NULL, NULL,
                         0, FILE_BOTTOM, out);
}

static void
reset_score(fileheader_t *fhdr, const void *ctx GCC_UNUSED)
{
    fhdr->score = 0;
}

bof_error_t
bof_fs_bbs_reset_post_score(void *ctx GCC_UNUSED,
                            const bof_actor_t *actor GCC_UNUSED,
                            const bof_bbs_reset_post_score_t *req,
                            bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    fileheader_t updated;

    if (!valid_ref(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (bof_files_update_record(post->bbs_dir, post->bbs_index,
                                post->bbs_header->filename, reset_score,
                                NULL, &updated) != 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;
}

bof_error_t
bof_fs_create_post(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                   const bof_create_post_t *req, bof_post_response_t *out)
{
    if (!req->bbs_dir || !req->bbs_header || !req->bbs_header->filename[0])
        return BOF_ERR_INVALID_BODY_FIELD;
    if (append_fileheader(req->bbs_dir, req->bbs_header) == -1)
        return BOF_ERR_COULDNT_CREATE_POST;
    if (out && out->bbs_header)
        *out->bbs_header = *req->bbs_header;
    return BOF_OK;
}
