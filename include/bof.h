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
    BOF_ERR_COULDNT_FIND_PERSON,    /* couldnt_find_person */
    BOF_ERR_BANNED_FROM_COMMUNITY,  /* banned_from_community (muted) */
    BOF_ERR_BBS_NOT_ALLOWED,        /* bbs_not_allowed: by a BBS rule */
    BOF_ERR_NOT_AN_ADMIN,           /* not_an_admin */
    BOF_ERR_MAX
} bof_error_t;

/* Returns the Lemmy name of an error, e.g. "couldnt_find_post". */
const char *bof_error_name(bof_error_t err);

/* Room for the names of communities (boards) and of the files of posts. */
#define BOF_COMMUNITY_NAME_MAX 16
#define BOF_POST_FILE_MAX 32

/* ------------------------------------------------------------------------
 * People
 * ------------------------------------------------------------------------ */

/* A person (Lemmy's person.id, an int32 serial). 0 means unknown or the
 * system. People get their IDs when accounts move to a database; until
 * then, bof knows a person by the BBS userid in bof_actor_t. The BBS
 * account behind a person (Lemmy's local_user) is its usernum and
 * firstlogin, as usernums of deleted accounts are reused. */
typedef int32_t bof_person_id_t;

/* A person as Lemmy shows one (GetPersonDetails), with what the rules of
 * the BBS need (bof_bbs_can_*()); nothing private (no mail address, real
 * name, IP or money). Texts are in the encoding of the files. */
typedef struct bof_person_t {
    bof_person_id_t id;        /* 0 until people have IDs */
    char name[16];             /* the BBS userid */
    char display_name[24];     /* the BBS nickname */
    int32_t published;         /* when the account was made (firstlogin) */
    int admin;                 /* Lemmy's admin: a sysop of the BBS */
    /* PTT: the account (as bof_actor_t.bbs_local_user), its permissions
     * (userlevel) and the counts the rules of boards look at. */
    uint64_t bbs_local_user;
    uint32_t bbs_perms;
    int bbs_numlogindays;
    int bbs_numposts;
    int bbs_badpost;
    int bbs_over_18;
} bof_person_t;

/* Lemmy's GetPersonDetails (the person only): from the BBS accounts.
 * Errors: couldnt_find_person. */
bof_error_t bof_get_person(const char *name, bof_person_t *out);

/* Who calls bof (Lemmy's authenticated local user). With bbs_person, bof
 * checks that the actor may do what it asks (as Lemmy does): the rules of
 * the BBS (bof_bbs_can_*()), being the author (no_post_edit_allowed) or a
 * moderator (not_a_moderator); without, the caller has checked. */
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
    /* The person, for bof to check the rules (bof_bbs_can_*()) itself, as
     * Lemmy does for each call; NULL: the caller has checked them (mbbsd,
     * which also posts for the system where the user could not). */
    const struct bof_person_t *bbs_person;
} bof_actor_t;

/* ------------------------------------------------------------------------
 * Communities
 * ------------------------------------------------------------------------ */

/* Lemmy's community.id: as post.svc gives it (boards are known by their
 * names, and their places in the board cache are taken again by new
 * boards); none in the files backend. */
typedef int32_t bof_community_id_t;

#define BOF_MODERATORS_MAX 8

/* A community (board) as Lemmy shows one (GetCommunity), with what the
 * rules of the BBS need (bof_bbs_can_*()). Texts are in the encoding of
 * the files. */
