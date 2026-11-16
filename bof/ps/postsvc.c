/*
 * postsvc.c - bof (Birds Of Feather): the storage backend of post.svc.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * It is not the default backend: a site with post.svc running chooses it
 * (CONFIG_POST_SVC, or BOF_DEFAULT_STORAGE in pttbbs.conf).
 *
 * post.svc keeps the posts, comments and votes (a database); the files of
 * the board (.DIR, the articles) are a cache of it, compatible with the
 * files backend, so that reading needs no IPC. Each write goes to post.svc
 * first (its error is the result), then to the cache: post.svc writes the
 * article into the board itself on each change, and the records (.DIR) are
 * updated with the files backend (or the article rendered again with
 * RENDER_FILE where it matters). Reports and the modlog, which post.svc
 * does not keep, stay in the files.
 *
 * Posts are named by board and file name, as post.svc keys them (the
 * *_FILE commands, e.g. REMOVE_POST_FILE). Persons are sent by name and by
 * their account (bof_actor_t.bbs_local_user).
 */

#include "../bof_int.h"
#include "bof_fs.h"
#include "bof_storage.h"
#include <inttypes.h>

// Where post.svc listens; tests point it elsewhere (bof_postsvc_set_socket()).
static const char *postsvc_sock = GET_SVC_SOCKET(post);

void
bof_postsvc_set_socket(const char *path)
{
    postsvc_sock = path ? path : GET_SVC_SOCKET(post);
}
#define POSTSVC_TIMEOUT_MS  (3000)

// --------------------------------------------------------------------------
// IPC

// Sends hdr and the payloads, and reads the whole response (to EOF, as
// post.svc closes the connection) into a buffer to free(), waiting up to
// timeout_ms for each read. Returns NULL if post.svc cannot be reached.
static char *
svc_call_wait(const char *hdr, const void *p1, size_t l1, const void *p2,
              size_t l2, int timeout_ms)
{
    size_t cap = 4096, len = 0;
    char *buf;
    int s, n;

    if ((s = svc_connect(postsvc_sock)) < 0)
        return NULL;
    if (svc_write(s, hdr, strlen(hdr)) < 0 ||
        (p1 && l1 && svc_write(s, p1, l1) < 0) ||
        (p2 && l2 && svc_write(s, p2, l2) < 0) ||
        !(buf = malloc(cap))) {
        close(s);
        return NULL;
    }
    while ((n = svc_read(s, buf + len, cap - len - 1, timeout_ms)) > 0) {
        len += n;
        if (cap - len < 1024) {
            char *nb = realloc(buf, cap * 2);
            if (!nb)
                break;
            buf = nb;
            cap *= 2;
        }
    }
    close(s);
    buf[len] = '\0';
    return buf;
}

static char *
svc_call(const char *hdr, const void *p1, size_t l1, const void *p2,
         size_t l2)
{
    return svc_call_wait(hdr, p1, l1, p2, l2, POSTSVC_TIMEOUT_MS);
}

// Whether a response is OK; frees it, after copying it to resp if given.
static int
svc_ok(char *r, char *resp, size_t sz)
{
    int ok = r && strncmp(r, "OK", 2) == 0;

    if (resp && sz) {
        strlcpy(resp, r ? r : "", sz);
    }
    free(r);
    return ok;
}

// Sends a one line command; 1 if OK (the response in resp, if given).
static int svc_cmd(char *resp, size_t sz, const char *fmt, ...)
    GCC_CHECK_FORMAT(3, 4);

static int
svc_cmd(char *resp, size_t sz, const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(cmd))
        return 0;
    return svc_ok(svc_call(cmd, NULL, 0, NULL, 0), resp, sz);
}

// s converted for post.svc, to free().
static char *
to_svc(const char *s)
{
    size_t sz = strlen(s ? s : "") * 3 + 1;
    char *out = malloc(sz);

    if (out)
        MB_TO_SERVICE(s ? s : "", out, sz);
    return out;
}

