#include "bbs.h"
#include "daemons.h"
#include <stdarg.h>

static inline int connect_post_svc(void)
{
    return toconnectex(POSTSVC_SOCK_PATH, 2);
}

static int postsvc_cmd_payload(const char *hdr,
                               const void *p1, size_t len1,
                               const void *p2, size_t len2,
                               char *out_resp, size_t resplen)
{
    int s = connect_post_svc();
    if (s < 0)
        return -1;

    size_t hlen = strlen(hdr);
    if (towrite(s, hdr, hlen) < 0 ||
        (p1 && len1 > 0 && towrite(s, p1, len1) < 0) ||
        (p2 && len2 > 0 && towrite(s, p2, len2) < 0)) {
        close(s);
        return -1;
    }

    char resp_buf[128];
    char *resp = out_resp ? out_resp : resp_buf;
    size_t rlen = out_resp ? resplen : sizeof(resp_buf);
    int n = toread_ex(s, resp, rlen - 1, '\n');
    close(s);

    if (n <= 0)
        return -1;
    resp[n] = '\0';

    return (strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

static int postsvc_cmd(char *out_resp, size_t resplen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static int postsvc_cmd(char *out_resp, size_t resplen, const char *fmt, ...)
{
    char cmd[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (len < 0 || len >= (int)sizeof(cmd))
        return -1;

    return postsvc_cmd_payload(cmd, NULL, 0, NULL, 0, out_resp, resplen);
}

int
PostAddRecord(const char *board, const fileheader_t *fhdr, const char *filepath)
{
    char dir[PATHLEN];
    setbdir(dir, board);

    if (filepath) {
        FILE *fp = fopen(filepath, "rb");
        if (!fp)
            return -1;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        char *buf = (char *)malloc(sz + 1);
        if (!buf) {
            fclose(fp);
            return -1;
        }
        if (sz > 0 && fread(buf, 1, sz, fp) != (size_t)sz) {
            free(buf);
            fclose(fp);
            return -1;
        }
        fclose(fp);
        buf[sz] = '\0';

        size_t title_len = strlen(fhdr->title);
        char hdr_buf[256];
        snprintf(hdr_buf, sizeof(hdr_buf),
                 "POST %s %s %s %u %d %ld 0 %zu %ld\n",
                 board, fhdr->filename, cuser.userid,
                 (unsigned int)cuser.firstlogin,
                 fhdr->filemode, (long)now, title_len, sz);

        int rc = postsvc_cmd_payload(hdr_buf, fhdr->title, title_len, buf, sz, NULL, 0);
        free(buf);
        if (rc != 0)
            return -1;

        char can_path[PATHLEN];
        setbfile(can_path, board, fhdr->filename);
        if (strcmp(filepath, can_path) != 0) {
            unlink(filepath);
        }
    }

    if (append_fileheader(dir, fhdr) == -1)
        return -1;

    return 0;
}

int
CommentAddRecord(const char *board, const char *direct, fileheader_t *fhdr,
                 int ent, int type, const char *msg)
{
    BEGINSTAT(STAT_DOCOMMENT);

    if (!msg) {
        ENDSTAT(STAT_DOCOMMENT);
        return -1;
    }
    size_t msg_len = strlen(msg);
    int legType = (type == RECTYPE_GOOD ? 1 : (type == RECTYPE_BAD ? 2 : 3));
    char hdr_buf[256];
    snprintf(hdr_buf, sizeof(hdr_buf),
             "COMMENT %s %s %s %u %s %ld %zu %d\n",
             board, fhdr->filename, cuser.userid,
             (unsigned int)cuser.firstlogin,
             fromhost, (long)now, msg_len, legType);

    char resp[64] = {0};
    if (postsvc_cmd_payload(hdr_buf, msg, msg_len, NULL, 0, resp, sizeof(resp)) != 0) {
        vmsg("錯誤: 傳送留言失敗。");
        goto error;
    }

    int seq = 0;
    long long resp_mod = 0;
    int resp_score = 0;
    if (sscanf(resp, "OK %d %lld %d", &seq, &resp_mod, &resp_score) >= 2 && resp_mod > 0) {
        fhdr->modified = (time4_t)resp_mod;
    } else {
        fhdr->modified = (time4_t)now;
    }

    if (fhdr->comments < UINT16_MAX)
        fhdr->comments++;

    if (modify_dir_lite(direct, ent, fhdr->filename,
                        fhdr->modified, NULL, NULL, NULL,
                        0, 0, 1, NULL, 0, 0) < 0)
        goto error;

    brc_addlist(fhdr->filename, fhdr->modified);

    ENDSTAT(STAT_DOCOMMENT);
    return 0;

error:
    ENDSTAT(STAT_DOCOMMENT);
    return -1;
}

int
CommentUpdateRecord(const char *board, const char *file, int seq, const char *msg, const char *editor)
{
    size_t msg_len = strlen(msg);
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "UPDATE_COMMENT_FILE %s %s %d %s %zu\n",
             board, file, seq, editor ? editor : cuser.userid, msg_len);
    return postsvc_cmd_payload(hdr, msg, msg_len, NULL, 0, NULL, 0);
}

int
RatePostRecord(const char *board, const char *direct, fileheader_t *fhdr, int ent, int vote)
{
    char resp[64] = {0};
    if (postsvc_cmd(resp, sizeof(resp), "VOTE_FILE %s %s %s %u %d\n",
                    board, fhdr->filename, cuser.userid,
                    (unsigned int)cuser.firstlogin, vote) != 0) {
        return -1;
    }

    int up_delta = 0, down_delta = 0;
    if (sscanf(resp, "OK %d %d", &up_delta, &down_delta) < 2) {
        if (vote == 1)
            up_delta = 1;
        else if (vote == -1)
            down_delta = 1;
    }

    if (up_delta == 0 && down_delta == 0 && vote != 0) {
        return 1;
    }

    int new_up = (int)fhdr->upvote + up_delta;
    if (new_up < 0) new_up = 0;
    else if (new_up > 255) new_up = 255;
    fhdr->upvote = (uint8_t)new_up;

    int new_down = (int)fhdr->downvote + down_delta;
    if (new_down < 0) new_down = 0;
    else if (new_down > 255) new_down = 255;
    fhdr->downvote = (uint8_t)new_down;

    if (direct && ent > 0) {
        if (modify_dir_lite(direct, ent, fhdr->filename, 0, NULL, NULL, NULL,
                            up_delta, down_delta, 0, NULL, 0, 0) < 0)
            return -1;
    }

    return 0;
}

int
CrosspostRecord(const char *src_board, const char *src_file,
                const char *target_board, const char *target_file)
{
    return (postsvc_cmd(NULL, 0, "CROSSPOST %s %s %s %s %s %u %ld\n",
                        src_board, src_file, target_board, target_file,
                        cuser.userid, (unsigned int)cuser.firstlogin, (long)now) == 0) ? 0 : 1;
}

int
PostFetchOriginal(const char *board, const char *file, const char *out_path,
                  const char *src_path, time4_t *out_modified)
{
    if (out_modified)
        *out_modified = 0;

    char resp[128] = {0};
    if (postsvc_cmd(resp, sizeof(resp), "FETCH_POST_FILE %s %s %s %s\n",
                    board, file, out_path, src_path ? src_path : "-") != 0)
        return -1;

    if (out_modified) {
        long long m = 0;
        if (sscanf(resp, "OK %lld", &m) == 1) {
            *out_modified = (time4_t)m;
        }
    }
    return 0;
}

int
PostRenderFile(const char *board, const char *file, const char *out_path)
{
    return postsvc_cmd(NULL, 0, "RENDER_FILE %s %s %s\n", board, file, out_path);
}

int
PostUpdateRecord(const char *board, const char *file, const char *title,
                 const char *filepath, time4_t expected_modified,
                 time4_t *out_latest_modified)
{
    if (out_latest_modified)
        *out_latest_modified = 0;

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
        return POST_UPDATE_ERROR;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        return POST_UPDATE_ERROR;
    }
    if (sz > 0 && fread(buf, 1, sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return POST_UPDATE_ERROR;
    }
    fclose(fp);
    buf[sz] = '\0';

    size_t title_len = strlen(title);
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "UPDATE_POST_FILE %s %s %s %lld %zu %ld\n",
             board, file, cuser.userid, (long long)expected_modified,
             title_len, sz);

    char resp[128] = {0};
    int rc = postsvc_cmd_payload(hdr, title, title_len, buf, sz, resp, sizeof(resp));
    free(buf);

    if (rc < 0 && resp[0] == '\0')
        return POST_UPDATE_ERROR;

    if (strncmp(resp, "ERR CONFLICT", 12) == 0 || strncmp(resp, "ERR MODIFIED", 12) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            if (sscanf(resp, "%*s %*s %lld", &lm) == 1) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_CONFLICT;
    }

    if (strncmp(resp, "OK 0", 4) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            if (sscanf(resp, "OK 0 NO_CHANGE %lld", &lm) == 1) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_NO_CHANGE;
    } else if (strncmp(resp, "OK ", 3) == 0) {
        if (out_latest_modified) {
            long long lm = 0;
            unsigned int rev = 0;
            if (sscanf(resp, "OK %u %lld", &rev, &lm) == 2) {
                *out_latest_modified = (time4_t)lm;
            }
        }
        return POST_UPDATE_SUCCESS;
    }
    return POST_UPDATE_ERROR;
}

int
PostDeleteRecord(const char *board, const char *file, const char *deleter, const char *reason)
{
    return postsvc_cmd(NULL, 0, "DELETE_POST_FILE %s %s %s %s\n",
                       board, file, deleter ? deleter : "", reason ? reason : "");
}

int
PostUndeleteRecord(const char *board, const char *file)
{
    return postsvc_cmd(NULL, 0, "UNDELETE_POST_FILE %s %s\n", board, file);
}

int
CommentDeleteRecord(const char *board, const char *file, int seq, const char *deleter, const char *reason)
{
    return postsvc_cmd(NULL, 0, "DELETE_COMMENT_FILE %s %s %d %s %s\n",
                       board, file, seq, deleter ? deleter : "", reason ? reason : "");
}

int
CommentUndeleteRecord(const char *board, const char *file, int seq)
{
    return postsvc_cmd(NULL, 0, "UNDELETE_COMMENT_FILE %s %s %d\n", board, file, seq);
}

int
PostUpdateTitleRecord(const char *board, const char *file, const char *title, const char *editor)
{
    size_t title_len = strlen(title);
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "UPDATE_POST_TITLE_FILE %s %s %s %zu\n",
             board, file, editor ? editor : cuser.userid, title_len);
    return postsvc_cmd_payload(hdr, title, title_len, NULL, 0, NULL, 0);
}

// ---------------------------------------------------------------------------
// Comment Reading & Management API
// ---------------------------------------------------------------------------

/* Locally defined context for CommentsOpen / CommentsRead. */
typedef struct {
    uint32_t allocated;
    uint32_t loaded;
    CommentKeyReq key;
    CommentBodyReq *resp;
} CommentsCtx;

static int parse_comment_tsv_line(char *line, CommentBodyReq *req)
{
    char *fields[16] = {0};
    int fidx = 0;
    char *p = line;
    fields[fidx++] = p;
    while (*p && fidx < 16) {
        if (*p == '\t') {
            *p = '\0';
            fields[fidx++] = p + 1;
        }
        p++;
    }
    if (fidx < 7)
        return -1;

    int seq = atoi(fields[0]);
    time4_t ctime = (time4_t)atoll(fields[2]);
    int type_val = atoi(fields[4]);
    int is_del = (type_val < 0);

    req->time = ctime;
    req->ipv4 = inet_addr(fields[3]);
    req->userref = (uint32_t)seq;
    STRLCPY(req->userid, fields[1]);
    req->type = type_val;

    if (is_del && fields[5][0]) {
        utf8_to_big5(fields[5], req->msg, sizeof(req->msg));
    } else {
        char utf8_msg[8192] = "";
        for (int fi = 6; fi < fidx; fi++) {
            if (fi > 6)
                strlcat(utf8_msg, "\n", sizeof(utf8_msg));
            strlcat(utf8_msg, fields[fi], sizeof(utf8_msg));
        }
        utf8_to_big5(utf8_msg, req->msg, sizeof(req->msg));
    }
    return 0;
}

void *CommentsOpenUser(const char *board, const char *file, const char *userid, const char *token)
{
    CommentsCtx *c = (CommentsCtx *)calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    STRLCPY(c->key.board, board);
    STRLCPY(c->key.file, file);

    int s = connect_post_svc();
    if (s < 0)
        return c;

    char cmd[PATHLEN + 128];
    int len;
    if (userid && userid[0]) {
        len = snprintf(cmd, sizeof(cmd), "COMMENTS_FILE %s %s 1 10000 %s %s\n",
                       board, file, userid, (token && token[0]) ? token : "-");
    } else {
        len = snprintf(cmd, sizeof(cmd), "COMMENTS_FILE %s %s 1 10000\n", board, file);
    }
    if (towrite(s, cmd, len) < 0) {
        close(s);
        return c;
    }

    char hdr[128] = {0};
    int n = toread_ex(s, hdr, sizeof(hdr) - 1, '\n');
    if (n <= 0) {
        close(s);
        return c;
    }
    hdr[n] = '\0';

    int count = 0, total = 0;
    if (sscanf(hdr, "OK %d %d", &count, &total) < 1 || count <= 0) {
        close(s);
        return c;
    }

    c->allocated = count;
    c->resp = (CommentBodyReq *)calloc(count, sizeof(CommentBodyReq));
    if (!c->resp) {
        close(s);
        return c;
    }

    char line[8192];
    int idx = 0;
    while (idx < count) {
        int llen = toread_ex(s, line, sizeof(line) - 1, '\n');
        if (llen <= 0)
            break;
        while (llen > 0 && (line[llen - 1] == '\n' || line[llen - 1] == '\r'))
            line[--llen] = '\0';
        if (llen == 0)
            continue;
        if (parse_comment_tsv_line(line, &c->resp[idx]) == 0)
            idx++;
    }
    c->loaded = idx;
    close(s);
    return c;
}

void *CommentsOpen(const char *board, const char *file)
{
    return CommentsOpenUser(board, file, NULL, NULL);
}

int CommentsClose(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    if (c) {
        if (c->allocated)
            free(c->resp);
        free(c);
    }
    return 0;
}

const struct CommentBodyReq *CommentsRead(void *ctx, int i)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    if (!c || i < 0 || (uint32_t)i >= c->loaded)
        return NULL;
    return &c->resp[i];
}

int CommentsGetCount(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    return c ? (int)c->loaded : 0;
}

const struct CommentKeyReq *CommentsGetKeyReq(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    return c ? &c->key : NULL;
}

int CommentsDeleteFromTextFile(void *ctx, int i, const char *reason)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    const CommentBodyReq *req = CommentsRead(ctx, i);
    if (!c || !req || req->type < 0)
        return -1;

    if (CommentDeleteRecord(c->key.board, c->key.file, i + 1, cuser.userid, reason) != 0)
        return -1;
    c->resp[i].type = -1;
    if (reason && reason[0])
        strlcpy(c->resp[i].msg, reason, sizeof(c->resp[i].msg));
    else
        strlcpy(c->resp[i].msg, "<當刪>", sizeof(c->resp[i].msg));
    return 0;
}

int CommentsUserHasComments(const char *board, const char *file, const char *userid)
{
    if (!board || !file || !userid)
        return 0;

    char hdr[128] = {0};
    if (postsvc_cmd(hdr, sizeof(hdr), "COMMENTS_FILE %s %s 1 1 %s -\n", board, file, userid) != 0)
        return 0;

    int count = 0;
    if (sscanf(hdr, "OK %d", &count) >= 1 && count > 0)
        return 1;
    return 0;
}
