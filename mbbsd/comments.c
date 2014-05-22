#include "bbs.h"
#include "daemons.h"

void FormatCommentString(char *buf, size_t szbuf, int type,
                         const char *myid, int maxlength,
                         const char *msg, const char *tail)
{
#ifdef OLDRECOMMEND
    snprintf(buf, szbuf,
             ANSI_COLOR(1;31) "→ " ANSI_COLOR(33) "%s" ANSI_RESET
             ANSI_COLOR(33) ":%-*s" ANSI_RESET "推%s\n",
             myid, maxlength, msg, tail);
#else
    // TODO(piaip) Make bbs.c#recomment use same structure.
    // Now we just assume they are the same.
    enum { RECTYPE_SIZE = 3 };
    static const char *ctype[RECTYPE_SIZE] = {
        "推", "噓", "→",
    };
    // Note attr2 is slightly different from ctype_attr in bbs.c#recommend
    static const char *ctype_attr2[RECTYPE_SIZE] = {
        ANSI_COLOR(1;37),
        ANSI_COLOR(1;31),
        ANSI_COLOR(1;31),
    };
    snprintf(buf, szbuf,
             "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33)
             ":%-*s" ANSI_RESET "%s\n",
             ctype_attr2[type], ctype[type], myid,
             maxlength, msg, tail);
#endif // OLDRECOMMEND

}

#ifdef USE_COMMENTD
/* Locally defined context. */
typedef struct {
    uint32_t allocated;
    uint32_t loaded;
    CommentKeyReq key;
    CommentBodyReq *resp;
} CommentsCtx;

int CommentsAddRecord(const char *board, const char *file,
                      int type, const char *msg)
{
    int s;
    CommentAddRequest req = {0};

    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_ADD;
    strlcpy(req.key.board, board, sizeof(req.key.board));
    strlcpy(req.key.file, file, sizeof(req.key.file));

    req.comment.time = now;
    req.comment.ipv4 = inet_addr(fromhost);
    req.comment.userref = cuser.firstlogin;
    req.comment.type = type;
    strlcpy(req.comment.userid, cuser.userid, sizeof(req.comment.userid));
    strlcpy(req.comment.msg, msg, sizeof(req.comment.msg));

    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0)
        return 1;
    if (towrite(s, &req, sizeof(req)) < 0) {
        close(s);
        return 1;
    }
    close(s);
    return 0;
}

void *CommentsOpen(const char *board, const char *file)
{
    CommentsCtx *c = (CommentsCtx *)malloc(sizeof(*c));
    memset(c, 0, sizeof(*c));
    strlcpy(c->key.board, board, sizeof(c->key.board));
    strlcpy(c->key.file, file, sizeof(c->key.file));
    return c;
}

static int CommentsAlloc(void *ctx, size_t count)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    if (c->allocated > count)
        return 0;
    count /= t_lines;
    count++;
    count *= t_lines;
    c->allocated = count;
    c->resp = (CommentBodyReq *)realloc(c->resp, sizeof(*c->resp) * count);
    return (c->resp == NULL);
}

int CommentsClose(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    if (c->allocated)
        free(c->resp);
    free(c);
    return 0;
}

static int CommentsLoad(CommentsCtx *c, int i)
{
    int s;
    uint16_t resp_size;
    CommentQueryRequest req = {0};
    CommentBodyReq *resp;

    if (i >= (int)c->allocated)
        CommentsAlloc(c, i);
    assert(c->resp);

    resp = &c->resp[i];
    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_QUERY_BODY;
    strlcpy(req.key.board, c->key.board, sizeof(req.key.board));
    strlcpy(req.key.file, c->key.file, sizeof(req.key.file));
    req.start = i;
    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0) {
        return -1;
    }
    if (towrite(s, &req, sizeof(req)) < 0 ||
        toread(s, &resp_size, sizeof(resp_size)) < 0 ||
        !resp_size || toread(s, resp, resp_size) < 0) {
        close(s);
        return -1;
    }
    close(s);
    return 0;
}

