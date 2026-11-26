/*
 * nest.c - bof (Birds Of Feather): the storage backend of nest.svc.
 *
 * Copyright (c) 2026 Hung-Te Lin (piaip) <hungte@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * It is not the default backend: a site with nest.svc running chooses it
 * (CONFIG_NEST_SVC, or BOF_DEFAULT_STORAGE in pttbbs.conf).
 *
 * nest.svc keeps the posts, comments and votes (a database); the files of
 * the board (.DIR, the articles) are a cache of it, compatible with the
 * files backend, so that reading needs no IPC. Each write goes to nest.svc
 * first (its error is the result), then to the cache: nest.svc writes the
 * article into the board itself on each change, and the records (.DIR) are
 * updated with the files backend (or the article rendered again with
 * RENDER_FILE where it matters). Reports and the modlog, which nest.svc
 * does not keep, stay in the files.
 *
 * Posts are named by board and file name, as nest.svc keys them (the
 * *_FILE commands, e.g. REMOVE_POST_FILE). Persons are sent by name and by
 * their account (bof_actor_t.bbs_local_user).
 */

#include "../bof_int.h"
#include "bof_fs.h"
#include "bof_storage.h"
#include <inttypes.h>

// Where nest.svc listens; tests point it elsewhere (bof_nest_set_socket()).
static const char *nest_sock = GET_SVC_SOCKET(nest);

void
bof_nest_set_socket(const char *path)
{
    nest_sock = path ? path : GET_SVC_SOCKET(nest);
}
#define NEST_TIMEOUT_MS  (3000)

// --------------------------------------------------------------------------
// IPC

