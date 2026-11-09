#include "bbs.h"
#include "livepost.h"

////////////////////////////////////////////////////////////////////////////
// livepost.c: live mode (實況) of the post reader (read_post in bbs.c)
//
// Pressing 'v' in the post reader shows the last page and leaves the pager;
// read_post() then runs live_run(), which owns the screen: new comments of
// the article (IPC PubSub) are printed below the last page and scrolled up
// once the screen is full, with a dedicated status bar. 'q'/'Q' or LEFT
// leaves live mode.
////////////////////////////////////////////////////////////////////////////

#define LIVE_MAX_PENDING_COMMENTS 128
#define LIVE_CATCHUP_MAX        (16 * 1024) // max bytes of missed comments to replay
#define LIVE_STATUS_REFRESH_MS  1000
#define LIVE_TIMER_MAX_SEC      (8 * 60 * 60)   // timer range: +/- 8 hours

// After a user opened the same article this many times in a row within
// LIVE_AUTO_PROMPT_SECS (likely re-entering to refresh new comments), offer
// live mode on the next open.
#ifndef LIVE_AUTO_PROMPT_REPEAT
#define LIVE_AUTO_PROMPT_REPEAT 10
#endif
#ifndef LIVE_AUTO_PROMPT_SECS
#define LIVE_AUTO_PROMPT_SECS   18
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

typedef struct {
    uint64_t fsize;     // article size after this comment (0: unknown)
    char     text[IPC_TEXT_LEN];
} live_comment_t;

typedef struct {
    uint32_t bid;
    aidu_t   aid;
    bool     active;    // subscribed; comments are being queued
    int      head;
    int      tail;
    int      count;
    live_comment_t *comments;
} live_provider_t;

static live_provider_t s_live;
static bool s_live_bell = false;    // (B) bell notification, kept per session

// Consecutive opens of one article, kept per session.
static struct {
    uint32_t bid;
    aidu_t   aid;
    int      count;
    time4_t  since;     // the first open counted
} s_live_repeat;

/* pmore hands the last page over to live mode; the mini pager does not. */
static bool
live_supported(void)
{
#ifdef USE_PMORE
    return true;
#else
    return false;
#endif
}

/* The user left the article to do something (comment, reply, ...), not just
 * re-entering to refresh: start counting over. */
void
live_reset_repeat(void)
{
    s_live_repeat.count = 0;
}

static void
live_provider_free(void)
{
    if (s_live.comments) {
        size_t sz = sizeof(live_comment_t) * LIVE_MAX_PENDING_COMMENTS;
        munmap(s_live.comments, sz);
        s_live.comments = NULL;
    }
    s_live.head = s_live.tail = s_live.count = 0;
}

static int
live_provider_alloc(void)
{
    if (s_live.comments)
        return 0;
    size_t sz = sizeof(live_comment_t) * LIVE_MAX_PENDING_COMMENTS;
    s_live.comments = (live_comment_t *)mmap(
        NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s_live.comments == MAP_FAILED) {
        s_live.comments = NULL;
        return -1;
    }
    s_live.head = s_live.tail = s_live.count = 0;
    return 0;
}

static const live_comment_t *
live_provider_pop(void)
{
    if (!s_live.comments || s_live.count == 0)
        return NULL;

    const live_comment_t *c = &s_live.comments[s_live.head];
    s_live.head = (s_live.head + 1) % LIVE_MAX_PENDING_COMMENTS;
    s_live.count--;
    return c;
}

/*
 * IPC handler for IPC_TYPE_LIVE_COMMENT: queue the comment and post
 * VKEY_EV_PUBSUB, which only live_run() asks for. Packets for other posts or
 * after we left live mode (already in flight when unsubscribing) are dropped.
 */