typedef struct bof_community_t {
    bof_community_id_t id;     /* 0 unless the backend keeps IDs */
    char name[BOF_COMMUNITY_NAME_MAX];
    char title[64];            /* the description of the board */
    int nsfw;                  /* only for adults (BRD_OVER18) */
    int hidden;                /* a hidden board (Lemmy's visibility) */
    /* PTT: its place in the board cache (bid; taken again by a new board
     * once it is removed), its attributes (brdattr) and the permissions to
     * read it (level), whether the system keeps it read only, and the
     * limits for posting (as the board header keeps them). */
    int bbs_id;
    uint32_t bbs_attr;
    uint32_t bbs_level;
    int bbs_read_only;
    int bbs_post_limit_logins;
    int bbs_post_limit_badpost;
    /* Its moderators, as accounts (see bof_actor_t.bbs_local_user). */
    uint64_t moderators[BOF_MODERATORS_MAX];
    int nmoderators;
} bof_community_t;

/* Lemmy's GetCommunity, by name: from the board cache of the BBS (each
 * process of the BBS attaches it). Errors: couldnt_find_community. */
bof_error_t bof_get_community(const char *name, bof_community_t *out);

/* PTT: the same by its place in the board cache (bbs_id, 1-based), for a
 * user interface going through all of them (the lists of boards); its id
 * is left 0, as the backend may have to be asked (bof_bbs_community_id()).
 * Errors: couldnt_find_community. */
bof_error_t bof_bbs_get_community_at(int bbs_id, bof_community_t *out);

/* Lemmy's ListCommunities, in the order of the board cache, page by page;
 * hidden boards and boards with a reading level only if bbs_all (the
 * caller then checks bof_bbs_can_read() for whom it lists them). */
typedef struct bof_list_communities_t {
    int page;                  /* 1-based, of limit each; 0: the first */
    int limit;
    int bbs_all;
} bof_list_communities_t;

typedef struct bof_list_communities_response_t {
    bof_community_t *communities;   /* by the caller, room for limit */
    int count;
} bof_list_communities_response_t;

bof_error_t bof_list_communities(const bof_list_communities_t *req,
                                 bof_list_communities_response_t *out);

/* The ID of a community (Lemmy's community.id), into *id (0 if none).
 * Errors: couldnt_find_community, bbs_storage_not_supported (the files
 * backend). */
bof_error_t bof_bbs_community_id(const char *name, bof_community_id_t *id);

/* ------------------------------------------------------------------------
 * The rules of the BBS (who may do what)
 * ------------------------------------------------------------------------ */

struct fileheader_t;

/* PTT: which rule of the BBS keeps a person from doing something, so that
 * the UI can say why in its own words; the calls return the Lemmy error as
 * well. */
typedef enum bof_bbs_rule_t {
    BOF_BBS_RULE_NONE = 0,
    BOF_BBS_RULE_NO_COMMUNITY,      /* there is no such board */
    BOF_BBS_RULE_READ_ONLY,         /* the system keeps it read only */
    BOF_BBS_RULE_MUTED,             /* muted in the board */
    BOF_BBS_RULE_NOT_VERIFIED,      /* no permission to post (PERM_POST) */
    BOF_BBS_RULE_RESTRICTED,        /* only its friends post (restricted) */
    BOF_BBS_RULE_UNPAID_TICKET,     /* a ticket is not paid (VIOLATELAW) */
    BOF_BBS_RULE_LEVEL,             /* the level the board asks for */
    BOF_BBS_RULE_TOO_FEW_LOGINS,    /* fewer login days than the board asks */
    BOF_BBS_RULE_TOO_MANY_BADPOSTS, /* more bad posts than the board allows */
    BOF_BBS_RULE_NO_COMMENTS,       /* comments are closed (board or post) */
    BOF_BBS_RULE_ONLY_OPS_COMMENT,  /* only operators comment in the board */
    BOF_BBS_RULE_DELETED,           /* the post is deleted */
    BOF_BBS_RULE_HIDDEN,            /* a hidden board, not a friend of it */
    BOF_BBS_RULE_ADULTS_ONLY,       /* only for adults */
} bof_bbs_rule_t;

/* PTT: whether person is a moderator of community (one of its moderators,
 * as the same account). */
int bof_bbs_is_moderator(const bof_person_t *person,
                         const bof_community_t *community);

