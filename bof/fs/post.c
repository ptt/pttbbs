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
