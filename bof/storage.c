/*
 * storage.c - bof (Birds Of Feather): the storage virtualization, and the
 * calls of the bof API that go through it.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * See bof_storage.h: every call goes to the one backend. The work of the
 * bof API that is not about storage (the checks of the actor, the modlog
 * of moderation actions) is done here, once, for any backend.
 */

#include "bof_int.h"
#include "bof_storage.h"

// The one backend; NULL until set (or first used: the default one).
static const bof_storage_ops_t *backend;
static void *backend_ctx;

void
bof_storage_set(const bof_storage_ops_t *ops, void *ctx)
{
    backend = ops;
    backend_ctx = ops ? ctx : NULL;
}

void
bof_storage_reset(void)
{
    bof_storage_set(NULL, NULL);
}

// The backend when none was set: BOF_DEFAULT_STORAGE (config.h), the
// files backend unless pttbbs.conf names another (a bof_storage_ops_t
// declared in bof_storage.h).

static const bof_storage_ops_t *
get_backend(void)
{
    if (!backend)
        bof_storage_set(&BOF_DEFAULT_STORAGE, NULL);
    return backend;
}

// Calls op of the backend; bbs_storage_not_supported if it has none.
// Writes and reads go the same way; they are named apart to show which
// calls change things.
#define CALL(op, ...) ({                                                    \
    const bof_storage_ops_t *b_ = get_backend();                            \
    b_->op ? b_->op(backend_ctx, __VA_ARGS__) :                             \
        BOF_ERR_BBS_STORAGE_NOT_SUPPORTED; })
#define WRITE(op, out, ...) CALL(op, __VA_ARGS__, (out))
#define READ(op, ...) CALL(op, __VA_ARGS__)

// Writes a record of the modlog. Returns 0, or -1.
static int
modlog_write_all(const char *community, const bof_actor_t *actor,
                 const char *reason, int reason_audit_only,
                 const bof_modlog_t *m)
{
    const bof_storage_ops_t *b = get_backend();

    if (!b->write_modlog ||
        b->write_modlog(backend_ctx, community, actor, reason,
                        reason_audit_only, m) != 0)
        return -1;
    return 0;
}

// Writes the record of a moderation action, once it is done.
static void
modlog_done(const char *community, const bof_actor_t *actor,
            const char *reason, const bof_modlog_t *m)
{
    if (m && actor && community)
        modlog_write_all(community, actor, reason, 0, m);
}

/* ------------------------------------------------------------------------
 * The checks of requests, and of an actor with a person
 *
 * For any caller, names and paths in a request must stay where they
 * belong: a file name is a name in its list, and a list is in the
 * directory of its board. For an actor with a person (a gateway, as Lemmy
 * checks each call), bof also takes nothing about the post from the
 * caller: the list is the board's own, the record is read again from it,
 * the author is the actor and the line of a comment is made by bof.
 * ------------------------------------------------------------------------ */

int
bof_bbs_valid_post_file(const char *name)
{
    size_t n;

    if (!name || !isalpha((unsigned char)name[0]))
        return 0;
    for (n = 0; name[n]; n++) {
        unsigned char ch = (unsigned char)name[n];
        if (!isalnum(ch) && ch != '.' && ch != '_' && ch != '-')
            return 0;
    }
    return n < FNLEN;
}

// Whether a file name given by any caller cannot leave its list: no '/',
// and not one of the files of the list itself (.DIR, ...). "" is a record
// without a file, FN_SAFEDEL the name tombstones keep.
static int
safe_post_file(const char *name)
{
    if (strcmp(name, FN_SAFEDEL) == 0)
        return 1;
    return !strchr(name, '/') && name[0] != '.' && strlen(name) < FNLEN;
}

// Whether path has no way up ("..").
static int
safe_path(const char *path)
{
    return path && !strstr(path, "..");
}

// Whether dir is a list in the directory of community (boards/c/Name/),
// or, without a community, of some board.
static int
dir_in_community(const char *community, const char *dir)
{
    size_t n;

    if (!safe_path(dir))
        return 0;
    if (!community)
        return strncmp(dir, "boards/", 7) == 0;
    if (!is_valid_brdname(community))
        return 0;
    // Under boards/<c>/<community>/ (as setbpath() makes it), compared
    // without making it: lists are read record by record through here.
    n = strlen(community);
    return strncasecmp(dir, "boards/", 7) == 0 &&
           tolower((unsigned char)dir[7]) ==
               tolower((unsigned char)community[0]) &&
           dir[8] == '/' && strncasecmp(dir + 9, community, n) == 0 &&
           dir[9 + n] == '/' && dir[10 + n];
}