// The content of a file converted for post.svc, to free().
static char *
file_to_svc(const char *path)
{
    struct stat st;
    char *raw, *out;
    FILE *fp;

    if (stat(path, &st) != 0 || !(fp = fopen(path, "rb")))
        return NULL;
    raw = malloc(st.st_size + 1);
    out = malloc(st.st_size * 3 + 1);
    if (!raw || !out ||
        (st.st_size && fread(raw, 1, st.st_size, fp) != (size_t)st.st_size)) {
        free(raw);
        free(out);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    raw[st.st_size] = '\0';
    STORAGE_TO_SERVICE_N(raw, (size_t)st.st_size, out, st.st_size * 3 + 1);
    free(raw);
    return out;
}

static const char *
who(const bof_actor_t *actor)
{
    return actor && actor->name && *actor->name ? actor->name : "system";
}

static uint64_t
uid(const bof_actor_t *actor)
{
    return actor ? actor->bbs_local_user : 0;
}

static const char *
from(const bof_actor_t *actor)
{
    return actor && actor->from && *actor->from ? actor->from : "-";
}

static void
post_path(char *path, const bof_post_ref_t *post)
{
    setdirpath(path, post->bbs_dir, post->bbs_header->filename);
}

static int
valid(const bof_post_ref_t *post)
{
    return post->community && post->bbs_dir && post->bbs_header &&
        post->bbs_header->filename[0];
}

// Has post.svc render the article (the cache) of a post into its file.
static int
render(const char *community, const char *filename, const char *path)
{
    return svc_cmd(NULL, 0, "RENDER_FILE %s %s %s modern\n", community,
                   filename, path);
}

static int
render_post(const bof_post_ref_t *post)
{
    char path[PATHLEN];

    post_path(path, post);
    return render(post->community, post->bbs_header->filename, path);
}

// Gets the body of a post (without comments) into path; *modified gets
// its time in post.svc (the version of the content).
static int
fetch_body(const bof_post_ref_t *post, const char *path, int32_t *modified)
{
    char resp[128];
    long long m = 0;

    if (!svc_cmd(resp, sizeof(resp), "FETCH_POST_FILE %s %s %s -\n",
                 post->community, post->bbs_header->filename, path))
        return 0;
    sscanf(resp, "OK %lld", &m);
    *modified = (int32_t)m;
    return 1;
}

// Sends the filemode and money of a record (SET_META_FILE).
static void
set_meta(const char *community, const fileheader_t *fh)
{
    int money = (fh->filemode & (FILE_ANONYMOUS | FILE_PETITION)) ?
        0 : fh->multi.money;
    svc_cmd(NULL, 0, "SET_META_FILE %s %.*s %d %d\n", community,
            (int)sizeof(fh->filename), fh->filename, fh->filemode, money);
}

// Logs a failure to update the cache: post.svc has the change, and
// sync-dir makes the files again.
static void
cache_failed(const char *op, const char *community, const char *file,
             bof_error_t err)
{
    time4_t now = time4(NULL);
    file_appendf(BOF_STORAGE_LOG, "%s postsvc cache %s %s/%s: %s\n",
                 Cdatelite(&now), op, community, file, bof_error_name(err));
}

// --------------------------------------------------------------------------
// Records in the cache

struct record_update {
    int32_t modified;       // 0: unchanged
    int score_delta;
    int comments_delta;
};

static void
apply_update(fileheader_t *fh, const void *ctx)
{
    const struct record_update *u = ctx;
    int v;

    if (u->modified)
        fh->modified = u->modified;
    // post.svc keeps the whole score; it is shown capped.
    v = (int)fh->score + u->score_delta;
    fh->score = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
    v = (int)fh->comments + u->comments_delta;
    fh->comments = v < 0 ? 0 : v > 65535 ? 65535 : v;
}

static int
update_record(const bof_post_ref_t *post, int32_t modified, int score_delta,
              int comments_delta, fileheader_t *out)
{
    struct record_update u = { modified, score_delta, comments_delta };

    if (post->bbs_index <= 0) {
        if (out)
            *out = *post->bbs_header;
        return 0;
    }
    return bof_files_update_record(post->bbs_dir, post->bbs_index,
                                   post->bbs_header->filename, apply_update,
                                   &u, out);
}

// --------------------------------------------------------------------------
// Posts

static bof_error_t
ps_create_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_create_post_t *req, bof_post_response_t *out)
{
    fileheader_t named;
    const fileheader_t *fh = &named;
    bof_create_post_t cache;
    char path[PATHLEN], tmp[PATHLEN] = "", hdr[512];
    char *title, *body;
    const char *author;
    uint64_t author_uid;
    int money, ok, made = 0;

    if (!req->community || !req->bbs_dir || !req->bbs_header ||
        (req->bbs_content_path && req->body))
        return BOF_ERR_INVALID_BODY_FIELD;
    // Aggregate boards that only list the posts of others (live ALLPOST)
    // have no posts of their own in post.svc.
    if (IS_ENABLED(CONFIG_LIVE_ALLPOST) && bof_bbs_is_aggregate(req->community))
        return bof_fs_create_post(NULL, actor, req, out);

    // Named here if not yet: post.svc keys posts by board and file name.
    named = *req->bbs_header;
    if (!named.filename[0]) {
        if (bof_bbs_name_post(req->community, &named) != BOF_OK)
            return BOF_ERR_COULDNT_CREATE_POST;
        made = 1;
    }
    setdirpath(path, req->bbs_dir, fh->filename);
    // The body alone (a Lemmy client): written as a post of the BBS aside.
    if (req->body) {
        snprintf(tmp, sizeof(tmp), "%s.body%d", path, (int)getpid());
        if (bof_files_write_post(tmp, fh->owner, req->bbs_nickname,
                                 req->community, fh->title, time4(NULL),
                                 req->body, actor ? actor->from : NULL)) {
            unlink(tmp);
            if (made)
                unlink(path);
            return BOF_ERR_COULDNT_CREATE_POST;
        }
    }
    title = to_svc(fh->title);
    body = file_to_svc(*tmp ? tmp : req->bbs_content_path ?
                       req->bbs_content_path : path);
    if (!title || !body) {
        free(title);
        free(body);
        if (*tmp)
            unlink(tmp);
        if (made)
            unlink(path);
        return BOF_ERR_COULDNT_CREATE_POST;
    }
    // The shown name may be a nickname (anonymous posts), but the account
    // is always who posted it; system posts have none.
    author = fh->owner[0] ? fh->owner : who(actor);
    author_uid = (fh->filemode & FILE_ANONYMOUS) ||
        (actor && actor->name && strcasecmp(author, actor->name) == 0) ?
        uid(actor) : 0;
    money = (fh->filemode & (FILE_ANONYMOUS | FILE_PETITION)) ?
        0 : fh->multi.money;
    snprintf(hdr, sizeof(hdr),
             "CREATE_POST %s %s %s %" PRIu64 " %d %d %ld 0 %zu %zu\n",
             req->community, fh->filename, author, author_uid, fh->filemode,
             money, (long)time4(NULL), strlen(title), strlen(body));
    ok = svc_ok(svc_call(hdr, title, strlen(title), body, strlen(body)),
                NULL, 0);
    free(title);
    free(body);
    if (!ok) {
        if (*tmp)
            unlink(tmp);
        if (made)
            unlink(path);
        return BOF_ERR_COULDNT_CREATE_POST;
    }

    if (req->bbs_crosspost_community && req->bbs_crosspost_file)
        svc_cmd(NULL, 0, "CROSSPOST %s %s %s %s %s %" PRIu64 " %ld\n",
                req->bbs_crosspost_community, req->bbs_crosspost_file,
                req->community, fh->filename, who(actor), uid(actor),
                (long)time4(NULL));

    // The record in the cache. post.svc writes the article into the board
    // itself; the content is copied there only if it has not.
    cache = *req;
    cache.bbs_header = &named;
    cache.body = NULL;
    cache.bbs_content_path = *tmp ? tmp : req->bbs_content_path;
    if (dashs(path) > 0)
        cache.bbs_content_path = NULL;
    bof_error_t err = bof_fs_create_post(NULL, actor, &cache, out);
    if (*tmp)
        unlink(tmp);
    if (err != BOF_OK) {
        // The post is in post.svc (the result), not yet in the cache: the
        // caller still gets its record, as post.svc has it.
        cache_failed("create_post", req->community, fh->filename, err);
        if (out && out->bbs_header)
            *out->bbs_header = named;
    }
    return BOF_OK;
}

static bof_error_t
ps_create_post_like(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_create_post_like_t *req,
                    bof_post_like_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char resp[128];
    int up = 0, down = 0;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!svc_cmd(resp, sizeof(resp), "CREATE_POST_LIKE_FILE %s %s %s %" PRIu64
                 " %d %s %ld\n", post->community, post->bbs_header->filename,
                 who(actor), uid(actor), req->score, from(actor),
                 (long)time4(NULL)))
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (sscanf(resp, "OK %d %d", &up, &down) < 2) {
        up = req->score > 0;
        down = req->score < 0;
    }
    out->bbs_score_delta = up - down;
    if (update_record(post, 0, up - down, 0, out->bbs_header) != 0)
        cache_failed("create_post_like", post->community,
                     post->bbs_header->filename, BOF_ERR_COULDNT_UPDATE_POST);
    return BOF_OK;
}

