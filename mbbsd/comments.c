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
    static const char *ctype[] = {
        "推", "噓", "→",
    };
    // Note attr2 is slightly different from ctype_attr in bbs.c#recommend
    static const char *ctype_attr2[] = {
        ANSI_COLOR(1;37),
        ANSI_COLOR(1;31),
        ANSI_COLOR(1;31),
    };
    // Callers check type against RECTYPE_SIZE; these must cover it all.
    _Static_assert(ARRAY_SIZE(ctype) == RECTYPE_SIZE,
                   "ctype must have one entry per comment type");
    _Static_assert(ARRAY_SIZE(ctype_attr2) == RECTYPE_SIZE,
                   "ctype_attr2 must have one entry per comment type");
    snprintf(buf, szbuf,
             "%s%s " ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(33)
             ": %-*s" ANSI_RESET "%s\n",
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
    // commentd stores records as its clients sent them, so check them here.
    // A record of an unknown type or with an invalid user ID is kept, as a
    // deleted one, so that the records after it can still be read. An ID
    // that fills its field without a NUL is invalid: mbbsd never sends one.
    if (!memchr(resp.userid, '\0', sizeof(resp.userid)) ||
        !is_validuserid(resp.userid)) {
        resp.userid[0] = '\0';
        resp.type = COMMENT_DELETED;
    }
    resp.msg[sizeof(resp.msg) - 1] = '\0';
    if (((uint32_t)resp.type & ~(uint32_t)COMMENT_DELETED) >= RECTYPE_SIZE)
        resp.type = COMMENT_DELETED;
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

// Older comments may have no space after the colon.
static void
strip_comment_sep_space(char *s)
{
    char *p = strstr(s, ": ");
    if (p)
        memmove(p + 1, p + 2, strlen(p + 2) + 1);
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
    const int legacy = !IS_ENABLED(CONFIG_OLD_RECOMMEND);
    char pattern3[ANSILINELEN] = "", pattern4[ANSILINELEN] = "";
    size_t pattern3_len = 0, pattern4_len = 0;
    if (legacy) {
        strlcpy(pattern3, pattern, sizeof(pattern3));
        strlcpy(pattern4, pattern2, sizeof(pattern4));
        strip_comment_sep_space(pattern3);
        strip_comment_sep_space(pattern4);
        pattern3_len = strlen(pattern3);
        pattern4_len = strlen(pattern4);
    }
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
             (buf[pattern2_len] == ' ' || buf[pattern2_len] == ESC_CHR)) ||
            (legacy && strncmp(buf, pattern3, pattern3_len) == 0 &&
             (buf[pattern3_len] == ' ' || buf[pattern3_len] == ESC_CHR)) ||
            (legacy && strncmp(buf, pattern4, pattern4_len) == 0 &&
             (buf[pattern4_len] == ' ' || buf[pattern4_len] == ESC_CHR)))) {
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
    [COMMENT_THEME_ALIGNED] = {
        .name = "對齊顯示",
        .color_commenter = ANSI_COLOR(0;33),
        .color_commenter_op = ANSI_COLOR(1;36),
        .color_comment_text = NULL,
        .color_comment_text_op = NULL,
        .color_commenter_info = COLOR_COMMENTER_INFO,
        .color_border = NULL,
        .border_first = NULL,
        .border_next = NULL,
        .border_fill = NULL,
        .body_indent = "  ",
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
    return "經典醒目\t單純易讀\t對齊顯示";
}

static int s_comment_pref_theme = COMMENT_THEME_CLASSIC;

int
comment_pref_theme(void)
{
    return s_comment_pref_theme;
}

void
comment_set_pref_theme(int theme_idx)
{
    if (theme_idx < 0 || theme_idx >= COMMENT_THEMES)
        theme_idx = COMMENT_THEME_CLASSIC;
    s_comment_pref_theme = theme_idx;
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
    if (!ctx || !ctx->op_author || !ctx->op_author[0] || *line != '|')
        return 0;
    if (line[1] == ' ')
        return 0;

    const char *eol = (const char *)strchrnul((const char *)line, '\n');
    if (eol > (const char *)ctx->buf_end)
        eol = (const char *)ctx->buf_end;

    const char *p1 = (const char *)memchr((const char *)line + 1, '|', eol - ((const char *)line + 1));
    if (!p1)
        return 0;

    int user_len = (int)(p1 - ((const char *)line + 1));
    if (user_len > 0 && *(p1 - 1) == '!')
        user_len--;
    if (user_len <= 0)
        return 0;

    int op_len = (int)strlen(ctx->op_author);
    return (user_len == op_len && strncasecmp((const char *)line + 1, ctx->op_author, user_len) == 0);
}

static int
get_modern_header_width(const comment_ctx_t *ctx, const unsigned char *line)
{
    if (!ctx || !line || *line != '|')
        return 0;
    if (line[1] == ' ')
        return 0;

    const char *eol = (const char *)strchrnul((const char *)line, '\n');
    if (eol > (const char *)ctx->buf_end)
        eol = (const char *)ctx->buf_end;

    const char *p1 = (const char *)memchr((const char *)line + 1, '|', eol - ((const char *)line + 1));
    if (!p1)
        return 0;

    int user_len = (int)(p1 - ((const char *)line + 1));
    if (user_len > 0 && *(p1 - 1) == '!')
        user_len--;
    if (user_len <= 0)
        return 0;

    if (ctx->theme == COMMENT_THEME_ALIGNED) {
        int u_width = (user_len < IDLEN) ? IDLEN : user_len;
        return u_width + 2;
    }

    return user_len + 2;
}

void
comment_init_op_state(comment_ctx_t *ctx,
                      const unsigned char *line_head)
{
    if (!ctx)
        return;
    ctx->cur_is_op = 0;
    ctx->cur_header_width = 0;
    ctx->cur_is_tombstone = 0;

    if (!ctx->comments_start || !line_head || line_head <= ctx->comments_start)
        return;

    const unsigned char *p = line_head;

    /* Scan backwards to find the comment header line */
    while (p > ctx->comments_start) {
        p--;
        if (p > ctx->comments_start && *p == '\n' && *(p - 1) == '\r')
            p--;
        while (p > ctx->comments_start && *(p - 1) != '\n')
            p--;

        if (*p == '|') {
            if (p[1] != ' ') {
                ctx->cur_is_op = is_modern_header_op(ctx, p);
                ctx->cur_header_width = get_modern_header_width(ctx, p);
                const char *eol = (const char *)strchrnul((const char *)p, '\n');
                if (eol > (const char *)ctx->buf_end)
                    eol = (const char *)ctx->buf_end;
                const char *p1 = (const char *)memchr((const char *)p + 1, '|', eol - ((const char *)p + 1));
                ctx->cur_is_tombstone = (p1 && *(p1 - 1) == '!');
                return;
            }
        } else {
            return;
        }
    }
}

void
comment_update_line_state(comment_ctx_t *ctx, const unsigned char *line)
{
    if (!ctx || !ctx->comments_start || !line || line < ctx->comments_start)
        return;

    if (*line == '|') {
        if (line[1] != ' ') {
            ctx->cur_is_op = is_modern_header_op(ctx, line);
            ctx->cur_header_width = get_modern_header_width(ctx, line);
            const char *eol = (const char *)strchrnul((const char *)line, '\n');
            if (eol > (const char *)ctx->buf_end)
                eol = (const char *)ctx->buf_end;
            const char *p1 = (const char *)memchr((const char *)line + 1, '|', eol - ((const char *)line + 1));
            ctx->cur_is_tombstone = (p1 && *(p1 - 1) == '!');
        }
    } else {
        ctx->cur_is_op = 0;
        ctx->cur_header_width = 0;
        ctx->cur_is_tombstone = 0;
    }
}

static void
comment_render_content(const comment_ctx_t *ctx,
                       const char *c_start,
                       int c_len,
                       int is_op,
                       int is_tombstone)
{
    if (c_len <= 0 || !c_start)
        return;

    if (is_tombstone) {
        outs(ANSI_COLOR(1;30));
        prints("%.*s", c_len, c_start);
        outs(ANSI_RESET);
        return;
    }

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    const char *t_color = is_op ? thm->color_comment_text_op : thm->color_comment_text;

    if (t_color && *t_color)
        outs(t_color);

    const char *p = c_start;
    const char *end = c_start + c_len;
    const char *seg_start = p;

    while (p < end) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 0x80) {
            /* Big5 DBCS: skip 2 bytes if available */
            p += (p + 1 < end) ? 2 : 1;
            continue;
        }

        if (ch == '>' && p + 1 < end && *(p + 1) == '>') {
            int ref_len = comment_match_ref((const unsigned char *)p, (const unsigned char *)end, NULL);
            if (ref_len > 0) {
                if (p > seg_start)
                    prints("%.*s", (int)(p - seg_start), seg_start);

                outs(ANSI_COLOR(1));
                prints("%.*s", ref_len, p);
                if (t_color && *t_color)
                    outs(t_color);
                else
                    outs(ANSI_RESET);

                p += ref_len;
                seg_start = p;
                continue;
            }
        }
        p++;
    }

    if (p > seg_start)
        prints("%.*s", (int)(p - seg_start), seg_start);

    if (t_color && *t_color)
        outs(ANSI_RESET);
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

    if (*line != '|' || *(line + 1) == ' ')
        return 0;

    /* Parse |userid[!] | comment | ip time | [~]No */
    const char *p1 = (const char *)memchr(line + 1, '|', eol - (line + 1));
    if (!p1)
        return 0;

    const char *u_start = line + 1;
    int auth_len = (int)(p1 - u_start);
    int is_tombstone = 0;
    int user_len = auth_len;
    if (user_len > 0 && *(p1 - 1) == '!') {
        is_tombstone = 1;
        user_len--;
    }
    if (user_len <= 0)
        return 0;

    /* Find last '|' for No */
    const char *p3 = NULL;
    for (const char *p = eol - 1; p > p1; p--) {
        if (*p == '|') {
            p3 = p;
            break;
        }
    }
    if (!p3)
        return 0;

    const char *no_start = p3 + 1;
    const char *no_end = eol;
    while (no_end > no_start && (*(no_end - 1) == '\r' || *(no_end - 1) == '\n' || *(no_end - 1) == ' '))
        no_end--;
    if (no_end <= no_start)
        return 0;

    int is_modified = 0;
    if (*no_start == '~') {
        is_modified = 1;
        no_start++;
    }
    int no_len = (int)(no_end - no_start);
    if (no_len <= 0)
        return 0;

    /* Find second-to-last '|' for ip time */
    const char *p2 = NULL;
    for (const char *p = p3 - 1; p >= p1; p--) {
        if (*p == '|') {
            p2 = p;
            break;
        }
    }
    if (!p2 || p2 <= p1)
        return 0;

    const char *c_start = p1 + 1;
    int c_len = (int)(p2 - c_start);

    const char *payload = p2 + 1;
    const char *p_end = p3;
    while (p_end > payload && *(p_end - 1) == ' ')
        p_end--;
    int meta_len = (int)(p_end - payload);

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    int is_op = (ctx->op_author && ctx->op_author[0] != '\0' &&
                 (int)strlen(ctx->op_author) == user_len &&
                 strncasecmp(u_start, ctx->op_author, user_len) == 0);
    ctx->cur_is_op = is_op;
    ctx->cur_is_tombstone = is_tombstone;
    const char *c_color = is_op ? thm->color_commenter_op : thm->color_commenter;

    /* Render Header (Left) */
    int header_width = 0;
    if (ctx->theme == COMMENT_THEME_ALIGNED) {
        int pad_id = (user_len < IDLEN) ? (IDLEN - user_len) : 0;
        if (pad_id > 0)
            prints("%*s", pad_id, "");
        outs(c_color);
        prints("%.*s", user_len, u_start);
        outs(ANSI_RESET);
        outs(": ");
        header_width = (user_len < IDLEN ? IDLEN : user_len) + 2;
    } else {
        outs(c_color);
        prints("%.*s", user_len, u_start);
        outs(ANSI_RESET);
        outs(": ");
        header_width = user_len + 2;
    }
    ctx->cur_header_width = header_width;

    /* Render Content */
    comment_render_content(ctx, c_start, c_len, is_op, is_tombstone);

    /* Render Tail (Right) - Floor always on the right! */
    int floor_width = no_len + 2 + (is_modified ? 1 : 0);

    int cur_x = vgetx();
    int avail = ctx->max_width - cur_x;

    /* Parse payload tokens: [IP] [DATE] TIME */
    const char *time_str = NULL;
    int time_len = 0;
    const char *date_str = NULL;
    int date_len = 0;
    const char *short_date_str = NULL;
    int short_date_len = 0;
    const char *ip_str = NULL;
    int ip_len = 0;

    const char *last_space = NULL;
    for (const char *p = p_end - 1; p >= payload; p--) {
        if (*p == ' ') {
            last_space = p;
            break;
        }
    }

    if (last_space) {
        time_str = last_space + 1;
        time_len = p_end - time_str;

        const char *second_last_space = NULL;
        for (const char *p = last_space - 1; p >= payload; p--) {
            if (*p == ' ') {
                second_last_space = p;
                break;
            }
        }

        if (second_last_space) {
            ip_str = payload;
            ip_len = second_last_space - payload;
            date_str = second_last_space + 1;
            date_len = last_space - date_str;
        } else {
            const char *tok = payload;
            int tok_len = last_space - payload;
            if (memchr(tok, '/', tok_len)) {
                date_str = tok;
                date_len = tok_len;
            } else {
                ip_str = tok;
                ip_len = tok_len;
            }
        }
    } else if (meta_len > 0) {
        time_str = payload;
        time_len = meta_len;
    }

    if (date_str && date_len > 0) {
        const char *slash1 = (const char *)memchr(date_str, '/', date_len);
        if (slash1) {
            const char *slash2 = (const char *)memchr(slash1 + 1, '/', date_len - (slash1 + 1 - date_str));
            if (slash2) {
                short_date_str = date_str;
                short_date_len = slash2 - date_str;
            } else {
                short_date_str = date_str;
                short_date_len = date_len;
            }
        } else {
            short_date_str = date_str;
            short_date_len = date_len;
        }
    }

    char disp_buf[64] = "";
    int disp_len = 0;

    bool show_floor = (avail >= floor_width + 1);
    int candidate_avail = show_floor ? (avail - floor_width - 1) : 0;

    if (candidate_avail > 0) {
        /* 1. IP + Full Date + Time */
        if (ctx->show_comment_ip && ip_str && date_str && time_str) {
            int len = ip_len + 1 + date_len + 1 + time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s %.*s %.*s",
                         ip_len, ip_str, date_len, date_str, time_len, time_str);
                disp_len = len;
            }
        }

        /* 2. IP + Short Date + Time */
        if (!disp_len && ctx->show_comment_ip && ip_str && short_date_str && short_date_len < date_len && time_str) {
            int len = ip_len + 1 + short_date_len + 1 + time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s %.*s %.*s",
                         ip_len, ip_str, short_date_len, short_date_str, time_len, time_str);
                disp_len = len;
            }
        }

        /* 3. IP + Time (if no date) */
        if (!disp_len && ctx->show_comment_ip && ip_str && !date_str && time_str) {
            int len = ip_len + 1 + time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s %.*s",
                         ip_len, ip_str, time_len, time_str);
                disp_len = len;
            }
        }

        /* 4. Full Date + Time */
        if (!disp_len && date_str && time_str) {
            int len = date_len + 1 + time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s %.*s",
                         date_len, date_str, time_len, time_str);
                disp_len = len;
            }
        }

        /* 5. Short Date + Time */
        if (!disp_len && short_date_str && short_date_len < date_len && time_str) {
            int len = short_date_len + 1 + time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s %.*s",
                         short_date_len, short_date_str, time_len, time_str);
                disp_len = len;
            }
        }

        /* 6. Time only */
        if (!disp_len && time_str) {
            int len = time_len;
            if (len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s", time_len, time_str);
                disp_len = len;
            }
        }

        /* 7. Fallback: whatever fits from payload directly */
        if (!disp_len && meta_len > 0) {
            if (meta_len <= candidate_avail) {
                snprintf(disp_buf, sizeof(disp_buf), "%.*s", meta_len, payload);
                disp_len = meta_len;
            }
        }
    }

    if (show_floor) {
        const char *info_color = thm->color_commenter_info ? thm->color_commenter_info : COLOR_COMMENTER_INFO;
        if (disp_len > 0) {
            int total_needed = disp_len + 1 + floor_width;
            int pad = avail - total_needed;
            if (pad > 0)
                comment_print_fill(thm, pad);
            outs(info_color);
            outs(disp_buf);
            outs(ANSI_RESET " ");
        } else {
            int pad = avail - floor_width;
            if (pad > 0)
                comment_print_fill(thm, pad);
            else
                outs(" ");
        }
        outs(ANSI_RESET);
        if (is_modified) {
            outs("[" ANSI_COLOR(1;33) "~" ANSI_RESET);
            prints("%.*s]", no_len, no_start);
        } else {
            prints("[%.*s]", no_len, no_start);
        }
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

    if (ctx->comments_start && line >= ctx->comments_start && *line == '|' && *(line + 1) != ' ') {
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

    if (line_head[0] != '|' || line_head[1] != ' ')
        return 0;

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    int is_op = ctx->cur_is_op;
    int is_tombstone = ctx->cur_is_tombstone;

    int indent_width = ctx->cur_header_width > 0 ? ctx->cur_header_width : 2;

    if (is_tombstone) {
        outs(ANSI_COLOR(1;30));
        if (need_reset_color)
            *need_reset_color = 1;
    } else {
        const char *t_color = is_op ? thm->color_comment_text_op : thm->color_comment_text;
        if (t_color && *t_color) {
            outs(t_color);
            if (need_reset_color)
                *need_reset_color = 1;
        }
    }

    if (*dispe == line_head) {
        prints("%*s", indent_width, "");
        *dispe += 2;
        if (col)
            *col += indent_width;
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

int
comment_render_line(comment_ctx_t *ctx, const char *line, int len)
{
    if (!ctx)
        return 0;

    if (line && len >= 2 && line[0] == '|') {
        if (line[1] != ' ') {
            /* Header: render it as a one-line comments section. */
            const unsigned char *head = (const unsigned char *)line;
            const unsigned char *buf_start = ctx->buf_start,
                                *buf_end = ctx->buf_end,
                                *comments_start = ctx->comments_start;
            ctx->buf_start = ctx->comments_start = head;
            ctx->buf_end = head + len;
            int r = comment_render_modern_header(ctx, head, NULL);
            ctx->buf_start = buf_start;
            ctx->buf_end = buf_end;
            ctx->comments_start = comments_start;
            if (r)
                return 1;
        } else {
            /* Body: indented to align with the text of its header. */
            int indent = ctx->cur_header_width > 0 ? ctx->cur_header_width : 2;
            prints("%*s", indent, "");
            comment_render_content(ctx, line + 2, len - 2,
                                   ctx->cur_is_op, ctx->cur_is_tombstone);
            return 1;
        }
    }

    ctx->cur_is_op = 0;
    ctx->cur_header_width = 0;
    ctx->cur_is_tombstone = 0;
    return 0;
}