/* PTT: the groups of boards (a board is in a group, and groups in groups)
 * have moderators too, as Discourse's category moderators (Lemmy has no
 * groups of communities). What person is by moderating a group community
 * is in (a group that lists its boards, up the chain): a group operator
 * (BOF_BBS_GROUP_OP), and also a group lead (BOF_BBS_GROUP_LEAD) for a
 * group right under the root of the classes; 0 if neither. */
#define BOF_BBS_GROUP_OP    (1)
#define BOF_BBS_GROUP_LEAD  (2)
int bof_bbs_group_roles(const bof_person_t *person,
                        const bof_community_t *community);

/* PTT: whether person may moderate community: an admin (sysop) or board
 * admin (PERM_BOARDS), one of its moderators, or a group operator of it
 * (bof_bbs_group_roles()) while it has no moderators of its own, as the
 * BBS gives the moderator mode. bof checks this for an actor with a
 * person (not_a_moderator). */
int bof_bbs_may_moderate(const bof_person_t *person,
                         const bof_community_t *community);

/* PTT: whether person may read community. Errors: couldnt_find_community
 * (hidden), bbs_not_allowed; *rule (if not NULL) tells which rule. When
 * allowed, *rule is BOF_BBS_RULE_HIDDEN if only because the hidden board
 * does not hide its posts (BRD_HIDE without BRD_POSTMASK; not a friend of
 * it), else BOF_BBS_RULE_NONE. */
bof_error_t bof_bbs_can_read(const bof_person_t *person,
                             const bof_community_t *community,
                             bof_bbs_rule_t *rule);

/* PTT: the same for the community at bbs_id in the board cache (as
 * bof_bbs_get_community_at()), filling only what the rules read: for
 * the lists of boards, which check every board. */
bof_error_t bof_bbs_can_read_at(const bof_person_t *person, int bbs_id,
                                bof_bbs_rule_t *rule);

/* PTT: whether person may post in community, by its permissions (the
 * board's attributes and level, muting, tickets) and by the limits of the
 * board (login days, bad posts), which its moderators and sysops pass.
 * Errors: couldnt_find_community, banned_from_community,
 * bbs_not_allowed. */
bof_error_t bof_bbs_post_permission(const bof_person_t *person,
                                    const bof_community_t *community,
                                    bof_bbs_rule_t *rule);
bof_error_t bof_bbs_post_restriction(const bof_person_t *person,
                                     const bof_community_t *community,
                                     bof_bbs_rule_t *rule);
bof_error_t bof_bbs_can_post(const bof_person_t *person,
                             const bof_community_t *community,
                             bof_bbs_rule_t *rule);

/* PTT: whether person may comment on post (its record) in community: as
 * for posting, and only where comments are open (not closed on the board
 * or the post, the post not locked nor deleted). Errors: as
 * bof_bbs_can_post(), and locked. */
bof_error_t bof_bbs_can_comment(const bof_person_t *person,
                                const bof_community_t *community,
                                const struct fileheader_t *post,
                                bof_bbs_rule_t *rule);

/* PTT: until when person is muted in community, or 0 if not. */
int32_t bof_bbs_muted_until(const bof_person_t *person,
                            const bof_community_t *community);

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
    const char *bbs_dir;                    /* the list (.DIR) */
    int bbs_index;                          /* 1-based position in bbs_dir */
    const struct fileheader_t *bbs_header;  /* the record there */
} bof_post_ref_t;

/* Lemmy's post.id: a global serial (int32, never used again), as post.svc
 * gives its posts. The files of a board keep no such ID (a record has no
 * room for one), so the calls with IDs need a backend that keeps posts in
 * a database; the files backend gives
 * bbs_storage_not_supported. */
typedef int32_t bof_post_id_t;
#define BOF_POST_ID_MAX 0x7fffffff

/* Lemmy's comment.id: the post and the floor of the comment, as post.svc
 * makes it (a comment is known by its post and floor, as on PTT and in
 * Discourse); it needs no counter of its own and is never used again. */
