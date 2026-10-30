#define _GNU_SOURCE // for strchrnul
#include <ctype.h>
#include "bbs.h"
#include "ansi.h"
#include "daemons.h"
#include "comments.h"

void FormatCommentString(char *buf, size_t szbuf, int type GCC_UNUSED,
                         const char *myid, int maxlength,
                         const char *msg, const char *tail)
{
    if (IS_ENABLED(CONFIG_OLD_RECOMMEND)) {
    snprintf(buf, szbuf,
             ANSI_COLOR(1;31) "→ " ANSI_COLOR(33) "%s" ANSI_RESET
             ANSI_COLOR(33) ":%-*s" ANSI_RESET "推%s\n",
             myid, maxlength, msg, tail);
    } else {
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
    }

}

#ifdef USE_COMMENTD
/* Locally defined context. */
typedef struct {
    uint32_t allocated;
    uint32_t loaded;
    int stopped;    // commentd is down or out of records: stop asking it
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
    STRLCPY(req.key.board, board);
    STRLCPY(req.key.file, file);

    req.comment.time = now;
    req.comment.ipv4 = inet_addr(fromhost);
    req.comment.userref = cuser.firstlogin;
    req.comment.type = type;
    STRLCPY(req.comment.userid, cuser.userid);
    STRLCPY(req.comment.msg, msg);

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
    STRLCPY(c->key.board, board);
    STRLCPY(c->key.file, file);
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

// A deleted comment's type has this bit set.
#define COMMENT_DELETED ((int32_t)0x80000000)

// Loads record i into c->resp[i]. Returns 0 when it is filled: with the
// record, or with an empty deleted one when commentd did not answer with a
// whole record. Returns -1 when commentd cannot be reached or has no more
// records, so that the caller stops.
static int CommentsLoad(CommentsCtx *c, int i)
{
    int s;
    uint16_t resp_size;
    CommentQueryRequest req = {0};
    CommentBodyReq resp;

    if (i >= (int)c->allocated)
        CommentsAlloc(c, i);
    assert(c->resp);

    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_QUERY_BODY;
    STRLCPY(req.key.board, c->key.board);
    STRLCPY(req.key.file, c->key.file);
    req.start = i;
    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0) {
        return -1;
    }
    // commentd sends a whole record, or a size of 0 when there are no more.
    if (towrite(s, &req, sizeof(req)) < 0 ||
        toread(s, &resp_size, sizeof(resp_size)) < 0)
        goto unusable;
    if (resp_size == 0) {
        close(s);
        return -1;
    }
    if (resp_size != sizeof(resp) || toread(s, &resp, sizeof(resp)) < 0)
        goto unusable;
    close(s);
    c->resp[i] = resp;
    return 0;

unusable:
    // Keep an empty deleted record, so that the records after it can still
    // be read. If commentd has gone away, the next connect fails and stops.
    close(s);
    memset(&c->resp[i], 0, sizeof(c->resp[i]));
    c->resp[i].type = COMMENT_DELETED;
    return 0;
}

