/*
 * content.c - bof (Birds Of Feather), the files of a BBS: the content
 * of posts.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The file of a post is its body followed by lines appended later
 * (comments, notes of crossposts or deletion). Lines are appended in
 * place (O_APPEND, under flock while the file may be being replaced); any
 * other change replaces the file: the new content is written to another
 * file, the old one is kept in the time capsule, and what was appended to
 * the old one since it was read is moved over before the rename. The
 * version of the content read (its first bytes) is checked under the lock,
 * so a change made meanwhile is never overwritten.
 */

#include "../bof_int.h"

// The FNV-64 hash of the first size bytes of fd (from offset 0), into
// *hash. Returns 0, or -1 if fd is shorter or cannot be read.
static int
hash_fd(int fd, int64_t size, uint64_t *hash)
{
    unsigned char buf[4096];
    Fnv64_t h = FNV1_64_INIT;
    off_t off = 0;

    while (off < size) {
        size_t want = (size - off) < (int64_t)sizeof(buf) ?
            (size_t)(size - off) : sizeof(buf);
        ssize_t n = pread(fd, buf, want, off);
        if (n <= 0)
            return -1;
        h = fnv_64_buf(buf, (size_t)n, h);
        off += n;
    }
    *hash = h;
    return 0;
}

int
bof_files_get_version(int fd, int64_t size, bof_post_version_t *ver)
{
    if (size < 0)
        return -1;
    ver->size = size;
    return hash_fd(fd, size, &ver->hash);
}

int
bof_files_version_matches(const char *path, const bof_post_version_t *ver)
{
    uint64_t hash;
    int fd = open(path, O_RDONLY), ok;

    if (fd < 0)
        return 0;
    ok = hash_fd(fd, ver->size, &hash) == 0 && hash == ver->hash;
    close(fd);
    return ok;
}

// Appends what src has from offset to the end onto fd_dest. Returns 0 or
// -1.
static int
copy_tail(int fd_src, off_t offset, int fd_dest)
{
    char buf[4096];
    ssize_t n;

    if (lseek(fd_src, offset, SEEK_SET) != offset)
        return -1;
    while ((n = read(fd_src, buf, sizeof(buf))) > 0)
        if (write(fd_dest, buf, n) != n)
            return -1;
    return n == 0 ? 0 : -1;
}

