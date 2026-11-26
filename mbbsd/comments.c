#define _GNU_SOURCE // for strchrnul
#include <ctype.h>
#include "bbs.h"
#include "ansi.h"
#include "daemons.h"
#include "comments.h"

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

// Removes comment i from the file of the post (see bof_remove_comment()),
// with log (if not NULL) in the modlog, then marks it deleted in commentd.
// Returns 0, -2 if the file has no such comment (e.g. it was edited), or -1.
int CommentsDeleteFromTextFile(void *ctx, int i, const char *reason,
                               const bof_modlog_t *log)
{
    CommentsCtx *c = (CommentsCtx *)ctx;
    const CommentBodyReq *req = CommentsRead(ctx, i);
    fileheader_t fh = {0};

    if (!req || req->type < 0)
        return -1;

    STRLCPY(fh.filename, c->key.file);
    AUTO_BDIR(dir, c->key.board);
    bof_actor_t me = mbbsd_actor(c->key.board, false);
    bof_remove_comment_t rm = {
        .post = {
            .community = c->key.board, .bbs_dir = dir,
            .bbs_header = &fh,
        },
        .bbs_vote = req->type == RECTYPE_GOOD ? 1 :
                    req->type == RECTYPE_BAD ? -1 : 0,
        .bbs_author = req->userid,
        .bbs_content = req->msg,
        .reason = reason,
        .bbs_modlog = log,
    };
    switch (bof_remove_comment(&me, &rm, NULL)) {
    case BOF_OK:
        CommentsDelete(ctx, i);
        return 0;
    case BOF_ERR_COULDNT_FIND_COMMENT:
        return -2;
    default:
        return -1;
    }
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

/*
 * The lines nest.svc writes for a comment (FormatCommentBlock):
 *   RS[*][-|!]userid|[~]No|MM/DD/YYYY HH:MM|[ip]|text
 *   RS[*]<spaces>|text     (further lines)
 * RS is COMMENT_LINE_MARK, which editors drop, so no article line is taken
 * for a comment. "*": by the OP, "-": deleted, "!": removed, "~": edited.
 * The fields before the text never have '|': they are read forward, then
 * the text is printed as it comes. Further lines have as many spaces as the
 * userid is wide, so each line is drawn on its own, aligned with the first.
 */
#define COMMENT_OP_MARK      '*'
#define COMMENT_DELETED_MARK '-'
#define COMMENT_REMOVED_MARK '!'
#define COMMENT_DATE_LEN     16  /* "MM/DD/YYYY HH:MM" */

typedef struct {
    int is_op, is_tombstone, is_edited;
    const char *user, *floor, *date, *ip, *text;
    int user_len, floor_len, ip_len;
} comment_head_t;

/* Reads header line p (up to end) to its text into h; 0 if not a header. */
static int
parse_head(const char *p, const char *end, comment_head_t *h)
{
    memset(h, 0, sizeof(*h));
    if (p >= end || *p++ != COMMENT_LINE_MARK)
        return 0;
    if (p < end && *p == COMMENT_OP_MARK)
        h->is_op = 1, p++;
    if (p < end && (*p == COMMENT_DELETED_MARK || *p == COMMENT_REMOVED_MARK))
        h->is_tombstone = 1, p++;

    for (h->user = p; p < end && *p != '|' && *p != '\n'; p++)
        ;
    h->user_len = (int)(p - h->user);
    if (!h->user_len || *h->user == ' ' || p >= end || *p++ != '|')
        return 0;

    if (p < end && *p == '~')
        h->is_edited = 1, p++;
    for (h->floor = p; p < end && isdigit((unsigned char)*p); p++)
        ;
    h->floor_len = (int)(p - h->floor);
    if (!h->floor_len || p >= end || *p++ != '|')
        return 0;

    if (end - p <= COMMENT_DATE_LEN || p[COMMENT_DATE_LEN] != '|')
        return 0;
    h->date = p;
    p += COMMENT_DATE_LEN + 1;

    for (h->ip = p; p < end && *p != '|' && *p != '\n'; p++)
        ;
    if (p >= end || *p != '|')
        return 0;
    h->ip_len = (int)(p - h->ip);
    h->text = p + 1;
    return 1;
}

/* Reads further line "RS[*]<spaces>|text" (up to end): returns the length
 * of its prefix up to the '|', or 0 if line is not one. */
static int
parse_body_prefix(const unsigned char *line, const unsigned char *end,
                  int *width, int *is_op)
{
    const unsigned char *p = line;
    if (p >= end || *p++ != COMMENT_LINE_MARK)
        return 0;
    *is_op = (p < end && *p == COMMENT_OP_MARK);
    p += *is_op;
    const unsigned char *spaces = p;
    while (p < end && *p == ' ')
        p++;
    if (p == spaces || p >= end || *p != '|')
        return 0;
    *width = (int)(p - spaces);
    return (int)(p + 1 - line);
}

/* The indent of further lines under the header of a userid width wide:
 * "userid: ", padded to IDLEN in the aligned theme. */
static int
body_indent(const comment_ctx_t *ctx, int width)
{
    if (ctx->theme == COMMENT_THEME_ALIGNED && width < IDLEN)
        width = IDLEN;
    return width + 2;
}

/* Prints the text of a comment from p up to the end of its line (or end);
 * returns where it stopped. */
static const char *
comment_render_content(const comment_ctx_t *ctx, const char *p,
                       const char *end, int is_op, int is_tombstone)
{
    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    const char *t_color = is_tombstone ? ANSI_COLOR(1;30) :
                          is_op ? thm->color_comment_text_op :
                          thm->color_comment_text;
    const char *seg_start = p;

    if (t_color && *t_color)
        outs(t_color);
    while (p < end && *p != '\n') {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 0x80) {
            /* Big5 DBCS: skip 2 bytes if available */
            p += (p + 1 < end && p[1] != '\n') ? 2 : 1;
            continue;
        }
        int ref_len;
        if (!is_tombstone && ch == '>' &&
            (ref_len = comment_match_ref((const unsigned char *)p,
                                         (const unsigned char *)end, NULL))) {
            if (p > seg_start)
                prints("%.*s", (int)(p - seg_start), seg_start);
            outs(ANSI_COLOR(1));
            prints("%.*s", ref_len, p);
            outs(t_color && *t_color ? t_color : ANSI_RESET);
            p += ref_len;
            seg_start = p;
            continue;
        }
        p++;
    }
    if (p > seg_start)
        prints("%.*s", (int)(p - seg_start), seg_start);
    if (t_color && *t_color)
        outs(ANSI_RESET);
    return p;
}

