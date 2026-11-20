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
    BOF_ERR_COULDNT_UPDATE_COMMENT, /* couldnt_update_comment */
    /* bbs_storage_not_supported: not by the storage backend */
    BOF_ERR_BBS_STORAGE_NOT_SUPPORTED,
    BOF_ERR_NO_COMMENT_EDIT_ALLOWED, /* no_comment_edit_allowed */
    BOF_ERR_COULDNT_CREATE_REPORT,  /* couldnt_create_report */
    BOF_ERR_BBS_ALREADY_REPORTED,   /* bbs_already_reported: by them, open */
    BOF_ERR_COULDNT_RESOLVE_REPORT, /* couldnt_resolve_report */
    BOF_ERR_RATE_LIMITED,           /* rate_limit_error */
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
    /* PTT, for the modlog: how the actor is shown in it (the role, e.g.
     * the moderators of the board, rather than the account, as Lemmy does
     * with hide_modlog_mod_names), and whether the account is also hidden
     * in the audit record (police doing police work). */
    const char *bbs_role;
    int bbs_hidden_in_audit;
    /* The BBS account (Lemmy's local_user): its usernum and firstlogin,
     * packed as post.svc takes them; 0 for the system or unknown. */
    uint64_t bbs_local_user;
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
    int deleted;            /* must be 1: see bof_restore_post() */
    /* PTT: keep a record (with bbs_title) in the list instead of dropping
     * it, as boards with many readers do. */
    int bbs_keep_record;
    const char *bbs_title;  /* NULL or "": the default for deleted posts */
} bof_delete_post_t;