static bof_error_t
ps_list_post_likes(void *ctx GCC_UNUSED, const bof_list_post_likes_t *req,
                   bof_list_post_likes_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    int first = (req->page > 0 ? req->page - 1 : 0) * req->limit;
    char cmd[256], *r, *line;
    int total = 0;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // LIST_POST_LIKES_FILE <community> <file>: OK <n>, then a line per vote:
    // <name> TAB <person_id> TAB <score> TAB <time> TAB <ip>.
    snprintf(cmd, sizeof(cmd), "LIST_POST_LIKES_FILE %s %s\n", post->community,
             post->bbs_header->filename);
    r = svc_call(cmd, NULL, 0, NULL, 0);
    if (!r || sscanf(r, "OK %d", &total) != 1) {
        free(r);
        return BOF_ERR_COULDNT_FIND_POST;
    }
    line = strchr(r, '\n');
    for (int i = 0; line && *++line && i < total; i++) {
        char *next = strchr(line, '\n');
        if (next)
            *next = '\0';
        if (i >= first && out->count < req->limit) {
            bof_post_like_t *v = &out->post_likes[out->count];
            char name[16] = "", from[40] = "";
            unsigned long long id = 0;
            long long when = 0;
            int score = 0;

            if (sscanf(line, "%15[^\t]\t%llu\t%d\t%lld\t%39s", name, &id,
                       &score, &when, from) >= 4) {
                memset(v, 0, sizeof(*v));
                strlcpy(v->creator, name, sizeof(v->creator));
                v->bbs_local_user = id;
                v->score = score;
                v->published = (int32_t)when;
                strlcpy(v->bbs_from, from, sizeof(v->bbs_from));
                out->count++;
            }
        }
        line = next;
    }
    out->bbs_total = total;
    free(r);
    return BOF_OK;
}

static bof_error_t
ps_edit_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_edit_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    const char *name = req->name && *req->name ? req->name : fh->title;
    char path[PATHLEN], resp[128], hdr[512];
    int32_t modified = req->bbs_modified;
    bof_error_t err;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (out)
        out->bbs_body_saved = 0;

    if (req->bbs_body_path) {
        char *title = to_svc(name), *body = file_to_svc(req->bbs_body_path);
        long long m = 0;
        unsigned rev = 0;
        int64_t expected = req->bbs_body_version ?
            (int64_t)req->bbs_body_version->hash : 0;

        if (!title || !body) {
            free(title);
            free(body);
            return BOF_ERR_COULDNT_UPDATE_POST;
        }
        snprintf(hdr, sizeof(hdr), "EDIT_POST_FILE %s %s %s %" PRIu64
                 " %lld %zu %zu\n", post->community, fh->filename, who(actor),
                 uid(actor), (long long)expected, strlen(title), strlen(body));
        svc_ok(svc_call(hdr, title, strlen(title), body, strlen(body)), resp,
               sizeof(resp));
        free(title);
        free(body);
        if (strncmp(resp, "OK 0", 4) == 0) {
            sscanf(resp, "OK 0 NO_CHANGE %lld", &m);
        } else if (strncmp(resp, "OK ", 3) == 0) {
            sscanf(resp, "OK %u %lld", &rev, &m);
        } else {
            // Changed since the version (ERR CONFLICT), or failed: the
            // file of the edit stays, as with the files backend.
            return BOF_ERR_COULDNT_UPDATE_POST;
        }
        unlink(req->bbs_body_path);
        if (out)
            out->bbs_body_saved = 1;
        post_path(path, post);
        if (!render(post->community, fh->filename, path))
            cache_failed("edit_post", post->community, fh->filename,
                         BOF_ERR_UNKNOWN);
        if (!modified)
            modified = m > 0 ? (int32_t)m : dasht4(path);
    } else if (req->name && *req->name && strcmp(req->name, fh->title)) {
        char *title = to_svc(req->name);
        int ok;

        if (!title)
            return BOF_ERR_COULDNT_UPDATE_POST;
        snprintf(hdr, sizeof(hdr), "EDIT_POST_TITLE_FILE %s %s %s %" PRIu64
                 " %zu\n", post->community, fh->filename, who(actor),
                 uid(actor), strlen(title));
        ok = svc_ok(svc_call(hdr, title, strlen(title), NULL, 0), NULL, 0);
        free(title);
        if (!ok)
            return BOF_ERR_COULDNT_UPDATE_POST;
    }

    // The record in the cache, then its money and marks in post.svc.
    fileheader_t updated;
    bof_post_response_t res = { .bbs_header = &updated };
    bof_edit_post_t rec = *req;
    rec.bbs_body_path = NULL;
    rec.bbs_modified = modified;
    err = bof_fs_edit_post(NULL, actor, &rec, &res);
    if (err != BOF_OK)
        return out && out->bbs_body_saved ? BOF_ERR_COULDNT_UPDATE_POST : err;
    if (req->bbs_money || req->bbs_owner)
        set_meta(post->community, &updated);
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;
}

// What deleting and removing share: post.svc keeps it as deleted by its
// creator (DELETE_POST_FILE, without a reason: reason is NULL) or as
// removed, with a reason (REMOVE_POST_FILE); the cache archives the
// article and drops (or keeps a tombstone of) the record.
static bof_error_t
drop_post(const bof_actor_t *actor, const bof_post_ref_t *post,
          const char *reason)
{
    char *why = NULL;
    int ok;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // The article as it is, to be archived from the cache.
    render_post(post);
    if (!reason)
        ok = svc_cmd(NULL, 0, "DELETE_POST_FILE %s %s %s %" PRIu64 "\n",
                     post->community, post->bbs_header->filename,
                     who(actor), uid(actor));
    else
        ok = (why = to_svc(reason)) &&
             svc_cmd(NULL, 0, "REMOVE_POST_FILE %s %s %s %" PRIu64 " %s\n",
                     post->community, post->bbs_header->filename,
                     who(actor), uid(actor), why);
    free(why);
    return ok ? BOF_OK : BOF_ERR_COULDNT_UPDATE_POST;
}

static bof_error_t
ps_delete_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_delete_post_t *req, bof_post_result_t *out)
{
    bof_error_t err = drop_post(actor, &req->post, NULL);
    return err != BOF_OK ? err : bof_fs_delete_post(NULL, actor, req, out);
}

static bof_error_t
ps_remove_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_remove_post_t *req, bof_post_result_t *out)
{
    bof_error_t err = drop_post(actor, &req->post,
                                req->reason ? req->reason : "");
    return err != BOF_OK ? err : bof_fs_remove_post(NULL, actor, req, out);
}