typedef int64_t bof_comment_id_t;
#define BOF_FLOOR_MAX 0xffff

static inline bof_comment_id_t
bof_comment_id(bof_post_id_t post_id, int floor)
{
    if (post_id <= 0 || floor <= 0 || floor > BOF_FLOOR_MAX)
        return 0;
    return (bof_comment_id_t)post_id << 16 | (bof_comment_id_t)floor;
}

/* The post and floor of a comment ID. Returns 1, or 0 if it is not one. */
static inline int
bof_comment_id_split(bof_comment_id_t id, bof_post_id_t *post_id, int *floor)
{
    int64_t p = id >> 16;
    int f = (int)(id & BOF_FLOOR_MAX);

    if (id <= 0 || p <= 0 || p > BOF_POST_ID_MAX || f <= 0)
        return 0;
    *post_id = (bof_post_id_t)p;
    *floor = f;
    return 1;
}

/* What names a post across backends: its board and the name of its file
 * (the time and the random part in it, as PTT's article IDs, AIDs). */
typedef struct bof_post_key_t {
    char community[BOF_COMMUNITY_NAME_MAX];
    char bbs_post_file[BOF_POST_FILE_MAX];
} bof_post_key_t;

/* The ID of a post (Lemmy's post.id). Errors: couldnt_find_post,
 * bbs_storage_not_supported (the files backend). */
bof_error_t bof_get_post_id(const bof_post_ref_t *post, bof_post_id_t *id);

/* The post an ID names. Errors: couldnt_find_post,
 * bbs_storage_not_supported (the files backend). */
bof_error_t bof_get_post_key(bof_post_id_t id, bof_post_key_t *key);

/* PTT: where the post named by key is now: its board's list (bbs_dir),
 * its position there (bbs_index) and its record (into *bbs_header, room by
 * the caller), so that a bof_post_ref_t can be made of them, e.g. from an
 * ID (bof_get_post_key()). Errors: couldnt_find_community,
 * couldnt_find_post. */
typedef struct bof_found_post_t {
    char bbs_dir[BOF_PATH_MAX];
    int bbs_index;
    struct fileheader_t *bbs_header;
} bof_found_post_t;

bof_error_t bof_bbs_find_post(const bof_post_key_t *key,
                              bof_found_post_t *out);

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

/* Lemmy's CreatePost. Its content (Lemmy's body) is given as a whole file
 * the caller wrote, as the editor of the BBS writes it (bbs_content_path:
 * bof copies it into place, and the caller removes its own), or as the
 * body alone (body, in the encoding of the files), which bof writes as a
 * post of the BBS, with the header of its author, board, title and time
 * and the line of where it was posted from (bof_bbs_format_post_header(),
 * bof_bbs_format_post_origin()). Without either, the content is at its
 * place already or there is none (PTT: the copies in ALLPOST). With
 * bbs_header->filename empty, bof names the post; out->bbs_header gets the
 * record as listed. */
typedef struct bof_create_post_t {
    const char *community;                  /* board name */
    const char *bbs_dir;                    /* the list (.DIR) to add it to */
    /* Its record: the title (Lemmy's name), author, modes, money; the file
     * name (or "" for bof to name it) and date. */
    const struct fileheader_t *bbs_header;
    const char *bbs_content_path;
    const char *body;
    const char *bbs_nickname;  /* of the author, for the header of body */
    /* A crosspost (Reddit's crosspost_parent): the post it was copied
     * from, by its board and file name; NULL if none. */
    const char *bbs_crosspost_community;
    const char *bbs_crosspost_file;
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
     * the reason. NULL: not recorded. */
    const struct bof_modlog_t *bbs_modlog;
} bof_remove_posts_t;

bof_error_t bof_remove_posts(const bof_actor_t *actor,
                             const bof_remove_posts_t *req, int *count);