// Sends hdr and the payloads, and reads the whole response (to EOF, as
// nest.svc closes the connection) into a buffer to free(), waiting up to
// timeout_ms for each read. Returns NULL if nest.svc cannot be reached.
static char *
svc_call_wait(const char *hdr, const void *p1, size_t l1, const void *p2,
              size_t l2, int timeout_ms)
{
    size_t cap = 4096, len = 0;
    char *buf;
    int s, n;

    if ((s = svc_connect(nest_sock)) < 0)
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
    return svc_call_wait(hdr, p1, l1, p2, l2, NEST_TIMEOUT_MS);
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

// s converted for nest.svc, to free().
static char *
to_svc(const char *s)
{
    size_t sz = strlen(s ? s : "") * 3 + 1;
    char *out = malloc(sz);

    if (out)
        MB_TO_SERVICE(s ? s : "", out, sz);
    return out;
}

// The comment of what was typed (or the line for the file), without the
// line break, for nest.svc, to free().
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

// The content of a file converted for nest.svc, to free().
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

// The file of a post in the board. A locked post (L. in its record) is
// kept in X.: nest.svc writes it there (BoardFilePath), as the files
// backend renames it (bof_fs_lock_post()). nest.svc itself takes either
// name of the post.
static void
post_path(char *path, const bof_post_ref_t *post)
{
    char name[sizeof(post->bbs_header->filename)];

    strlcpy(name, post->bbs_header->filename, sizeof(name));
    if (name[0] == 'L' && name[1] == '.')
        name[0] = 'X';
    setdirpath(path, post->bbs_dir, name);
}

static int
valid(const bof_post_ref_t *post)
{
    return post->community && post->bbs_dir && post->bbs_header &&
        post->bbs_header->filename[0];
}

// Has nest.svc render the article (the cache) of a post into its file.
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
// its time in nest.svc (the version of the content).
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

// Sends the file at path as the new body of a post (EDIT_POST_FILE), if
// it is still at the version modified; 1 if OK.
static int
send_body(const bof_actor_t *actor, const bof_post_ref_t *post,
          const char *path, int32_t modified)
{
    char hdr[512], *title = to_svc(post->bbs_header->title);
    char *body = file_to_svc(path);
    int ok;

    if (!title || !body) {
        free(title);
        free(body);
        return 0;
    }
    snprintf(hdr, sizeof(hdr), "EDIT_POST_FILE %s %s %s %" PRIu64
             " %lld %zu %zu\n", post->community, post->bbs_header->filename,
             who(actor), uid(actor), (long long)modified, strlen(title),
             strlen(body));
    ok = svc_ok(svc_call(hdr, title, strlen(title), body, strlen(body)),
                NULL, 0);
    free(title);
    free(body);
    return ok;
}

// Sends the filemode and money of a record (SET_META_FILE); 1 if OK.
static int
set_meta(const char *community, const fileheader_t *fh)
{
    int money = (fh->filemode & (FILE_ANONYMOUS | FILE_PETITION)) ?
        0 : fh->multi.money;
    return svc_cmd(NULL, 0, "SET_META_FILE %s %.*s %d %d\n", community,
                   (int)sizeof(fh->filename), fh->filename, fh->filemode,
                   money);
}

// The record of a post as the cache has it now (at its index, if it is
// still there), or else as given.
static fileheader_t
current_record(const bof_post_ref_t *post)
{
    fileheader_t fh = *post->bbs_header;

    if (post->bbs_index > 0 && post->bbs_dir) {
        fileheader_t cur;
        if (get_fileheader(post->bbs_dir, &cur, post->bbs_index) > 0 &&
            strcmp(cur.filename, fh.filename) == 0)
            fh = cur;
    }
    return fh;
}

// Logs a failure to update the cache: nest.svc has the change, and
// sync-dir makes the files again.
static void
cache_failed(const char *op, const char *community, const char *file,
             bof_error_t err)
{
    time4_t now = time4(NULL);
    file_appendf(BOF_STORAGE_LOG, "%s nest cache %s %s/%s: %s\n",
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
    // nest.svc keeps the whole score; it is shown capped.
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
nest_create_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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
    // have no posts of their own in nest.svc.
    if (IS_ENABLED(CONFIG_LIVE_ALLPOST) && bof_bbs_is_aggregate(req->community))
        return bof_fs_create_post(NULL, actor, req, out);

    // Named here if not yet: nest.svc keys posts by board and file name.
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

    // The record in the cache. nest.svc writes the article into the board
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
        // The post is in nest.svc (the result), not yet in the cache: the
        // caller still gets its record, as nest.svc has it.
        cache_failed("create_post", req->community, fh->filename, err);
        if (out && out->bbs_header)
            *out->bbs_header = named;
    }
    return BOF_OK;
}

static bof_error_t
nest_create_post_like(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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
nest_list_post_likes(void *ctx GCC_UNUSED, const bof_list_post_likes_t *req,
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
nest_edit_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_edit_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    const fileheader_t *fh = post->bbs_header;
    const char *name = req->name && *req->name ? req->name : fh->title;
    char path[PATHLEN], resp[128], hdr[1024];
    int32_t modified = req->bbs_modified;
    fileheader_t updated;
    bof_post_response_t res = { .bbs_header = &updated };
    bof_edit_post_t rec;
    bof_error_t err;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (out)
        out->bbs_body_saved = 0;
    // The money goes to nest.svc first (SET_META_FILE carries the money
    // and the filemode; the owner and date are kept in the cache only):
    // if nest.svc does not take it, nothing is changed.
    if (req->bbs_money) {
        fileheader_t m = current_record(post);
        m.multi.money = *req->bbs_money;
        if (!set_meta(post->community, &m))
            return BOF_ERR_COULDNT_UPDATE_POST;
    }

    if (req->bbs_body_path) {
        char *title = to_svc(name), *body = file_to_svc(req->bbs_body_path);
        long long m = 0;
        unsigned rev = 0;
        int64_t expected = req->bbs_body_version ?
            (int64_t)req->bbs_body_version->hash : 0;

        if (!title || !body) {
            free(title);
            free(body);
            goto failed;
        }
        // The edit mark, as UTF-8 in hex (one word of the header).
        char mark[2 * 3 * STRLEN + 1] = "-";
        if (req->bbs_edit_mark && *req->bbs_edit_mark) {
            char *m = to_svc(req->bbs_edit_mark);
            size_t n = 0;
            for (const unsigned char *c = (const unsigned char *)(m ? m : "");
                 *c && n + 2 < sizeof(mark); c++, n += 2)
                snprintf(mark + n, 3, "%02x", *c);
            if (n == 0)
                strcpy(mark, "-");
            free(m);
        }
        snprintf(hdr, sizeof(hdr), "EDIT_POST_FILE %s %s %s %" PRIu64
                 " %lld %zu %zu %s\n", post->community, fh->filename,
                 who(actor), uid(actor), (long long)expected, strlen(title),
                 strlen(body), mark);
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
            goto failed;
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
            goto failed;
        snprintf(hdr, sizeof(hdr), "EDIT_POST_TITLE_FILE %s %s %s %" PRIu64
                 " %zu\n", post->community, fh->filename, who(actor),
                 uid(actor), strlen(title));
        ok = svc_ok(svc_call(hdr, title, strlen(title), NULL, 0), NULL, 0);
        free(title);
        if (!ok)
            goto failed;
    }

    // The record in the cache.
    rec = *req;
    rec.bbs_body_path = NULL;
    rec.bbs_modified = modified;
    err = bof_fs_edit_post(NULL, actor, &rec, &res);
    if (err != BOF_OK) {
        if (req->bbs_money || (out && out->bbs_body_saved))
            cache_failed("edit_post", post->community, fh->filename, err);
        return out && out->bbs_body_saved ? BOF_ERR_COULDNT_UPDATE_POST : err;
    }
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;

failed:
    // nest.svc has taken the money already: so does the cache.
    if (req->bbs_money) {
        memset(&rec, 0, sizeof(rec));
        rec.post = req->post;
        rec.bbs_money = req->bbs_money;
        if ((err = bof_fs_edit_post(NULL, actor, &rec, &res)) != BOF_OK)
            cache_failed("edit_post", post->community, fh->filename, err);
    }
    return BOF_ERR_COULDNT_UPDATE_POST;
}

// What deleting and removing share: nest.svc keeps it as deleted by its
// creator (DELETE_POST_FILE, without a reason: reason is NULL) or as
// removed, with a reason (REMOVE_POST_FILE). nest.svc writes a note of
// that over the article at once, so the article is kept aside first and
// put back after, for the cache to archive it (the recycle bin, the time
// capsule) as the files backend does. If nest.svc fails, the article
// stays as it was.
static bof_error_t
drop_post(const bof_actor_t *actor, const bof_post_ref_t *post,
          const char *reason)
{
    char path[PATHLEN], keep[PATHLEN];
    char *why = NULL;
    int ok, kept;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    post_path(path, post);
    if (!dashf(path))
        render_post(post);
    snprintf(keep, sizeof(keep), "%s.keep%d", path, (int)getpid());
    kept = dashf(path) && Copy(path, keep) == 0;
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
    if (kept && (!ok || Rename(keep, path) != 0))
        unlink(keep);
    return ok ? BOF_OK : BOF_ERR_COULDNT_UPDATE_POST;
}

static bof_error_t
nest_delete_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_delete_post_t *req, bof_post_result_t *out)
{
    bof_error_t err;

    if (!req->deleted)
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((err = drop_post(actor, &req->post, NULL)) != BOF_OK)
        return err;
    // nest.svc has taken it down (the result); the cache follows.
    if ((err = bof_fs_delete_post(NULL, actor, req, out)) != BOF_OK)
        cache_failed("delete_post", req->post.community,
                     req->post.bbs_header->filename, err);
    return BOF_OK;
}

static bof_error_t
nest_remove_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
               const bof_remove_post_t *req, bof_post_result_t *out)
{
    bof_error_t err;

    if (!req->removed)
        return BOF_ERR_INVALID_BODY_FIELD;
    if ((err = drop_post(actor, &req->post,
                         req->reason ? req->reason : "")) != BOF_OK)
        return err;
    if ((err = bof_fs_remove_post(NULL, actor, req, out)) != BOF_OK)
        cache_failed("remove_post", req->post.community,
                     req->post.bbs_header->filename, err);
    return BOF_OK;
}

