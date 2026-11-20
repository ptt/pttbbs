/*
 * content.c - bof (Birds Of Feather), the files backend: the content of
 * posts kept in the files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The body to edit and the version of the content, done with
 * files/content.c.
 */

#include "../bof_int.h"
#include "bof_fs.h"

bof_error_t
bof_fs_bbs_get_post_body(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                         const char *dest_path, bof_post_version_t *ver)
{
    char path[PATHLEN], buf[4096];
    int fd_src, fd_dest;
    int64_t size = 0;
    ssize_t n;
    bof_error_t err = BOF_OK;

    if (!post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || !dest_path || !ver)
        return BOF_ERR_INVALID_BODY_FIELD;
    setdirpath(path, post->bbs_dir, post->bbs_header->filename);
    if ((fd_src = open(path, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if ((fd_dest = OpenCreate(dest_path, O_WRONLY | O_TRUNC)) < 0) {
        close(fd_src);
        return BOF_ERR_UNKNOWN;
    }
    while ((n = read(fd_src, buf, sizeof(buf))) > 0) {
        if (write(fd_dest, buf, n) != n) {
            err = BOF_ERR_UNKNOWN;
            break;
        }
        size += n;
    }
    if (n < 0)
        err = BOF_ERR_UNKNOWN;
    // The version of what was copied, from the same file even if it has
    // been replaced meanwhile.
    if (err == BOF_OK && bof_files_get_version(fd_src, size, ver) != 0)
        err = BOF_ERR_UNKNOWN;
    close(fd_dest);
    close(fd_src);
    if (err != BOF_OK)
        unlink(dest_path);
    return err;
}