// Whether text (a title) is free of control codes (ANSI colors and line
// breaks are not for a title given by a person).
static int
plain_text(const char *text)
{
    for (; text && *text; text++)
        if ((unsigned char)*text < ' ' || *text == 0x7f)
            return 0;
    return 1;
}

static const bof_person_t *
person_of(const bof_actor_t *actor)
{
    return actor ? actor->bbs_person : NULL;
}

// A list as bof will use it: for an actor with a person, the list of the
// board itself (.DIR, by the name the board has), whatever the caller
// gave; else what was given, if it is in the board.
typedef struct {
    char community[BOF_COMMUNITY_NAME_MAX];
    char dir[PATHLEN];
} gated_dir_t;

static bof_error_t
gate_dir(const bof_actor_t *actor, const char **community, const char **dir,
         gated_dir_t *g)
{
    bof_community_t c;

    if (!person_of(actor)) {
        if (*dir && !dir_in_community(*community, *dir))
            return BOF_ERR_INVALID_BODY_FIELD;
        return BOF_OK;
    }
    if (!*community || bof_get_community(*community, &c) != BOF_OK)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    strlcpy(g->community, c.name, sizeof(g->community));
    setbfile(g->dir, g->community, FN_DIR);
    *community = g->community;
    *dir = g->dir;
    return BOF_OK;
}

// A post as bof will use it: for an actor with a person, in the list of
// its board, with its record read again from there (at post->bbs_index if
// it is still there, else found by its file name), so the checks look at
// the author and modes the list has; else as given, if its names are safe.
typedef struct {
    gated_dir_t d;
    fileheader_t fh;
    bof_post_ref_t ref;
} gated_post_t;

static bof_error_t
gate_post(const bof_actor_t *actor, const bof_post_ref_t *in,
          gated_post_t *g)
{
    const char *community = in->community, *dir = in->bbs_dir;
    bof_error_t err;

    g->ref = *in;
    if (in->bbs_header && !safe_post_file(in->bbs_header->filename))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!person_of(actor)) {
        if (dir && !dir_in_community(community, dir))
            return BOF_ERR_INVALID_BODY_FIELD;
        return BOF_OK;
    }
    if (!in->bbs_header || !bof_bbs_valid_post_file(in->bbs_header->filename))
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((err = gate_dir(actor, &community, &dir, &g->d)) != BOF_OK)
        return err;
    g->ref.community = community;
    g->ref.bbs_dir = dir;
    g->ref.bbs_header = &g->fh;
    if (in->bbs_index > 0 &&
        get_fileheader(dir, &g->fh, in->bbs_index) > 0 &&
        strcmp(g->fh.filename, in->bbs_header->filename) == 0)
        return BOF_OK;

    bof_post_key_t key;
    bof_found_post_t found = { .bbs_header = &g->fh };
    strlcpy(key.community, community, sizeof(key.community));
    strlcpy(key.bbs_post_file, in->bbs_header->filename,
            sizeof(key.bbs_post_file));
    if ((err = READ(find_post, &key, &found)) != BOF_OK)
        return err == BOF_ERR_BBS_STORAGE_NOT_SUPPORTED ?
            BOF_ERR_COULDNT_FIND_POST : err;
    g->ref.bbs_index = found.bbs_index;
    return BOF_OK;
}

// Replaces the post field of *req (of type T) with the post gate_post()
// gives, for the rest of the call.
#define GATE_POST(T, field) \
    T gated_req_ = *req; \
    gated_post_t gated_post_; \
    do { \
        bof_error_t ge_ = gate_post(actor, &req->field, &gated_post_); \
        if (ge_ != BOF_OK) \
            return ge_; \
        gated_req_.field = gated_post_.ref; \
        req = &gated_req_; \
    } while (0)

// Replaces the community and bbs_dir of *req (of type T) as gate_dir().
#define GATE_DIR(T) \
    T gated_req_ = *req; \
    gated_dir_t gated_dir_; \
    do { \
        bof_error_t ge_ = gate_dir(actor, &gated_req_.community, \
                                   &gated_req_.bbs_dir, &gated_dir_); \
        if (ge_ != BOF_OK) \
            return ge_; \
        req = &gated_req_; \
    } while (0)

