/*
 * bof_files.h - the files of a BBS in bof (Birds Of Feather): how bof keeps
 * things in the files of a classic PTT BBS (.DIR, boards/, the time
 * capsule), as primitives (bof/files/) with no actor or rules, for the
 * files backend (bof_fs.h, bof/fs/) and its cache in other backends.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * These are lower level than the Lemmy-shaped API in bof.h. mbbsd still
 * calls some of them directly while it moves onto that API.
 */

#ifndef INCLUDE_BOF_FILES_H
#define INCLUDE_BOF_FILES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The types of comments in the files of posts: push, boo and arrow. */
enum {
    RECTYPE_GOOD,
    RECTYPE_BAD,
    RECTYPE_ARROW,

    RECTYPE_SIZE,
    RECTYPE_MAX     = RECTYPE_SIZE - 1,
    RECTYPE_DEFAULT = RECTYPE_GOOD,
};

/* What bof_files_delete_content() returns. */
#define DELETE_FILE_CONTENT_SUCCESS (0)
#define DELETE_FILE_CONTENT_FAILED  (-1)
#define DELETE_FILE_CONTENT_BACKUP_FAILED  (1)
#define IS_DELETE_FILE_CONTENT_OK(x) ((x) != DELETE_FILE_CONTENT_FAILED)

/* The time capsule (bof/files/timecap.c): the revisions of edited files
 * and the archive of deleted ones. */
int timecapsule_add_revision(const char *filename);
int timecapsule_get_max_revision_number(const char *filename);
int timecapsule_get_max_archive_number(const char *filename, size_t szrefblob);
int timecapsule_archive(const char *filename, const void *ref, size_t szref);
int timecapsule_get_by_revision(const char *filename, int rev, char *rev_path,
                                size_t sz_rev_path);
int timecapsule_archive_new_revision(const char *filename,
                                     const void *ref, size_t szref,
                                     char *archived_path, size_t sz_archived_path);
int timecapsule_get_archive_blobs(const char *filename, int idx, int nblobs,
                                  void *blobsptr, size_t szblob);


/* Posts in the files of a board (bof/files/record.c). */
struct fileheader_t;

/* Turns the record of a post into the record left for it when it is
 * deleted (by deleter, as the creator if deleter is its owner) or removed:
 * titled title, or by default as deleted or removed. */
void bof_files_set_tombstone(struct fileheader_t *fh, const char *title,
                             const char *deleter);

/* Replaces the record of fh at position ent of direct with its tombstone.
 * Returns 0 on success. */
int bof_files_tombstone(const char *direct, int ent,
                        const struct fileheader_t *fh, const char *title,
                        const char *deleter);

/* Removes the content of the post fh listed in direct, first appending the
 * reason and who deleted it (deleter from host), and archiving it under
 * backup_direct (if not NULL; then backup_path, if not NULL, gets where).
 * Returns DELETE_FILE_CONTENT_SUCCESS, _FAILED or _BACKUP_FAILED. */
int bof_files_delete_content(const char *direct, const struct fileheader_t *fh,
                             const char *backup_direct, char *backup_path,
                             size_t sz_backup_path, const char *reason,
                             const char *deleter, const char *host);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_FILES_H */
