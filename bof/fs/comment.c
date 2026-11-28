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
    int up = 0, down = 0;
    bof_error_t err;

    if (!post->bbs_dir || !fh || post->bbs_index <= 0 || !fh->filename[0] ||
        !req->content)
        return BOF_ERR_INVALID_BODY_FIELD;

    setdirpath(path, post->bbs_dir, fh->filename);
    if ((err = append_comment(path, req->content, req, &size)) != BOF_OK)
        return err;

    if (req->bbs_vote > 0 && fh->upvote < INT16_MAX)
        up = 1;
    else if (req->bbs_vote < 0 && fh->downvote < INT16_MAX)
        down = 1;

    updated = *fh;
    updated.upvote += up;
    updated.downvote += down;
    updated.modified = dasht4(path);
    if (updated.modified != 0 &&
        bof_files_modify_record(post->bbs_dir, post->bbs_index, fh->filename,
                                updated.modified, NULL, NULL, NULL, up, down,
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

static int parse_comment_line(const char *line, bof_comment_t *c,
                              size_t *coff, size_t *clen);

// Where the comments of the file of a post begin: after its origin line
// (STR_POST_MARK STR_POST_ORIGIN, as the BBS writes it under the body),
// the last one, as a crosspost copies another post whole, its comments
// too, and adds its own. Lines of the body that look like comments (quoted,
// copied, or a summary of comments) are not comments. Returns the offset
// after that line, or 0 if there is none (posts of old may have none:
// then all lines count).
static off_t
comments_offset(FILE *fp)
{
    static const char origin[] = STR_POST_MARK STR_POST_ORIGIN;
    char buf[ANSILINELEN];
    off_t pos = 0, after = 0;
    int start = 1, in_origin = 0;

    while (fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);
        if (start)
            in_origin = strncmp(buf, origin, sizeof(origin) - 1) == 0;
        pos += n;
        start = n > 0 && buf[n - 1] == '\n';
        if (in_origin && start)
            after = pos;
    }
    if (in_origin && !start)
        after = pos;
    return after;
}

// The same, of the file at path (0 if it cannot be read).
static off_t
comments_offset_of(const char *path)
{
    FILE *fp = fopen(path, "r");
    off_t off;

    if (!fp)
        return 0;
    off = comments_offset(fp);
    fclose(fp);
    return off;
}

// For the filters of bof_files_rewrite_post(), which see every line in
// order: whether line (read at *pos, which it moves past) is where comments
// may be, at or after from.
static int
in_comments(const char *line, off_t *pos, off_t from)
{
    off_t at = *pos;

    *pos += (off_t)strlen(line);
    return at >= from;
}

// Finds the line of a comment (the one at floor, or else the first that
// looks like it) and replaces it with a note of its removal.
typedef struct {
    char patterns[4][ANSILINELEN];
    size_t lens[4];
    int npatterns;
    int found;
    const bof_remove_comment_t *req;
    int floor;      // of the line read
    off_t from, pos;    // where the comments begin; where the line is
    const char *remover, *author, *reason;
} remove_ctx;

static int
remove_line(const char *line, FILE *out, void *vctx)
{
    remove_ctx *c = (remove_ctx *)vctx;
    int i, match = 0;

    if (!in_comments(line, &c->pos, c->from) || c->found)
        return 0;
    if (c->req->bbs_floor > 0) {
        // By its floor: still the comment the moderator saw.
        bof_comment_t pc;
        if (!parse_comment_line(line, &pc, NULL, NULL) ||
            ++c->floor != c->req->bbs_floor)
            return 0;
        match = !pc.removed && pc.bbs_vote == c->req->bbs_vote &&
                strcasecmp(pc.creator, c->req->bbs_author) == 0 &&
                strcmp(pc.content, c->req->bbs_content) == 0;
    } else {
        for (i = 0; !match && i < c->npatterns; i++)
            match = strncmp(line, c->patterns[i], c->lens[i]) == 0 &&
                    (line[c->lens[i]] == ' ' || line[c->lens[i]] == ESC_CHR);
    }
    if (!match)
        return 0;
    // The reason is short (the UI asks for one of MODLOG_REASON_LEN).
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
    remove_ctx c = { .npatterns = 4 };
    int type, i, matched;
    bof_error_t err;

    if (!post->bbs_dir || !fh || !fh->filename[0] || !req->bbs_author ||
        !req->bbs_author[0] || !req->bbs_content || !actor || !actor->name)
        return BOF_ERR_INVALID_BODY_FIELD;
    type = req->bbs_vote > 0 ? RECTYPE_GOOD :
           req->bbs_vote < 0 ? RECTYPE_BAD : RECTYPE_ARROW;
    c.req = req;
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
    c.from = comments_offset_of(path);
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

/* ------------------------------------------------------------------------
 * Reading and editing comments in the file of a post
 * ------------------------------------------------------------------------ */

// The marks a comment line starts with, by type (see
// bof_files_format_comment()), up to the color of the author.
static int
comment_prefix_of(int type, char *buf, size_t sz)
{
    static const char *const kinds[] = {
        STR_COMMENT_PUSH, STR_COMMENT_BOO, STR_COMMENT_ARROW,
    };
    static const char *const attrs[] = {
        ANSI_COLOR(1;37), ANSI_COLOR(1;31), ANSI_COLOR(1;31),
    };

    snprintf(buf, sz, "%s%s " ANSI_COLOR(33), attrs[type], kinds[type]);
    return 1;
}

// Copies the n bytes at s into dst (sz bytes), without trailing spaces.
static void
copy_trimmed(char *dst, size_t sz, const char *s, size_t n)
{
    while (n > 0 && s[n - 1] == ' ')
        n--;
    if (n >= sz)
        n = sz - 1;
    memcpy(dst, s, n);
    dst[n] = '\0';
}

// Where the date (MM/DD) is in the tail s of a comment line, or NULL.
static const char *
tail_date(const char *s)
{
    for (; *s && *s != '\n'; s++)
        if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
            s[2] == '/' && isdigit((unsigned char)s[3]) &&
            isdigit((unsigned char)s[4]))
            return s;
    return NULL;
}

// Parses a line of the file of a post. Returns 1 for a comment, 2 for the
// note left of a removed one (both filling c, but its floor), or 0. For a
// comment, *coff and *clen (if not NULL) tell where its content (with the
// padding) is in line.
static int
parse_comment_line(const char *line, bof_comment_t *c, size_t *coff,
                   size_t *clen)
{
    char prefix[64];
    const char *p, *e, *end = line + strcspn(line, "\n");

    memset(c, 0, sizeof(*c));

    // The note of a removed comment: "(remover BY creator OF reason)".
    p = ANSI_COLOR(1;30) "(";
    if (strncmp(line, p, strlen(p)) == 0) {
        const char *by = strstr(line, STR_COMMENT_REMOVED_BY);
        const char *of = by ? strstr(by, STR_COMMENT_REMOVED_OF) : NULL;
        if (!of || of > end)
            return 0;
        by += strlen(STR_COMMENT_REMOVED_BY);
        copy_trimmed(c->creator, sizeof(c->creator), by, of - by);
        c->removed = 1;
        return 2;
    }

    for (int type = 0; type < RECTYPE_SIZE; type++) {
        if (!comment_prefix_of(type, prefix, sizeof(prefix)) ||
            strncmp(line, prefix, strlen(prefix)) != 0)
            continue;
        p = line + strlen(prefix);
        // The author, then its color ends and the content's starts.
        if (!(e = strstr(p, ANSI_RESET ANSI_COLOR(33) ":")) || e > end)
            return 0;
        copy_trimmed(c->creator, sizeof(c->creator), p, e - p);
        p = e + strlen(ANSI_RESET ANSI_COLOR(33) ":");
        // Older comments may have no space after the colon.
        if (*p == ' ')
            p++;
        if (!(e = strstr(p, ANSI_RESET)) || e > end)
            return 0;
        copy_trimmed(c->content, sizeof(c->content), p, e - p);
        c->bbs_width = (int)(e - p);
        if (coff)
            *coff = p - line;
        if (clen)
            *clen = e - p;
        e += strlen(ANSI_RESET);
        copy_trimmed(c->bbs_tail, sizeof(c->bbs_tail), e,
                     e < end ? (size_t)(end - e) : 0);
        // An edited comment has the mark before its date.
        const char *date = tail_date(e);
        c->bbs_edited = date && date > e && date[-1] == BOF_COMMENT_EDITED_MARK;
        c->bbs_vote = type == RECTYPE_GOOD ? 1 : type == RECTYPE_BAD ? -1 : 0;
        return 1;
    }
    return 0;
}

bof_error_t
bof_fs_get_comments(void *ctx GCC_UNUSED, const bof_get_comments_t *req,
                    bof_get_comments_response_t *out)
{
    const fileheader_t *fh = req->post.bbs_header;
    char path[PATHLEN], line[ANSILINELEN];
    bof_comment_t c;
    FILE *fp;
    int floor = 0, skip;

    out->count = 0;
    if (!req->post.bbs_dir || !fh || !fh->filename[0] || !out->comments ||
        req->limit <= 0 || req->page < 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    // The pages before.
    skip = req->page > 1 ? (req->page - 1) * req->limit : 0;
    setdirpath(path, req->post.bbs_dir, fh->filename);
    if (!(fp = fopen(path, "r")))
        return BOF_ERR_COULDNT_FIND_POST;
    if (fseeko(fp, comments_offset(fp), SEEK_SET) != 0) {
        fclose(fp);
        return BOF_ERR_COULDNT_FIND_POST;
    }
    while (out->count < req->limit && fgets(line, sizeof(line), fp)) {
        if (!parse_comment_line(line, &c, NULL, NULL))
            continue;
        c.bbs_floor = ++floor;
        if (req->bbs_creator && strcasecmp(c.creator, req->bbs_creator) != 0)
            continue;
        if (skip > 0) {
            skip--;
            continue;
        }
        out->comments[out->count++] = c;
    }
    fclose(fp);
    return BOF_OK;
}

// Finds the line of the comment at a floor and replaces its content.
typedef struct {
    const bof_edit_comment_t *req;
    int floor;
    off_t from, pos;    // where the comments begin; where the line is
    int mismatch;
    int too_long;   // the new content does not fit in the room of the old
} edit_ctx;

static int
edit_line(const char *line, FILE *out, void *vctx)
{
    edit_ctx *x = (edit_ctx *)vctx;
    bof_comment_t c;
    size_t coff, clen, n;

    if (!in_comments(line, &x->pos, x->from) ||
        !parse_comment_line(line, &c, &coff, &clen) ||
        ++x->floor != x->req->bbs_floor)
        return 0;
    if (c.removed || strcasecmp(c.creator, x->req->bbs_creator) != 0 ||
        strcmp(c.content, x->req->bbs_old_content) != 0) {
        x->mismatch = 1;
        return 0;
    }
    if (strlen(x->req->content) > clen) {
        x->too_long = 1;
        return 0;
    }
    // The new content in the room of the old, so what follows stays, but
    // with the mark of an edit before the date (in place of the space
    // before it, if there is one).
    const char *rest = line + coff + clen, *date = tail_date(rest);
    n = strlen(x->req->content);
    fwrite(line, 1, coff, out);
    fputs(x->req->content, out);
    for (; n < clen; n++)
        fputc(' ', out);
    if (!date || (date > rest && date[-1] == BOF_COMMENT_EDITED_MARK)) {
        fputs(rest, out);
    } else {
        size_t keep = (size_t)(date - rest);
        if (keep > 0 && date[-1] == ' ')
            keep--;
        fwrite(rest, 1, keep, out);
        fputc(BOF_COMMENT_EDITED_MARK, out);
        fputs(date, out);
    }
    return 1;
}

bof_error_t
bof_fs_edit_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_edit_comment_t *req, bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    char path[PATHLEN], rev_note[ANSILINELEN * 2];
    time4_t now = time4(NULL);
    edit_ctx x = { .req = req };
    int matched;
    bof_error_t err;

    // bof_edit_comment() has checked the input (one line of plain text
    // that fits) and that the actor is the creator.
    if (!post->bbs_dir || !fh || !fh->filename[0] || req->bbs_floor <= 0 ||
        !req->bbs_creator || !req->bbs_old_content || !req->content ||
        !actor || !actor->name)
        return BOF_ERR_INVALID_BODY_FIELD;

    setdirpath(path, post->bbs_dir, fh->filename);
    x.from = comments_offset_of(path);
    snprintf(rev_note, sizeof(rev_note),
             "\n" STR_POST_MARK STR_COMMENT_EDITED_NOTE, Cdatelite(&now),
             actor->name, req->bbs_creator, req->bbs_old_content);
    err = bof_files_rewrite_post(path, edit_line, &x, NULL, rev_note,
                                 &matched);
    if (err != BOF_OK)
        return err;
    if (!matched)
        return x.too_long ? BOF_ERR_INVALID_BODY_FIELD :
            x.mismatch ? BOF_ERR_COULDNT_UPDATE_COMMENT :
            BOF_ERR_COULDNT_FIND_COMMENT;

    // The post changed: readers may see it again.
    fileheader_t updated = *fh;
    if (post->bbs_index > 0) {
        updated.modified = dasht4(path);
        if (updated.modified == 0 ||
            bof_files_modify_record(post->bbs_dir, post->bbs_index,
                                    fh->filename, updated.modified, NULL,
                                    NULL, NULL, 0, 0, NULL, 0, 0,
                                    &updated) != 0)
            return BOF_ERR_COULDNT_UPDATE_POST;  // edited, not in the list
    }
    if (out) {
        if (out->bbs_header)
            *out->bbs_header = updated;
        out->bbs_file_size = dashs(path);
    }
    return BOF_OK;
}
