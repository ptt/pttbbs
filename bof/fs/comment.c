/*
 * comment.c - bof (Birds Of Feather), the files backend: comments on
 * posts kept in the files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The comments of a post are lines appended to its file; the record of the
 * post in the list keeps its score (net votes of pushes and boos) and when
 * the file was last modified.
 */

#include "../bof_int.h"
#include "bof_fs.h"

// Appends text to path, under an exclusive lock if lock (trying again a few
// times while someone else holds it). Returns 0 and the size after, or a
// bof error.
static bof_error_t
append_comment(const char *path, const char *text, const bof_create_comment_t *req,
               int64_t *size)
{
    int fd = open(path, O_APPEND | O_WRONLY);
    size_t len = strlen(text);

    if (fd < 0)
        return errno == EROFS ? BOF_ERR_BBS_READ_ONLY :
            BOF_ERR_COULDNT_FIND_POST;

    if (req->bbs_lock) {
        int tries = 5;
        while (flock(fd, LOCK_EX | LOCK_NB) < 0) {
            if (tries-- <= 0) {
                close(fd);
                return BOF_ERR_COULDNT_CREATE_COMMENT;
            }
            if (req->bbs_on_wait)
                req->bbs_on_wait(tries + 1, req->bbs_wait_ctx);
            sleep(1);
            // Open it again: an editor saves a new file.
            close(fd);
            if ((fd = open(path, O_APPEND | O_WRONLY)) < 0)
                return errno == EROFS ? BOF_ERR_BBS_READ_ONLY :
                    BOF_ERR_COULDNT_FIND_POST;
        }
    }
    if (write(fd, text, len) != (ssize_t)len) {
        close(fd);
        return BOF_ERR_COULDNT_CREATE_COMMENT;
    }
    *size = lseek(fd, 0, SEEK_CUR);
    if (req->bbs_lock)
        flock(fd, LOCK_UN);
    close(fd);
    return BOF_OK;
}

bof_error_t
bof_fs_create_comment(void *ctx GCC_UNUSED,
                      const bof_actor_t *actor GCC_UNUSED,
                      const bof_create_comment_t *req,
                      bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    fileheader_t updated;
    char path[PATHLEN];
    int64_t size = 0;
    int delta = 0;
    bof_error_t err;

    if (!post->bbs_dir || !fh || post->bbs_index <= 0 || !fh->filename[0] ||
        !req->content)
        return BOF_ERR_INVALID_BODY_FIELD;

    setdirpath(path, post->bbs_dir, fh->filename);
    if ((err = append_comment(path, req->content, req, &size)) != BOF_OK)
        return err;

    if (req->bbs_vote > 0 && fh->score < MAX_DISPLAY_SCORE)
        delta = 1;
    else if (req->bbs_vote < 0 && fh->score > -MAX_DISPLAY_SCORE)
        delta = -1;

    updated = *fh;
    updated.score += delta;
    updated.modified = dasht4(path);
    if (updated.modified != 0 &&
        bof_files_modify_record(post->bbs_dir, post->bbs_index, fh->filename,
                                updated.modified, NULL, NULL, NULL, delta,
                                NULL, 0, 0, &updated) != 0)
        return BOF_ERR_COULDNT_UPDATE_POST;  // appended, but not counted

    if (out) {
        if (out->bbs_header)
            *out->bbs_header = updated;
        out->bbs_file_size = size;
    }
    return BOF_OK;
}

// What a comment line starts with, up to its last color code; older
// comments may have no space after the colon (strip_sep).
static size_t
comment_prefix(char *buf, int strip_sep)
{
    char *p;

    if (strip_sep && (p = strstr(buf, ": ")))
        memmove(p + 1, p + 2, strlen(p + 2) + 1);
    if ((p = strrchr(buf, ESC_CHR)))
        *p = '\0';
    return strlen(buf);
}

// Finds the line of a comment (the first that looks like it) and replaces
// it with a note of its removal.
typedef struct {
    char patterns[4][ANSILINELEN];
    size_t lens[4];
    int npatterns;
    int found;
    const char *remover, *author, *reason;
} remove_ctx;

static int
remove_line(const char *line, FILE *out, void *vctx)
{
    remove_ctx *c = (remove_ctx *)vctx;
    int i, match = 0;

    for (i = 0; !c->found && !match && i < c->npatterns; i++)
        match = strncmp(line, c->patterns[i], c->lens[i]) == 0 &&
                (line[c->lens[i]] == ' ' || line[c->lens[i]] == ESC_CHR);
    if (!match)
        return 0;
    // The reason is at most 40 characters.
    fprintf(out, ANSI_COLOR(1;30) STR_COMMENT_REMOVED_LINE ANSI_RESET "\n",
            c->remover, c->author, c->reason);
    c->found = 1;
    return 1;
}

bof_error_t
bof_fs_remove_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                      const bof_remove_comment_t *req,
                      bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    const char *reason = req->reason ? req->reason : "";
    char path[PATHLEN], padded[IDLEN + 1], rev_note[ANSILINELEN * 2];
    time4_t now = time4(NULL);
    remove_ctx c = { .npatterns = IS_ENABLED(CONFIG_OLD_RECOMMEND) ? 2 : 4 };
    int type, i, matched;
    bof_error_t err;

    if (!post->bbs_dir || !fh || !fh->filename[0] || !req->bbs_author ||
        !req->bbs_author[0] || !req->bbs_content || !actor || !actor->name)
        return BOF_ERR_INVALID_BODY_FIELD;
    type = req->bbs_vote > 0 ? RECTYPE_GOOD :
           req->bbs_vote < 0 ? RECTYPE_BAD : RECTYPE_ARROW;
    c.remover = actor->name;
    c.author = req->bbs_author;
    c.reason = reason;

    // The line as written, with the author as is or padded, and (for
    // comments of old versions) without the space after the colon.
    snprintf(padded, sizeof(padded), "%-*s", IDLEN, req->bbs_author);
    for (i = 0; i < c.npatterns; i++) {
        bof_files_format_comment(c.patterns[i], sizeof(c.patterns[i]), type,
                                 (i % 2) ? padded : req->bbs_author, 0,
                                 req->bbs_content, "");
        c.lens[i] = comment_prefix(c.patterns[i], i >= 2);
    }

    setdirpath(path, post->bbs_dir, fh->filename);
    snprintf(rev_note, sizeof(rev_note),
             "\n" STR_POST_MARK STR_COMMENT_REMOVED_NOTE, Cdatelite(&now),
             actor->name, reason, req->bbs_author, req->bbs_content);
    err = bof_files_rewrite_post(path, remove_line, &c, NULL, rev_note,
                                 &matched);
    if (err != BOF_OK)
        return err;
    if (!matched)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    if (out) {
        if (out->bbs_header)
            *out->bbs_header = *fh;
        out->bbs_file_size = dashs(path);
    }
    return BOF_OK;
}