// The posts nest.svc did not take down, which the cache keeps: the
// selection of the caller, without them.
typedef struct {
    int (*selected)(const fileheader_t *fh, int index, void *ctx);
    void (*removed)(const fileheader_t *fh, void *ctx);
    void *ctx;
    char (*failed)[FNLEN];
    int nfailed;
} kept_posts_t;

static int
select_dropped(const fileheader_t *fh, int index, void *vctx)
{
    const kept_posts_t *k = vctx;

    for (int i = 0; i < k->nfailed; i++)
        if (strncmp(fh->filename, k->failed[i], FNLEN) == 0)
            return 0;
    return k->selected(fh, index, k->ctx);
}

static void
removed_dropped(const fileheader_t *fh, void *vctx)
{
    const kept_posts_t *k = vctx;

    k->removed(fh, k->ctx);
}

// Notes that nest.svc did not take fh down.
static void
keep_post(kept_posts_t *k, const fileheader_t *fh)
{
    char (*more)[FNLEN] = realloc(k->failed,
                                  (k->nfailed + 1) * sizeof(*k->failed));
    if (!more)
        return;
    k->failed = more;
    strlcpy(k->failed[k->nfailed++], fh->filename, FNLEN);
}

static bof_error_t
nest_remove_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_remove_posts_t *req, int *count)
{
    kept_posts_t k = {
        req->bbs_selected, req->bbs_removed, req->bbs_ctx, NULL, 0,
    };
    bof_remove_posts_t rec = *req;
    fileheader_t fh;
    bof_error_t err;
    int fd, index;

    *count = 0;
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
        if (drop_post(actor, &post, req->reason ? req->reason : "") !=
            BOF_OK) {
            cache_failed("remove_posts", req->community, fh.filename,
                         BOF_ERR_COULDNT_UPDATE_POST);
            keep_post(&k, &fh);
        }
    }
    close(fd);
    // Only those nest.svc took down leave the cache.
    rec.bbs_selected = select_dropped;
    rec.bbs_ctx = &k;
    rec.bbs_removed = req->bbs_removed ? removed_dropped : NULL;
    err = bof_fs_remove_posts(NULL, actor, &rec, count);
    free(k.failed);
    return err;
}

// Expired posts (PurgePost): nest.svc keeps them as removed, as before
// (nest.ctl purges them for good), rather than dropping them at once;
// the cache drops them as the files backend does. They are not archived,
// so what nest.svc writes over the article does not matter. Expiring is
// durable, as it always was: a post nest.svc did not take is logged, and
// dropped from the cache all the same (sync-dir brings it back).
static bof_error_t
nest_purge_posts(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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
        if (!svc_cmd(NULL, 0, "REMOVE_POST_FILE %s %.*s %s %" PRIu64 " %s\n",
                     req->community, (int)sizeof(fh.filename), fh.filename,
                     who(actor), uid(actor), why))
            cache_failed("purge_posts", req->community, fh.filename,
                         BOF_ERR_COULDNT_UPDATE_POST);
    }
    free(why);
    close(fd);
    return bof_fs_purge_posts(NULL, actor, req, count);
}

// The files backend puts the archived article and its record back first;
// then nest.svc takes the post back (RECOVER_POST_FILE), which writes the
// article again as it has it. If nest.svc does not, the cache is put back
// as it was, so that the two agree: nest.svc's result is the result.
static bof_error_t
nest_restore_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_bbs_restore_post_t *req,
                bof_post_response_t *out)
{
    const fileheader_t *ar = req->bbs_archived;
    char path[PATHLEN], stale[PATHLEN] = "";
    fileheader_t before, rec;
    bof_error_t err;
    int had, i;

    if (!req->community || !req->bbs_dir || !ar || !ar->filename[0] ||
        ar->filename[0] == '.')
        return BOF_ERR_INVALID_BODY_FIELD;
    setdirpath(path, req->bbs_dir, ar->filename);
    had = search_dir_by_filename(req->bbs_dir, ar->filename, &before);
    // Its record and its file: not deleted, or restored already.
    if (had > 0 && dashf(path))
        return BOF_ERR_COULDNT_UPDATE_POST;
    // A file without its record is the note nest.svc wrote over the
    // article when it took the post down: aside, for the archived one.
    if (dashf(path)) {
        snprintf(stale, sizeof(stale), "%s.stale%d", path, (int)getpid());
        if (Rename(path, stale) != 0)
            return BOF_ERR_COULDNT_UPDATE_POST;
    }
    if ((err = bof_fs_bbs_restore_post(NULL, actor, req, out)) != BOF_OK) {
        if (*stale)
            Rename(stale, path);
        return err;
    }
    if (svc_cmd(NULL, 0, "RECOVER_POST_FILE %s %s\n", req->community,
                ar->filename)) {
        if (*stale)
            unlink(stale);
        return BOF_OK;
    }
    // nest.svc keeps it down: so does the cache.
    if ((i = search_dir_by_filename(req->bbs_dir, ar->filename, &rec)) > 0) {
        if (had > 0 ? substitute_fileheader(req->bbs_dir, &rec, &before, i) :
            delete_fileheader(req->bbs_dir, &rec, i))
            cache_failed("restore_post", req->community, ar->filename,
                         BOF_ERR_COULDNT_UPDATE_POST);
    }
    unlink(path);
    if (*stale)
        Rename(stale, path);
    if (out && out->bbs_header)
        memset(out->bbs_header, 0, sizeof(*out->bbs_header));
    return BOF_ERR_COULDNT_UPDATE_POST;
}

