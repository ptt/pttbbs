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
    BOF_ERR_COULDNT_CREATE_POST,    /* couldnt_create_post */
    BOF_ERR_COULDNT_CREATE_COMMENT, /* couldnt_create_comment */
    BOF_ERR_BBS_READ_ONLY,          /* bbs_read_only: the files are read only */
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

/* PTT: the version of the content of a post as read, like an ETag: its
 * first size bytes, by their hash. The file of a post is its body followed
 * by lines appended later (comments); a change of the content made from a
 * version checks that those bytes are still there, and keeps the lines
 * appended after them meanwhile (the files backend without
 * CONFIG_EDITPOST_SMARTMERGE fails with couldnt_update_post instead). */
typedef struct bof_post_version_t {
    int64_t size;
    uint64_t hash;
} bof_post_version_t;

/* The body of a post (post.body of Lemmy's GetPost) copied to dest_path,
 * e.g. for editing, and its version. Errors: couldnt_find_post. */
bof_error_t bof_bbs_get_post_body(const bof_post_ref_t *post,
                                  const char *dest_path,
                                  bof_post_version_t *ver);

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
    /* The record was kept in the list as a tombstone (so positions after
     * it did not move), rather than dropped. */
    int bbs_record_kept;
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

/* Lemmy's CreatePost. The UI's editor still writes the file of the post
 * (named as bbs_header->filename, in the directory of bbs_dir); this
 * lists it. */
typedef struct bof_create_post_t {
    const char *community;                  /* board name */
    const char *bbs_dir;                    /* the list (.DIR) to add it to */
    const struct fileheader_t *bbs_header;  /* its record */
} bof_create_post_t;

/* What changing a post gives back (Lemmy's PostResponse has the post view;
 * PTT callers keep the record of the post they show). */
typedef struct bof_post_response_t {
    /* If not NULL, gets the updated record of the post. */
    struct fileheader_t *bbs_header;
    /* EditPost with a body: whether the body was saved (it may be, while
     * the record could not be changed). */
    int bbs_body_saved;
} bof_post_response_t;

bof_error_t bof_create_post(const bof_actor_t *actor,
                            const bof_create_post_t *req,
                            bof_post_response_t *out);

/* Lemmy's EditPost: the title, the body, and (PTT) other fields of the
 * record. */
typedef struct bof_edit_post_t {
    bof_post_ref_t post;
    const char *name;          /* the new title; NULL or "": unchanged */
    /* The body (Lemmy's body), as a file the editor wrote from the content
     * of version bbs_body_version (see bof_bbs_get_post_body()); it is
     * moved over the post, after the old one is kept in the time capsule
     * and the lines appended since that version are added to it. NULL:
     * unchanged. If the content has changed otherwise since that version,
     * nothing changes (couldnt_update_post) and the file stays. */
    const char *bbs_body_path;
    const bof_post_version_t *bbs_body_version;
    /* PTT, for sysops and the editor: NULL or 0 leaves them unchanged. */
    int32_t bbs_modified;      /* when the content was last changed (with a
                                * body: 0 for when it is saved) */
    const char *bbs_owner;     /* the author shown in the list */
    const char *bbs_date;      /* the date shown in the list */
    const int32_t *bbs_money;  /* fileheader_t.multi */
} bof_edit_post_t;

bof_error_t bof_edit_post(const bof_actor_t *actor,
                          const bof_edit_post_t *req,
                          bof_post_response_t *out);

/* PTT: marks of a post that Lemmy does not have: FILE_MARKED (m, kept),
 * FILE_SOLVED (L) and FILE_SUSPICIOUS. Discourse has "solved"; none of
 * them is in Lemmy or Reddit. */
typedef struct bof_bbs_set_post_modes_t {
    bof_post_ref_t post;
    uint16_t enable;           /* filemode bits to set */
    uint16_t disable;          /* filemode bits to clear */
} bof_bbs_set_post_modes_t;

bof_error_t bof_bbs_set_post_modes(const bof_actor_t *actor,
                                   const bof_bbs_set_post_modes_t *req,
                                   bof_post_response_t *out);

/* Lemmy's LockPost. A locked PTT post takes no comments and its content is
 * hidden: its record is named L.<stamp> and its file X.<stamp>. Locking a
 * locked post (or unlocking an unlocked one) does nothing. */
typedef struct bof_lock_post_t {
    bof_post_ref_t post;
    int locked;
} bof_lock_post_t;

bof_error_t bof_lock_post(const bof_actor_t *actor,
                          const bof_lock_post_t *req,
                          bof_post_response_t *out);