static bof_error_t
ps_remove_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_remove_posts_t *req, int *count)
{
    fileheader_t fh;
    int fd, index;

    if (!req->bbs_dir || !req->bbs_selected || !req->community)
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((fd = open(req->bbs_dir, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    for (index = 1; read_fileheader(fd, &fh) == 1; index++) {
        if ((fh.filemode & req->bbs_keep_modes) ||
            !req->bbs_selected(&fh, index, req->bbs_ctx))
            continue;
        bof_post_ref_t post = {
            .community = req->community, .bbs_dir = req->bbs_dir,
            .bbs_index = index, .bbs_header = &fh,
        };
        drop_post(actor, &post, req->reason ? req->reason : "");
    }
    close(fd);
    return bof_fs_remove_posts(NULL, actor, req, count);
}

// Expired posts (PurgePost): post.svc keeps them as deleted, as before
// (post.ctl purges them for good), rather than dropping them at once;
// the cache drops them as the files backend does.
static bof_error_t
ps_purge_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_purge_posts_t *req, int *count)
{
    fileheader_t fh;
    char *why;
    int fd, index;

    if (!req->bbs_dir || !req->bbs_selected || !req->community)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (IS_ENABLED(CONFIG_LIVE_ALLPOST) && bof_bbs_is_aggregate(req->community))
        return bof_fs_purge_posts(NULL, actor, req, count);
    if ((fd = open(req->bbs_dir, O_RDONLY)) < 0)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    why = to_svc(req->reason ? req->reason : "");
    for (index = 1; why && read_fileheader(fd, &fh) == 1; index++) {
        if (!fh.filename[0] || fh.owner[0] == '-' ||
            !req->bbs_selected(&fh, index, req->bbs_ctx))
            continue;
        svc_cmd(NULL, 0, "REMOVE_POST_FILE %s %.*s %s %" PRIu64 " %s\n",
                req->community, (int)sizeof(fh.filename), fh.filename,
                who(actor), uid(actor), why);
    }
    free(why);
    close(fd);
    return bof_fs_purge_posts(NULL, actor, req, count);
}

static bof_error_t
ps_restore_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_bbs_restore_post_t *req,
                bof_post_response_t *out)
{
    if (!req->community || !req->bbs_archived)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!svc_cmd(NULL, 0, "RECOVER_POST_FILE %s %s\n", req->community,
                 req->bbs_archived->filename))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return bof_fs_bbs_restore_post(NULL, actor, req, out);
}

static bof_error_t
ps_lock_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_lock_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // post.svc takes the M. or L. name of the post.
    if (!svc_cmd(NULL, 0, "LOCK_POST_FILE %s %s %d\n", post->community,
                 post->bbs_header->filename, req->locked ? 1 : 0))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return bof_fs_lock_post(NULL, actor, req, out);
}

// Changes the marks of a post in the cache, then sends them to post.svc.
static bof_error_t
with_meta(const bof_post_ref_t *post, bof_error_t err, fileheader_t *updated,
          bof_post_response_t *out)
{
    if (err != BOF_OK)
        return err;
    set_meta(post->community, updated);
    if (out && out->bbs_header)
        *out->bbs_header = *updated;
    return BOF_OK;
}

static bof_error_t
ps_feature_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_feature_post_t *req, bof_post_response_t *out)
{
    fileheader_t updated;
    bof_post_response_t res = { .bbs_header = &updated };
    return with_meta(&req->post, bof_fs_feature_post(NULL, actor, req, &res),
                     &updated, out);
}

static bof_error_t
ps_set_post_modes(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_bbs_set_post_modes_t *req,
                  bof_post_response_t *out)
{
    fileheader_t updated;
    bof_post_response_t res = { .bbs_header = &updated };
    return with_meta(&req->post,
                     bof_fs_bbs_set_post_modes(NULL, actor, req, &res),
                     &updated, out);
}

static bof_error_t
ps_reset_post_score(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_bbs_reset_post_score_t *req,
                    bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!svc_cmd(NULL, 0, "RESET_POST_SCORE_FILE %s %s\n", post->community,
                 post->bbs_header->filename))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return bof_fs_bbs_reset_post_score(NULL, actor, req, out);
}

static bof_error_t
ps_mask_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_bbs_mask_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char src[PATHLEN], dst[PATHLEN], line[ANSILINELEN], text[ANSILINELEN];
    char hdr[512], resp[128];
    time4_t now = time4(NULL);
    int32_t modified;
    int matched = 0, ok;
    FILE *in, *outf;

    if (!valid(post) || !req->pattern || strlen(req->pattern) < 2)
        return BOF_ERR_INVALID_BODY_FIELD;
    snprintf(src, sizeof(src), "%s.mask%d", post->bbs_dir, (int)getpid());
    snprintf(dst, sizeof(dst), "%s.masked%d", post->bbs_dir, (int)getpid());
    if (!fetch_body(post, src, &modified))
        return BOF_ERR_COULDNT_FIND_POST;
    if (!(in = fopen(src, "r")) || !(outf = fopen(dst, "w"))) {
        if (in)
            fclose(in);
        unlink(src);
        return BOF_ERR_UNKNOWN;
    }
    while (fgets(line, sizeof(line), in)) {
        strip_control_sequence(text, line);
        if (mbs_strstr(text, req->pattern)) {
            fputs(STR_POST_MARK STR_MASKED_LINE "\n", outf);
            matched++;
        } else {
            fputs(line, outf);
        }
    }
    fclose(in);
    fclose(outf);
    unlink(src);
    if (!matched) {
        unlink(dst);
        return BOF_ERR_NOT_FOUND;
    }
    file_appendf(dst, STR_POST_MARK STR_MASK_NOTE, who(actor),
                 Cdatelite(&now), req->reason ? req->reason : "");

    char *title = to_svc(post->bbs_header->title), *body = file_to_svc(dst);
    unlink(dst);
    if (!title || !body) {
        free(title);
        free(body);
        return BOF_ERR_UNKNOWN;
    }
    snprintf(hdr, sizeof(hdr), "EDIT_POST_FILE %s %s %s %" PRIu64
             " %lld %zu %zu\n", post->community, post->bbs_header->filename,
             who(actor), uid(actor), (long long)modified, strlen(title),
             strlen(body));
    ok = svc_ok(svc_call(hdr, title, strlen(title), body, strlen(body)), resp,
                sizeof(resp));
    free(title);
    free(body);
    if (!ok)
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (!render_post(post))
        cache_failed("mask_post", post->community,
                     post->bbs_header->filename, BOF_ERR_UNKNOWN);
    if (out && out->bbs_header)
        *out->bbs_header = *post->bbs_header;
    return BOF_OK;
}

static bof_error_t
ps_remove_crosspost(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_bbs_remove_crosspost_t *req,
                    bof_post_result_t *out)
{
    if (!req->bbs_content_only && req->community && req->bbs_header) {
        bof_post_ref_t post = {
            .community = req->community, .bbs_dir = req->bbs_dir,
            .bbs_header = req->bbs_header,
        };
        drop_post(actor, &post, req->reason ? req->reason : "");
    }
    return bof_fs_bbs_remove_crosspost(NULL, actor, req, out);
}

// --------------------------------------------------------------------------
// Comments

// The comment of what was typed (or the line for the file), without the
// line break, for post.svc, to free().
static char *
comment_to_svc(const char *text)
{
    char buf[ANSILINELEN * 4];
    size_t n;

    strlcpy(buf, text ? text : "", sizeof(buf));
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    return to_svc(buf);
}

