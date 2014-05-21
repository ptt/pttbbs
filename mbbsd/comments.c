#include "bbs.h"
#include "daemons.h"

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

    if (i >= c->allocated)
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

    while (i >= c->loaded) {
        if (CommentsLoad(c, c->loaded++))
            break;
    }
    if (i >= c->loaded)
        return NULL;

    return &c->resp[i];
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
#endif