void
live_on_comment(uint32_t bid, aidu_t aid, const char *text, uint64_t fsize)
{
    if (!s_live.active || !s_live.comments ||
        s_live.bid != bid || aidu_raw(s_live.aid) != aidu_raw(aid) ||
        !text || !*text)
        return;

    if (s_live.count >= LIVE_MAX_PENDING_COMMENTS) {
        /* Drop oldest comment when queue is full */
        s_live.head = (s_live.head + 1) % LIVE_MAX_PENDING_COMMENTS;
        s_live.count--;
    }
    s_live.comments[s_live.tail].fsize = fsize;
    STRLCPY(s_live.comments[s_live.tail].text, text);
    s_live.tail = (s_live.tail + 1) % LIVE_MAX_PENDING_COMMENTS;
    s_live.count++;
    vkey_post_event(VKEY_EV_PUBSUB);
}

/* Called after the IPC link is re-established: the daemon has no memory of
 * our subscriptions, so re-subscribe or live updates silently stop. */
void
live_resubscribe(void)
{
    if (s_live.active)
        ipc_sub(s_live.bid, aidu_raw(s_live.aid));
}

typedef struct {
    int     row;        // next screen row to print on; >= b_lines: scroll
    time4_t start;
    int     received;
    off_t   synced;     // file bytes already on screen (0: unknown)
    bool    countdown;  // timer is negative: counting down to the event
    comment_ctx_t cmt;  // state of the new format comment being printed
} live_view_t;

/* Move to the row for the next line, scrolling once the screen is full. */
static void
live_next_row(live_view_t *v)
{
    if (v->row < b_lines) {
        move(v->row++, 0);
    } else {
        /* Scroll the whole screen; the status bar is redrawn afterwards. */
        move(b_lines, 0);
        clrtoeol();
        scroll();
        move(b_lines - 1, 0);
    }
}

static void
live_print_line(live_view_t *v, const char *line)
{
    live_next_row(v);
    if (!comment_render_line(&v->cmt, line, (int)strlen(line)))
        outs(line);
    outs(ANSI_RESET);
    clrtoeol();
}

/* Full-width separator marking where live mode starts; "LIVE" style colors,
 * unlike any comment, pmore footer or the live status bar. */
#define LIVE_BANNER_COLOR   ANSI_COLOR(1;33;41)
#define LIVE_BANNER_TEXT    "實 況 開 始"

static void
live_print_banner(live_view_t *v)
{
    int width = t_columns - 1;
    int len = (int)strlen(LIVE_BANNER_TEXT);   // Big5: bytes == columns
    int left = (width - len) / 2;
    if (left < 0)
        left = 0;
    int right = width - len - left;

    live_next_row(v);
    outs(LIVE_BANNER_COLOR);
    nblank(left);
    outs(LIVE_BANNER_TEXT);
    if (right > 0)
        nblank(right);
    outs(ANSI_RESET);
    clrtoeol();
}

static void
live_print_text(live_view_t *v, const char *text)
{
    char buf[IPC_TEXT_LEN];
    STRLCPY(buf, text);

    char *line = buf;
    while (line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r')
            line[--len] = '\0';
        if (*line || next)
            live_print_line(v, line);
        line = next;
    }
}

/*
 * pmore showed the file as it was when the article was opened, which may be
 * long ago: replay the complete lines appended since then (new comments).
 * Called after subscribing, so nothing falls in between; comments delivered
 * by IPC are then skipped by their fsize token if v->synced already covers
 * them. (Until comments have unique ids, an article edit shifting the file
 * may make this skip or repeat a few.)
 */