const struct CommentBodyReq *CommentsRead(void *ctx, int i)
{
    CommentsCtx *c = (CommentsCtx *)ctx;

    // Count a record as loaded only once it is in the array. When commentd
    // cannot be reached or has no more records, don't ask it again on every
    // redraw: it may be down, or slow to answer.
    while (!c->stopped && i >= (int)c->loaded) {
        if (CommentsLoad(c, c->loaded)) {
            c->stopped = 1;
            break;
        }
        c->loaded++;
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
    c->resp[i].type |= COMMENT_DELETED;

    CommentQueryRequest req = {0};
    req.cb = sizeof(req);
    req.operation = COMMENTD_REQ_MARK_DELETED;
    STRLCPY(req.key.board, c->key.board);
    STRLCPY(req.key.file, c->key.file);
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
    STRLCPY(req.key.board, c->key.board);
    STRLCPY(req.key.file, c->key.file);
    req.start = 0;
    s = toconnectex(COMMENTD_ADDR, 10);
    if (s < 0)
        return 0;
    if (towrite(s, &req, sizeof(req)) < 0 ||
        toread(s, &num, sizeof(num)) < 0) {
        close(s);
        return 0;
    }
    close(s);
    return num;
}

int CommentsDeleteFromTextFile(void *ctx, int i, const char *reason)
{
    size_t pattern_len, pattern2_len;
    CommentsCtx *c = (CommentsCtx *)ctx;
    const CommentBodyReq *req;
    char buf[ANSILINELEN], pattern[ANSILINELEN], pattern2[ANSILINELEN];
    FILE *in, *out;
    int found = 0;

    req = CommentsRead(ctx, i);
    if (!req || req->type < 0)
        return -1;

    AUTO_BFILE(filename, c->key.board, c->key.file);
    AUTO_STR(tmpfile, "%s.tmp", filename);
    FormatCommentString(pattern, sizeof(pattern), req->type,
                        req->userid, 0, req->msg, "");
    sprintf(buf, "%-*s", IDLEN, req->userid);
    FormatCommentString(pattern2, sizeof(pattern2), req->type,
                        buf, 0, req->msg, "");
    // It's stupid but we have to remove the trailing ANSI_RESET
    // for comparison.
    *strrchr(pattern, ESC_CHR) = 0;
    *strrchr(pattern2, ESC_CHR) = 0;
    /* Remove the trailing \n for strncmp. */
    pattern_len = strlen(pattern);
    pattern2_len = strlen(pattern2);
    // Now, try to construct and filter the message from file.
    in = fopen(filename, "rt");
    out = fopen(tmpfile, "wt");
    while (fgets(buf, sizeof(buf), in)) {
        if (!found && (
            (strncmp(buf, pattern, pattern_len) == 0 &&
             (buf[pattern_len] == ' ' || buf[pattern_len] == ESC_CHR)) ||
            (strncmp(buf, pattern2, pattern2_len) == 0 &&
             (buf[pattern2_len] == ' ' || buf[pattern2_len] == ESC_CHR)))) {
            // Note reason is 40 chars in length.
            fprintf(out, ANSI_COLOR(1;30)
                    "(%s 刪除 %s 的推文: %s)" ANSI_RESET "\n",
                    cuser.userid, req->userid, reason);
            found = 1;
        } else {
            fputs(buf, out);
        }
    }
    fclose(out);
    fclose(in);
    if (found) {
        // For everytime we have to use time capsule system.
        int rev = timecapsule_add_revision(filename);

        remove(filename);
        rename(tmpfile, filename);
        if (rev > 0) {
            char revfn[PATHLEN];
            timecapsule_get_by_revision(filename, rev, revfn, sizeof(revfn));
            if (dashf(revfn))
            file_appendf(revfn, "\n" STR_POST_MARK "刪除推文: %s %s理由: %s\n"
                      "推文內容: %s: %s\n", Cdatelite(&now), cuser.userid,
                      reason, req->userid, req->msg);
        }
        CommentsDelete(ctx, i);
    } else {
        remove(tmpfile);
        return -2;
    }
    return 0;
}

const struct CommentKeyReq *CommentsGetKeyReq(void *ctx)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    return &c->key;
}
#endif


/* Comment rendering for pagers */

#define COLOR_COMMENTER       ANSI_COLOR(1;33)
#define COLOR_COMMENTER_OP    ANSI_COLOR(1;37)
#define COLOR_COMMENT_TEXT    ANSI_COLOR(0;33)
#define COLOR_COMMENT_TEXT_OP ANSI_COLOR(0;37)
#define COLOR_COMMENTER_INFO  ANSI_COLOR(1;30)