static bof_error_t
ps_create_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_create_comment_t *req,
                  bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char hdr[512], resp[128], status[16] = "";
    long long ctime = 0;
    int seq = 0, score = 0, merged, legtype;
    char *msg;

    if (!valid(post) || (!req->bbs_text && !req->content))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!(msg = comment_to_svc(req->bbs_text ? req->bbs_text :
                               req->content)))
        return BOF_ERR_UNKNOWN;
    // post.svc keeps votes apart (CREATE_POST_LIKE_FILE): the type is how
    // it is shown.
    legtype = req->bbs_vote > 0 ? 1 : req->bbs_vote < 0 ? 2 : 3;
    snprintf(hdr, sizeof(hdr),
             "CREATE_COMMENT %s %s %s %" PRIu64 " %s %ld %zu %d\n",
             post->community, post->bbs_header->filename, who(actor),
             uid(actor), from(actor), (long)time4(NULL), strlen(msg),
             legtype);
    int ok = svc_ok(svc_call(hdr, msg, strlen(msg), NULL, 0), resp,
                    sizeof(resp));
    free(msg);
    if (!ok)
        return BOF_ERR_COULDNT_CREATE_COMMENT;
    sscanf(resp, "OK %d %lld %d %15s", &seq, &ctime, &score, status);
    merged = strcmp(status, "MERGED") == 0;

    // The record in the cache: when it changed, and how many comments
    // (post.svc has written the article again itself).
    fileheader_t updated;
    if (update_record(post, ctime > 0 ? (int32_t)ctime : time4(NULL), 0,
                      merged ? 0 : 1, &updated) != 0)
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (out) {
        if (out->bbs_header)
            *out->bbs_header = updated;
        out->bbs_floor = seq;
        out->bbs_merged = merged;
    }
    return BOF_OK;
}

// After a comment changed: the article again, and the time of the change.
static bof_error_t
comment_changed(const bof_post_ref_t *post, bof_comment_response_t *out)
{
    char path[PATHLEN];
    fileheader_t updated;

    post_path(path, post);
    if (!render(post->community, post->bbs_header->filename, path))
        cache_failed("comment", post->community, post->bbs_header->filename,
                     BOF_ERR_UNKNOWN);
    if (update_record(post, dasht4(path) ? dasht4(path) : time4(NULL), 0, 0,
                      &updated) != 0)
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (out) {
        if (out->bbs_header)
            *out->bbs_header = updated;
        out->bbs_file_size = dashs(path);
    }
    return BOF_OK;
}

static bof_error_t
ps_edit_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_edit_comment_t *req, bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char hdr[512];
    char *msg;
    int ok;

    if (!valid(post) || req->bbs_floor <= 0 || !req->content)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!(msg = comment_to_svc(req->content)))
        return BOF_ERR_UNKNOWN;
    snprintf(hdr, sizeof(hdr), "EDIT_COMMENT_FILE %s %s %d %s %" PRIu64
             " %zu\n", post->community, post->bbs_header->filename,
             req->bbs_floor, who(actor), uid(actor), strlen(msg));
    ok = svc_ok(svc_call(hdr, msg, strlen(msg), NULL, 0), NULL, 0);
    free(msg);
    if (!ok)
        return BOF_ERR_COULDNT_UPDATE_COMMENT;
    return comment_changed(post, out);
}

// Deletes a comment as its creator (DELETE_COMMENT_FILE; reason is NULL)
// or removes it for reason (REMOVE_COMMENT_FILE).
static bof_error_t
delete_comment(const bof_actor_t *actor, const bof_post_ref_t *post,
               int floor, const char *reason, bof_comment_response_t *out)
{
    char *why = NULL;
    int ok;

    if (!reason)
        ok = svc_cmd(NULL, 0, "DELETE_COMMENT_FILE %s %s %d %s %" PRIu64 "\n",
                     post->community, post->bbs_header->filename, floor,
                     who(actor), uid(actor));
    else
        ok = (why = to_svc(reason)) &&
             svc_cmd(NULL, 0, "REMOVE_COMMENT_FILE %s %s %d %s %" PRIu64
                     " %s\n", post->community, post->bbs_header->filename,
                     floor, who(actor), uid(actor), why);
    free(why);
    if (!ok)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    return comment_changed(post, out);
}