static bof_error_t
nest_lock_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_lock_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;

    if (!valid(post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // nest.svc takes the M. or L. name of the post.
    if (!svc_cmd(NULL, 0, "LOCK_POST_FILE %s %s %d\n", post->community,
                 post->bbs_header->filename, req->locked ? 1 : 0))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return bof_fs_lock_post(NULL, actor, req, out);
}

static bof_error_t nest_get_comments(void *ctx, const bof_get_comments_t *req,
                                   bof_get_comments_response_t *out);

// Changes the modes (marks) of a post: in nest.svc first (SET_META_FILE,
// whose error is the result), then in the cache. *sent gets the record
// as sent.
static int
send_modes(const bof_post_ref_t *post, uint16_t enable, uint16_t disable,
           fileheader_t *sent)
{
    *sent = current_record(post);
    sent->filemode = (sent->filemode | enable) & ~disable;
    return set_meta(post->community, sent);
}

// After send_modes(): err is what the cache did; out gets the record.
static bof_error_t
modes_cached(const bof_post_ref_t *post, bof_error_t err,
             const fileheader_t *sent, const fileheader_t *updated,
             bof_post_response_t *out)
{
    if (err != BOF_OK) {
        // nest.svc has them (the result); sync-dir makes the record again.
        cache_failed("post_modes", post->community,
                     post->bbs_header->filename, err);
        updated = sent;
    }
    if (out && out->bbs_header)
        *out->bbs_header = *updated;
    return BOF_OK;
}

static bof_error_t
nest_feature_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_feature_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    fileheader_t sent, updated;
    bof_post_response_t res = { .bbs_header = &updated };

    if (!valid(post) || post->bbs_index <= 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    // As the files backend does (bof_fs_feature_post()).
    if (!send_modes(post, req->featured ? FILE_BOTTOM | FILE_MARKED : 0,
                    req->featured ? 0 : FILE_BOTTOM, &sent))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return modes_cached(post, bof_fs_feature_post(NULL, actor, req, &res),
                        &sent, &updated, out);
}

static bof_error_t
nest_set_post_modes(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_bbs_set_post_modes_t *req,
                  bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    fileheader_t sent, updated;
    bof_post_response_t res = { .bbs_header = &updated };

    if (!valid(post) || post->bbs_index <= 0 ||
        (!req->enable && !req->disable))
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!send_modes(post, req->enable, req->disable, &sent))
        return BOF_ERR_COULDNT_UPDATE_POST;
    return modes_cached(post,
                        bof_fs_bbs_set_post_modes(NULL, actor, req, &res),
                        &sent, &updated, out);
}

// Appends note to the body of a post in nest.svc: the body as it is now,
// with the note, as an edit (a revision) of the actor. The files backend
// appends such notes to the file of the post, which nest.svc writes
// again on each change; in the body, the note stays (above the comments).
static int
note_body(const bof_actor_t *actor, const bof_post_ref_t *post,
          const char *note)
{
    char tmp[PATHLEN];
    int32_t modified;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.note%d", post->bbs_dir, (int)getpid());
    if (!fetch_body(post, tmp, &modified)) {
        unlink(tmp);
        return 0;
    }
    file_appendf(tmp, "%s", note);
    ok = send_body(actor, post, tmp, modified);
    unlink(tmp);
    return ok;
}

static void
zero_score(fileheader_t *fh, const void *ctx GCC_UNUSED)
{
    fh->score = 0;
}

static bof_error_t
nest_reset_post_score(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_bbs_reset_post_score_t *req,
                    bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    fileheader_t updated = *post->bbs_header;
    char note[ANSILINELEN];
    time4_t now = time4(NULL);

    if (!valid(post) || post->bbs_index <= 0)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!svc_cmd(NULL, 0, "RESET_POST_SCORE_FILE %s %s\n", post->community,
                 post->bbs_header->filename))
        return BOF_ERR_COULDNT_UPDATE_POST;
    // Readers of the post see who did it, as with the files backend
    // (bof_fs_bbs_reset_post_score()), but in the body kept by nest.svc.
    snprintf(note, sizeof(note), STR_REF_MARK STR_SCORE_RESET_NOTE,
             actor && actor->name ? actor->name : "", Cdatelite(&now));
    if (!note_body(actor, post, note))
        cache_failed("reset_post_score note", post->community,
                     post->bbs_header->filename, BOF_ERR_COULDNT_UPDATE_POST);
    if (!render_post(post))
        cache_failed("reset_post_score", post->community,
                     post->bbs_header->filename, BOF_ERR_UNKNOWN);
    if (bof_files_update_record(post->bbs_dir, post->bbs_index,
                                post->bbs_header->filename, zero_score, NULL,
                                &updated) != 0) {
        cache_failed("reset_post_score", post->community,
                     post->bbs_header->filename, BOF_ERR_COULDNT_FIND_POST);
        updated.score = 0;
    }
    if (out && out->bbs_header)
        *out->bbs_header = updated;
    return BOF_OK;
}