static const comment_theme_t comment_themes[] = {
    [COMMENT_THEME_CLASSIC] = {
        .name = "經典醒目",
        .color_commenter = COLOR_COMMENTER,
        .color_commenter_op = COLOR_COMMENTER_OP,
        .color_comment_text = COLOR_COMMENT_TEXT,
        .color_comment_text_op = COLOR_COMMENT_TEXT_OP,
        .color_commenter_info = COLOR_COMMENTER_INFO,
        .color_border = NULL,
        .border_first = NULL,
        .border_next = NULL,
        .border_fill = NULL,
        .body_indent = "│ ",
    },
    [COMMENT_THEME_SIMPLE] = {
        .name = "單純易讀",
        .color_commenter = ANSI_COLOR(0;33),
        .color_commenter_op = ANSI_COLOR(1;36),
        .color_comment_text = NULL,
        .color_comment_text_op = NULL,
        .color_commenter_info = COLOR_COMMENTER_INFO,
        .color_border = NULL,
        .border_first = NULL,
        .border_next = NULL,
        .border_fill = NULL,
        .body_indent = "│ ",
    },
    [COMMENT_THEME_BORDERED] = {
        .name = "典雅邊框",
        .color_commenter = ANSI_COLOR(0;33),
        .color_commenter_op = ANSI_COLOR(1;36),
        .color_comment_text = NULL,
        .color_comment_text_op = NULL,
        .color_commenter_info = COLOR_COMMENTER_INFO,
        .color_border = ANSI_COLOR(1;30),
        .border_first = "╭ ",
        .border_next = "├ ",
        .border_fill = "─",
        .body_indent = "│ ",
    },
};

const comment_theme_t *
comment_get_theme(int theme_idx)
{
    if (theme_idx < 0 || theme_idx >= COMMENT_THEMES)
        theme_idx = 0;
    return &comment_themes[theme_idx];
}

int
comment_theme_count(void)
{
    return COMMENT_THEMES;
}

const char *
comment_theme_name(int theme_idx)
{
    return comment_get_theme(theme_idx)->name;
}

const char *
comment_theme_menu_options(void)
{
    return "經典醒目\t單純易讀\t典雅邊框";
}

static void
comment_print_fill(const comment_theme_t *thm, int pad)
{
    if (pad <= 0)
        return;

    if (thm->border_fill && pad >= 4) {
        outs(" ");
        int dash_width = pad - 2;
        int num_dashes = dash_width / 2;
        int rem = dash_width % 2;
        if (thm->color_border && *thm->color_border)
            outs(thm->color_border);
        for (int i = 0; i < num_dashes; i++)
            outs(thm->border_fill);
        if (rem > 0)
            prints("%*s", rem, "");
        if (thm->color_border && *thm->color_border)
            outs(ANSI_RESET);
        outs(" ");
    } else {
        prints("%*s", pad, "");
    }
}

static int
is_modern_header_op(const comment_ctx_t *ctx, const unsigned char *line)
{
    if (!ctx || !ctx->op_author || !ctx->op_author[0] || *line != '[')
        return 0;

    const char *eol = (const char *)strchrnul((const char *)line, '\n');
    if (eol > (const char *)ctx->buf_end)
        eol = (const char *)ctx->buf_end;

    if (eol <= (const char *)(line + 1))
        return 0;

    /* 1. Find closing ']' followed by space */
    const char *p_close = (const char *)memchr((const char *)line + 1, ']', eol - ((const char *)line + 1));
    if (!p_close || *(p_close + 1) != ' ')
        return 0;

    const char *u_start = p_close + 2;

    /* 2. Find next space to separate id from CREATION */
    const char *u_end = (const char *)memchr(u_start, ' ', eol - u_start);
    if (!u_end)
        return 0;

    int user_len = u_end - u_start;
    int op_len = (int)strlen(ctx->op_author);
    return (user_len == op_len && strncasecmp(u_start, ctx->op_author, user_len) == 0);
}

void
comment_init_op_state(comment_ctx_t *ctx,
                      const unsigned char *line_head)
{
    if (!ctx)
        return;
    ctx->cur_is_op = 0;

    if (!ctx->op_author || !ctx->op_author[0] ||
        !ctx->comments_start || !line_head || line_head <= ctx->comments_start)
        return;

    const unsigned char *p = line_head;
    while (p > ctx->comments_start) {
        p--;
        if (p > ctx->comments_start && *p == '\n' && *(p - 1) == '\r')
            p--;
        while (p > ctx->comments_start && *(p - 1) != '\n')
            p--;

        if (*p == '[') {
            if (is_modern_header_op(ctx, p))
                ctx->cur_is_op = 1;
            return;
        }

        if (strncmp((const char *)p, "  ", 2) != 0)
            return;
    }
}

