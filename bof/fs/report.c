/*
 * report.c - bof (Birds Of Feather), the files backend: reports of posts
 * and comments to the moderators of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The reports of a board are records of a fixed size (bof_report_t) in
 * FN_REPORTS of the board directory, in the order made; a report's ID is
 * its position. New ones are appended and resolving one rewrites it in
 * place, both under the lock of the file. Reports against the rules of
 * the site are also listed (by board and ID) in FN_SITE_REPORTS, for the
 * site's admins.
 */

#include "../bof_int.h"
#include "bof_fs.h"

_Static_assert(sizeof(bof_report_t) == 592, "the size of a report on disk");

// An entry of the site's list of reports.
typedef struct {
    char community[16];
    int32_t id;
    int32_t published;
} site_report_t;

static void
reports_path(const char *community, char *path)
{
    setbfile(path, community, FN_REPORTS);
}

// Opens the reports of community, locked; -1 if it cannot.
static int
open_reports(const char *community, int flags)
{
    char path[PATHLEN];
    int fd;

    reports_path(community, path);
    if ((fd = (flags & O_CREAT) ? OpenCreate(path, flags & ~O_CREAT) :
                                  open(path, flags)) < 0)
        return -1;
    if (flock(fd, (flags & O_ACCMODE) == O_RDONLY ? LOCK_SH : LOCK_EX) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void
close_reports(int fd)
{
    flock(fd, LOCK_UN);
    close(fd);
}

static int
read_report(int fd, int32_t id, bof_report_t *r)
{
    return pread(fd, r, sizeof(*r), (off_t)(id - 1) * sizeof(*r)) ==
        (ssize_t)sizeof(*r);
}

bof_error_t
bof_fs_create_report(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                     const bof_create_report_t *req, bof_report_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    char path[PATHLEN];
    bof_report_t r, old;
    struct stat st;
    int fd, src;
    int32_t n;

    if (!post->community || !post->bbs_dir || !fh || !fh->filename[0] ||
        !actor || !actor->name || !*actor->name || !req->reason ||
        !*req->reason || req->bbs_floor < 0 ||
        (req->bbs_floor > 0 && !req->bbs_comment_text))
        return BOF_ERR_INVALID_BODY_FIELD;

    memset(&r, 0, sizeof(r));
    r.type = req->bbs_floor > 0 ? BOF_REPORT_COMMENT : BOF_REPORT_POST;
    r.published = time4(NULL);
    r.bbs_floor = req->bbs_floor;
    r.bbs_site_rule = req->bbs_site_rule ? 1 : 0;
    strlcpy(r.creator, actor->name, sizeof(r.creator));
    strlcpy(r.bbs_post_file, fh->filename, sizeof(r.bbs_post_file));
    strlcpy(r.original_post_name, fh->title, sizeof(r.original_post_name));
    if (req->bbs_comment_text)
        strlcpy(r.original_comment_text, req->bbs_comment_text,
                sizeof(r.original_comment_text));
    strlcpy(r.reason, req->reason, sizeof(r.reason));
    strlcpy(r.bbs_community, post->community, sizeof(r.bbs_community));

    // The post as reported, by the version of its content.
    setdirpath(path, post->bbs_dir, fh->filename);
    if ((src = open(path, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_POST;
    bof_post_version_t ver = { 0 };
    if (fstat(src, &st) == 0)
        bof_files_get_version(src, st.st_size, &ver);
    close(src);
    r.bbs_size = ver.size;
    r.bbs_hash = ver.hash;

    if ((fd = open_reports(post->community, O_RDWR | O_CREAT)) < 0)
        return BOF_ERR_COULDNT_CREATE_REPORT;
    if (fstat(fd, &st) != 0) {
        close_reports(fd);
        return BOF_ERR_COULDNT_CREATE_REPORT;
    }
    n = (int32_t)(st.st_size / sizeof(r));
    // One open report per person per post or comment, and only so many
    // in a while.
    int recent = 0;
    for (int32_t id = 1; id <= n; id++) {
        if (!read_report(fd, id, &old) || strcasecmp(old.creator, r.creator))
            continue;
        if (!old.resolved && old.type == r.type &&
            old.bbs_floor == r.bbs_floor &&
            strcmp(old.bbs_post_file, r.bbs_post_file) == 0) {
            close_reports(fd);
            return BOF_ERR_BBS_ALREADY_REPORTED;
        }
        if (r.published - old.published < BOF_REPORT_RATE_WINDOW)
            recent++;
    }
    if (recent >= BOF_REPORT_RATE_MAX) {
        close_reports(fd);
        return BOF_ERR_RATE_LIMITED;
    }
    r.id = n + 1;
    if (pwrite(fd, &r, sizeof(r), (off_t)n * sizeof(r)) !=
        (ssize_t)sizeof(r)) {
        close_reports(fd);
        return BOF_ERR_COULDNT_CREATE_REPORT;
    }
    close_reports(fd);

    if (r.bbs_site_rule) {
        site_report_t e = { .id = r.id, .published = r.published };
        strlcpy(e.community, r.bbs_community, sizeof(e.community));
        if ((fd = OpenCreate(FN_SITE_REPORTS, O_WRONLY | O_APPEND)) >= 0) {
            flock(fd, LOCK_EX);
            if (write(fd, &e, sizeof(e)) != (ssize_t)sizeof(e))
                ;  // the report is still on its board
            flock(fd, LOCK_UN);
            close(fd);
        }
    }
    if (out)
        *out = r;
    return BOF_OK;
}

// The site's reports, newest first (see bof_list_reports()).
static bof_error_t
list_site_reports(const bof_list_reports_t *req,
                  bof_list_reports_response_t *out)
{
    site_report_t e;
    bof_report_t r;
    struct stat st;
    int fd, bfd;

    if ((fd = open(FN_SITE_REPORTS, O_RDONLY)) < 0)
        return BOF_OK;
    flock(fd, LOCK_SH);
    if (fstat(fd, &st) == 0) {
        for (off_t i = st.st_size / sizeof(e); i > 0; i--) {
            if (pread(fd, &e, sizeof(e), (i - 1) * sizeof(e)) !=
                (ssize_t)sizeof(e))
                continue;
            e.community[sizeof(e.community) - 1] = '\0';
            if ((bfd = open_reports(e.community, O_RDONLY)) < 0)
                continue;
            int ok = read_report(bfd, e.id, &r) && r.id == e.id;
            close_reports(bfd);
            if (!ok)
                continue;
            if (!r.resolved)
                out->bbs_unresolved++;
            if ((req->unresolved_only && r.resolved) ||
                out->count >= req->limit)
                continue;
            out->reports[out->count++] = r;
        }
    }
    flock(fd, LOCK_UN);
    close(fd);
    return BOF_OK;
}

bof_error_t
bof_fs_list_reports(void *ctx GCC_UNUSED, const bof_list_reports_t *req,
                    bof_list_reports_response_t *out)
{
    bof_report_t r;
    struct stat st;
    int fd;

    out->count = 0;
    out->bbs_unresolved = 0;
    if (!out->reports || req->limit <= 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!req->community)
        return list_site_reports(req, out);
    if ((fd = open_reports(req->community, O_RDONLY)) < 0)
        return BOF_OK;  // none yet
    if (fstat(fd, &st) == 0) {
        for (int32_t id = (int32_t)(st.st_size / sizeof(r)); id > 0; id--) {
            if (!read_report(fd, id, &r))
                continue;
            if (!r.resolved)
                out->bbs_unresolved++;
            if ((req->unresolved_only && r.resolved) ||
                out->count >= req->limit)
                continue;
            out->reports[out->count++] = r;
        }
    }
    close_reports(fd);
    return BOF_OK;
}

bof_error_t
bof_fs_resolve_report(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                      const bof_resolve_report_t *req, bof_report_t *out)
{
    bof_report_t r;
    int fd;

    if (!req->community || req->report_id <= 0 || !actor || !actor->name)
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((fd = open_reports(req->community, O_RDWR)) < 0)
        return BOF_ERR_COULDNT_RESOLVE_REPORT;
    if (!read_report(fd, req->report_id, &r) || r.id != req->report_id) {
        close_reports(fd);
        return BOF_ERR_COULDNT_RESOLVE_REPORT;
    }
    r.resolved = req->resolved ? 1 : 0;
    r.updated = time4(NULL);
    strlcpy(r.resolver, req->resolved ? actor->name : "", sizeof(r.resolver));
    if (pwrite(fd, &r, sizeof(r), (off_t)(r.id - 1) * sizeof(r)) !=
        (ssize_t)sizeof(r)) {
        close_reports(fd);
        return BOF_ERR_COULDNT_RESOLVE_REPORT;
    }
    close_reports(fd);
    if (out)
        *out = r;
    return BOF_OK;
}

int
bof_report_matches_post(const char *bbs_dir, const bof_report_t *r)
{
    char path[PATHLEN];
    bof_post_version_t ver = { .size = r->bbs_size, .hash = r->bbs_hash };

    setdirpath(path, bbs_dir, r->bbs_post_file);
    return bof_files_version_matches(path, &ver);
}