// Sends the new text of the comment at floor (EDIT_COMMENT_FILE); 1 if OK.
static int
send_comment_text(const bof_actor_t *actor, const bof_post_ref_t *post,
                  int floor, const char *text)
{
    char hdr[512], *msg = comment_to_svc(text);
    int ok;

    if (!msg)
        return 0;
    snprintf(hdr, sizeof(hdr), "EDIT_COMMENT_FILE %s %s %d %s %" PRIu64
             " %zu\n", post->community, post->bbs_header->filename, floor,
             who(actor), uid(actor), strlen(msg));
    ok = svc_ok(svc_call(hdr, msg, strlen(msg), NULL, 0), NULL, 0);
    free(msg);
    return ok;
}

// Calls fn for each comment of post (of creator, if not NULL), a page at
// a time, until fn returns nonzero. Returns that, or -1 if the comments
// could not be read.
static int
each_comment(const bof_post_ref_t *post, const char *creator,
             int (*fn)(const bof_comment_t *c, void *ctx), void *ctx)
{
    enum { PAGE = 256 };
    bof_comment_t *cs = calloc(PAGE, sizeof(*cs));
    int r = 0;

    if (!cs)
        return -1;
    for (int page = 1; !r; page++) {
        bof_get_comments_t get = {
            .post = *post, .bbs_creator = creator, .page = page,
            .limit = PAGE,
        };
        bof_get_comments_response_t res = { .comments = cs };

        if (nest_get_comments(NULL, &get, &res) != BOF_OK) {
            r = -1;
            break;
        }
        for (int i = 0; i < res.count && !r; i++)
            r = fn(&cs[i], ctx);
        if (res.count < PAGE)
            break;
    }
    free(cs);
    return r;
}

// Masks the lines of comments that have the pattern, as the files backend
// masks every line of the file (bof_fs_bbs_mask_post()): each such line
// of a comment says STR_MASKED_LINE instead, as an edit by the moderator
// (nest.svc keeps what it said in the revisions of the comment). If dry,
// only counts them.
typedef struct {
    const bof_actor_t *actor;
    const bof_post_ref_t *post;
    const char *pattern;
    int dry;
    int matched;
} mask_ctx;

static int
mask_comment(const bof_comment_t *c, void *vctx)
{
    mask_ctx *m = vctx;
    char masked[sizeof(c->content)] = "", line[sizeof(c->content)];
    char text[sizeof(c->content)];
    const char *p = c->content;
    int n = 0;

    if (c->removed)
        return 0;
    while (*p) {
        size_t len = strcspn(p, "\n");

        snprintf(line, sizeof(line), "%.*s", (int)len, p);
        strip_control_sequence(text, line);
        if (p != c->content)
            strlcat(masked, "\n", sizeof(masked));
        if (mbs_strstr(text, m->pattern)) {
            strlcat(masked, STR_MASKED_LINE, sizeof(masked));
            n++;
        } else {
            strlcat(masked, line, sizeof(masked));
        }
        p += len;
        if (*p)
            p++;
    }
    m->matched += n;
    if (n && !m->dry &&
        !send_comment_text(m->actor, m->post, c->bbs_floor, masked))
        cache_failed("mask_post comment", m->post->community,
                     m->post->bbs_header->filename,
                     BOF_ERR_COULDNT_UPDATE_COMMENT);
    return 0;
}

static bof_error_t
nest_mask_post(void *ctx GCC_UNUSED, const bof_actor_t *actor,
             const bof_bbs_mask_post_t *req, bof_post_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    char src[PATHLEN], dst[PATHLEN], line[ANSILINELEN], text[ANSILINELEN];
    time4_t now = time4(NULL);
    mask_ctx m = { actor, post, req->pattern, 1, 0 };
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
    // The comments too, as the files backend masks the whole file.
    each_comment(post, NULL, mask_comment, &m);
    if (!matched && !m.matched) {
        unlink(dst);
        return BOF_ERR_NOT_FOUND;
    }
    // The body with the note of who did it (even if only comments had
    // it), at the version read: if it changed meanwhile, nothing is done.
    file_appendf(dst, STR_POST_MARK STR_MASK_NOTE, who(actor),
                 Cdatelite(&now), req->reason ? req->reason : "");
    ok = send_body(actor, post, dst, modified);
    unlink(dst);
    if (!ok)
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (m.matched) {
        m.dry = 0;
        m.matched = 0;
        each_comment(post, NULL, mask_comment, &m);
    }
    if (!render_post(post))
        cache_failed("mask_post", post->community,
                     post->bbs_header->filename, BOF_ERR_UNKNOWN);
    if (out && out->bbs_header)
        *out->bbs_header = *post->bbs_header;
    return BOF_OK;
}

