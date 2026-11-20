/*
 * post.c - bof (Birds Of Feather), the files backend: posts kept in the
 * files of a board.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * The calls of bof.h on posts, done with the records and files of a board
 * (files/record.c, files/content.c) and the time capsule.
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

    if (keep_record &&
        bof_files_tombstone(post->bbs_dir, post->bbs_index, fh, title,
                            deleter) == 0)
        out->bbs_record_kept = 1;
    else if (delete_fileheader(post->bbs_dir, fh, post->bbs_index) != 0)
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
    const bof_post_ref_t *post = &req->post;
    int32_t modified = req->bbs_modified;

    if (out)
        out->bbs_body_saved = 0;
    if (req->bbs_body_path) {
        char path[PATHLEN], note[STRLEN];
        time4_t oldmt;
        int saved = 0;
        bof_error_t err;

        if (!req->bbs_body_version || !post->bbs_dir || !post->bbs_header ||
            !post->bbs_header->filename[0])
            return BOF_ERR_INVALID_BODY_FIELD;
        setdirpath(path, post->bbs_dir, post->bbs_header->filename);
        // The revision kept tells when the content it has was made.
        oldmt = dasht4(path);
        snprintf(note, sizeof(note), "\n" STR_POST_MARK "Last modified: %s",
                 Cdatelite(&oldmt));
        err = bof_files_replace_post(path, req->bbs_body_path,
                                     req->bbs_body_version,
                                     !req->bbs_no_revision, note, &saved);
        if (out)
            out->bbs_body_saved = saved;
        if (!saved)
            return err;
        if (!modified)
            modified = dasht4(path);
    }
    return change_record(post, modified, req->name,
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
bof_fs_bbs_reset_post_score(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                            const bof_bbs_reset_post_score_t *req,
                            bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    fileheader_t updated;
    char path[PATHLEN];
    time4_t now = time4(NULL);

    if (!valid_ref(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (bof_files_update_record(post->bbs_dir, post->bbs_index,
                                post->bbs_header->filename, reset_score,
                                NULL, &updated) != 0)
        return BOF_ERR_COULDNT_FIND_POST;
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    // Readers of the post see who did it.
    setdirpath(path, post->bbs_dir, post->bbs_header->filename);
    if (dashf(path))
        file_appendf(path, STR_REF_MARK STR_SCORE_RESET_NOTE,
                     actor && actor->name ? actor->name : "",
                     Cdatelite(&now));
    return BOF_OK;
}

// The directory of a list (bbs_dir without its file name), into dir.
static int
list_directory(const char *bbs_dir, char *dir, size_t sz)
{
    const char *slash = strrchr(bbs_dir, '/');
    size_t n = slash ? (size_t)(slash - bbs_dir) : 0;

    if (!n || n >= sz)
        return -1;
    memcpy(dir, bbs_dir, n);
    dir[n] = '\0';
    return 0;
}

bof_error_t
bof_fs_create_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                   const bof_create_post_t *req, bof_post_response_t *out)
{
    fileheader_t fh;
    char path[PATHLEN], tmp[PATHLEN];
    int named = 0, placed = 0;

    if (!req->bbs_dir || !req->bbs_header ||
        (req->bbs_content_path && req->body))
        return BOF_ERR_INVALID_BODY_FIELD;
    fh = *req->bbs_header;

    // Named here, in the directory of the list.
    if (!fh.filename[0]) {
        if (list_directory(req->bbs_dir, path, sizeof(path)) != 0 ||
            stampfile_u(path, &fh) != 0)
            return BOF_ERR_COULDNT_CREATE_POST;
        named = 1;
    } else {
        if (strchr(fh.filename, '/'))
            return BOF_ERR_INVALID_BODY_FIELD;
        setdirpath(path, req->bbs_dir, fh.filename);
    }

    // The content, written aside and moved into place at once.
    if (req->bbs_content_path || req->body) {
        snprintf(tmp, sizeof(tmp), "%s.new%d", path, (int)getpid());
        if (req->body ?
            bof_files_write_post(tmp, fh.owner, req->bbs_nickname,
                                 req->community, fh.title, time4(NULL),
                                 req->body, actor ? actor->from : NULL) :
            Copy(req->bbs_content_path, tmp)) {
            unlink(tmp);
            if (named)
                unlink(path);
            return BOF_ERR_COULDNT_CREATE_POST;
        }
        if (Rename(tmp, path) != 0) {
            unlink(tmp);
            if (named)
                unlink(path);
            return BOF_ERR_COULDNT_CREATE_POST;
        }
        placed = 1;
        // A post given whole (body) gets the time of its file; a
        // caller that wrote the file keeps the time it set in the
        // record (none for anonymous posts).
        if (req->body)
            fh.modified = dasht4(path);
    }

    if (append_fileheader(req->bbs_dir, &fh) == -1) {
        if (named || placed)
            unlink(path);
        return BOF_ERR_COULDNT_CREATE_POST;
    }
    if (out && out->bbs_header)
        *out->bbs_header = fh;
    return BOF_OK;
}

bof_error_t
bof_fs_purge_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                   const bof_purge_posts_t *req, int *count)
{
    int n;

    *count = 0;
    if (!req->bbs_dir || !req->bbs_selected)
        return BOF_ERR_INVALID_BODY_FIELD;
    // The records go and their files are unlinked, unarchived.
    n = bof_files_prune(req->bbs_dir, 0, 0, req->bbs_selected, NULL,
                        req->bbs_ctx, actor ? actor->name : NULL);
    if (n < 0)
        return BOF_ERR_COULDNT_UPDATE_POST;
    *count = n;
    return BOF_OK;
}

bof_error_t
bof_fs_remove_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_remove_posts_t *req, int *count)
{
    const char *deleter = actor ? actor->name : NULL;
    fileheader_t fh;
    int fd, index, n;

    *count = 0;
    if (!req->bbs_dir || !req->bbs_selected)
        return BOF_ERR_INVALID_BODY_FIELD;

    // First archive the content of each, while the records are all there;
    // then drop the records (or leave tombstones) at once.
    if ((fd = open(req->bbs_dir, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    for (index = 1; read_fileheader(fd, &fh) == 1; index++) {
        if ((fh.filemode & req->bbs_keep_modes) ||
            !req->bbs_selected(&fh, index, req->bbs_ctx))
            continue;
        // The reason is in the modlog, not in the archived posts.
        bof_files_delete_content(req->bbs_dir, &fh, req->bbs_dir, NULL, 0,
                                 NULL, deleter,
                                 actor ? actor->from : NULL);
    }
    close(fd);

    n = bof_files_prune(req->bbs_dir, req->bbs_keep_modes,
                        req->bbs_keep_record, req->bbs_selected,
                        req->bbs_removed, req->bbs_ctx, deleter);
    if (n < 0)
        return BOF_ERR_COULDNT_UPDATE_POST;
    *count = n;
    return BOF_OK;
}

// Inserts fh into direct in the order of the times in the file names,
// before the first record of a later time, under the lock of the list.
// Returns 0, or -1.
static int
insert_record(const char *direct, const fileheader_t *fh)
{
    char newfn[PATHLEN], lockfn[PATHLEN];
    fileheader_t rec, stored = *fh;
    int fdr, fdw, fd, done = 0, ok = 1;
    int stamp = get_fhdr_stamp_ts(fh->filename);

    // The records are copied as stored; this one is converted to that.
    fileheader_mem_to_storage(&stored);
    snprintf(newfn, sizeof(newfn), "%s.ins", direct);
    snprintf(lockfn, sizeof(lockfn), "%s.lock", direct);
    if ((fd = OpenCreate(lockfn, O_RDWR | O_APPEND)) == -1)
        return -1;
    flock(fd, LOCK_EX);
    fdr = open(direct, O_RDONLY);
    if ((fdw = OpenCreate(newfn, O_WRONLY | O_TRUNC)) == -1) {
        if (fdr >= 0)
            close(fdr);
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    while (fdr >= 0 && ok &&
           read(fdr, &rec, sizeof(rec)) == (ssize_t)sizeof(rec)) {
        if (!done && rec.filename[0] &&
            time4_gt(get_fhdr_stamp_ts(rec.filename), stamp)) {
            ok = write(fdw, &stored, sizeof(stored)) == (ssize_t)sizeof(stored);
            done = 1;
        }
        ok = ok && write(fdw, &rec, sizeof(rec)) == (ssize_t)sizeof(rec);
    }
    if (ok && !done)
        ok = write(fdw, &stored, sizeof(stored)) == (ssize_t)sizeof(stored);
    if (fdr >= 0)
        close(fdr);
    close(fdw);
    if (!ok || Rename(newfn, direct) != 0) {
        unlink(newfn);
        ok = 0;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return ok ? 0 : -1;
}

bof_error_t
bof_fs_restore_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_restore_post_t *req, bof_post_response_t *out)
{
    const fileheader_t *ar = req->bbs_archived;
    char path[PATHLEN], revpath[PATHLEN], tmp[PATHLEN];
    fileheader_t restored, rec;
    time4_t now = time4(NULL);
    int maxrev, i;

    if (!req->bbs_dir || !ar || !ar->filename[0] || ar->filename[0] == '.')
        return BOF_ERR_INVALID_BODY_FIELD;
    setdirpath(path, req->bbs_dir, ar->filename);
    // Not deleted, or restored already.
    if (dashf(path))
        return BOF_ERR_COULDNT_UPDATE_POST;
    if ((maxrev = timecapsule_get_max_revision_number(path)) < 1 ||
        !timecapsule_get_by_revision(path, maxrev, revpath, sizeof(revpath)) ||
        !dashf(revpath))
        return BOF_ERR_COULDNT_FIND_POST;
    // Made aside and linked into place, which fails if something (another
    // restore) put the post back meanwhile.
    snprintf(tmp, sizeof(tmp), "%s.rs%d", path, (int)getpid());
    if (Copy(revpath, tmp) < 0) {
        unlink(tmp);
        return BOF_ERR_COULDNT_UPDATE_POST;
    }
    file_appendf(tmp, "\n" STR_POST_MARK "Restored by: %s (%s) %s\n",
                 actor && actor->name ? actor->name : "",
                 actor && actor->from ? actor->from : "", Cdatelite(&now));
    i = link(tmp, path);
    unlink(tmp);
    if (i != 0)
        return BOF_ERR_COULDNT_UPDATE_POST;

    // The archive keeps the author, date, title and file name.
    memset(&restored, 0, sizeof(restored));
    strlcpy(restored.filename, ar->filename, sizeof(restored.filename));
    strlcpy(restored.owner, ar->owner, sizeof(restored.owner));
    strlcpy(restored.date, ar->date, sizeof(restored.date));
    strlcpy(restored.title, ar->title, sizeof(restored.title));
    restored.modified = dasht4(path);

    // In place of its record, if only the file was gone, or else into the
    // list in the order of time (a tombstone, FN_SAFEDEL, stays).
    if ((i = search_dir_by_filename(req->bbs_dir, restored.filename,
                                    &rec)) > 0) {
        if (substitute_fileheader(req->bbs_dir, &rec, &restored, i) != 0) {
            unlink(path);
            return BOF_ERR_COULDNT_UPDATE_POST;
        }
    } else if (insert_record(req->bbs_dir, &restored) != 0) {
        unlink(path);
        return BOF_ERR_COULDNT_UPDATE_POST;
    }
    if (out && out->bbs_header)
        *out->bbs_header = restored;
    return BOF_OK;
}

/* ------------------------------------------------------------------------
 * The time capsule: versions of posts and the recycle bin
 * ------------------------------------------------------------------------ */