/* Lemmy's PurgePost, for the posts of a board that bbs_selected selects
 * (the posts expired by the limits of the board): gone for good, neither
 * archived nor left as tombstones. bbs_selected may be called once per
 * backend, so it should not keep state (e.g. pick the posts beforehand).
 * *count gets how many were purged. */
typedef struct bof_purge_posts_t {
    const char *community;
    const char *bbs_dir;
    int (*bbs_selected)(const struct fileheader_t *fh, int index, void *ctx);
    void *bbs_ctx;
    const char *reason;     /* may be NULL */
} bof_purge_posts_t;

bof_error_t bof_purge_posts(const bof_actor_t *actor,
                            const bof_purge_posts_t *req, int *count);

/* Lemmy's CreatePostLike, a vote alone (score +1, -1 or 0 to take it
 * back), for boards that keep votes apart from comments. One vote per
 * person. The files backend keeps votes only with comments: there, it is
 * bbs_storage_not_supported. */
typedef struct bof_create_post_like_t {
    bof_post_ref_t post;
    int score;
} bof_create_post_like_t;

typedef struct bof_post_like_response_t {
    /* If not NULL, gets the updated record of the post. */
    struct fileheader_t *bbs_header;
    int bbs_score_delta;       /* how the score of the post changed */
} bof_post_like_response_t;

bof_error_t bof_create_post_like(const bof_actor_t *actor,
                                 const bof_create_post_like_t *req,
                                 bof_post_like_response_t *out);

/* PTT: whether the content of a post is still of version ver (see
 * bof_bbs_get_post_body()), to tell early that an edit would fail. */
int bof_bbs_post_version_matches(const bof_post_ref_t *post,
                                 const bof_post_version_t *ver);

/* PTT: the file to read a post from, up to date, into path (a backend that
 * keeps posts elsewhere writes it first, as a cache of them), so that the
 * UI reads posts without knowing where they are kept. Errors:
 * couldnt_find_post. */
bof_error_t bof_bbs_get_post_file(const bof_post_ref_t *post, char *path,
                                  size_t sz);

/* PTT: the earlier versions of a post the time capsule keeps (Discourse's
 * post_revisions; Lemmy keeps none): how many (*count; 1 is the oldest),
 * and the file to read version rev from. Also for a post deleted or
 * removed, by its record in the recycle bin (bof_bbs_list_removed_posts()).
 * Errors: couldnt_find_post (version rev is not kept). */
bof_error_t bof_bbs_get_post_revisions(const bof_post_ref_t *post,
                                       int *count);
bof_error_t bof_bbs_get_post_revision_file(const bof_post_ref_t *post,
                                           int rev, char *path, size_t sz);

/* PTT: the posts deleted or removed from a list, as the time capsule keeps
 * them (its recycle bin): their records from the start-th (0 is the
 * oldest), up to limit, and how many it keeps (bbs_total). */
typedef struct bof_bbs_list_removed_posts_t {
    const char *community;
    const char *bbs_dir;
    int start;
    int limit;
} bof_bbs_list_removed_posts_t;

typedef struct bof_bbs_list_removed_posts_response_t {
    struct fileheader_t *posts;     /* by the caller, room for limit */
    int count;
    int bbs_total;
} bof_bbs_list_removed_posts_response_t;

bof_error_t bof_bbs_list_removed_posts(
    const bof_bbs_list_removed_posts_t *req,
    bof_bbs_list_removed_posts_response_t *out);

/* PTT: names a new post of community before it is created (e.g. to put its
 * address, which comes from the name, in its content): its file name and
 * date into fh, the rest of fh kept. The name is taken until
 * bof_create_post() uses it. Errors: couldnt_find_community. */
bof_error_t bof_bbs_name_post(const char *community, struct fileheader_t *fh);

/* PTT: the header a post of the BBS starts with, as its editor writes it:
 * the lines of its author (with nickname) and board (none if community is
 * NULL, as for mails), title and time, each ending with a newline, into buf.
 * Returns the length, or -1 if it does not fit. */