void
comment_update_line_state(comment_ctx_t *ctx, const unsigned char *line)
{
    if (!ctx || !ctx->comments_start || !line || line < ctx->comments_start)
        return;

    if (*line == '[') {
        ctx->cur_is_op = is_modern_header_op(ctx, line);
    } else if (strncmp((const char *)line, "  ", 2) != 0) {
        ctx->cur_is_op = 0;
    }
}

static int
comment_render_modern_header(comment_ctx_t *ctx,
                                   const unsigned char *line_u,
                                   const unsigned char **next_line)
{
    const char *line = (const char *)line_u;
    const char *buf_end = (const char *)ctx->buf_end;
    const char *eol = (const char *)strchrnul(line, '\n');
    if (eol > buf_end)
        eol = buf_end;

    if (*line != '[')
        return 0;

    /* 1. Find closing ']' followed by space */
    const char *p_close = (const char *)memchr(line + 1, ']', eol - (line + 1));
    if (!p_close || *(p_close + 1) != ' ')
        return 0;

    const char *u_start = p_close + 2;

    /* 2. Find next space to separate id from CREATION */
    const char *u_end = (const char *)memchr(u_start, ' ', eol - u_start);
    if (!u_end)
        return 0;

    int user_len = u_end - u_start;
    if (user_len <= 0)
        return 0;

    const char *c_start = u_end + 1;
    while (c_start < eol && *c_start == ' ')
        c_start++;

    const char *c_end = eol;
    while (c_end > c_start && (*(c_end - 1) == '\r' || *(c_end - 1) == ' '))
        c_end--;

    int c_len = c_end - c_start;

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    int is_op = (ctx->op_author && ctx->op_author[0] != '\0' &&
                 (int)strlen(ctx->op_author) == user_len &&
                 strncasecmp(u_start, ctx->op_author, user_len) == 0);
    ctx->cur_is_op = is_op;
    const char *c_color = is_op ? thm->color_commenter_op : thm->color_commenter;

    int is_first_comment = (line_u == ctx->comments_start ||
                           (line[0] == '[' && line[1] == '1' && line[2] == ']'));
    const char *pfx = thm->border_first ? (is_first_comment ? thm->border_first : thm->border_next) : NULL;
    int pfx_width = pfx ? stream_width(pfx) : 0;

    /* Check for deleted comment marker: "[N] author 留言..." */
    if (strncmp(c_start, "留言", strlen("留言")) == 0) {
        if (pfx) {
            if (thm->color_border && *thm->color_border)
                outs(thm->color_border);
            outs(pfx);
            if (thm->color_border && *thm->color_border)
                outs(ANSI_RESET);
        }
        prints("%.*s", (int)(u_start - line), line);        /* "[N] " */
        outs(c_color);
        prints("%.*s", user_len, u_start);                  /* ID */
        outs(ANSI_RESET);
        outs(ANSI_COLOR(1;30) " ");
        prints("%.*s", c_len, c_start);                     /* "留言已刪除" */
        outs(ANSI_RESET);

        if (next_line)
            *next_line = (const unsigned char *)eol;
        return 1;
    }

    if (!ctx->show_comment_ip) {
        const char *sp1 = (const char *)memchr(c_start, ' ', c_len);
        if (sp1 && memchr(sp1 + 1, ' ', (size_t)(c_end - (sp1 + 1)))) {
            c_start = sp1 + 1;
            while (c_start < c_end && *c_start == ' ')
                c_start++;
            c_len = c_end - c_start;
        }
    }

    /* 3. Width: reserve >= 1 space between ID and CREATION */
    int left_width = (u_end - line) + pfx_width;
    int avail = ctx->max_width - left_width - 1;
    int fitted_len = 0;

    if (avail > 0) {
        while (c_len > avail) {
            const char *sp = (const char *)memchr(c_start, ' ', c_len);
            if (!sp)
                break;
            c_start = sp + 1;
            while (c_start < c_end && *c_start == ' ')
                c_start++;
            c_len = c_end - c_start;
        }
        if (c_len <= avail && c_len > 0) {
            fitted_len = c_len;
        }
    }

    int pad = (fitted_len > 0) ? (ctx->max_width - left_width - fitted_len) : 0;

    /* 4. Sequential Zero-Copy rendering */
    if (pfx) {
        if (thm->color_border && *thm->color_border)
            outs(thm->color_border);
        outs(pfx);
        if (thm->color_border && *thm->color_border)
            outs(ANSI_RESET);
    }
    prints("%.*s", (int)(u_start - line), line);        /* "[N] " */
    outs(c_color);
    prints("%.*s", user_len, u_start);                  /* ID */
    outs(ANSI_RESET);
    if (fitted_len > 0) {
        comment_print_fill(thm, pad);             /* Fill / Padding */
        const char *info_color = thm->color_commenter_info ? thm->color_commenter_info : COLOR_COMMENTER_INFO;
        outs(info_color);
        const char *tilde = (const char *)memchr(c_start, '~', fitted_len);
        if (tilde) {
            int pre_len = (int)(tilde - c_start);
            if (pre_len > 0)
                prints("%.*s", pre_len, c_start);
            outs(ANSI_COLOR(1;33) "~" ANSI_RESET);
            outs(info_color);
            int post_len = fitted_len - pre_len - 1;
            if (post_len > 0)
                prints("%.*s", post_len, tilde + 1);
        } else {
            prints("%.*s", fitted_len, c_start);        /* CREATION */
        }
        outs(ANSI_RESET);
    }

    if (next_line)
        *next_line = (const unsigned char *)eol;
    return 1;
}

