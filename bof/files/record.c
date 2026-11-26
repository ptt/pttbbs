/*
 * record.c - bof (Birds Of Feather), the files of a BBS: the records of
 * posts in a list and their files.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * A board keeps its posts as files (boards/<c>/<board>/M.<stamp>.A.<xxx>)
 * listed in .DIR (fileheader_t records). Deleting a post drops its record
 * or leaves a tombstone record in its place (safe delete), and moves its
 * content into the time capsule.
 */

#include "../bof_int.h"

int
bof_files_update_record(const char *direct, int ent, const char *fhdr_name,
                        void (*change)(fileheader_t *, const void *),
                        const void *ctx, fileheader_t *out)
{
    int fd;
    off_t sz = dashs(direct);
    fileheader_t fhdr;

    // prevent black holes
    if (ent <= 0 || sz < (off_t)sizeof(fileheader_t) * ent ||
        (fd = open(direct, O_RDWR)) < 0)
        return -1;

    // also check if fhdr->filename is same.
    // let sz = base offset
    sz = (sizeof(fileheader_t) * (ent - 1));
    if (lseek(fd, sz, SEEK_SET) < 0 ||
        read(fd, &fhdr, sizeof(fhdr)) != sizeof(fhdr) ||
        strcmp(fhdr.filename, fhdr_name) != 0)
    {
        close(fd);
        return -1;
    }

    change(&fhdr, ctx);

    if (lseek(fd, sz, SEEK_SET) < 0 ||
        write(fd, &fhdr, sizeof(fhdr)) != sizeof(fhdr)) {
        close(fd);
        return -1;
    }
    close(fd);
    if (out)
        *out = fhdr;
    return 0;
}

struct record_changes {
    time4_t modified;
    const char *title, *owner, *date;
    int score_delta;
    int comments_delta;
    const void *multi;
    uint16_t enable_modes, disable_modes;
};

static void
apply_record_changes(fileheader_t *fhdr, const void *ctx)
{
    const struct record_changes *c = ctx;

    if (c->modified != 0)
        fhdr->modified = c->modified;
    if (c->enable_modes)
        fhdr->filemode |= c->enable_modes;
    if (c->disable_modes)
        fhdr->filemode &= ~c->disable_modes;
    if (c->title && *c->title)
        mb_to_storage(c->title, fhdr->title, sizeof(fhdr->title));
    if (c->owner && *c->owner)
        mb_to_storage(c->owner, fhdr->owner, sizeof(fhdr->owner));
    if (c->date && *c->date)
        mb_to_storage(c->date, fhdr->date, sizeof(fhdr->date));
    if (c->multi)
        memcpy(&fhdr->multi, c->multi, sizeof(fhdr->multi));
    if (c->score_delta) {
        int score = fhdr->score + c->score_delta;
        if (score > MAX_DISPLAY_SCORE) score = MAX_DISPLAY_SCORE;
        else if (score < -MAX_DISPLAY_SCORE) score = -MAX_DISPLAY_SCORE;
        fhdr->score = score;
    }
    if (c->comments_delta) {
        int n = (int)fhdr->comments + c->comments_delta;
        fhdr->comments = n < 0 ? 0 : n > 65535 ? 65535 : n;
    }
}

int
bof_files_count_comments(const char *direct, int ent, const char *fhdr_name,
                         time4_t modified, int score_delta,
                         int comments_delta, fileheader_t *out)
{
    struct record_changes c = {
        .modified = modified, .score_delta = score_delta,
        .comments_delta = comments_delta,
    };
    return bof_files_update_record(direct, ent, fhdr_name,
                                   apply_record_changes, &c, out);
}

int
bof_files_modify_record(const char *direct, int ent, const char *fhdr_name,
                        time4_t modified, const char *title,
                        const char *owner, const char *date, int score_delta,
                        const void *multi, uint16_t enable_modes,
                        uint16_t disable_modes, fileheader_t *out)
{
    struct record_changes c = {
        .modified = modified, .title = title, .owner = owner, .date = date,
        .score_delta = score_delta, .multi = multi,
        .enable_modes = enable_modes, .disable_modes = disable_modes,
    };
    return bof_files_update_record(direct, ent, fhdr_name,
                                   apply_record_changes, &c, out);
}