static void
live_catchup(live_view_t *v, const char *fpath, off_t shown)
{
    int fd = open(fpath, O_RDONLY);
    if (fd < 0)
        return;

    struct stat st;
    if (fstat(fd, &st) != 0 || shown <= 0 || st.st_size < shown) {
        /* Unknown, or the file was replaced (edited): don't filter. */
        v->synced = 0;
        close(fd);
        return;
    }
    if (st.st_size == shown) {
        close(fd);
        return;
    }

    off_t from = shown;
    bool partial_head = false;
    if (st.st_size - from > LIVE_CATCHUP_MAX) {
        from = st.st_size - LIVE_CATCHUP_MAX;
        partial_head = true;
    }

    size_t len = (size_t)(st.st_size - from);
    char *buf = (char *)malloc(len + 1);
    ssize_t n = buf ? pread(fd, buf, len, from) : -1;
    close(fd);
    if (n <= 0) {
        free(buf);
        return;
    }
    buf[n] = '\0';

    char *line = buf;
    if (partial_head) {
        char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : buf + n;
    }
    while (*line) {
        char *nl = strchr(line, '\n');
        if (!nl)
            break;  // incomplete line being written: its PUB will follow
        *nl = '\0';
        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\r')
            line[l - 1] = '\0';

        live_print_line(v, line);
        line = nl + 1;
    }
    v->synced = from + (line - buf);
    free(buf);
}

static void
live_flush(live_view_t *v)
{
    const live_comment_t *c;
    int shown = 0;

    while ((c = live_provider_pop()) != NULL) {
        if (c->fsize && v->synced && (off_t)c->fsize <= v->synced)
            continue;   // already shown (pmore page or catch-up)
        live_print_text(v, c->text);
        shown++;
    }
    if (shown) {
        v->received += shown;
        if (s_live_bell)
            bell();
    }
}

static void
live_draw_status(const live_view_t *v, bool can_comment)
{
    /* Negative: counting down to the start of the event (see inc_timer). */
    int elapsed = (int)time4_diff(now, v->start);
    const char *sign = "";
    if (elapsed < 0) {
        sign = "-";
        elapsed = -elapsed;
    }
    AUTO_STR(part1, ANSI_COLOR(1;37;42) " 實況中 %s%02d:%02d [+%d] ",
            sign, elapsed / 60, elapsed % 60, v->received);
    AUTO_STR(part2, " %s(B)鈴聲%s (T/M/S)調時 (←/q)離開\t%s",
            can_comment ? "(X)留言 " : "",
            s_live_bell ? "[打開]" : "[關閉]", Cdate_mdHM(&now));
    vs_footer(part1, part2);
}

/*
 * T/M/S: align the timer with the event (e.g. a game) by moving its start;
 * the shown time changes by -delta. It may go negative, counting down to
 * the start like a live stream does, within +/- LIVE_TIMER_MAX_SEC.
 */
static void
inc_timer(live_view_t *v, int delta)
{
    int64_t elapsed = time4_diff(now, v->start) - delta;
    if (elapsed < -LIVE_TIMER_MAX_SEC || elapsed > LIVE_TIMER_MAX_SEC)
        return;
    v->start = time4_add(v->start, delta);
    /* Adjusting by hand is not the countdown reaching zero. */
    v->countdown = time4_lt(now, v->start);
}

/* Rings (if the bell is on) when the countdown reaches zero. */
static void
live_check_countdown(live_view_t *v)
{
    bool countdown = time4_lt(now, v->start);
    if (v->countdown && !countdown && s_live_bell)
        bell();
    v->countdown = countdown;
}

/*
 * 'X': comment through the caller (comment() needs the article's .DIR
 * context). Prompts may draw above the status bar, so save and restore the
 * screen. Nothing scrolls while typing: the prompt's vkey() calls don't take
 * live events, so comments queue up and are flushed once we are back; our
 * own comment comes back the same way, confirming it was posted.
 */
static int
live_comment(int (*comment)(void *ctx), void *ctx)
{
    screen_backup_t old_screen;

    scr_dump(&old_screen);
    int r = comment(ctx);
    scr_restore(&old_screen);
    return r;
}

/*
 * Runs live mode on the post (file fpath) after the pager left for it
 * (live->entered); comment (may be NULL) is for 'X'. Returns a read result.
 */