// For a call without an actor (reads): the names of the post must be safe.
#define SAFE_POST(post) do { \
    gated_post_t sp_; \
    bof_error_t se_ = gate_post(NULL, (post), &sp_); \
    if (se_ != BOF_OK) \
        return se_; \
} while (0)

typedef enum {
    MAY_READ,       // read the community (e.g. to report)
    MAY_POST,       // post in it
    MAY_COMMENT,    // comment on (or vote on) post
    MAY_EDIT,       // change post as its author, or as a moderator
    MAY_DELETE,     // delete post as its creator only (Lemmy's DeletePost)
    MAY_MODERATE,   // as a moderator of the community (or an admin)
} may_t;

// Whether p created post (Lemmy's Post::is_post_creator): the author shown
// in the list, and not an older account of the same name, as mbbsd's
// is_file_owner() checks (posts made before the account are not its own).
static int
is_post_creator(const bof_person_t *p, const fileheader_t *post)
{
    if (!post || strcasecmp(post->owner, p->name) != 0)
        return 0;
    if (p->published) {
        time4_t ts = get_fhdr_stamp_ts(post->filename);
        if (!ts || !time4_ge(ts, p->published))
            return 0;
    }
    return 1;
}

static bof_error_t
check_actor(const bof_actor_t *actor, const char *community, may_t may,
            const fileheader_t *post)
{
    const bof_person_t *p = person_of(actor);
    bof_community_t c;
    bof_error_t err;

    if (!p)
        return BOF_OK;      // the caller has checked
    if (!community || bof_get_community(community, &c) != BOF_OK)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    if ((err = bof_bbs_can_read(p, &c, NULL)) != BOF_OK)
        return err;
    switch (may) {
    case MAY_READ:
        return BOF_OK;
    case MAY_POST:
        return bof_bbs_can_post(p, &c, NULL);
    case MAY_COMMENT:
        return bof_bbs_can_comment(p, &c, post, NULL);
    case MAY_EDIT:
        if (bof_bbs_may_moderate(p, &c) ||
            (post && strcasecmp(post->owner, p->name) == 0))
            return BOF_OK;
        return BOF_ERR_NO_POST_EDIT_ALLOWED;
    case MAY_DELETE:
        // Moderators take others' posts down with RemovePost, which is
        // recorded with a reason; as in Lemmy, DeletePost is not for them.
        return is_post_creator(p, post) ? BOF_OK :
            BOF_ERR_NO_POST_EDIT_ALLOWED;
    case MAY_MODERATE:
        return bof_bbs_may_moderate(p, &c) ? BOF_OK :
            BOF_ERR_NOT_A_MODERATOR;
    }
    return BOF_ERR_UNKNOWN;
}

#define CHECK(actor, community, may, post) do {                             \
    bof_error_t ce_ = check_actor((actor), (community), (may), (post));     \
    if (ce_ != BOF_OK)                                                      \
        return ce_;                                                         \
} while (0)

// For an actor with a person: the comment at floor of post must be theirs
// as the backend has it (not only as the caller says, claimed).
static bof_error_t
check_comment_creator(const bof_actor_t *actor, const bof_post_ref_t *post,
                      int floor, const char *claimed)
{
    const bof_person_t *p = person_of(actor);
    bof_comment_t c;
    bof_get_comments_t q = { .post = *post, .page = floor, .limit = 1 };
    bof_get_comments_response_t r = { .comments = &c };

    if (!p)
        return BOF_OK;
    if (floor <= 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!claimed || strcasecmp(claimed, p->name) != 0)
        return BOF_ERR_NO_COMMENT_EDIT_ALLOWED;
    if (READ(get_comments, &q, &r) != BOF_OK || r.count < 1 ||
        c.bbs_floor != floor)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    if (c.removed || strcasecmp(c.creator, p->name) != 0)
        return BOF_ERR_NO_COMMENT_EDIT_ALLOWED;
    return BOF_OK;
}

