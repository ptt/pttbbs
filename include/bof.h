/*
 * bof.h - Birds Of Feather: the meta layer between BBS and community systems.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * bof is what a BBS user interface (mbbsd over telnet, a web front end, a
 * Lemmy gateway) calls to read and change the boards, posts, comments and
 * people of a site. Its API follows Lemmy (https://join-lemmy.org/docs/,
 * API v3): the same entities (person, community, post, comment, post_like),
 * the same field and method names (CreatePost, DeletePost, RemovePost,
 * LockPost, ...), and Lemmy's error names. What Lemmy cannot express follows
 * Reddit, then Discourse; PTT-only fields are prefixed with bbs_.
 *
 * The API is in-process C (no IPC), as a telnet UI calls it for every key
 * press. Behind it, a site's data may live in the files of a classic PTT BBS
 * (.DIR, boards/, the time capsule) or in services such as post.svc.
 *
 * See bof/README.md.
 */

#ifndef INCLUDE_BOF_H
#define INCLUDE_BOF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOF_VERSION "0.1.0"

/* ------------------------------------------------------------------------
 * Errors
 * ------------------------------------------------------------------------ */

/* Results of bof calls. Errors are Lemmy's (bof_error_name() returns the
 * name Lemmy uses, e.g. "couldnt_find_post"). */
typedef enum bof_error_t {
    BOF_OK = 0,
    BOF_ERR_UNKNOWN,                /* unknown */
    BOF_ERR_INVALID_BODY_FIELD,     /* invalid_body_field: a bad request */
    BOF_ERR_NOT_FOUND,              /* not_found */
    BOF_ERR_COULDNT_FIND_COMMUNITY, /* couldnt_find_community */
    BOF_ERR_COULDNT_FIND_POST,      /* couldnt_find_post */
    BOF_ERR_COULDNT_FIND_COMMENT,   /* couldnt_find_comment */
    BOF_ERR_COULDNT_UPDATE_POST,    /* couldnt_update_post */
    BOF_ERR_NOT_A_MODERATOR,        /* not_a_moderator */
    BOF_ERR_NO_POST_EDIT_ALLOWED,   /* no_post_edit_allowed */
    BOF_ERR_LOCKED,                 /* locked */
    /* bbs_storage_not_supported: not by the storage backend */
    BOF_ERR_BBS_STORAGE_NOT_SUPPORTED,
    BOF_ERR_MAX
} bof_error_t;

/* Returns the Lemmy name of an error, e.g. "couldnt_find_post". */
const char *bof_error_name(bof_error_t err);

/* ------------------------------------------------------------------------
 * People
 * ------------------------------------------------------------------------ */

/* A person (Lemmy's person.id, an int32 serial). 0 means unknown or the
 * system. People get their IDs when accounts move to a database; until
 * then, bof knows a person by the BBS userid in bof_actor_t. The BBS
 * account behind a person (Lemmy's local_user) is its usernum and
 * firstlogin, as usernums of deleted accounts are reused. */
typedef int32_t bof_person_id_t;

/* Who calls bof (Lemmy's authenticated local user). bof trusts the caller
 * to have checked that the actor may do what it asks: permissions stay in
 * the user interface for now. */
typedef struct bof_actor_t {
    bof_person_id_t person_id; /* 0 until people have IDs */
    const char *name;          /* BBS userid */
    const char *from;          /* where from (host), for logs; may be NULL */
} bof_actor_t;

/* ------------------------------------------------------------------------
 * Posts
 * ------------------------------------------------------------------------ */

struct fileheader_t;

/* Room for the paths bof gives back. */
#define BOF_PATH_MAX 256

/* Where a post is. Lemmy names a post by its ID (post.id); posts kept in
 * the files of a board have none yet, so they are named by their board
 * and, as the UI last saw them, their record in a list and its position.
 * bof checks that the record there is still the same post. */
typedef struct bof_post_ref_t {
    const char *community;                  /* board name */
    const char *bbs_dir;                    /* the list (.DIR, or the digest's) */
    int bbs_index;                          /* 1-based position in bbs_dir */
    const struct fileheader_t *bbs_header;  /* the record there */
} bof_post_ref_t;

/* Lemmy's DeletePost: the creator deletes the post. */
typedef struct bof_delete_post_t {
    bof_post_ref_t post;
    int deleted;            /* must be 1: restoring is not supported yet */
    /* PTT: keep a record (with bbs_title) in the list instead of dropping
     * it, as boards with many readers do. */
    int bbs_keep_record;
    const char *bbs_title;  /* NULL or "": the default for deleted posts */
} bof_delete_post_t;

/* Lemmy's RemovePost: a moderator removes the post, for a reason. */
typedef struct bof_remove_post_t {
    bof_post_ref_t post;
    int removed;            /* must be 1: restoring is not supported yet */
    const char *reason;     /* also appended to the archived post */
    int bbs_keep_record;    /* as in bof_delete_post_t */
    const char *bbs_title;  /* NULL or "": the default for removed posts */
} bof_remove_post_t;


/* What changing a post did, as far as the files tell (Lemmy's PostResponse
 * has the post view; PTT callers need where its content went). */
typedef struct bof_post_result_t {
    /* Where the content was archived ("" if it was not), e.g. to mail it
     * back to its author. */
    char bbs_archived_path[BOF_PATH_MAX];
    /* DELETE_FILE_CONTENT_SUCCESS, _FAILED (the content could not be
     * removed) or _BACKUP_FAILED (removed, but not archived). */
    int bbs_content_result;
} bof_post_result_t;

/* Deletes a post (DeletePost) or removes it (RemovePost): drops its record
 * from the list (or replaces it with a record titled as deleted or
 * removed), and moves its content to the archive, noting who did it. The
 * record must still be at post.bbs_index (BOF_ERR_COULDNT_FIND_POST
 * otherwise). Other boards (ALLPOST) and caches are the caller's. */
bof_error_t bof_delete_post(const bof_actor_t *actor,
                            const bof_delete_post_t *req,
                            bof_post_result_t *out);
bof_error_t bof_remove_post(const bof_actor_t *actor,
                            const bof_remove_post_t *req,
                            bof_post_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_H */