int bof_bbs_format_post_header(char *buf, size_t sz, const char *author,
                               const char *nickname, const char *community,
                               const char *title, int32_t when);

/* PTT: the line that ends a post of the BBS (after "--"): the site and
 * where it was posted from. Returns the length, or -1 if it does not fit. */
int bof_bbs_format_post_origin(char *buf, size_t sz, const char *from);

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
    /* PTT: do not keep the old body in the time capsule, for changes made
     * by the system that come often (a petition signed). Lemmy keeps no
     * revisions at all; Discourse (post_revisions) skips them for such
     * changes too. */
    int bbs_no_revision;
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
     * the reason. NULL: not recorded. */
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
     * the reason. NULL: not recorded. */
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
     * the reason. NULL: not recorded. */
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
    /* The comment, as the lines for the file; if NULL, bof makes the line
     * from bbs_text (bof_bbs_format_comment()) as the board shows its
     * comments: the actor's name (aligned if BRD_ALIGNEDCMT), where from
     * (actor->from, if BRD_COMMENT_SHOWIP) and now. */
    const char *content;
    /* What the commenter typed (Lemmy's content), for backends that keep
     * comments apart from the file; NULL if only content is known. */
    const char *bbs_text;
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
    /* The floor of the comment, if the backend numbers them (0 if not),
     * and whether it was merged into the commenter's previous one. */
    int bbs_floor;
    int bbs_merged;
} bof_comment_response_t;

/* Errors: couldnt_find_post (no file), bbs_read_only,
 * couldnt_create_comment (the file stayed locked), couldnt_update_post
 * (appended, but the record of the post has changed meanwhile),
 * invalid_body_field (neither content nor bbs_text). */
bof_error_t bof_create_comment(const bof_actor_t *actor,
                               const bof_create_comment_t *req,
                               bof_comment_response_t *out);

/* A comment as found in the file of a post (Lemmy's CommentView, in
 * part). Comments in files have no IDs: they are numbered by their floor,
 * the order among the comments of the post (counting removed ones, whose
 * lines note it), as post.svc keys them by (post, floor). */
#define BOF_COMMENT_CONTENT_LEN (1024)

typedef struct bof_comment_t {
    int bbs_floor;           /* 1-based */
    int bbs_vote;            /* +1 (push), -1 (boo) or 0 (arrow) */
    int removed;             /* only a note of its removal is left */
    char creator[16];        /* BBS userid */
    /* Without the padding. One line in files; backends that keep comments
     * apart (post.svc) may give several, separated by '\n'. */
    char content[BOF_COMMENT_CONTENT_LEN];
    char bbs_tail[64];       /* what follows: where from and when */
    int bbs_width;           /* the room for the content in the line */
    /* Edited (Lemmy's updated is a time; a line keeps only the mark of an
     * edit, BOF_COMMENT_EDITED_MARK before its date). */
    int bbs_edited;
} bof_comment_t;

#define BOF_COMMENT_EDITED_MARK '~'

/* PTT: a comment of the BBS is one line as wide as the screen: its mark
 * (push, boo or arrow by vote: +1, -1, 0), the author as shown
 * (author_shown: the userid, padded for aligned boards, or the nickname
 * of an angel), what was typed, where from (from; NULL if the board does
 * not show it) and when. bof_bbs_comment_width() is the room left for what
 * is typed; bof_bbs_format_comment() makes the line (with its newline),
 * text cut to the room, control characters made spaces. */
int bof_bbs_comment_width(const char *author_shown, int show_from);
void bof_bbs_format_comment(char *buf, size_t sz, int vote,
                            const char *author_shown, const char *text,
                            const char *from, int32_t when);