static bof_error_t
ps_delete_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_delete_comment_t *req,
                  bof_comment_response_t *out)
{
    if (!valid(&req->post) || req->bbs_floor <= 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    // Taking a comment back is not supported yet (RECOVER_COMMENT_FILE).
    if (!req->deleted)
        return BOF_ERR_BBS_STORAGE_NOT_SUPPORTED;
    return delete_comment(actor, &req->post, req->bbs_floor, NULL, out);
}

static bof_error_t ps_get_comments(void *ctx, const bof_get_comments_t *req,
                                   bof_get_comments_response_t *out);

static bof_error_t
ps_remove_comment(void *ctx, const bof_actor_t *actor,
                  const bof_remove_comment_t *req,
                  bof_comment_response_t *out)
{
    int floor = req->bbs_floor;

    if (!valid(&req->post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // Without the floor, the first comment by the author saying it.
    if (floor <= 0 && req->bbs_author && req->bbs_content) {
        bof_comment_t *cs = calloc(512, sizeof(*cs));
        bof_get_comments_t get = {
            .post = req->post, .bbs_creator = req->bbs_author, .limit = 512,
        };
        bof_get_comments_response_t res = { .comments = cs };
        if (cs && ps_get_comments(ctx, &get, &res) == BOF_OK)
            for (int i = 0; i < res.count && floor <= 0; i++)
                if (!cs[i].removed &&
                    strcmp(cs[i].content, req->bbs_content) == 0)
                    floor = cs[i].bbs_floor;
        free(cs);
    }
    if (floor <= 0)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    return delete_comment(actor, &req->post, floor,
                          req->reason ? req->reason : "", out);
}

// --------------------------------------------------------------------------
// Reads

static bof_error_t
ps_get_post_body(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                 const char *dest_path, bof_post_version_t *ver)
{
    int32_t modified = 0;

    if (!valid(post) || !dest_path || !ver)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!fetch_body(post, dest_path, &modified))
        return BOF_ERR_COULDNT_FIND_POST;
    // The version is the time of the content in post.svc.
    ver->size = 0;
    ver->hash = (uint64_t)(uint32_t)modified;
    return BOF_OK;
}

static int
ps_post_version_matches(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                        const bof_post_version_t *ver)
{
    int32_t modified = 0;

    if (!valid(post) || !ver || !fetch_body(post, "/dev/null", &modified))
        return 0;
    return (uint64_t)(uint32_t)modified == ver->hash;
}

// post.svc writes the article again into the board on each change
// (RenderAndSave), so only a missing file is rendered.
static bof_error_t
ps_refresh_post(void *ctx GCC_UNUSED, const bof_post_ref_t *post)
{
    char path[PATHLEN];

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    post_path(path, post);
    if (dashf(path))
        return BOF_OK;
    return render(post->community, post->bbs_header->filename, path) ?
        BOF_OK : BOF_ERR_COULDNT_FIND_POST;
}

static bof_error_t
ps_get_comments(void *ctx GCC_UNUSED, const bof_get_comments_t *req,
                bof_get_comments_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char cmd[512], *r, *line, *save = NULL;
    const int by_creator = req->bbs_creator && *req->bbs_creator;
    bof_community_t comm;
    int show_ip, skip = 0, start = 1, limit;

    out->count = 0;
    if (!valid(post) || !out->comments || req->limit <= 0 || req->page < 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    // Where they are shown with the line (BRD_COMMENT_SHOWIP), as the files
    // keep them: post.svc keeps the address of every comment, which is
    // private otherwise.
    show_ip = bof_get_community(post->community, &comm) == BOF_OK &&
              (comm.bbs_attr & BRD_COMMENT_SHOWIP);
    // Lemmy's page of limit: comments of all are by floor; those of one
    // person are counted from the first.
    limit = req->limit;
    if (req->page > 1) {
        if ((int64_t)(req->page - 1) * req->limit > BOF_FLOOR_MAX)
            return BOF_OK;
        if (by_creator) {
            skip = (req->page - 1) * req->limit;
            limit += skip;
        } else {
            start = (req->page - 1) * req->limit + 1;
        }
    }
    snprintf(cmd, sizeof(cmd), "GET_COMMENTS_FILE %s %s %d %d %s\n",
             post->community, post->bbs_header->filename, start, limit,
             by_creator ? req->bbs_creator : "-");
    if (!(r = svc_call(cmd, NULL, 0, NULL, 0)))
        return BOF_ERR_COULDNT_FIND_POST;
    if (strncmp(r, "OK", 2) != 0) {
        free(r);
        return BOF_ERR_COULDNT_FIND_POST;
    }
    // <seq> <userid> <ctime> <ip> <type> <reason> <comment_id> <lines...>
    strtok_r(r, "\n", &save);
    while ((line = strtok_r(NULL, "\n", &save)) && out->count < req->limit) {
        char *f[64];
        int nf = 0;
        f[nf++] = line;
        for (char *p = line; *p && nf < 64; p++)
            if (*p == '\t') {
                *p = '\0';
                f[nf++] = p + 1;
            }
        if (nf < 8)
            continue;
        if (skip > 0) {
            skip--;
            continue;
        }
        bof_comment_t *c = &out->comments[out->count++];
        char text[sizeof(c->content) * 3] = "";
        time4_t t = (time4_t)atoll(f[2]);
        int type = atoi(f[4]);

        memset(c, 0, sizeof(*c));
        c->bbs_floor = atoi(f[0]);
        strlcpy(c->creator, f[1], sizeof(c->creator));
        c->removed = type < 0;
        c->bbs_vote = type == 1 ? 1 : type == 2 ? -1 : 0;
        for (int i = 7; i < nf; i++) {
            // The lines of a comment (post.svc keeps several).
            if (i > 7)
                strlcat(text, "\n", sizeof(text));
            strlcat(text, f[i], sizeof(text));
        }
        SERVICE_TO_MB(text, c->content, sizeof(c->content));
        if (show_ip && strcmp(f[3], "-") != 0)
            snprintf(c->bbs_tail, sizeof(c->bbs_tail), " %s %s", f[3],
                     Cdate_mdHM(&t));
        else
            snprintf(c->bbs_tail, sizeof(c->bbs_tail), " %s",
                     Cdate_mdHM(&t));
        c->bbs_width = (int)strlen(c->content);
    }
    free(r);
    return BOF_OK;
}

// GET_COMMENT_REVISIONS_FILE: "OK <n>", then a line per revision, oldest first:
// <rev> <edited_at> <editor> [<edit|delete> <reason or ->] <lines...>,
// separated by tabs.
static bof_error_t
ps_get_comment_revisions(void *ctx GCC_UNUSED,
                         const bof_get_comment_revisions_t *req,
                         bof_get_comment_revisions_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char cmd[512], *r, *line, *save = NULL;

    out->count = 0;
    if (!post->community || !post->bbs_header)
        return BOF_ERR_INVALID_BODY_FIELD;
    snprintf(cmd, sizeof(cmd), "GET_COMMENT_REVISIONS_FILE %s %s %d\n",
             post->community, post->bbs_header->filename, req->bbs_floor);
    if (!(r = svc_call(cmd, NULL, 0, NULL, 0)))
        return BOF_ERR_COULDNT_FIND_COMMENT;
    if (strncmp(r, "OK", 2) != 0) {
        free(r);
        return BOF_ERR_COULDNT_FIND_COMMENT;
    }
    strtok_r(r, "\n", &save);
    while ((line = strtok_r(NULL, "\n", &save)) && out->count < req->limit) {
        char *f[64], text[BOF_COMMENT_CONTENT_LEN * 3] = "";
        int nf = 0, start = 3;
        f[nf++] = line;
        for (char *p = line; *p && nf < 64; p++)
            if (*p == '\t') {
                *p = '\0';
                f[nf++] = p + 1;
            }
        if (nf < 4)
            continue;
        bof_comment_revision_t *v = &out->revisions[out->count++];
        memset(v, 0, sizeof(*v));
        v->number = atoi(f[0]);
        v->edited_at = (int32_t)atoll(f[1]);
        strlcpy(v->editor, f[2], sizeof(v->editor));
        if (nf >= 6 && (!strcmp(f[3], "edit") || !strcmp(f[3], "delete"))) {
            v->deleted = !strcmp(f[3], "delete");
            if (strcmp(f[4], "-"))
                SERVICE_TO_MB(f[4], v->reason, sizeof(v->reason));
            start = 5;
        }
        for (int i = start; i < nf; i++) {
            if (i > start)
                strlcat(text, "\n", sizeof(text));
            strlcat(text, f[i], sizeof(text));
        }
        SERVICE_TO_MB(text, v->content, sizeof(v->content));
    }
    free(r);
    return BOF_OK;
}

// --------------------------------------------------------------------------
// IDs: post.svc's (Lemmy's post.id, community.id)

static bof_error_t
ps_get_post_id(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
               bof_post_id_t *id)
{
    char resp[128];
    unsigned long long v = 0;

    if (!post->community || !post->bbs_header ||
        !post->bbs_header->filename[0])
        return BOF_ERR_INVALID_BODY_FIELD;
    // GET_POST_FILE <community> <file>: OK <id> ... (and the post, not needed).
    if (!svc_cmd(resp, sizeof(resp), "GET_POST_FILE %s %s\n", post->community,
                 post->bbs_header->filename) ||
        sscanf(resp, "OK %llu", &v) != 1 || !v || v > INT32_MAX)
        return BOF_ERR_COULDNT_FIND_POST;
    *id = (bof_post_id_t)v;
    return BOF_OK;
}

static bof_error_t
ps_get_post_key(void *ctx GCC_UNUSED, bof_post_id_t id, bof_post_key_t *key)
{
    char resp[256], community[BOF_COMMUNITY_NAME_MAX];
    char file[BOF_POST_FILE_MAX];

    // GET_POST <id>: OK <id> <parent> <community> <file> ...
    if (!svc_cmd(resp, sizeof(resp), "GET_POST %lld\n", (long long)id) ||
        sscanf(resp, "OK %*u %*u %15s %31s", community, file) != 2)
        return BOF_ERR_COULDNT_FIND_POST;
    strlcpy(key->community, community, sizeof(key->community));
    strlcpy(key->bbs_post_file, file, sizeof(key->bbs_post_file));
    return BOF_OK;
}

static bof_error_t
ps_get_community_id(void *ctx GCC_UNUSED, const char *name,
                    bof_community_id_t *id)
{
    char cmd[64], *r, *p;
    unsigned long long v = 0;

    // GET_COMMUNITY <name>: OK <bytes>, then RFC 822 headers with ID.
    if (snprintf(cmd, sizeof(cmd), "GET_COMMUNITY %s\n", name) >=
        (int)sizeof(cmd))
        return BOF_ERR_INVALID_BODY_FIELD;
    r = svc_call(cmd, NULL, 0, NULL, 0);
    if (r && strncmp(r, "OK", 2) == 0 && (p = strstr(r, "\nID: ")))
        v = strtoull(p + 5, NULL, 10);
    free(r);
    if (!v || v > INT32_MAX)
        return BOF_ERR_COULDNT_FIND_COMMUNITY;
    *id = (bof_community_id_t)v;
    return BOF_OK;
}

// --------------------------------------------------------------------------
// The versions of posts: post.svc keeps them (GET_POST_REVISIONS_FILE); posts
// from before it, in the time capsule of the files.

// The versions post.svc keeps of post, oldest first: their revision
// numbers in post.svc into revs and what is known of them into info
// (each with room for max, or NULL). Returns how many it keeps (may be
// more than max).
static int
svc_revisions(const bof_post_ref_t *post, int *revs,
              bof_bbs_post_revision_t *info, int max)
{
    char cmd[128], *r, *line, *next;
    int count = 0, n = 0;

    if (!post->community || snprintf(cmd, sizeof(cmd),
                                     "GET_POST_REVISIONS_FILE %s %s\n",
                                     post->community,
                                     post->bbs_header->filename) >=
        (int)sizeof(cmd))
        return 0;
    r = svc_call(cmd, NULL, 0, NULL, 0);
    // OK <count>, then a line each: <rev>\t<edited at>\t<editor>[\t<title>]
    if (!r || sscanf(r, "OK %d", &count) != 1 || count <= 0) {
        free(r);
        return 0;
    }
    for (line = strchr(r, '\n'); line && n < count; line = next) {
        char *f[4] = { 0 };
        int nf = 0;

        line++;
        if ((next = strchr(line, '\n')))
            *next = '\0';
        if (!*line)
            continue;
        for (char *p = line; p && nf < 4; nf++) {
            f[nf] = p;
            if ((p = strchr(p, '\t')))
                *p++ = '\0';
        }
        if (n < max) {
            if (revs)
                revs[n] = atoi(f[0]);
            if (info) {
                memset(&info[n], 0, sizeof(info[n]));
                info[n].number = n + 1;
                if (f[1])
                    info[n].edited_at = (int32_t)atoll(f[1]);
                if (f[2])
                    strlcpy(info[n].editor, f[2], sizeof(info[n].editor));
                if (f[3])
                    SERVICE_TO_MB(f[3], info[n].name, sizeof(info[n].name));
            }
        }
        n++;
    }
    free(r);
    return n;
}

static bof_error_t
ps_get_post_revisions(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                      int *count)
{
    int n = svc_revisions(post, NULL, NULL, 0);

    if (n > 0) {
        *count = n;
        return BOF_OK;
    }
    return bof_fs_get_post_revisions(NULL, post, count);
}

static bof_error_t
ps_get_post_revision_file(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                          int rev, char *path, size_t sz)
{
    int revs[256], n = svc_revisions(post, revs, NULL, ARRAY_SIZE(revs));

    if (n <= 0)
        return bof_fs_get_post_revision_file(NULL, post, rev, path, sz);
    if (rev > n || rev > (int)ARRAY_SIZE(revs))
        return BOF_ERR_COULDNT_FIND_POST;
    // Fetched into a file of the process, valid until the next call.
    snprintf(path, sz, BBSHOME "/tmp/bof_rev.%d", (int)getpid());
    if (!svc_cmd(NULL, 0, "GET_POST_REVISIONS_FILE %s %s %d %s\n",
                 post->community, post->bbs_header->filename,
                 revs[rev - 1], path))
        return BOF_ERR_COULDNT_FIND_POST;
    return BOF_OK;
}

static bof_error_t
ps_list_post_revisions(void *ctx GCC_UNUSED,
                       const bof_bbs_list_post_revisions_t *req,
                       bof_bbs_list_post_revisions_response_t *out)
{
    bof_bbs_post_revision_t *all;
    int n = svc_revisions(&req->post, NULL, NULL, 0);

    if (n <= 0)
        return bof_fs_list_post_revisions(NULL, req, out);
    if (!(all = calloc(n, sizeof(*all))))
        return BOF_ERR_UNKNOWN;
    n = svc_revisions(&req->post, NULL, all, n);
    out->bbs_total = n;
    for (int i = req->start; i < n && out->count < req->limit; i++)
        out->revisions[out->count++] = all[i];
    free(all);
    return BOF_OK;
}

// --------------------------------------------------------------------------
// Communities. The board headers stay in .BRD, which post.svc mirrors:
// told of each slot written (EDIT_COMMUNITY), and all of it when it starts.
// What a header cannot say goes to post.svc first, then to the files of
// the board, its cache.

// Merging and rebuilding boards go through all of their posts.
#define POSTSVC_SLOW_TIMEOUT_MS (10 * 60 * 1000)

// Sends a one line command about a community, waiting up to timeout_ms;
// 1 if OK (the first line of the response in resp). What post.svc did not
// do is logged, with its response, for the operators.
static int community_cmd(char *resp, size_t sz, int timeout_ms,
                         const char *fmt, ...) GCC_CHECK_FORMAT(4, 5);

static int
community_cmd(char *resp, size_t sz, int timeout_ms, const char *fmt, ...)
{
    char cmd[512];
    va_list ap;
    int n, ok;
    char *r;

    va_start(ap, fmt);
    n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(cmd))
        return 0;
    r = svc_call_wait(cmd, NULL, 0, NULL, 0, timeout_ms);
    ok = r && strncmp(r, "OK", 2) == 0;
    if (!ok) {
        const char *why = r && *r ? r : "no response";
        time4_t now = time4(NULL);
        file_appendf(BOF_STORAGE_LOG, "%s postsvc %.*s: %.*s\n",
                     Cdatelite(&now), (int)strcspn(cmd, "\r\n"), cmd,
                     (int)strcspn(why, "\r\n"), why);
    }
    if (resp && sz) {
        strlcpy(resp, r ? r : "", sz);
        resp[strcspn(resp, "\r\n")] = '\0';
    }
    free(r);
    return ok;
}

// Tells post.svc to mirror slot bid of .BRD (EDIT_COMMUNITY <bid>). .BRD
// has it already: if post.svc does not take it, it is logged; post.svc
// takes all of .BRD again when it starts (or with post.ctl import-boards).
static void
mirror_slot(int bid)
{
    community_cmd(NULL, 0, POSTSVC_TIMEOUT_MS, "EDIT_COMMUNITY %d\n", bid);
}

static bof_error_t
ps_edit_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_edit_community_t *req)
{
    bof_error_t err = bof_fs_edit_community(NULL, actor, req);

    if (err == BOF_OK)
        mirror_slot(req->bbs_id);
    return err;
}

static bof_error_t
ps_create_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_create_community_t *req,
                    bof_create_community_response_t *out)
{
    bof_create_community_response_t res = { 0 };
    bof_error_t err = bof_fs_create_community(NULL, actor, req, &res);

    if (err == BOF_OK)
        mirror_slot(res.bbs_id);
    if (out)
        *out = res;
    return err;
}

