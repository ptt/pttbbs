/*
 * crosspost.c - bof (Birds Of Feather), the files backend: removing the
 * copies of posts in other boards.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * New posts of open boards are copied (or noted, with CONFIG_LIVE_ALLPOST)
 * into boards that collect them (ALLPOST, ALLHIDPOST, NEWIDPOST, and
 * UnAnonymous for anonymous posts), with the same file name and the
 * origin board at the end of the title. Lemmy and Reddit show such
 * listings (r/all) without copies, so these are PTT's; a crosspost by a
 * user (Reddit's crosspost_parent) is a new post and not handled here.
 */

#include "../bof_int.h"
#include "bof_fs.h"

bof_error_t
bof_fs_bbs_remove_crosspost(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                            const bof_bbs_remove_crosspost_t *req,
                            bof_post_result_t *out)
{
    const fileheader_t *fh = req->bbs_header;
    const char *deleter = actor ? actor->name : NULL;
    fileheader_t found;
    char path[PATHLEN];
    bof_error_t err = BOF_OK;
    int i;

    memset(out, 0, sizeof(*out));
    if (!req->bbs_dir || !fh || !fh->filename[0])
        return BOF_ERR_INVALID_BODY_FIELD;
    setdirpath(path, req->bbs_dir, fh->filename);
    if (req->bbs_content_only) {
        unlink(path);
        return BOF_OK;
    }

    // Found by its AID (search_dir_by_filename()), into found, so fh stays
    // as given.
    found = *fh;
    if ((i = search_dir_by_filename(req->bbs_dir, fh->filename,
                                    &found)) > 0) {
        if (req->bbs_keep_record &&
            bof_files_tombstone(req->bbs_dir, i, &found, NULL, deleter) == 0)
            out->bbs_record_kept = 1;
        else if (delete_fileheader(req->bbs_dir, &found, i) != 0)
            err = BOF_ERR_COULDNT_FIND_POST;
    } else {
        err = BOF_ERR_COULDNT_FIND_POST;
    }

    // Remove the file even if its record was not found, so that it is not
    // left behind.
    out->bbs_content_result = bof_files_delete_content(
            req->bbs_dir, fh, req->bbs_dir, out->bbs_archived_path,
            sizeof(out->bbs_archived_path), req->reason, deleter,
            actor ? actor->from : NULL);
    unlink(path);
    return err;
}