/* Lemmy's GetComments, for one post, in order. */
typedef struct bof_get_comments_t {
    bof_post_ref_t post;     /* bbs_index is not checked (may be 0) */
    const char *bbs_creator; /* only those by them, or NULL for all */
    int page;                /* 1-based, of limit each; 0: the first */
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
 * have no IDs yet, so the comment is given by its floor (as
 * bof_get_comments() numbers them) and what it is: the comment at
 * bbs_floor must still be of bbs_vote by bbs_author saying bbs_content
 * (else couldnt_find_comment); without a floor, the first line of the file
 * that looks like such a comment is it. That line is replaced with a note
 * of who removed it and why, and the file before is kept as a revision in
 * the time capsule. Unlike Lemmy (removed: false), a removed comment
 * cannot be restored: its line is gone. */
typedef struct bof_remove_comment_t {
    bof_post_ref_t post;       /* bbs_index is not checked (may be 0) */
    int bbs_floor;             /* the floor if known; 0: found by the rest */
    int bbs_vote;              /* +1 (push), -1 (boo) or 0 (arrow) */
    const char *bbs_author;    /* the BBS userid who commented */
    const char *bbs_content;   /* what the comment says */
    const char *reason;        /* may be NULL */
    /* PTT: the record of this action for the modlog of the board, written
     * when it is done (as Lemmy writes a mod_* row with the action), with
     * the reason. NULL: not recorded. */
    const struct bof_modlog_t *bbs_modlog;
} bof_remove_comment_t;

/* Errors: couldnt_find_post (no file), couldnt_find_comment (no such line,
 * e.g. the post was edited). out->bbs_header (if not NULL) gets the
 * record of the post, which does not change. */
bof_error_t bof_remove_comment(const bof_actor_t *actor,
                               const bof_remove_comment_t *req,
                               bof_comment_response_t *out);

/* Discourse's post_revisions, for a comment (Lemmy keeps none): how the
 * comment at bbs_floor was before each change, oldest first. Comments in
 * files keep no revisions of their own (the time capsule keeps the whole
 * post): there, it is bbs_storage_not_supported. */
typedef struct bof_comment_revision_t {
    int number;              /* 1-based */
    int32_t edited_at;       /* when it was changed */
    char editor[16];         /* who changed it */
    int deleted;             /* the change took it out (deleted, removed) */
    char reason[64];         /* why, if it was taken out */
    char content[BOF_COMMENT_CONTENT_LEN];  /* what it said then */
} bof_comment_revision_t;

typedef struct bof_get_comment_revisions_t {
    bof_post_ref_t post;     /* bbs_index is not checked (may be 0) */
    int bbs_floor;
    int limit;               /* room in revisions */
} bof_get_comment_revisions_t;

typedef struct bof_get_comment_revisions_response_t {
    bof_comment_revision_t *revisions;  /* by the caller, room for limit */
    int count;
} bof_get_comment_revisions_response_t;

/* Errors: couldnt_find_comment, bbs_storage_not_supported. */
bof_error_t bof_get_comment_revisions(
    const bof_get_comment_revisions_t *req,
    bof_get_comment_revisions_response_t *out);

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
    const char *bbs_dir;         /* NULL: the list of community itself */
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
/* For a gateway serving many persons one call at a time: makes userid the
 * person in session, saving the records of the one before (finalize) if
 * it was another; nothing if it is the same. Loading the records of a
 * person on each switch costs a read of their file; a gateway may keep
 * the calls of one person together (or one process per person). */
void bof_bbs_read_marks_switch(const char *userid, int savable,
                               int32_t login_time);

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
    char bbs_post_file[BOF_POST_FILE_MAX];  /* the post's file name */
    char original_post_name[80];   /* its title */
    char original_comment_text[256];
    char reason[128];
    char bbs_community[BOF_COMMUNITY_NAME_MAX]; /* the board */
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

/* For an actor with a person: the moderators of the board
 * (bof_bbs_may_moderate(): not_a_moderator), or for the site's, its admins
 * and the police (not_an_admin). */
bof_error_t bof_list_reports(const bof_actor_t *actor,
                             const bof_list_reports_t *req,
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