/* ------------------------------------------------------------------------
 * Posts
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_post(const bof_actor_t *actor, const bof_create_post_t *req,
                bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    fileheader_t fh;
    GATE_DIR(bof_create_post_t);
    if (req->bbs_header && req->bbs_header->filename[0] &&
        !safe_post_file(req->bbs_header->filename))
        return BOF_ERR_INVALID_BODY_FIELD;
    CHECK(actor, req->community, MAY_POST, NULL);
    if (person_of(actor)) {
        // A new post of the actor, from its title and body alone.
        if (!actor->name || !req->body || req->bbs_content_path ||
            req->bbs_crosspost_community || req->bbs_crosspost_file ||
            !req->bbs_header || !req->bbs_header->title[0] ||
            !plain_text(req->bbs_header->title))
            return BOF_ERR_INVALID_BODY_FIELD;
        memset(&fh, 0, sizeof(fh));
        strlcpy(fh.title, req->bbs_header->title, sizeof(fh.title));
        strlcpy(fh.owner, actor->name, sizeof(fh.owner));
        gated_req_.bbs_header = &fh;
    }
    return WRITE(create_post, out ? out : &local, actor, req);
}

bof_error_t
bof_edit_post(const bof_actor_t *actor, const bof_edit_post_t *req,
              bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_edit_post_t, post);
    if (req->bbs_body_path && !safe_path(req->bbs_body_path))
        return BOF_ERR_INVALID_BODY_FIELD;
    CHECK(actor, req->post.community, MAY_EDIT, req->post.bbs_header);
    if (person_of(actor)) {
        // The title and body; the rest of the record is the editor's.
        bof_community_t c;
        if ((req->name && !plain_text(req->name)) ||
            (req->bbs_body_path &&
             strncmp(req->bbs_body_path, "tmp/", 4) != 0))
            return BOF_ERR_INVALID_BODY_FIELD;
        if ((req->bbs_owner || req->bbs_date || req->bbs_money ||
             req->bbs_modified || req->bbs_no_revision) &&
            (bof_get_community(req->post.community, &c) != BOF_OK ||
             !bof_bbs_may_moderate(person_of(actor), &c)))
            return BOF_ERR_NO_POST_EDIT_ALLOWED;
    }
    bof_error_t err = WRITE(edit_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, NULL, req->bbs_modlog);
    return err;
}

bof_error_t
bof_delete_post(const bof_actor_t *actor, const bof_delete_post_t *req,
                bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    GATE_POST(bof_delete_post_t, post);
    CHECK(actor, req->post.community, MAY_DELETE, req->post.bbs_header);
    return WRITE(delete_post, out ? out : &local, actor, req);
}

bof_error_t
bof_remove_post(const bof_actor_t *actor, const bof_remove_post_t *req,
                bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    bof_post_result_t *res = out ? out : &local;
    GATE_POST(bof_remove_post_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(remove_post, res, actor, req);

    if (err == BOF_OK && req->bbs_modlog) {
        // Not as asked when the content could not be archived.
        const bof_modlog_t *m = req->bbs_modlog;
        if (req->bbs_modlog_unarchived &&
            (res->bbs_content_result == DELETE_FILE_CONTENT_FAILED ||
             !res->bbs_archived_path[0]))
            m = req->bbs_modlog_unarchived;
        modlog_done(req->post.community, actor, req->reason, m);
    }
    return err;
}

bof_error_t
bof_remove_posts(const bof_actor_t *actor, const bof_remove_posts_t *req,
                 int *count)
{
    int local;
    GATE_DIR(bof_remove_posts_t);
    CHECK(actor, req->community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(remove_posts, count ? count : &local, actor,
                            req);
    if (err == BOF_OK)
        modlog_done(req->community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_purge_posts(const bof_actor_t *actor, const bof_purge_posts_t *req,
                int *count)
{
    int local;
    GATE_DIR(bof_purge_posts_t);
    CHECK(actor, req->community, MAY_MODERATE, NULL);
    if (!req->bbs_selected)
        return BOF_ERR_INVALID_BODY_FIELD;
    return WRITE(purge_posts, count ? count : &local, actor, req);
}

bof_error_t
bof_restore_post(const bof_actor_t *actor, const bof_restore_post_t *req,
                 bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_DIR(bof_restore_post_t);
    if (req->bbs_archived && !safe_post_file(req->bbs_archived->filename))
        return BOF_ERR_INVALID_BODY_FIELD;
    CHECK(actor, req->community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(restore_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_lock_post(const bof_actor_t *actor, const bof_lock_post_t *req,
              bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_lock_post_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(lock_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_feature_post(const bof_actor_t *actor, const bof_feature_post_t *req,
                 bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_feature_post_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    return WRITE(feature_post, out ? out : &local, actor, req);
}

bof_error_t
bof_bbs_set_post_modes(const bof_actor_t *actor,
                       const bof_bbs_set_post_modes_t *req,
                       bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_bbs_set_post_modes_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    return WRITE(set_post_modes, out ? out : &local, actor, req);
}

bof_error_t
bof_bbs_reset_post_score(const bof_actor_t *actor,
                         const bof_bbs_reset_post_score_t *req,
                         bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_bbs_reset_post_score_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(reset_post_score, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_bbs_mask_post(const bof_actor_t *actor, const bof_bbs_mask_post_t *req,
                  bof_post_response_t *out)
{
    bof_post_response_t local = { 0 };
    GATE_POST(bof_bbs_mask_post_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(mask_post, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

bof_error_t
bof_bbs_remove_crosspost(const bof_actor_t *actor,
                         const bof_bbs_remove_crosspost_t *req,
                         bof_post_result_t *out)
{
    bof_post_result_t local = { 0 };
    GATE_DIR(bof_bbs_remove_crosspost_t);
    if (!req->bbs_header || !safe_post_file(req->bbs_header->filename))
        return BOF_ERR_INVALID_BODY_FIELD;
    CHECK(actor, req->community, MAY_MODERATE, NULL);
    return WRITE(remove_crosspost, out ? out : &local, actor, req);
}

bof_error_t
bof_create_post_like(const bof_actor_t *actor,
                     const bof_create_post_like_t *req,
                     bof_post_like_response_t *out)
{
    bof_post_like_response_t local = { 0 };
    GATE_POST(bof_create_post_like_t, post);
    CHECK(actor, req->post.community, MAY_COMMENT, req->post.bbs_header);
    return WRITE(create_post_like, out ? out : &local, actor, req);
}

/* ------------------------------------------------------------------------
 * Comments
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_comment(const bof_actor_t *actor, const bof_create_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    bof_create_comment_t made;
    char line[STRLEN * 2];
    GATE_POST(bof_create_comment_t, post);
    CHECK(actor, req->post.community, MAY_COMMENT, req->post.bbs_header);
    if (person_of(actor)) {
        // Only what was typed: the line is bof's, with the actor's name;
        // and the file is locked, as it may be being rewritten.
        if (req->content)
            return BOF_ERR_INVALID_BODY_FIELD;
        gated_req_.bbs_lock = 1;
    }
    if (!req->content) {
        // Only what was typed (a gateway): the line as the board shows it.
        bof_community_t c;
        char author[IDLEN + 1];
        uint32_t attr = 0;

        if (!req->bbs_text || !actor || !actor->name)
            return BOF_ERR_INVALID_BODY_FIELD;
        if (req->post.community &&
            bof_get_community(req->post.community, &c) == BOF_OK)
            attr = c.bbs_attr;
        snprintf(author, sizeof(author),
                 (attr & BRD_ALIGNEDCMT) ? "%-*s" : "%.*s", IDLEN,
                 actor->name);
        bof_bbs_format_comment(line, sizeof(line), req->bbs_vote, author,
                               req->bbs_text,
                               (attr & BRD_COMMENT_SHOWIP) ?
                               (actor->from ? actor->from : "") : NULL,
                               (int32_t)time(NULL));
        made = *req;
        made.content = line;
        req = &made;
    }
    return WRITE(create_comment, out ? out : &local, actor, req);
}

bof_error_t
bof_edit_comment(const bof_actor_t *actor, const bof_edit_comment_t *req,
                 bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    bof_error_t err;
    GATE_POST(bof_edit_comment_t, post);
    CHECK(actor, req->post.community, MAY_COMMENT, req->post.bbs_header);
    if ((err = check_comment_creator(actor, &req->post, req->bbs_floor,
                                     req->bbs_creator)) != BOF_OK)
        return err;
    return WRITE(edit_comment, out ? out : &local, actor, req);
}

bof_error_t
bof_delete_comment(const bof_actor_t *actor, const bof_delete_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    bof_error_t err;
    GATE_POST(bof_delete_comment_t, post);
    CHECK(actor, req->post.community, MAY_READ, NULL);
    if (!req->post.bbs_header || req->bbs_floor <= 0 || !req->bbs_creator)
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((err = check_comment_creator(actor, &req->post, req->bbs_floor,
                                     req->bbs_creator)) != BOF_OK)
        return err;
    return WRITE(delete_comment, out ? out : &local, actor, req);
}

bof_error_t
bof_remove_comment(const bof_actor_t *actor, const bof_remove_comment_t *req,
                   bof_comment_response_t *out)
{
    bof_comment_response_t local = { 0 };
    GATE_POST(bof_remove_comment_t, post);
    CHECK(actor, req->post.community, MAY_MODERATE, NULL);
    bof_error_t err = WRITE(remove_comment, out ? out : &local, actor, req);
    if (err == BOF_OK)
        modlog_done(req->post.community, actor, req->reason, req->bbs_modlog);
    return err;
}

/* ------------------------------------------------------------------------
 * Reports and the modlog
 * ------------------------------------------------------------------------ */