void
bof_files_set_tombstone(fileheader_t *fhdr, const char *newtitle,
                        const char *deleter)
{
    char title[sizeof(fhdr->title)];

    if (newtitle && *newtitle)
    {
        snprintf(title, sizeof(title), "%s", newtitle);
    }
    else if (fhdr->filemode & FILE_ANONYMOUS ||
             !fhdr->owner[0] ||
             (fhdr->owner[0] == '-' && fhdr->owner[1] == 0))
    {
        // Not putting the owner, as we can't tell if it was deleted by a
        // moderator or by the owner.
        snprintf(title, sizeof(title), "%s", STR_SAFEDEL_TITLE);
    }
    else if (deleter && strcmp(fhdr->owner, deleter) == 0)
    {
        // Deleted by its owner.
        snprintf(title, sizeof(title), "%s [%s]",
                 STR_SAFEDEL_TITLE, fhdr->owner);
    }
    else
    {
        // Removed by a moderator, the system, a sysop or another service.
        snprintf(title, sizeof(title), "%s <%s>",
                 STR_SAFEREMOVE_TITLE, fhdr->owner);
    }
    // The fields are rewritten whole: what was after the end of the new value
    // (the old title, file name and author) is cleared, as readers that take
    // a whole field would show it.
    memset(fhdr->title, 0, sizeof(fhdr->title));
    strlcpy(fhdr->title, title, sizeof(fhdr->title));
    memset(fhdr->filename, 0, sizeof(fhdr->filename));
    strlcpy(fhdr->filename, FN_SAFEDEL, sizeof(fhdr->filename));
    memset(fhdr->owner, 0, sizeof(fhdr->owner));
    strlcpy(fhdr->owner, "-", sizeof(fhdr->owner));
    fhdr->modified = time4(NULL);
}

int
bof_files_tombstone(const char *direct, int ent, const fileheader_t *fhdr,
                    const char *newtitle, const char *deleter)
{
    fileheader_t newfhdr;
    memcpy(&newfhdr, fhdr, sizeof(fileheader_t));
    bof_files_set_tombstone(&newfhdr, newtitle, deleter);
    return substitute_fileheader(direct, fhdr, &newfhdr, ent);
}

int
bof_files_delete_content(const char *direct, const fileheader_t *fh,
                         const char *backup_direct, char *backup_path,
                         size_t sz_backup_path, const char *reason,
                         const char *deleter, const char *host)
{
    char fpath[PATHLEN];
    fileheader_t backup = { {0} };
    int backup_failed = DELETE_FILE_CONTENT_SUCCESS;
    time4_t now = time4(NULL);

    if (!is_valid_fileheader(fh) || !fh->filename[0] || !direct)
        return DELETE_FILE_CONTENT_FAILED;

    // A tombstone has no content.
    if (strcmp(fh->filename, FN_SAFEDEL) == 0)
        return DELETE_FILE_CONTENT_SUCCESS;

    if (backup_path) {
        assert(backup_direct);
        assert(sz_backup_path > 0);
        *backup_path = 0;
    }

    // solve source file name
    setdirpath(fpath, direct, fh->filename);
    if (!dashf(fpath))
        return DELETE_FILE_CONTENT_BACKUP_FAILED;

    // FIXME some announcements were made by sym-links, especial for those
    // in mail. we should ignore them.
    if (backup_direct &&
        !dashl(fpath) &&
        strncmp(fh->owner, RECYCLE_BIN_OWNER, strlen(RECYCLE_BIN_OWNER)) != 0) {

        // FIXME maybe for non-board files we should do this by simply touching
        // file instead of full log.
        if (reason && *reason)
            file_appendf(fpath, "\n" STR_POST_MARK STR_DELETE_REASON ": %s",
                         reason);
        file_appendf(fpath, "\n" STR_POST_MARK "Deleted by: %s (%s) %s\n",
                     deleter ? deleter : "", host ? host : "",
                     Cdatelite(&now));

        // TODO or only memcpy(&backup, fh, sizeof(backup)); ?
        strlcpy(backup.owner, fh->owner, sizeof(backup.owner));
        strlcpy(backup.date, fh->date, sizeof(backup.date));
        strlcpy(backup.title, fh->title, sizeof(backup.title));
        strlcpy(backup.filename, fh->filename, sizeof(backup.filename));

        if (backup_direct != direct &&
            strcmp(backup_direct, direct) != 0) {
            // need to create a new file entry.
            char *slash = NULL;
            char bakpath[PATHLEN];

            strlcpy(bakpath, backup_direct, sizeof(bakpath));
            slash = strrchr(bakpath, '/');
            if (slash)
                *slash = 0;
            if (stampfile_u(bakpath, &backup) == 0 &&
                Rename(fpath, bakpath) == 0) {
                strlcpy(fpath, bakpath, sizeof(fpath));
            } else {
                backup_direct = NULL;
                backup_failed = 1;
            }
        }

        // now, always backup according to fpath
        if (backup_direct) {
            fileheader_mem_to_storage(&backup);
            if (!timecapsule_archive_new_revision(
                        fpath, &backup, sizeof(backup),
                        backup_path, sz_backup_path))
                backup_failed = DELETE_FILE_CONTENT_BACKUP_FAILED;
        }
    }

    // the file should be already in time capsule
    if (*fpath && unlink(fpath) != 0)
        return DELETE_FILE_CONTENT_FAILED;
    return backup_failed;
}