static int
comment_render_modern_header(comment_ctx_t *ctx,
                             const unsigned char *line_u,
                             const unsigned char **next_line)
{
    const char *end = (const char *)ctx->buf_end;
    comment_head_t h;
    if (!parse_head((const char *)line_u, end, &h))
        return 0;

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    ctx->cur_is_op = h.is_op;

    /* Left: the userid, then the text as it comes. */
    if (ctx->theme == COMMENT_THEME_ALIGNED && h.user_len < IDLEN)
        prints("%*s", IDLEN - h.user_len, "");
    outs(h.is_op ? thm->color_commenter_op : thm->color_commenter);
    prints("%.*s", h.user_len, h.user);
    outs(ANSI_RESET ": ");
    const char *eol = comment_render_content(ctx, h.text, end, h.is_op,
                                             h.is_tombstone);

    /* Right: what fits of the address and time, then the floor. */
    int floor_width = h.floor_len + 2 + h.is_edited;
    int avail = ctx->max_width - vgetx();
    if (avail >= floor_width + 1) {
        static const struct { int ip, date_len; } forms[] = {
            { 1, 10 }, { 1, 5 }, { 0, 10 }, { 0, 5 }, { 0, 0 },
        };
        const char *info_color = thm->color_commenter_info ?
                                 thm->color_commenter_info : COLOR_COMMENTER_INFO;
        int room = avail - floor_width - 1, len = 0;
        char info[64];

        for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
            int ip = forms[i].ip, dl = forms[i].date_len;
            if (ip && (!ctx->show_comment_ip || !h.ip_len))
                continue;
            len = snprintf(info, sizeof(info), "%.*s%s%.*s%s%.5s",
                           ip ? h.ip_len : 0, h.ip, ip ? " " : "",
                           dl, h.date, dl ? " " : "", h.date + 11);
            if (len <= room)
                break;
            len = 0;
        }
        if (len > 0) {
            comment_print_fill(thm, avail - len - 1 - floor_width);
            outs(info_color);
            outs(info);
            outs(ANSI_RESET " ");
        } else if (avail > floor_width) {
            comment_print_fill(thm, avail - floor_width);
        } else {
            outs(" ");
        }
        outs(ANSI_RESET "[");
        if (h.is_edited)
            outs(ANSI_COLOR(1;33) "~" ANSI_RESET);
        prints("%.*s]", h.floor_len, h.floor);
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
    if (!line || line >= ctx->buf_end || *line != COMMENT_LINE_MARK)
        return 0;
    // Wherever it is, e.g. comments copied into the article by a crosspost.
    return comment_render_modern_header(ctx, line, next_line);
}

int
comment_render_body_prefix(comment_ctx_t *ctx,
                           const unsigned char *line_head,
                           unsigned char **dispe,
                           int *col,
                           int *need_reset_color)
{
    int plen, width, is_op;
    if (!ctx || !(plen = parse_body_prefix(line_head, ctx->buf_end, &width,
                                           &is_op)))
        return 0;

    const comment_theme_t *thm = comment_get_theme(ctx->theme);
    ctx->cur_is_op = is_op;

    const char *t_color = is_op ? thm->color_comment_text_op : thm->color_comment_text;
    if (t_color && *t_color) {
        outs(t_color);
        if (need_reset_color)
            *need_reset_color = 1;
    }

    if (*dispe == line_head) {
        int indent = body_indent(ctx, width);
        prints("%*s", indent, "");
        *dispe += plen;
        if (col)
            *col += indent;
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

    const unsigned char *head = (const unsigned char *)line;
    int plen, width, is_op;
    if (line && len > 0 && *head == COMMENT_LINE_MARK) {
        if ((plen = parse_body_prefix(head, head + len, &width, &is_op))) {
            ctx->cur_is_op = is_op;
            prints("%*s", body_indent(ctx, width), "");
            comment_render_content(ctx, line + plen, line + len, is_op, 0);
            return 1;
        }
        /* Header: as if the buffer were this line alone. */
        const unsigned char *buf_end = ctx->buf_end;
        ctx->buf_end = head + len;
        int r = comment_render_modern_header(ctx, head, NULL);
        ctx->buf_end = buf_end;
        if (r)
            return 1;
    }

    ctx->cur_is_op = 0;
    return 0;
}