const struct CommentBodyReq *CommentsRead(void *ctx, int i)
{
    CommentsCtx *c = (CommentsCtx *)ctx;

    while (i >= (int)c->loaded) {
        if (CommentsLoad(c, c->loaded++))
            break;
    }
    if (i >= (int)c->loaded)
        return NULL;

    return &c->resp[i];
}

static int CommentsDelete(void *ctx, int i)
{
    int s, result = 0;
    CommentsCtx *c = (CommentsCtx *)ctx;
    if (!CommentsRead(ctx, i))
        return -1;
    // TODO(piaip) Notif commentd.
    c->resp[i].type |= 0x80000000;

    CommentQueryRequest req = {0};
    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_MARK_DELETED;
    strlcpy(req.key.board, c->key.board, sizeof(req.key.board));
    strlcpy(req.key.file, c->key.file, sizeof(req.key.file));
    req.start = i;
    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0) {
        return -1;
    }
    if (towrite(s, &req, sizeof(req)) < 0 ||
        toread(s, &result, sizeof(result)) < 0) {
        close(s);
        return -1;
    }
    close(s);
    return result;
}

int CommentsGetCount(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    int s, num = 0;
    CommentQueryRequest req = {0};
    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_QUERY_COUNT;
    strlcpy(req.key.board, c->key.board, sizeof(req.key.board));
    strlcpy(req.key.file, c->key.file, sizeof(req.key.file));
    req.start = 0;
    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0)
        return 0;
    if (towrite(s, &req, sizeof(req)) < 0 ||
        toread(s, &num, sizeof(num)) < 0) {
        close(s);
        return 0;
    }
    return num;
}

int CommentsDeleteFromTextFile(void *ctx, int i)
{
    size_t pattern_len;
    CommentsCtx *c = (CommentsCtx *)ctx;
    const CommentBodyReq *req;
    char buf[ANSILINELEN], pattern[ANSILINELEN];
    char filename[PATHLEN], tmpfile[PATHLEN];
    FILE *in, *out;
    int found = 0;

    req = CommentsRead(ctx, i);
    if (!req || req->type < 0)
        return -1;

    setbfile(filename, c->key.board, c->key.file);
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", filename);
    FormatCommentString(pattern, sizeof(pattern), req->type,
                        req->userid, 0, req->msg, "");
    // It's stupid but we have to remove the trailing ANSI_RESET
    // for comparison.
    *strrchr(pattern, ESC_CHR) = 0;
    /* Remove the trailing \n for strncmp. */
    pattern_len = strlen(pattern);
    // Now, try to construct and filter the message from file.
    in = fopen(filename, "rt");
    out = fopen(tmpfile, "wt");
    while (fgets(buf, sizeof(buf), in)) {
        if (strncmp(buf, pattern, pattern_len) == 0 &&
            (buf[pattern_len] == ' ' || buf[pattern_len] == ESC_CHR) &&
            !found) {
            fprintf(out, "[本行文字已被 %s 刪除。]\n", cuser.userid);
            found = 1;
        } else {
            fputs(buf, out);
        }
    }
    fclose(out);
    fclose(in);
    if (found) {
        // For everytime we have to use time capsule system.
#ifdef USE_TIME_CAPSULE
        int rev = timecapsule_add_revision(filename);
#endif
        remove(filename);
        rename(tmpfile, filename);
#ifdef USE_TIME_CAPSULE
        if (rev > 0) {
            char revfn[PATHLEN];
            timecapsule_get_by_revision(filename, rev, revfn, sizeof(revfn));
            log_filef(revfn, 0, "\n※ 刪除推文: %s %s <%s:%s>", cuser.userid,
                      Cdatelite(&now), req->userid, req->msg);
        }
#endif
        CommentsDelete(ctx, i);
    } else {
        remove(tmpfile);
    }
    return 0;
}
#endif