int
comment_render_header(comment_ctx_t *ctx,
                      const unsigned char *line,
                      const unsigned char **next_line)
{
    if (!line || line >= ctx->buf_end)
        return 0;

    /* 1. Modern comment header: [N] author creation */
    if (ctx->comments_start && line >= ctx->comments_start && *line == '[') {
        if (comment_render_modern_header(ctx, line, next_line))
            return 1;
    }

    return 0;
}

int
comment_render_body_prefix(const comment_ctx_t *ctx,
                                 const unsigned char *line_head,
                                 unsigned char **dispe,
                                 int *col,
                                 int *need_reset_color)
{
    if (!ctx || !ctx->comments_start || line_head < ctx->comments_start)
        return 0;

    if (strncmp((const char *)line_head, "  ", 2) != 0)
        return 0;

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    int is_op = ctx->cur_is_op;
    const char *t_color = is_op ? thm->color_comment_text_op : thm->color_comment_text;
    const char *indent = thm->body_indent ? thm->body_indent : "│ ";

    if (thm->color_border && *thm->color_border) {
        if (*dispe == line_head) {
            outs(thm->color_border);
            outs(indent);
            outs(ANSI_RESET);
            *dispe += 2;
            if (col)
                *col += stream_width(indent);
        }
        if (t_color && *t_color) {
            outs(t_color);
            if (need_reset_color)
                *need_reset_color = 1;
        }
    } else {
        if (t_color && *t_color) {
            outs(t_color);
            if (need_reset_color)
                *need_reset_color = 1;
        }
        if (*dispe == line_head) {
            outs(indent);
            *dispe += 2;
            if (col)
                *col += stream_width(indent);
        }
    }

    return 1;
}


int
comment_match_ref(const unsigned char *p,
                  const unsigned char *end,
                  int *ref_num)
{
    if (!p || p + 2 >= end)
        return 0;
    if (p[0] != '>' || p[1] != '>')
        return 0;

    const unsigned char *d = p + 2;
    /* First digit must be 1-9 (N must be > 0) */
    if (*d < '1' || *d > '9')
        return 0;

    int val = 0;
    while (d < end && isdigit(*d)) {
        if (val < 100000000)
            val = val * 10 + (*d - '0');
        d++;
    }

    /* Do not match if immediately followed by ASCII letters or underscore */
    if (d < end && ((*d >= 'a' && *d <= 'z') || (*d >= 'A' && *d <= 'Z') || *d == '_'))
        return 0;

    if (ref_num)
        *ref_num = val;
    return (int)(d - p);
}

const char *
comment_get_body_color(const comment_ctx_t *ctx)
{
    if (!ctx)
        return NULL;
    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    return ctx->cur_is_op ? thm->color_comment_text_op : thm->color_comment_text;
}
