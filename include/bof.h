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

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_BOF_H */
