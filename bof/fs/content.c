/*
 * content.c - bof (Birds Of Feather), the files backend: the content of
 * posts kept in the files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The body to edit, masking lines and the version of the content, done
 * with files/content.c.
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

// Masks lines whose text (without color codes) has the pattern.
static int
mask_line(const char *line, FILE *out, void *ctx)
{
    char text[ANSILINELEN];

    strip_control_sequence(text, line);
    if (!mbs_strstr(text, (const char *)ctx))
        return 0;
    fputs(STR_POST_MARK STR_MASKED_LINE "\n", out);
    return 1;
}

bof_error_t
bof_fs_bbs_mask_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                     const bof_bbs_mask_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const char *reason = req->reason ? req->reason : "";
    char path[PATHLEN], note[ANSILINELEN], rev_note[ANSILINELEN * 2];
    time4_t now = time4(NULL);
    int matched;
    bof_error_t err;

    if (!post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || !req->pattern ||
        strlen(req->pattern) < 2 || !actor || !actor->name)
        return BOF_ERR_INVALID_BODY_FIELD;
    setdirpath(path, post->bbs_dir, post->bbs_header->filename);
    snprintf(note, sizeof(note), STR_POST_MARK STR_MASK_NOTE,
             actor->name, Cdatelite(&now), reason);
    snprintf(rev_note, sizeof(rev_note), "%s" STR_POST_MARK STR_MASK_PATTERN,
             note, req->pattern);
    err = bof_files_rewrite_post(path, mask_line, (void *)req->pattern, note,
                                 rev_note, &matched);
    if (err == BOF_OK && !matched)
        return BOF_ERR_NOT_FOUND;
    if (err == BOF_OK && out && out->bbs_header)
        *out->bbs_header = *post->bbs_header;
    return err;
}