/* ------------------------------------------------------------------------
 * Removing many posts of a list at once
 * ------------------------------------------------------------------------ */

// Opens fname for writing, replacing a stale one (left by a crash) that is
// more than an hour old.
static int
force_open(const char *fname)
{
    time4_t expire = time4_add(time4(NULL), -3600);

    if (time4_gt(dasht4(fname), expire))
        return -1;
    unlink(fname);
    return OpenCreate(fname, O_WRONLY | O_TRUNC);
}

int
bof_files_prune(const char *direct, uint16_t keep_modes, int keep_records,
                int durable,
                int (*selected)(const fileheader_t *fh, int index, void *ctx),
                void (*removed)(const fileheader_t *fh, void *ctx),
                void *ctx, const char *deleter)
{
    fileheader_t fhdr;
    char path[PATHLEN], newfn[PATHLEN], oldfn[PATHLEN], lockfn[PATHLEN];
    int fdr, fdw, fd, index, count = 0;

    if (keep_records) {
        // Tombstones in place, so positions don't change.
        if ((fd = open(direct, O_RDONLY)) == -1)
            return -1;
        for (index = 1; read_fileheader(fd, &fhdr) == 1; index++) {
            if ((fhdr.filemode & keep_modes) || !selected(&fhdr, index, ctx))
                continue;
            if (bof_files_tombstone(direct, index, &fhdr, NULL, deleter) == 0) {
                count++;
                if (removed)
                    removed(&fhdr, ctx);
            }
        }
        close(fd);
        return count;
    }

    snprintf(newfn, sizeof(newfn), "%s.new", direct);
    snprintf(oldfn, sizeof(oldfn), "%s.old", direct);
    snprintf(lockfn, sizeof(lockfn), "%s.lock", direct);

    if ((fd = OpenCreate(lockfn, O_RDWR | O_APPEND)) == -1)
        return -1;
    flock(fd, LOCK_EX);

    if ((fdr = open(direct, O_RDONLY)) == -1) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    if (((fdw = OpenCreate(newfn, O_WRONLY | O_EXCL)) == -1) &&
        ((fdw = force_open(newfn)) == -1)) {
        close(fdr);
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    for (index = 1;
         read(fdr, &fhdr, sizeof(fileheader_t)) == sizeof(fileheader_t);
         index++) {
        if ((fhdr.filemode & keep_modes) || !selected(&fhdr, index, ctx)) {
            if (write(fdw, &fhdr, sizeof(fileheader_t)) == -1) {
                close(fdr);
                close(fdw);
                unlink(newfn);
                flock(fd, LOCK_UN);
                close(fd);
                return -1;
            }
        } else {
            setdirpath(path, direct, fhdr.filename);
            unlink(path);
            count++;
            if (removed)
                removed(&fhdr, ctx);
        }
    }
    close(fdr);
    // On disk before it takes the place of the list, where asked: it
    // holds the lock of the list, which the BBS waits for.
    if (durable && fsync(fdw) != 0) {
        close(fdw);
        unlink(newfn);
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    close(fdw);
    if (Rename(direct, oldfn) == -1 || Rename(newfn, direct) == -1) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return count;
}

bof_error_t
bof_bbs_name_post(const char *community, fileheader_t *fh)
{
    char path[PATHLEN];

    if (!community || !is_valid_existing_brdname(community) || !fh)
        return BOF_ERR_INVALID_BODY_FIELD;
    setbpath(path, community);
    if (!dashd(path))
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    // An empty file takes the name (stamped by the time, as all posts).
    return stampfile_u(path, fh) == 0 ? BOF_OK : BOF_ERR_COULDNT_CREATE_POST;
}