/* Lemmy's RemovePost: a moderator removes the post, for a reason. */
typedef struct bof_remove_post_t {
    bof_post_ref_t post;
    int removed;            /* must be 1: see bof_restore_post() */
    const char *reason;     /* also appended to the archived post */
    int bbs_keep_record;    /* as in bof_delete_post_t */
    const char *bbs_title;  /* NULL or "": the default for removed posts */
    /* The record for the modlog (see bof_lock_post_t), and the one to write
     * instead if the content could not be archived (e.g. a bad post is
     * not given then); NULL: bbs_modlog. */
    const struct bof_modlog_t *bbs_modlog;
    const struct bof_modlog_t *bbs_modlog_unarchived;
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
    /* Where its content (Lemmy's body) is, if not at its place in the
     * board yet (the caller moves it there); NULL: at its place. */
    const char *bbs_content_path;
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

/* Removes many posts of a board at once, e.g. those tagged in the UI.
 * Lemmy and Reddit have none; this is Discourse's bulk delete of posts
 * (destroy_many), as RemovePost does each: the content of each is archived
 * (with reason), then the records are dropped, or replaced with
 * tombstones (bbs_keep_record), in one rewrite of the list. Posts with any
 * of bbs_keep_modes (marked, pinned, digest) are kept. Posts are chosen
 * by bbs_selected (index is 1-based) rather than by IDs, which posts in
 * files do not have; bbs_removed (if not NULL) is called for each removed.
 * *count gets how many. */
typedef struct bof_remove_posts_t {
    const char *community;
    const char *bbs_dir;
    int (*bbs_selected)(const struct fileheader_t *fh, int index, void *ctx);
    void (*bbs_removed)(const struct fileheader_t *fh, void *ctx);
    void *bbs_ctx;
    uint16_t bbs_keep_modes;
    int bbs_keep_record;
    const char *reason;     /* may be NULL */
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded (e.g. in the digest). */
    const struct bof_modlog_t *bbs_modlog;
} bof_remove_posts_t;

bof_error_t bof_remove_posts(const bof_actor_t *actor,
                             const bof_remove_posts_t *req, int *count);

/* Lemmy's DeletePost with deleted: false and RemovePost with removed:
 * false: puts a deleted or removed post back from the archive (the time
 * capsule; its recycle bin lists the archived records) into its list, in
 * place of its record if only its file was gone, or else in the order of
 * time (a tombstone it left stays).
 * The archive keeps its author, date, title and content (with the notes
 * of its deletion, and now of who restored it); its marks and money are
 * not kept. Errors: couldnt_find_post (not in the archive anymore),
 * couldnt_update_post (it is in the list already, or cannot be written).
 * Copies in other boards (ALLPOST) are not restored. */
typedef struct bof_restore_post_t {
    const char *community;
    const char *bbs_dir;
    const struct fileheader_t *bbs_archived;  /* its record in the archive */
    const char *reason;                       /* may be NULL */
    const struct bof_modlog_t *bbs_modlog;    /* see bof_lock_post_t */
} bof_restore_post_t;

bof_error_t bof_restore_post(const bof_actor_t *actor,
                             const bof_restore_post_t *req,
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
    /* PTT: for a change by moderators, as in bof_lock_post_t (no reason). */
    const struct bof_modlog_t *bbs_modlog;
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
    const char *reason;        /* may be NULL */
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded (e.g. in the digest). */
    const struct bof_modlog_t *bbs_modlog;
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

/* PTT: sets the score of a post (its net votes in the list) back to 0,
 * noting who did it at the end of the post. Neither Lemmy nor Reddit lets
 * moderators do this. */
typedef struct bof_bbs_reset_post_score_t {
    bof_post_ref_t post;
    const char *reason;        /* may be NULL */
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded (e.g. in the digest). */
    const struct bof_modlog_t *bbs_modlog;
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
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded (e.g. in the digest). */
    const struct bof_modlog_t *bbs_modlog;
} bof_bbs_mask_post_t;

bof_error_t bof_bbs_mask_post(const bof_actor_t *actor,
                              const bof_bbs_mask_post_t *req,
                              bof_post_response_t *out);

/* PTT: boards that collect copies (or notes) of the posts of other boards
 * (ALLPOST, ALLHIDPOST, NEWIDPOST, UnAnonymous). Lemmy and Reddit show
 * such listings (r/all) without copies. */
int bof_bbs_is_aggregate(const char *community);

/* The board a copy in an aggregate board came from, by the end of its
 * title, into board (sz bytes). Returns 1 if found. */
int bof_bbs_crosspost_origin(const struct fileheader_t *fh, char *board,
                             size_t sz);

/* PTT: removes the other copy of a deleted post, which has the same file
 * name: the original in its board (when the copy in an aggregate board was
 * deleted) or the copy in ALLPOST (bbs_content_only: its record stays
 * until the board is cleared). The record is found by the time in its file
 * name; its content is archived with reason and removed even if the record
 * is not found (couldnt_find_post); with bbs_content_only it is only
 * removed, not archived. out must not be NULL. */
typedef struct bof_bbs_remove_crosspost_t {
    const char *community;                  /* the board with the copy */
    const char *bbs_dir;                    /* its list */
    const struct fileheader_t *bbs_header;  /* the deleted post */
    int bbs_content_only;
    int bbs_keep_record;    /* leave a tombstone, as in bof_remove_post_t */
    const char *reason;
} bof_bbs_remove_crosspost_t;

bof_error_t bof_bbs_remove_crosspost(const bof_actor_t *actor,
                                     const bof_bbs_remove_crosspost_t *req,
                                     bof_post_result_t *out);

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

/* A comment as found in the file of a post (Lemmy's CommentView, in
 * part). Comments in files have no IDs: they are numbered by their floor,
 * the order among the comments of the post (counting removed ones, whose
 * lines note it), as post.svc keys them by (post, floor). */
typedef struct bof_comment_t {
    int bbs_floor;           /* 1-based */
    int bbs_vote;            /* +1 (push), -1 (boo) or 0 (arrow) */
    int removed;             /* only a note of its removal is left */
    char creator[16];        /* BBS userid */
    char content[256];       /* without the padding */
    char bbs_tail[64];       /* what follows: where from and when */
    int bbs_width;           /* the room for the content in the line */
    /* Edited (Lemmy's updated is a time; a line keeps only the mark of an
     * edit, BOF_COMMENT_EDITED_MARK before its date). */
    int bbs_edited;
} bof_comment_t;

#define BOF_COMMENT_EDITED_MARK '~'


/* Lemmy's GetComments, for one post, in order. */
typedef struct bof_get_comments_t {
    bof_post_ref_t post;     /* bbs_index is not checked (may be 0) */
    const char *bbs_creator; /* only those by them, or NULL for all */
    int limit;               /* room in comments */
} bof_get_comments_t;

typedef struct bof_get_comments_response_t {
    bof_comment_t *comments; /* by the caller, room for limit */
    int count;
} bof_get_comments_response_t;

/* Errors: couldnt_find_post. */
bof_error_t bof_get_comments(const bof_get_comments_t *req,
                             bof_get_comments_response_t *out);

/* Lemmy's EditComment, by its creator (the actor must be bbs_creator, else
 * no_comment_edit_allowed): the content (one line, no control codes, in
 * the room of the old in files, else invalid_body_field) replaces what the
 * comment at bbs_floor says, which must still be by bbs_creator and say
 * bbs_old_content (else couldnt_update_comment); the post before is kept
 * in the time capsule, with a note of the edit. With post.bbs_index, the
 * record of the post gets the time of the change (out->bbs_header). Lines
 * appended meanwhile are kept. */
typedef struct bof_edit_comment_t {
    bof_post_ref_t post;
    int bbs_floor;
    const char *bbs_creator;
    const char *bbs_old_content;
    const char *content;
} bof_edit_comment_t;

bof_error_t bof_edit_comment(const bof_actor_t *actor,
                             const bof_edit_comment_t *req,
                             bof_comment_response_t *out);

/* Lemmy's DeleteComment, by its creator. Posts in files cannot take a
 * comment back the way post.svc does (it keeps the comment to restore,
 * deleted: false, and its history), so this backend gives
 * bbs_storage_not_supported; the UI offers it for post.svc. */
typedef struct bof_delete_comment_t {
    bof_post_ref_t post;
    int bbs_floor;
    const char *bbs_creator;
    const char *bbs_content;
    int deleted;
} bof_delete_comment_t;

bof_error_t bof_delete_comment(const bof_actor_t *actor,
                               const bof_delete_comment_t *req,
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
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded (e.g. in the digest). */
    const struct bof_modlog_t *bbs_modlog;
} bof_remove_comment_t;

/* Errors: couldnt_find_post (no file), couldnt_find_comment (no such line,
 * e.g. the post was edited). out->bbs_header (if not NULL) gets the
 * record of the post, which does not change. */
bof_error_t bof_remove_comment(const bof_actor_t *actor,
                               const bof_remove_comment_t *req,
                               bof_comment_response_t *out);

/* ------------------------------------------------------------------------
 * Reading posts
 * ------------------------------------------------------------------------ */

/* Lemmy's GetPosts, for a board in the order of its list (oldest first,
 * as PTT shows it). Lemmy pages by page and limit; posts in files are
 * read by their position instead. Featured (pinned) posts come after the
 * list, as PTT shows them at the bottom (Lemmy lists them first): their
 * positions in the list are given by the caller (the board header keeps
 * them), and they get FILE_BOTTOM. */
typedef struct bof_get_posts_t {
    const char *community;
    const char *bbs_dir;
    int bbs_start;               /* 1-based position of the first to get */
    int limit;                   /* how many at most */
    /* How many posts the list has, as the UI last saw it: none after that
     * many are given, and featured posts come after them; 0: as it has
     * now. */
    int bbs_total;
    const int32_t *bbs_featured; /* positions of featured posts */
    int bbs_nfeatured;
} bof_get_posts_t;

typedef struct bof_get_posts_response_t {
    struct fileheader_t *posts;  /* room for limit records, by the caller */
    int count;                   /* how many were got */
    int bbs_total;               /* the posts in the list, not featured */
} bof_get_posts_response_t;

bof_error_t bof_get_posts(const bof_get_posts_t *req,
                          bof_get_posts_response_t *out);

/* Lemmy's GetPost: the post at a position of a list, and where its
 * content is (its body and comments; see also bof_bbs_get_post_body()). */
typedef struct bof_get_post_t {
    const char *community;
    const char *bbs_dir;
    int bbs_index;               /* 1-based */
    /* If not NULL, the list is kept open there for the next call (-1 at
     * first; the caller closes it), for reading many one by one. */
    int *bbs_fd;
} bof_get_post_t;

typedef struct bof_get_post_response_t {
    struct fileheader_t *bbs_header;  /* gets the record, by the caller */
    char bbs_path[BOF_PATH_MAX];      /* "" for a record without a file */
} bof_get_post_response_t;

/* Errors: couldnt_find_post (no record there). */
bof_error_t bof_get_post(const bof_get_post_t *req,
                         bof_get_post_response_t *out);

/* Which posts a person has read (Lemmy's post_read), kept for the person
 * in session (one per process) as the BBS read records: per board (by its
 * ID, Lemmy's community_id), the times of the posts read, plus "all read
 * up to" marks. Posts made a year before login count as read. The records
 * are loaded when first needed and saved by
 * bof_bbs_read_marks_finalize(). The session is PTT's (Lemmy keeps
 * post_read in its database), hence bbs_. */
void bof_bbs_read_marks_set_person(const char *userid, int savable,
                                   int32_t login_time);
int bof_bbs_read_marks_initialize(void);  /* 1 if it was already */
void bof_bbs_read_marks_update(void);     /* the board's into the buffer */
void bof_bbs_read_marks_finalize(void);   /* saves and releases */
void bof_bbs_read_marks_release(void);
/* Makes community the board read now. Returns how many records it has. */
int bof_bbs_read_marks_enter(int community_id);

/* Lemmy's MarkPostAsRead (read: true), for the post with file name fname
 * changed at modified, on the board entered. Returns -1 if community_id is
 * not that board. */
int bof_mark_post_as_read(int community_id, const char *fname,
                          int32_t modified);
/* Whether a post is unread (Lemmy's read: false): 0 read, 1 unread, 2 read
 * but changed since (PTT's; Lemmy has no such state). By its file name, or
 * by the time in it. */
int bof_post_is_unread(int community_id, const char *fname,
                       int32_t modified);
int bof_bbs_post_unread_time(int community_id, int32_t ftime,
                             int32_t modified);
/* PTT: marks a whole board read (or unread), or read up to a time. */
void bof_bbs_mark_community_read(int community_id, int is_all_read);
void bof_bbs_mark_community_read_until(int community_id, int32_t t);
/* PTT: the post read nearest to ftime, newer (forward) or older, into
 * *result: 1 found, 0 none, -1 no records for the board. */
int bof_bbs_search_read(int community_id, int32_t ftime, int forward,
                        int32_t *result);

/* ------------------------------------------------------------------------
 * Reports
 * ------------------------------------------------------------------------ */

/* A report of a post or a comment to the moderators of its board (Lemmy's
 * post_report and comment_report, in one list as Lemmy 1.0 lists them).
 * Lemmy numbers reports globally; here they are numbered in their board,
 * 1-based, in the order made. Lemmy keeps the reported text; for a post
 * this keeps its title and the version of its content (the time capsule
 * keeps what edits replace), for a comment what it said. Who reported is
 * for the site's admins: the UI does not show it to moderators. */
enum {
    BOF_REPORT_POST = 1,
    BOF_REPORT_COMMENT = 2,
};

typedef struct bof_report_t {
    int64_t bbs_size;              /* the version of the post reported */
    uint64_t bbs_hash;
    int32_t id;
    int32_t type;                  /* BOF_REPORT_* */
    int32_t published;
    int32_t updated;               /* resolved or reopened; 0 if never */
    int32_t resolved;
    int32_t bbs_floor;             /* the comment's */
    int32_t bbs_site_rule;         /* against the rules of the site */
    int32_t bbs_reserved;
    char creator[16];              /* who reported */
    char resolver[16];
    char bbs_post_file[32];        /* the post's file name */
    char original_post_name[80];   /* its title */
    char original_comment_text[256];
    char reason[128];
    char bbs_community[16];        /* the board */
} bof_report_t;

/* At most this many reports by a person on a board in this many seconds
 * (rate_limit_error). */
#define BOF_REPORT_RATE_MAX     (5)
#define BOF_REPORT_RATE_WINDOW  (60 * 60)

/* Lemmy's CreatePostReport (no comment) and CreateCommentReport (with
 * bbs_floor and what the comment said). One open report per person per
 * post or comment (bbs_already_reported). out (if not NULL) gets it. */
typedef struct bof_create_report_t {
    bof_post_ref_t post;           /* bbs_index is not checked (may be 0) */
    int bbs_floor;                 /* 0: the post */
    const char *bbs_comment_text;  /* the comment's, as read */
    const char *reason;
    int bbs_site_rule;
} bof_create_report_t;

bof_error_t bof_create_report(const bof_actor_t *actor,
                              const bof_create_report_t *req,
                              bof_report_t *out);

/* Lemmy's ListReports: the reports of a board, newest first; or, with
 * no community, those against the rules of the site on all boards, for
 * the site's admins (Lemmy shows every report to admins; these are the
 * ones for them). */
typedef struct bof_list_reports_t {
    const char *community;         /* NULL: the site's */
    int unresolved_only;
    int limit;                     /* room in reports */
} bof_list_reports_t;

typedef struct bof_list_reports_response_t {
    bof_report_t *reports;         /* by the caller */
    int count;
    int bbs_unresolved;            /* open reports of the board, in all */
} bof_list_reports_response_t;

bof_error_t bof_list_reports(const bof_list_reports_t *req,
                             bof_list_reports_response_t *out);

/* Lemmy's ResolvePostReport / ResolveCommentReport (resolved: 0 to open it
 * again). Errors: couldnt_resolve_report (no such report). */
typedef struct bof_resolve_report_t {
    const char *community;
    int32_t report_id;
    int resolved;
} bof_resolve_report_t;

bof_error_t bof_resolve_report(const bof_actor_t *actor,
                               const bof_resolve_report_t *req,
                               bof_report_t *out);

/* Whether a post still has the content reported (1), or was edited (0). */
int bof_report_matches_post(const char *bbs_dir, const bof_report_t *r);

/* ------------------------------------------------------------------------
 * Modlog
 * ------------------------------------------------------------------------ */

/* A record of the modlog of a board (Lemmy's modlog, which keeps a table
 * per kind of action: mod_remove_post, mod_lock_post, ...). PTT records
 * are text: an action (a short name) and fields, a line each, built with
 * bof_modlog_*() and written to the public modlog and the audit record of
 * the board. */
/* Sizes as STRLEN (80) and ANSILINELEN (511) of the BBS. */
typedef struct bof_modlog_t {
    char action[80];
    char fields[511 * 2];
    /* Fields only in the audit record, added by bof_modlog_add_audit(). */
    char audit_lines[511];
    /* More lines only in the audit record, after those (e.g. which posts). */
    const char *audit_fields;
    /* Set by bof_modlog_set_*(): put after the action on the first line if
     * it fits there (with the reason, if any, when both fit), or else
     * first_lines as the first fields. */
    char first[511];
    char first_lines[511 * 2];
    /* The action is also recorded in the police log: the reason is only in
     * the audit record. */
    int reason_audit_only;
} bof_modlog_t;

/* Format checks (the BBS is built with gcc or clang). */
#define BOF_PRINTF(f, a) __attribute__((format(printf, f, a)))

/* Starts a record of the action (maybe with a short number, e.g. "muted
 * for 7 days"). Returns m. */
bof_modlog_t *bof_modlog_init(bof_modlog_t *m, const char *fmt, ...)
    BOF_PRINTF(2, 3);
/* Adds the field "label: value". */
void bof_modlog_add(bof_modlog_t *m, const char *label, const char *fmt, ...)
    BOF_PRINTF(3, 4);
/* Adds the field "label: from => to" (to on the next line if too long). */
void bof_modlog_add_change(bof_modlog_t *m, const char *label,
                           const char *from, const char *to);
/* Sets what the record is about: the value (or the change) after the action
 * on the first line if it fits there, or else as the first field. */
void bof_modlog_set_field(bof_modlog_t *m, const char *label,
                          const char *fmt, ...) BOF_PRINTF(3, 4);
void bof_modlog_set_change(bof_modlog_t *m, const char *label,
                           const char *from, const char *to);
/* The bits changed from from to to, as "+name -name" (names[i] for bit i):
 * as a field, or as what the record is about. Nothing if none changed. */
void bof_modlog_add_flags(bof_modlog_t *m, const char *label, uint32_t from,
                          uint32_t to, const char * const *names, int nnames);
void bof_modlog_set_flags(bof_modlog_t *m, const char *label, uint32_t from,
                          uint32_t to, const char * const *names, int nnames);
/* Adds the field "label: value" only to the audit record (e.g. what was
 * removed). */
void bof_modlog_add_audit(bof_modlog_t *m, const char *label,
                          const char *fmt, ...) BOF_PRINTF(3, 4);
/* A post as "date author title", as in the list; and as a field. */
void bof_modlog_format_post(char *buf, size_t sz,
                            const struct fileheader_t *fh);
void bof_modlog_add_post(bof_modlog_t *m, const struct fileheader_t *fh);

/* Formats the records of m by actor for the public modlog (*msg) and the
 * audit record (*amsg), to free(), e.g. for a preview. The reason is
 * left out of the public one if reason_audit_only (actions also recorded
 * in the police log). Returns -1 if out of memory. */
int bof_modlog_format(const bof_actor_t *actor, const char *reason,
                      int reason_audit_only, const bof_modlog_t *m,
                      char **msg, char **amsg);
/* Writes the records of m into the modlog of community. Returns 0, or -1
 * (invalid arguments, out of memory). */
int bof_modlog_write(const char *community, const bof_actor_t *actor,
                     const char *reason, int reason_audit_only,
                     const bof_modlog_t *m);
/* Notes in the audit record that actor edited the public modlog. Returns
 * 0, or -1 if it could not be written. */
int bof_modlog_note_edit(const char *community, const bof_actor_t *actor);
/* Where the public modlog (or the audit record) of community is (Lemmy's
 * GetModlog reads it from the database). */
void bof_get_modlog_path(const char *community, int audit, char *path,
                         size_t sz);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_H */