static bof_error_t
ps_remove_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_remove_community_t *req,
                    bof_remove_community_response_t *out)
{
    char resp[128], *reason = to_svc(req->reason);
    bof_error_t err;
    int ok;

    if (!reason)
        return BOF_ERR_UNKNOWN;
    // REMOVE_COMMUNITY <community> <mod_person_id> [<reason>]: OK <id>, or
    // OK 0 if post.svc does not know it. The reason is the rest of the line.
    ok = community_cmd(resp, sizeof(resp), POSTSVC_TIMEOUT_MS,
                       "REMOVE_COMMUNITY %s %" PRIu64 " %.*s\n",
                       req->community, uid(actor),
                       (int)strcspn(reason, "\r\n"), reason);
    free(reason);
    if (!ok)
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    err = bof_fs_remove_community(NULL, actor, req, out);
    if (err != BOF_OK && err != BOF_ERR_COULDNT_FIND_COMMUNITY)
        cache_failed("remove_community", req->community, "-", err);
    if (out)
        out->bbs_posts_kept = strcmp(resp, "OK 0") != 0;
    return BOF_OK;
}

static bof_error_t
ps_rename_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_bbs_rename_community_t *req)
{
    char resp[128];
    bof_error_t err;

    // RENAME_COMMUNITY <old> <new>: OK <id>, or OK 0 if post.svc does not
    // know it; ERR if the new name is taken.
    if (!community_cmd(resp, sizeof(resp), POSTSVC_TIMEOUT_MS,
                       "RENAME_COMMUNITY %s %s\n", req->community,
                       req->new_name))
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    if ((err = bof_fs_rename_community(NULL, actor, req)) != BOF_OK &&
        strcmp(resp, "OK 0") != 0) {
        // The board and its community must not part: put it back.
        community_cmd(NULL, 0, POSTSVC_TIMEOUT_MS, "RENAME_COMMUNITY %s %s\n",
                      req->new_name, req->community);
    }
    return err;
}