int
bof_files_replace_post(const char *path, const char *new_path,
                       const bof_post_version_t *ver, int keep_revision,
                       const char *rev_note, int *saved)
{
    struct stat st_pre, st_src, st_now;
    uint64_t hash;
    bof_error_t err = BOF_OK;
    int fd_src, fd_dest, rev = 0;

    *saved = 0;
    if ((fd_src = open(path, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if ((fd_dest = open(new_path, O_APPEND | O_WRONLY)) < 0) {
        close(fd_src);
        return errno == EROFS ? BOF_ERR_BBS_READ_ONLY : BOF_ERR_UNKNOWN;
    }

    // The content is checked before the lock, which comments wait for:
    // a post is only appended to (comments, notes) or replaced by a new
    // file (as here), so what was checked stays as long as it is the same
    // file, no shorter, under the lock. Under the lock nothing is appended
    // (comments lock it too), so what gets replaced is what was checked,
    // with what follows it.
    if (fstat(fd_src, &st_pre) != 0 || st_pre.st_size < (off_t)ver->size ||
        hash_fd(fd_src, ver->size, &hash) != 0 || hash != ver->hash) {
        err = BOF_ERR_COULDNT_UPDATE_POST;
        goto out_unlocked;
    }
    flock(fd_src, LOCK_EX);
    if (fstat(fd_src, &st_src) != 0 || st_src.st_dev != st_pre.st_dev ||
        st_src.st_ino != st_pre.st_ino || st_src.st_size < (off_t)ver->size) {
        err = BOF_ERR_COULDNT_UPDATE_POST;
        goto out;
    }

    // What was appended since (comments) is moved over with smart merge;
    // without it the post must not have grown, as nothing may be lost.
    if (IS_ENABLED(CONFIG_EDITPOST_SMARTMERGE) ?
        copy_tail(fd_src, (off_t)ver->size, fd_dest) != 0 :
        st_src.st_size != (off_t)ver->size) {
        err = BOF_ERR_COULDNT_UPDATE_POST;
        goto out;
    }
    close(fd_dest);
    fd_dest = -1;

    // Rename() replaces the post at once, so readers never see it missing;
    // but only while it is still the file checked: one deleted or replaced
    // meanwhile must not be brought back or overwritten.
    if (stat(path, &st_now) != 0 || st_now.st_dev != st_src.st_dev ||
        st_now.st_ino != st_src.st_ino) {
        err = BOF_ERR_COULDNT_UPDATE_POST;
        goto out;
    }

    // The revision is a hard link of the post: its note is appended once
    // the post is a new file.
    if (keep_revision)
        rev = timecapsule_add_revision(path);
    if (Rename(new_path, path) != 0) {
        // Rename() may have moved it by copying and failed after.
        err = BOF_ERR_COULDNT_UPDATE_POST;
        *saved = !dashf(new_path);
        goto out;
    }
    *saved = 1;
    if (rev > 0 && rev_note && *rev_note) {
        char revfn[PATHLEN];
        if (timecapsule_get_by_revision(path, rev, revfn, sizeof(revfn)) &&
            dashf(revfn))
            file_append(revfn, rev_note);
    }

out:
    flock(fd_src, LOCK_UN);
out_unlocked:
    if (fd_dest >= 0)
        close(fd_dest);
    close(fd_src);
    return *saved ? BOF_OK : err;
}

int
bof_files_rewrite_post(const char *path,
                       int (*filter)(const char *line, FILE *out, void *ctx),
                       void *ctx, const char *tail, const char *rev_note,
                       int *matched)
{
    char tmp[PATHLEN], line[ANSILINELEN];
    bof_post_version_t ver;
    FILE *in, *out;
    off_t size;
    int bad, saved = 0, err;

    *matched = 0;
    if (!(in = fopen(path, "r")))
        return BOF_ERR_COULDNT_FIND_POST;
    snprintf(tmp, sizeof(tmp), "%s.rw%d", path, (int)getpid());
    if (!(out = fopen(tmp, "w"))) {
        int rofs = errno == EROFS;
        fclose(in);
        return rofs ? BOF_ERR_BBS_READ_ONLY : BOF_ERR_UNKNOWN;
    }
    while (fgets(line, sizeof(line), in)) {
        if (filter(line, out, ctx))
            (*matched)++;
        else
            fputs(line, out);
    }
    // The version of what was read, from the file read even if another
    // has taken its place meanwhile.
    size = ferror(in) ? -1 : ftello(in);
    bad = size < 0 || bof_files_get_version(fileno(in), size, &ver) != 0;
    fclose(in);
    bad |= ferror(out);
    bad |= fclose(out) != 0;
    if (!bad && *matched && tail && *tail)
        bad = file_append(tmp, tail) != 0;
    if (bad || !*matched) {
        unlink(tmp);
        return bad ? BOF_ERR_UNKNOWN : BOF_OK;
    }
    err = bof_files_replace_post(path, tmp, &ver, 1, rev_note, &saved);
    if (!saved)
        unlink(tmp);
    return saved ? BOF_OK : err;
}

int
bof_bbs_format_post_header(char *buf, size_t sz, const char *author,
                           const char *nickname, const char *community,
                           const char *title, int32_t when)
{
    // Each line as the editor writes it, within its margin.
    char line[WRAPMARGIN];
    time4_t t = when;
    size_t len = 0;

    if (!buf || !sz)
        return -1;
    buf[0] = '\0';
    if (community)
        snprintf(line, sizeof(line), "%s %s (%s) %s %s", STR_AUTHOR1,
                 author ? author : "", nickname ? nickname : "", STR_POST1,
                 community);
    else
        snprintf(line, sizeof(line), "%s %s (%s)", STR_AUTHOR1,
                 author ? author : "", nickname ? nickname : "");
    len += snprintf(buf + len, sz > len ? sz - len : 0, "%s\n", line);
    snprintf(line, sizeof(line), STR_POST_TITLE " %s", title ? title : "");
    len += snprintf(buf + len, sz > len ? sz - len : 0, "%s\n", line);
    snprintf(line, sizeof(line), STR_POST_TIME " %s", ctime4(&t));
    len += snprintf(buf + len, sz > len ? sz - len : 0, "%s\n", line);
    return len < sz ? (int)len : -1;
}

int
bof_bbs_format_post_origin(char *buf, size_t sz, const char *from)
{
    size_t len;

    if (!buf || !sz)
        return -1;
    len = snprintf(buf, sz, STR_POST_MARK STR_POST_ORIGIN " " BBSNAME "("
                   MYHOSTNAME "), " STR_POST_FROM " %s", from ? from : "");
    return len < sz ? (int)len : -1;
}

int
bof_files_write_post(const char *path, const char *author,
                     const char *nickname, const char *community,
                     const char *title, int32_t when, const char *body,
                     const char *from)
{
    char head[WRAPMARGIN * 3 + 4], origin[WRAPMARGIN];
    size_t blen = body ? strlen(body) : 0;
    FILE *fp;
    int bad;

    if (bof_bbs_format_post_header(head, sizeof(head), author, nickname,
                                   community, title, when) < 0 ||
        bof_bbs_format_post_origin(origin, sizeof(origin), from) < 0)
        return -1;
    if (!(fp = fopen(path, "w")))
        return -1;
    // The header, a blank line, the body and the origin after "--". A
    // client may not make comment lines: COMMENT_LINE_MARK is dropped.
    fprintf(fp, "%s\n", head);
    for (size_t i = 0; i < blen; i++)
        if (body[i] != COMMENT_LINE_MARK)
            fputc(body[i], fp);
    if (blen && body[blen - 1] != '\n')
        fputc('\n', fp);
    fprintf(fp, "\n--\n%s\n", origin);
    bad = ferror(fp);
    bad |= fclose(fp) != 0;
    return bad ? -1 : 0;
}
