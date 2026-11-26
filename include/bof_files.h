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
#include <stdio.h>

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

/* Updates the record of the post named fhdr_name at position ent of
 * direct: modified (if not 0), title, owner and date (if not NULL or ""),
 * multi (if not NULL), the score by score_delta (within int16_t; the
 * list shows it capped at MAX_DISPLAY_SCORE), and the filemode bits to enable and disable. out
 * (if not NULL) gets the updated record. Returns 0, or -1 if the record
 * there is not that post. */
int bof_files_modify_record(const char *direct, int ent,
                            const char *fhdr_name, int32_t modified,
                            const char *title, const char *owner,
                            const char *date, int score_delta,
                            const void *multi, uint16_t enable_modes,
                            uint16_t disable_modes,
                            struct fileheader_t *out);

/* Reads the record of the post named fhdr_name at position ent of direct,
 * lets change() change it (with ctx), and writes it back; out (if not
 * NULL) gets it. Returns 0, or -1 if the record there is not that post. */
int bof_files_update_record(const char *direct, int ent,
                            const char *fhdr_name,
                            void (*change)(struct fileheader_t *fh,
                                           const void *ctx),
                            const void *ctx, struct fileheader_t *out);

/* Updates the record of the post named fhdr_name at position ent of direct
 * for comments: modified (if not 0), the score by score_delta (within
 * int16_t) and the count of comments by comments_delta. out (if
 * not NULL) gets the updated record. Returns 0, or -1. */
int bof_files_count_comments(const char *direct, int ent,
                             const char *fhdr_name, time4_t modified,
                             int score_delta, int comments_delta,
                             struct fileheader_t *out);

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

/* Removes the records of direct that selected() selects, except those
 * with any of keep_modes, calling removed() for each. With keep_records,
 * each becomes a tombstone (by deleter, see bof_files_set_tombstone()) in
 * place; otherwise the list is rewritten without them and their files are
 * unlinked (archive them first, see bof_files_delete_content()), the new
 * list synced to the disk before it takes the place of the old one only if
 * durable (as expire does; it holds the lock of the list meanwhile). index
 * is 1-based. Returns how many were removed, or -1. */
int bof_files_prune(const char *direct, uint16_t keep_modes, int keep_records,
                    int durable,
                    int (*selected)(const struct fileheader_t *fh, int index,
                                    void *ctx),
                    void (*removed)(const struct fileheader_t *fh, void *ctx),
                    void *ctx, const char *deleter);

/* The content of posts (bof/files/content.c). */
struct bof_post_version_t;

/* The version of the first size bytes of the file open as fd. Returns 0,
 * or -1 if it cannot be read that far. */
int bof_files_get_version(int fd, int64_t size,
                          struct bof_post_version_t *ver);

/* Whether the file at path still has the content of version ver. */
int bof_files_version_matches(const char *path,
                              const struct bof_post_version_t *ver);

/* Replaces the post at path with new_path, a new content made from version
 * ver of it: under the lock of the post, checks that it still has that
 * content, appends to new_path what was appended to the post after ver
 * (with CONFIG_EDITPOST_SMARTMERGE; without it, the post must have nothing
 * after ver), keeps it in the time capsule if keep_revision (appending
 * rev_note, if not NULL, to that revision), and renames new_path over the
 * post if it is still the same file. *saved tells if new_path took the
 * place of the post (it may have when an error is returned). Returns 0 or
 * a bof error: couldnt_find_post, read_only, couldnt_update_post (changed
 * meanwhile). */
int bof_files_replace_post(const char *path, const char *new_path,
                           const struct bof_post_version_t *ver,
                           int keep_revision, const char *rev_note,
                           int *saved);

/* Rewrites the post at path line by line: filter() writes what replaces a
 * line to out and returns 1, or returns 0 to keep it. If any line was
 * replaced (*matched counts them), tail (if not NULL) is appended and the
 * result replaces the post as bof_files_replace_post() does, keeping lines
 * appended while rewriting. Returns 0 (also when nothing matched) or a
 * bof error. */
int bof_files_rewrite_post(const char *path,
                           int (*filter)(const char *line, FILE *out,
                                         void *ctx),
                           void *ctx, const char *tail, const char *rev_note,
                           int *matched);

/* Writes the file of a new post at path from its body alone, as the
 * editor of the BBS writes posts: the header (bof_bbs_format_post_header()),
 * a blank line, the body, and the origin line after "--". Returns 0 or -1. */
int bof_files_write_post(const char *path, const char *author,
                         const char *nickname, const char *community,
                         const char *title, int32_t when, const char *body,
                         const char *from);

/* Comments in the files of posts (bof/files/comment.c). */

/* Formats the line of a comment of type (RECTYPE_*) by author saying
 * content (padded to width) followed by tail (e.g. where from and when),
 * as it is written into the file of the post. */
void bof_files_format_comment(char *buf, size_t szbuf, int type,
                              const char *author, int width,
                              const char *content, const char *tail);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_FILES_H */