bof_error_t
bof_fs_get_post_revisions(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                          int *count)
{
    char path[PATHLEN];
    int n;

    setdirpath(path, post->bbs_dir, post->bbs_header->filename);
    n = timecapsule_get_max_revision_number(path);
    *count = n > 0 ? n : 0;
    return BOF_OK;
}

bof_error_t
bof_fs_get_post_revision_file(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                              int rev, char *path, size_t sz)
{
    char base[PATHLEN];

    setdirpath(base, post->bbs_dir, post->bbs_header->filename);
    if (!timecapsule_get_by_revision(base, rev, path, sz) || !dashf(path))
        return BOF_ERR_COULDNT_FIND_POST;
    return BOF_OK;
}

bof_error_t
bof_fs_list_removed_posts(void *ctx GCC_UNUSED,
                          const bof_bbs_list_removed_posts_t *req,
                          bof_bbs_list_removed_posts_response_t *out)
{
    int total = timecapsule_get_max_archive_number(req->bbs_dir,
                                                   sizeof(fileheader_t));
    int n;

    out->bbs_total = total > 0 ? total : 0;
    n = out->bbs_total - req->start;
    if (n > req->limit)
        n = req->limit;
    if (n <= 0)
        return BOF_OK;
    // All of them, or none (it returns whether it could).
    if (timecapsule_get_archive_blobs(req->bbs_dir, req->start, n,
                                      out->posts, sizeof(fileheader_t))) {
        fileheader_storage_to_mem(out->posts, n);
        out->count = n;
    }
    return BOF_OK;
}