int
live_run(const live_ctx_t *live, const char *fpath,
         int (*comment)(void *ctx), void *ctx)
{
    bool can_comment = comment != NULL;
    int result = FULLUPDATE;
    int rows = live->row;
    off_t shown = live->shown;

    s_live.bid = live->bid;
    s_live.aid = live->aid;

    if (!ipc_is_connected())
        ipc_check_reconnect();
    if (live_provider_alloc() < 0 ||
        ipc_sub(s_live.bid, aidu_raw(s_live.aid)) != 0) {
        /* The ACK may merely be late (busy daemon); cancel so we don't leave
         * a phantom subscription receiving every comment. */
        ipc_unsub(s_live.bid, aidu_raw(s_live.aid));
        live_provider_free();
        vmsg("實況服務暫時無法使用，請稍後再試。");
        return FULLUPDATE;
    }
    s_live.active = true;

    live_view_t v;
    memset(&v, 0, sizeof(v));
    v.row = rows;
    v.synced = shown;
    v.cmt = live->cmt;
    syncnow();
    v.start = now;
    live_print_banner(&v);
    live_catchup(&v, fpath, shown);
    bool cont = true;

    while (cont) {
        syncnow();
        live_check_countdown(&v);
        live_draw_status(&v, can_comment);

        if (!vkey_poll_ex(LIVE_STATUS_REFRESH_MS, VKEY_EV_PUBSUB))
            continue;   // tick: refresh elapsed time

        int ch = vkey_ex(VKEY_EV_PUBSUB);
        switch (ch) {
        case I_EV_PUBSUB:
            live_flush(&v);
            break;

        case EOF:
        case 'q':
        case 'Q':
        case KEY_LEFT:
            cont = false;
            break;

        case 'b':
        case 'B':
            s_live_bell = !s_live_bell;
            break;

        case '%':
        case 'X':
        case 'x':
            if (!can_comment)
                break;
            if (live_comment(comment, ctx) == DIRCHANGED) {
                /* e.g. the article is gone: leave and let the caller reload */
                result = DIRCHANGED;
                cont = false;
            }
            break;

        case 'T':
            inc_timer(&v, 60*10);
            break;
        case 't':
            inc_timer(&v, -60*10);
            break;

        case 'M':
            inc_timer(&v, 60);
            break;
        case 'm':
            inc_timer(&v, -60);
            break;

        case 'S':
            inc_timer(&v, 1);
            break;
        case 's':
            inc_timer(&v, -1);
            break;

        default:
            break;
        }
    }

    ipc_unsub(s_live.bid, aidu_raw(s_live.aid));
    s_live.active = false;
    live_provider_free();
    return result;
}

/*
 * Counts consecutive opens of the same article. Once it was opened
 * LIVE_AUTO_PROMPT_REPEAT times in a row within LIVE_AUTO_PROMPT_SECS, asks
 * (on this open) whether to go straight into live mode. The count restarts
 * whenever the user was asked, the time is up, or the user left the article
 * by anything but a plain quit (live_reset_repeat).
 * Returns true if the user wants live mode.
 */
bool
live_check_repeat(uint32_t bid, aidu_t aid)
{
    if (!live_supported() || bid == 0 || aidu_raw(aid) == 0)
        return false;

    syncnow();
    if (s_live_repeat.bid != bid ||
        aidu_raw(s_live_repeat.aid) != aidu_raw(aid) ||
        s_live_repeat.count == 0 ||
        time4_diff(now, s_live_repeat.since) > LIVE_AUTO_PROMPT_SECS) {
        s_live_repeat.bid = bid;
        s_live_repeat.aid = aid;
        s_live_repeat.count = 0;
        s_live_repeat.since = now;
    }
    if (s_live_repeat.count < LIVE_AUTO_PROMPT_REPEAT) {
        s_live_repeat.count++;
        return false;
    }

    /* Keys typed ahead (e.g. the next comment) would answer the prompt;
     * keep the count and ask on a later open. */
    if (vkey_is_typeahead())
        return false;

    /* Don't offer what live_run() can't deliver; keep the count to retry. */
    if (!ipc_is_connected())
        ipc_check_reconnect();
    if (!ipc_is_connected())
        return false;

    s_live_repeat.count = 0;
    return vans("要直接進入實況模式看新推文嗎？ [Y/n]: ") != 'n';
}