/* Lemmy's FeaturePost, with feature_type Community: PTT's posts pinned to
 * the bottom of a board (FILE_BOTTOM; a pinned post is also marked). This
 * changes the post; the list of pinned posts of the board (in its board
 * header) is still kept by the caller. */
typedef struct bof_feature_post_t {
    bof_post_ref_t post;
    int featured;
} bof_feature_post_t;

bof_error_t bof_feature_post(const bof_actor_t *actor,
                             const bof_feature_post_t *req,
                             bof_post_response_t *out);

/* PTT: sets the score of a post (its net votes in the list) back to 0.
 * Neither Lemmy nor Reddit lets moderators do this. */
typedef struct bof_bbs_reset_post_score_t {
    bof_post_ref_t post;
} bof_bbs_reset_post_score_t;

bof_error_t bof_bbs_reset_post_score(const bof_actor_t *actor,
                                     const bof_bbs_reset_post_score_t *req,
                                     bof_post_response_t *out);

/* PTT: masks the lines of a post that have pattern (at least 2 bytes, in
 * the text without color codes), as moderators of some boards may. None of
 * Lemmy, Reddit or Discourse has it; the nearest is Discourse's staff edit
 * with an edit reason. Each such line becomes a mark, a note of who, when
 * and why is appended, and the post before is kept in the time capsule with
 * the pattern noted. Lines appended meanwhile (comments) are kept. Errors:
 * couldnt_find_post, not_found (no line has it), couldnt_update_post
 * (changed meanwhile). */
typedef struct bof_bbs_mask_post_t {
    bof_post_ref_t post;       /* bbs_index is not checked (may be 0) */
    const char *pattern;
    const char *reason;        /* may be NULL */
} bof_bbs_mask_post_t;

bof_error_t bof_bbs_mask_post(const bof_actor_t *actor,
                              const bof_bbs_mask_post_t *req,
                              bof_post_response_t *out);

/* ------------------------------------------------------------------------
 * Comments
 * ------------------------------------------------------------------------ */

/* Lemmy's CreateComment. A PTT board keeps the comments of a post as lines
 * appended to its file, and a push or boo also votes on the post (as
 * Lemmy's CreatePostLike would). */
typedef struct bof_create_comment_t {
    bof_post_ref_t post;
    const char *content;    /* the comment, as the lines for the file */
    int bbs_vote;           /* +1 (push), -1 (boo) or 0 (arrow) */
    /* Lock the file while appending (it may be being edited), trying
     * again for a while; bbs_on_wait (if not NULL) is called before each
     * wait with the tries left. */
    int bbs_lock;
    void (*bbs_on_wait)(int tries_left, void *ctx);
    void *bbs_wait_ctx;
} bof_create_comment_t;

typedef struct bof_comment_response_t {
    /* If not NULL, gets the updated record of the post. */
    struct fileheader_t *bbs_header;
    /* The size of the file after the comment (a token for live updates). */
    int64_t bbs_file_size;
} bof_comment_response_t;

/* Errors: couldnt_find_post (no file), bbs_read_only,
 * couldnt_create_comment (the file stayed locked), couldnt_update_post
 * (appended, but the record of the post has changed meanwhile). */
bof_error_t bof_create_comment(const bof_actor_t *actor,
                               const bof_create_comment_t *req,
                               bof_comment_response_t *out);

/* Lemmy's RemoveComment, by a moderator. Comments in the files of a board
 * have no IDs yet, so the comment is given as the comment database
 * (commentd) remembers it: the first line of the file that looks like a
 * comment of bbs_vote by bbs_author saying bbs_content is it. That line is
 * replaced with a note of who removed it and why, and the file before is
 * kept as a revision in the time capsule. Unlike Lemmy (removed: false),
 * a removed comment cannot be restored: its line is gone. */
typedef struct bof_remove_comment_t {
    bof_post_ref_t post;       /* bbs_index is not checked (may be 0) */
    int bbs_vote;              /* +1 (push), -1 (boo) or 0 (arrow) */
    const char *bbs_author;    /* the BBS userid who commented */
    const char *bbs_content;   /* what the comment says */
    const char *reason;        /* may be NULL */
} bof_remove_comment_t;

/* Errors: couldnt_find_post (no file), couldnt_find_comment (no such line,
 * e.g. the post was edited). out->bbs_header (if not NULL) gets the
 * record of the post, which does not change. */
bof_error_t bof_remove_comment(const bof_actor_t *actor,
                               const bof_remove_comment_t *req,
                               bof_comment_response_t *out);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_H */