// Makes the list and articles of a board again from post.svc.
static bof_error_t
rebuild(const char *community)
{
    char resp[128];

    if (community_cmd(resp, sizeof(resp), POSTSVC_SLOW_TIMEOUT_MS,
                      "SYNC_DIR %s 0 rebuild\n", community))
        return BOF_OK;
    return strstr(resp, "unknown community") ?
        BOF_ERR_COULDNT_FIND_COMMUNITY : BOF_ERR_COULDNT_UPDATE_COMMUNITY;
}

static bof_error_t
ps_merge_communities(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                     const bof_bbs_merge_communities_t *req,
                     bof_bbs_merge_communities_response_t *out)
{
    char resp[128];
    int moved = -1, collisions = 0;

    // MERGE_COMMUNITY <from> <to>: OK <moved> <collisions>.
    if (!community_cmd(resp, sizeof(resp), POSTSVC_SLOW_TIMEOUT_MS,
                       "MERGE_COMMUNITY %s %s\n", req->from, req->to))
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    sscanf(resp, "OK %d %d", &moved, &collisions);
    // post.svc has them; the cache of both boards is made again (what fails
    // is logged, and sync-dir makes it later).
    rebuild(req->to);
    rebuild(req->from);
    if (out) {
        out->moved = moved;
        out->collisions = collisions;
    }
    return BOF_OK;
}

static bof_error_t
ps_rebuild_community(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                     const bof_bbs_rebuild_community_t *req,
                     bof_bbs_rebuild_community_response_t *out)
{
    if (out)
        out->in_background = 0;
    return rebuild(req->community);
}

const bof_storage_ops_t bof_storage_postsvc = {
    .name = "postsvc",
    .create_post = ps_create_post,
    .edit_post = ps_edit_post,
    .delete_post = ps_delete_post,
    .remove_post = ps_remove_post,
    .remove_posts = ps_remove_posts,
    .purge_posts = ps_purge_posts,
    .restore_post = ps_restore_post,
    .lock_post = ps_lock_post,
    .feature_post = ps_feature_post,
    .set_post_modes = ps_set_post_modes,
    .reset_post_score = ps_reset_post_score,
    .mask_post = ps_mask_post,
    .remove_crosspost = ps_remove_crosspost,
    .create_post_like = ps_create_post_like,
    .create_comment = ps_create_comment,
    .edit_comment = ps_edit_comment,
    .delete_comment = ps_delete_comment,
    .remove_comment = ps_remove_comment,
    // Kept in the files, which post.svc does not keep.
    .create_report = bof_fs_create_report,
    .resolve_report = bof_fs_resolve_report,
    .write_modlog = bof_fs_write_modlog,
    .note_modlog_edit = bof_fs_note_modlog_edit,
    .list_reports = bof_fs_list_reports,
    .list_post_likes = ps_list_post_likes,
    // Read from the cache.
    .get_posts = bof_fs_get_posts,
    .get_post = bof_fs_get_post,
    .get_post_body = ps_get_post_body,
    .get_comments = ps_get_comments,
    .get_comment_revisions = ps_get_comment_revisions,
    .post_version_matches = ps_post_version_matches,
    .refresh_post = ps_refresh_post,
    // IDs from post.svc; a key found where it is now in the cache.
    .get_post_id = ps_get_post_id,
    .get_post_key = ps_get_post_key,
    .find_post = bof_fs_find_post,
    .get_community_id = ps_get_community_id,
    .get_post_revisions = ps_get_post_revisions,
    .get_post_revision_file = ps_get_post_revision_file,
    .list_post_revisions = ps_list_post_revisions,
    // The recycle bin is the time capsule of the cache.
    .list_archived_posts = bof_fs_list_archived_posts,
    .edit_community = ps_edit_community,
    .create_community = ps_create_community,
    .remove_community = ps_remove_community,
    .rename_community = ps_rename_community,
    .merge_communities = ps_merge_communities,
    .rebuild_community = ps_rebuild_community,
};