static bof_error_t
nest_remove_crosspost(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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

static bof_error_t
nest_create_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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
    // nest.svc keeps votes apart (CREATE_POST_LIKE_FILE): the type is how
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
        return strstr(resp, "bbs_post_too_large") ?
            BOF_ERR_BBS_POST_TOO_LARGE : BOF_ERR_COULDNT_CREATE_COMMENT;
    sscanf(resp, "OK %d %lld %d %15s", &seq, &ctime, &score, status);
    merged = strcmp(status, "MERGED") == 0;

    // The record in the cache: when it changed, and how many comments
    // (nest.svc has written the article again itself).
    fileheader_t updated;
    char path[PATHLEN];
    if (update_record(post, ctime > 0 ? (int32_t)ctime : time4(NULL), 0,
                      merged ? 0 : 1, &updated) != 0)
        return BOF_ERR_COULDNT_UPDATE_POST;
    if (out) {
        if (out->bbs_header)
            *out->bbs_header = updated;
        out->bbs_floor = seq;
        out->bbs_merged = merged;
        // The size of the article nest.svc wrote, which tells live readers
        // (mbbsd's ipc_pub) where the new comment ends.
        post_path(path, post);
        out->bbs_file_size = dashs(path);
        if (out->bbs_file_size < 0)
            out->bbs_file_size = 0;
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

// What bof_edit_comment() checks for all backends (the input, and that
// the actor is the creator it names) is not checked again here; the
// comment at the floor must still be by that creator and say
// bbs_old_content, as the files backend checks in the line.
static bof_error_t
nest_edit_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                const bof_edit_comment_t *req, bof_comment_response_t *out)
{
    const bof_post_ref_t *post = &req->post;
    bof_comment_t c;
    bof_get_comments_t get = { .post = *post, .page = req->bbs_floor,
                               .limit = 1 };
    bof_get_comments_response_t res = { .comments = &c };

    if (!valid(post) || req->bbs_floor <= 0 || !req->content ||
        !req->bbs_creator || !req->bbs_old_content)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (nest_get_comments(NULL, &get, &res) != BOF_OK || res.count < 1 ||
        c.bbs_floor != req->bbs_floor)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    if (c.removed || strcasecmp(c.creator, req->bbs_creator) != 0 ||
        strcmp(c.content, req->bbs_old_content) != 0)
        return BOF_ERR_COULDNT_UPDATE_COMMENT;
    if (!send_comment_text(actor, post, req->bbs_floor, req->content))
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
nest_delete_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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

// Finds the comment of remove_comment without a floor: the first one by
// the author (not removed) that says it, whole or in one of its lines, as
// nest.svc keeps the lines of a comment merged into it (the files backend
// has a line each). Removing it removes all of its lines.
typedef struct {
    const char *content;
    int floor;
} find_ctx;

static int
find_comment(const bof_comment_t *c, void *vctx)
{
    find_ctx *f = vctx;
    size_t n = strlen(f->content);
    const char *p = c->content;

    if (c->removed)
        return 0;
    while (*p) {
        size_t len = strcspn(p, "\n");
        if (len == n && strncmp(p, f->content, n) == 0) {
            f->floor = c->bbs_floor;
            return 1;
        }
        p += len;
        if (*p)
            p++;
    }
    if (strcmp(c->content, f->content) == 0) {
        f->floor = c->bbs_floor;
        return 1;
    }
    return 0;
}

static bof_error_t
nest_remove_comment(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_remove_comment_t *req,
                  bof_comment_response_t *out)
{
    int floor = req->bbs_floor;

    if (!valid(&req->post))
        return BOF_ERR_INVALID_BODY_FIELD;
    // Without the floor: all comments by the author, page by page.
    if (floor <= 0 && req->bbs_author && *req->bbs_author &&
        req->bbs_content) {
        find_ctx f = { req->bbs_content, 0 };
        if (each_comment(&req->post, req->bbs_author, find_comment, &f) > 0)
            floor = f.floor;
    }
    if (floor <= 0)
        return BOF_ERR_COULDNT_FIND_COMMENT;
    return delete_comment(actor, &req->post, floor,
                          req->reason ? req->reason : "", out);
}

// --------------------------------------------------------------------------
// Reads

static bof_error_t
nest_get_post_body(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                 const char *dest_path, bof_post_version_t *ver)
{
    int32_t modified = 0;

    if (!valid(post) || !dest_path || !ver)
        return BOF_ERR_INVALID_BODY_FIELD;
    if (!fetch_body(post, dest_path, &modified))
        return BOF_ERR_COULDNT_FIND_POST;
    // The version is the time of the content in nest.svc.
    ver->size = 0;
    ver->hash = (uint64_t)(uint32_t)modified;
    return BOF_OK;
}

static int
nest_post_version_matches(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                        const bof_post_version_t *ver)
{
    int32_t modified = 0;

    // Only the version: nest.svc writes nothing for the target "-".
    if (!valid(post) || !ver || !fetch_body(post, "-", &modified))
        return 0;
    return (uint64_t)(uint32_t)modified == ver->hash;
}

// nest.svc writes the article again into the board on each change
// (RenderAndSave), so only a missing file is rendered.
static bof_error_t
nest_refresh_post(void *ctx GCC_UNUSED, const bof_post_ref_t *post)
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
nest_get_comments(void *ctx GCC_UNUSED, const bof_get_comments_t *req,
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
    // keep them: nest.svc keeps the address of every comment, which is
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
        const char *edited = strchr(f[2], '/');
        int type = atoi(f[4]);

        memset(c, 0, sizeof(*c));
        c->bbs_floor = atoi(f[0]);
        strlcpy(c->creator, f[1], sizeof(c->creator));
        // As the files backend gives a removed one (its note in the file):
        // who wrote it, and nothing of what it said.
        if (type < 0) {
            c->removed = 1;
            continue;
        }
        c->bbs_vote = type == 1 ? 1 : type == 2 ? -1 : 0;
        for (int i = 7; i < nf; i++) {
            // The lines of a comment (nest.svc keeps several).
            if (i > 7)
                strlcat(text, "\n", sizeof(text));
            strlcat(text, f[i], sizeof(text));
        }
        SERVICE_TO_MB(text, c->content, sizeof(c->content));
        // The time may be followed by the time of the last edit
        // ("<ctime>/<modified>"), if nest.svc tells it.
        c->bbs_edited = edited && atoll(edited + 1) > 0;
        // An edited one has the mark in place of the space before the date,
        // as in the files.
        if (show_ip && *f[3] && strcmp(f[3], "-") != 0)
            snprintf(c->bbs_tail, sizeof(c->bbs_tail), " %s%c%s", f[3],
                     c->bbs_edited ? BOF_COMMENT_EDITED_MARK : ' ',
                     Cdate_mdHM(&t));
        else
            snprintf(c->bbs_tail, sizeof(c->bbs_tail), "%c%s",
                     c->bbs_edited ? BOF_COMMENT_EDITED_MARK : ' ',
                     Cdate_mdHM(&t));
        // The room of its longest line.
        for (const char *p = c->content; *p;) {
            int len = (int)strcspn(p, "\n");
            if (len > c->bbs_width)
                c->bbs_width = len;
            p += len;
            if (*p)
                p++;
        }
    }
    free(r);
    return BOF_OK;
}

// GET_COMMENT_REVISIONS_FILE: "OK <n>", then a line per revision, oldest first:
// <rev> <edited_at> <editor> [<edit|delete> <reason or ->] <lines...>,
// separated by tabs.
static bof_error_t
nest_get_comment_revisions(void *ctx GCC_UNUSED,
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
        char *f[64], text[BOF_COMMENT_CONTENT_SIZE * 3] = "";
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
// IDs: nest.svc's (Lemmy's post.id, community.id)

static bof_error_t
nest_get_post_id(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
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
nest_get_post_key(void *ctx GCC_UNUSED, bof_post_id_t id, bof_post_key_t *key)
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
nest_get_community_id(void *ctx GCC_UNUSED, const char *name,
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
// The versions of posts: nest.svc keeps them (GET_POST_REVISIONS_FILE); those
// from before it, in the time capsule of the files. Both are listed, the
// older first, numbered on.

// The versions nest.svc keeps of post, oldest first: their revision
// numbers in nest.svc into revs and what is known of them into info
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

// How many versions the time capsule keeps of post (those from before
// nest.svc, which come first).
static int
capsule_revisions(const bof_post_ref_t *post)
{
    int n = 0;

    if (bof_fs_get_post_revisions(NULL, post, &n) != BOF_OK || n < 0)
        return 0;
    return n;
}

// The versions are those of the time capsule (from before nest.svc),
// then those of nest.svc, numbered on from them.
static bof_error_t
nest_get_post_revisions(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                      int *count)
{
    int n = svc_revisions(post, NULL, NULL, 0);

    *count = capsule_revisions(post) + (n > 0 ? n : 0);
    return BOF_OK;
}

static bof_error_t
nest_get_post_revision_file(void *ctx GCC_UNUSED, const bof_post_ref_t *post,
                          int rev, char *path, size_t sz)
{
    int old = capsule_revisions(post), revs[256], n;

    if (rev <= old)
        return bof_fs_get_post_revision_file(NULL, post, rev, path, sz);
    rev -= old;
    n = svc_revisions(post, revs, NULL, ARRAY_SIZE(revs));
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
nest_list_post_revisions(void *ctx GCC_UNUSED,
                       const bof_bbs_list_post_revisions_t *req,
                       bof_bbs_list_post_revisions_response_t *out)
{
    bof_bbs_post_revision_t *all = NULL;
    int old = capsule_revisions(&req->post);
    int n = svc_revisions(&req->post, NULL, NULL, 0);
    bof_error_t err;

    out->count = 0;
    if (req->start < old &&
        (err = bof_fs_list_post_revisions(NULL, req, out)) != BOF_OK)
        return err;
    if (n > 0 && out->count < req->limit) {
        if (!(all = calloc(n, sizeof(*all))))
            return BOF_ERR_UNKNOWN;
        n = svc_revisions(&req->post, NULL, all, n);
        for (int i = req->start > old ? req->start - old : 0;
             i < n && out->count < req->limit; i++) {
            all[i].number = old + i + 1;
            out->revisions[out->count++] = all[i];
        }
        free(all);
    }
    out->bbs_total = old + (n > 0 ? n : 0);
    return BOF_OK;
}

// --------------------------------------------------------------------------
// Communities. The board headers stay in .BRD, which nest.svc mirrors:
// told of each slot written (EDIT_COMMUNITY), and all of it when it starts.
// What a header cannot say goes to nest.svc first, then to the files of
// the board, its cache.

// Merging and rebuilding boards go through all of their posts.
#define NEST_SLOW_TIMEOUT_MS (10 * 60 * 1000)

// Sends a one line command about a community, waiting up to timeout_ms;
// 1 if OK (the first line of the response in resp). What nest.svc did not
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
        file_appendf(BOF_STORAGE_LOG, "%s nest %.*s: %.*s\n",
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

// Tells nest.svc to mirror slot bid of .BRD (EDIT_COMMUNITY <bid>). .BRD
// has it already: if nest.svc does not take it, it is logged; nest.svc
// takes all of .BRD again when it starts (or with nest.ctl import-boards).
static void
mirror_slot(int bid)
{
    community_cmd(NULL, 0, NEST_TIMEOUT_MS, "EDIT_COMMUNITY %d\n", bid);
}

static bof_error_t
nest_edit_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                  const bof_edit_community_t *req)
{
    bof_error_t err = bof_fs_edit_community(NULL, actor, req);

    if (err == BOF_OK)
        mirror_slot(req->bbs_id);
    return err;
}

static bof_error_t
nest_create_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
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
nest_remove_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_remove_community_t *req,
                    bof_remove_community_response_t *out)
{
    char resp[128], *reason = to_svc(req->reason);
    bof_error_t err;
    int ok;

    if (!reason)
        return BOF_ERR_UNKNOWN;
    // REMOVE_COMMUNITY <community> <mod_person_id> [<reason>]: OK <id>, or
    // OK 0 if nest.svc does not know it. The reason is the rest of the line.
    ok = community_cmd(resp, sizeof(resp), NEST_TIMEOUT_MS,
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
nest_rename_community(void *ctx GCC_UNUSED, const bof_actor_t *actor,
                    const bof_bbs_rename_community_t *req)
{
    char resp[128];
    bof_error_t err;

    // RENAME_COMMUNITY <old> <new>: OK <id>, or OK 0 if nest.svc does not
    // know it; ERR if the new name is taken.
    if (!community_cmd(resp, sizeof(resp), NEST_TIMEOUT_MS,
                       "RENAME_COMMUNITY %s %s\n", req->community,
                       req->new_name))
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    if ((err = bof_fs_rename_community(NULL, actor, req)) != BOF_OK &&
        strcmp(resp, "OK 0") != 0) {
        // The board and its community must not part: put it back.
        community_cmd(NULL, 0, NEST_TIMEOUT_MS, "RENAME_COMMUNITY %s %s\n",
                      req->new_name, req->community);
    }
    return err;
}

// Makes the list and articles of a board again from nest.svc.
static bof_error_t
rebuild(const char *community)
{
    char resp[128];

    if (community_cmd(resp, sizeof(resp), NEST_SLOW_TIMEOUT_MS,
                      "SYNC_DIR %s 0 rebuild\n", community))
        return BOF_OK;
    return strstr(resp, "unknown community") ?
        BOF_ERR_COULDNT_FIND_COMMUNITY : BOF_ERR_COULDNT_UPDATE_COMMUNITY;
}

static bof_error_t
nest_merge_communities(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                     const bof_bbs_merge_communities_t *req,
                     bof_bbs_merge_communities_response_t *out)
{
    char resp[128];
    int moved = -1, collisions = 0;

    // MERGE_COMMUNITY <from> <to>: OK <moved> <collisions>.
    if (!community_cmd(resp, sizeof(resp), NEST_SLOW_TIMEOUT_MS,
                       "MERGE_COMMUNITY %s %s\n", req->from, req->to))
        return BOF_ERR_COULDNT_UPDATE_COMMUNITY;
    sscanf(resp, "OK %d %d", &moved, &collisions);
    // nest.svc has them; the cache of both boards is made again (what fails
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
nest_rebuild_community(void *ctx GCC_UNUSED, const bof_actor_t *actor GCC_UNUSED,
                     const bof_bbs_rebuild_community_t *req,
                     bof_bbs_rebuild_community_response_t *out)
{
    if (out)
        out->in_background = 0;
    return rebuild(req->community);
}

const bof_storage_ops_t bof_storage_nest = {
    .name = "nest",
    .create_post = nest_create_post,
    .edit_post = nest_edit_post,
    .delete_post = nest_delete_post,
    .remove_post = nest_remove_post,
    .remove_posts = nest_remove_posts,
    .purge_posts = nest_purge_posts,
    .restore_post = nest_restore_post,
    .lock_post = nest_lock_post,
    .feature_post = nest_feature_post,
    .set_post_modes = nest_set_post_modes,
    .reset_post_score = nest_reset_post_score,
    .mask_post = nest_mask_post,
    .remove_crosspost = nest_remove_crosspost,
    .create_post_like = nest_create_post_like,
    .create_comment = nest_create_comment,
    .edit_comment = nest_edit_comment,
    .delete_comment = nest_delete_comment,
    .remove_comment = nest_remove_comment,
    // Kept in the files, which nest.svc does not keep.
    .create_report = bof_fs_create_report,
    .resolve_report = bof_fs_resolve_report,
    .write_modlog = bof_fs_write_modlog,
    .note_modlog_edit = bof_fs_note_modlog_edit,
    .list_reports = bof_fs_list_reports,
    .list_post_likes = nest_list_post_likes,
    // Read from the cache.
    .get_posts = bof_fs_get_posts,
    .get_post = bof_fs_get_post,
    .get_post_body = nest_get_post_body,
    .get_comments = nest_get_comments,
    .get_comment_revisions = nest_get_comment_revisions,
    .post_version_matches = nest_post_version_matches,
    .refresh_post = nest_refresh_post,
    // IDs from nest.svc; a key found where it is now in the cache.
    .get_post_id = nest_get_post_id,
    .get_post_key = nest_get_post_key,
    .find_post = bof_fs_find_post,
    .get_community_id = nest_get_community_id,
    .get_post_revisions = nest_get_post_revisions,
    .get_post_revision_file = nest_get_post_revision_file,
    .list_post_revisions = nest_list_post_revisions,
    // The recycle bin is the time capsule of the cache.
    .list_archived_posts = bof_fs_list_archived_posts,
    .edit_community = nest_edit_community,
    .create_community = nest_create_community,
    .remove_community = nest_remove_community,
    .rename_community = nest_rename_community,
    .merge_communities = nest_merge_communities,
    .rebuild_community = nest_rebuild_community,
};
