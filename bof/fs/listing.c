/*
 * listing.c - bof (Birds Of Feather), the files backend: reading the
 * posts of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The posts of a board are listed in .DIR, oldest first. Featured
 * (pinned) posts are listed again after them, as PTT shows them at the
 * bottom of the board; Lemmy lists its featured posts first.
 */

#include "../bof_int.h"
#include "bof_fs.h"

bof_error_t
bof_fs_get_posts(void *ctx GCC_UNUSED, const bof_get_posts_t *req,
                 bof_get_posts_response_t *out)
{
    fileheader_t *buf;
    int total, n, rv = 0, fstart, avail;
    char bdir[PATHLEN];
    const char *dir = req->bbs_dir;

    // The list of the board itself, if not given (e.g. by a gateway).
    if (!dir && req->community && is_valid_existing_brdname(req->community)) {
        setbfile(bdir, req->community, FN_DIR);
        dir = bdir;
    }

    if (!out)
        return BOF_ERR_INVALID_BODY_FIELD;
    buf = out->posts;
    out->count = 0;
    out->bbs_total = 0;
    if (!dir || !buf || req->limit <= 0 || req->bbs_start <= 0 ||
        (req->bbs_nfeatured > 0 && !req->bbs_featured))
        return BOF_ERR_INVALID_BODY_FIELD;
    total = req->bbs_total > 0 ? req->bbs_total :
        get_num_records(dir, sizeof(fileheader_t));
    out->bbs_total = total;

    // The posts of the list from bbs_start, as many as fit (and not after
    // the last of the list).
    n = total - req->bbs_start + 1;
    if (n >= req->limit || req->bbs_nfeatured <= 0) {
        int want = n < req->limit ? n : req->limit;
        rv = want > 0 ?
            get_fileheaders(dir, buf, req->bbs_start, want) : 0;
        out->count = rv < 0 ? 0 : rv;
        return BOF_OK;
    }
    if (n > 0) {
        rv = get_fileheaders(dir, buf, req->bbs_start, n);
        if (rv < 0)
            rv = 0;
        fstart = 1;
    } else {
        fstart = 1 - n;
    }

    // Then the featured ones, as many as fit after them.
    avail = req->bbs_nfeatured - (fstart - 1);
    if (rv + avail > req->limit)
        avail = req->limit - rv;
    if (avail > 0) {
        int fd = -1;
        for (int i = 0; i < avail; i++) {
            int idx = (fstart - 1) + i;
            if (idx < 0 || idx >= req->bbs_nfeatured)
                continue;
            if (get_fileheader_keep(dir, &buf[rv],
                                    req->bbs_featured[idx], &fd) > 0) {
                buf[rv].filemode |= FILE_BOTTOM;
                rv++;
            }
        }
        if (fd != -1)
            close(fd);
    }
    out->count = rv;
    return BOF_OK;
}

bof_error_t
bof_fs_get_post(void *ctx GCC_UNUSED, const bof_get_post_t *req,
                bof_get_post_response_t *out)
{
    char path[PATHLEN];
    int rv;

    if (!req->bbs_dir || req->bbs_index <= 0 || !out || !out->bbs_header)
        return BOF_ERR_INVALID_BODY_FIELD;
    rv = req->bbs_fd ?
        get_fileheader_keep(req->bbs_dir, out->bbs_header, req->bbs_index,
                            req->bbs_fd) :
        get_fileheader(req->bbs_dir, out->bbs_header, req->bbs_index);
    if (rv <= 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if (out->bbs_header->filename[0]) {
        setdirpath(path, req->bbs_dir, out->bbs_header->filename);
        strlcpy(out->bbs_path, path, sizeof(out->bbs_path));
    } else {
        out->bbs_path[0] = '\0';
    }
    return BOF_OK;
}

bof_error_t
bof_fs_find_post(void *ctx GCC_UNUSED, const bof_post_key_t *key,
                 bof_found_post_t *out)
{
    int i, fd = -1;

    if (!is_valid_existing_brdname(key->community) ||
        strchr(key->bbs_post_file, '/'))
        return BOF_ERR_INVALID_BODY_FIELD;
    setbfile(out->bbs_dir, key->community, FN_DIR);
    if (!dashf(out->bbs_dir))
        return BOF_ERR_COULDNT_FIND_COMMUNITY;

    // By its AID (search_dir_by_filename()); else one by one (names that
    // are not M. or G., e.g. locked posts).
    if ((i = search_dir_by_filename(out->bbs_dir, key->bbs_post_file,
                                    out->bbs_header)) > 0) {
        out->bbs_index = i;
        return BOF_OK;
    }
    for (i = 1; get_fileheader_keep(out->bbs_dir, out->bbs_header, i, &fd) > 0;
         i++) {
        if (strcmp(out->bbs_header->filename, key->bbs_post_file) == 0) {
            close(fd);
            out->bbs_index = i;
            return BOF_OK;
        }
    }
    if (fd != -1)
        close(fd);
    return BOF_ERR_COULDNT_FIND_POST;
}
