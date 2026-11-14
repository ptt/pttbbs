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