bof_error_t
bof_create_report(const bof_actor_t *actor, const bof_create_report_t *req,
                  bof_report_t *out)
{
    bof_report_t local = { 0 };
    GATE_POST(bof_create_report_t, post);
    CHECK(actor, req->post.community, MAY_READ, NULL);
    return WRITE(create_report, out ? out : &local, actor, req);
}

bof_error_t
bof_resolve_report(const bof_actor_t *actor, const bof_resolve_report_t *req,
                   bof_report_t *out)
{
    bof_report_t local = { 0 };
    if (req->community && !is_valid_brdname(req->community))
        return BOF_ERR_INVALID_BODY_FIELD;
    CHECK(actor, req->community, MAY_MODERATE, NULL);
    return WRITE(resolve_report, out ? out : &local, actor, req);
}

int
bof_modlog_write(const char *community, const bof_actor_t *actor,
                 const char *reason, int reason_audit_only,
                 const bof_modlog_t *m)
{
    return modlog_write_all(community, actor, reason, reason_audit_only, m);
}

int
bof_modlog_note_edit(const char *community, const bof_actor_t *actor)
{
    const bof_storage_ops_t *b = get_backend();

    if (!b->note_modlog_edit ||
        b->note_modlog_edit(backend_ctx, community, actor) != 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------------
 * Reads
 * ------------------------------------------------------------------------ */

bof_error_t
bof_get_posts(const bof_get_posts_t *req, bof_get_posts_response_t *out)
{
    if (req->bbs_dir && !dir_in_community(req->community, req->bbs_dir))
        return BOF_ERR_INVALID_BODY_FIELD;
    return READ(get_posts, req, out);
}

bof_error_t
bof_get_post(const bof_get_post_t *req, bof_get_post_response_t *out)
{
    if (req->bbs_dir && !dir_in_community(req->community, req->bbs_dir))
        return BOF_ERR_INVALID_BODY_FIELD;
    return READ(get_post, req, out);
}

bof_error_t
bof_bbs_get_post_body(const bof_post_ref_t *post, const char *dest_path,
                      bof_post_version_t *ver)
{
    SAFE_POST(post);
    if (dest_path && !safe_path(dest_path))
        return BOF_ERR_INVALID_BODY_FIELD;
    return READ(get_post_body, post, dest_path, ver);
}

bof_error_t
bof_get_comments(const bof_get_comments_t *req,
                 bof_get_comments_response_t *out)
{
    SAFE_POST(&req->post);
    return READ(get_comments, req, out);
}

bof_error_t
bof_get_comment_revisions(const bof_get_comment_revisions_t *req,
                          bof_get_comment_revisions_response_t *out)
{
    out->count = 0;
    if (!req->post.bbs_header || req->bbs_floor <= 0 || req->limit <= 0 ||
        !out->revisions)
        return BOF_ERR_INVALID_BODY_FIELD;
    SAFE_POST(&req->post);
    return READ(get_comment_revisions, req, out);
}

bof_error_t
bof_list_reports(const bof_actor_t *actor, const bof_list_reports_t *req,
                 bof_list_reports_response_t *out)
{
    const bof_person_t *p = actor ? actor->bbs_person : NULL;

    if (!req)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (req->community)
        CHECK(actor, req->community, MAY_MODERATE, NULL);
    else if (p && !p->admin &&
             !(p->bbs_perms & (PERM_SYSOP | PERM_POLICE)))
        return BOF_ERR_NOT_AN_ADMIN;
    return READ(list_reports, req, out);
}

int
bof_bbs_post_version_matches(const bof_post_ref_t *post,
                             const bof_post_version_t *ver)
{
    const bof_storage_ops_t *b = get_backend();
    return b->post_version_matches ?
        b->post_version_matches(backend_ctx, post, ver) : 0;
}

bof_error_t
bof_bbs_get_post_file(const bof_post_ref_t *post, char *path, size_t sz)
{
    char buf[PATHLEN];
    bof_error_t err;

    if (!post || !post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || !path || !sz)
        return BOF_ERR_INVALID_BODY_FIELD;
    SAFE_POST(post);
    // The files of a board are the posts, or the cache of the backend.
    if ((err = READ(refresh_post, post)) != BOF_OK)
        return err;
    setdirpath(buf, post->bbs_dir, post->bbs_header->filename);
    if (strlcpy(path, buf, sz) >= sz)
        return BOF_ERR_INVALID_BODY_FIELD;
    return BOF_OK;
}

/* ------------------------------------------------------------------------
 * IDs
 * ------------------------------------------------------------------------ */

bof_error_t
bof_get_post_id(const bof_post_ref_t *post, bof_post_id_t *id)
{
    if (!post || !id)
        return BOF_ERR_INVALID_BODY_FIELD;
    *id = 0;
    SAFE_POST(post);
    return READ(get_post_id, post, id);
}

bof_error_t
bof_get_post_key(bof_post_id_t id, bof_post_key_t *key)
{
    if (id <= 0 || !key)
        return BOF_ERR_INVALID_BODY_FIELD;
    memset(key, 0, sizeof(*key));
    return READ(get_post_key, id, key);
}

bof_error_t
bof_bbs_find_post(const bof_post_key_t *key, bof_found_post_t *out)
{
    if (!key || !key->community[0] || !key->bbs_post_file[0] || !out ||
        !out->bbs_header || !is_valid_brdname(key->community) ||
        !bof_bbs_valid_post_file(key->bbs_post_file))
        return BOF_ERR_INVALID_BODY_FIELD;
    return READ(find_post, key, out);
}

bof_error_t
bof_bbs_community_id(const char *name, bof_community_id_t *id)
{
    if (!name || !*name || !id)
        return BOF_ERR_INVALID_BODY_FIELD;
    *id = 0;
    return READ(get_community_id, name, id);
}

/* ------------------------------------------------------------------------
 * The time capsule
 * ------------------------------------------------------------------------ */

bof_error_t
bof_bbs_get_post_revisions(const bof_post_ref_t *post, int *count)
{
    if (!post || !post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || !count)
        return BOF_ERR_INVALID_BODY_FIELD;
    *count = 0;
    SAFE_POST(post);
    return READ(get_post_revisions, post, count);
}

bof_error_t
bof_bbs_get_post_revision_file(const bof_post_ref_t *post, int rev,
                               char *path, size_t sz)
{
    if (!post || !post->bbs_dir || !post->bbs_header ||
        !post->bbs_header->filename[0] || rev <= 0 || !path || !sz)
        return BOF_ERR_INVALID_BODY_FIELD;
    SAFE_POST(post);
    return READ(get_post_revision_file, post, rev, path, sz);
}

bof_error_t
bof_bbs_list_post_revisions(const bof_bbs_list_post_revisions_t *req,
                            bof_bbs_list_post_revisions_response_t *out)
{
    if (!req || !req->post.bbs_dir || !req->post.bbs_header ||
        !req->post.bbs_header->filename[0] || req->start < 0 ||
        req->limit <= 0 || !out || !out->revisions)
        return BOF_ERR_INVALID_BODY_FIELD;
    out->count = out->bbs_total = 0;
    SAFE_POST(&req->post);
    return READ(list_post_revisions, req, out);
}

bof_error_t
bof_bbs_list_removed_posts(const bof_bbs_list_removed_posts_t *req,
                           bof_bbs_list_removed_posts_response_t *out)
{
    if (!req || !req->bbs_dir || req->start < 0 || req->limit <= 0 ||
        !out || !out->posts ||
        !dir_in_community(req->community, req->bbs_dir))
        return BOF_ERR_INVALID_BODY_FIELD;
    out->count = out->bbs_total = 0;
    return READ(list_removed_posts, req, out);
}
