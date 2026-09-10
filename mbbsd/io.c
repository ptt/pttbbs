#include <limits.h>
#include <sys/param.h>
#include "bbs.h"

//kcwu: 80x24 一般使用者名單 1.9k, 含 header 2.4k
// 一般文章推文頁約 2590 bytes
// 注意實際可用的空間為 N-1。
#define OBUFSIZE  3072
#define IBUFSIZE  256

// When charset encoding conversion is applied, we may need to write extra N
// bytes into buffer for one character input. Currently the number is 3 (UTF8).
# define OBUFMINSPACE (3)

#ifdef DEBUG
#define register
#define inline
// #define DBG_OUTRPT
#endif

static VBUF vout, *pvout = &vout;
#ifndef USE_NIOS
static VBUF vin, *pvin = &vin;
#endif

// we've seen such pattern - make it accessible for movie mode.
#define CLIENT_ANTI_IDLE_STR   ESC_STR "OA" ESC_STR "OB"

/* ----------------------------------------------------- */
/* debug reporting                                       */
/* ----------------------------------------------------- */
#if defined(DEBUG) || defined(DBG_OUTRPT)
// output counter
static unsigned long szTotalOutput = 0, szLastOutput = 0;
unsigned char fakeEscape = 0;

unsigned char fakeEscFilter(unsigned char c)
{
    if (!fakeEscape) return c;
    if (c == ESC_CHR) return '*';
    else if (c == '\n') return 'N';
    else if (c == '\r') return 'R';
    else if (c == '\b') return 'B';
    else if (c == '\t') return 'I';
    return c;
}

void
debug_output_buffer(void)
{
    char xbuf[STRLEN];
    SNPRINTF(xbuf, ESC_STR "[s" ESC_STR "[H" " [%lu/%lu] " ESC_STR "[u",
             szLastOutput, szTotalOutput);
    write(1, xbuf, strlen(xbuf));
    szLastOutput = 0;
}

ssize_t
debug_simple_input_buffer(unsigned char *buf GCC_UNUSED, ssize_t len)
{
    char xbuf[STRLEN];
    SNPRINTF(xbuf, ESC_STR "[s" ESC_STR "[2;1H [%ld] "
             ESC_STR "[u", len);
    write(1, xbuf, strlen(xbuf));
    return len;
}

ssize_t
debug_print_input_buffer(void *buf, ssize_t len)
{
    unsigned char *s = (unsigned char *)buf;
    int y, x, i;
    if (!s || !len)
        return len;

    getyx_ansi(&y, &x);
    move(0, 0); clrtocol();
    SOLVE_ANSI_CACHE();
    prints(ANSI_RESET "Input Buffer (%d): [ ", (int)len);
    for (i = 0; i < len; i++, s++)
    {
        int c = (unsigned char)*s;
        if (!isascii(c) || !isprint(c) || c == ' ')
        {
            if (c == ESC_CHR)
                outs(ANSI_COLOR(1;36) "Esc" ANSI_RESET);
            else if (c == ' ')
                outs(ANSI_COLOR(1;36) "Sp " ANSI_RESET);
            else if (c == 0)
                prints(ANSI_COLOR(1;31) "Nul" ANSI_RESET);
            else if (c > 0 && c < ' ')
                prints(ANSI_COLOR(1;32) "^%c", c + 'A' -1);
            else
                prints(ANSI_COLOR(1;33) "[%02X]" ANSI_RESET, c);
        } else {
            outc(c);
        }
    }
    prints(" ]\n");
    move_ansi(y, x);
    return len;
}

#endif // DBG_OUTRPT

/* ----------------------------------------------------- */
/* output routines                                       */
/* ----------------------------------------------------- */
void
oflush(void)
{
    if (!vbuf_is_empty(pvout)) {
        STATINC(STAT_SYSWRITESOCKET);
        vbuf_write(pvout, 1, VBUF_RWSZ_ALL);
    }

#ifdef DBG_OUTRPT
    debug_output_buffer();
#endif

    // XXX to flush, set TCP_NODELAY instead.
    // fsync does NOT work on network sockets.
    // fsync(1);
}

inline void
output(const char *s, int len)
{
    while (len-- > 0)
        ochar(*s++);
}

int
ochar(int c)
{
#ifdef DBG_OUTRPT
    // TODO we can support converted output in future.
    c = fakeEscFilter(c);
    szTotalOutput ++;
    szLastOutput ++;
#endif // DBG_OUTRPT

    if (vbuf_space(pvout) < OBUFMINSPACE)
        oflush();

    convert_write(pvout, c);

    return 0;
}

/* ----------------------------------------------------- */
/* VKey & dispatcher                                     */
/* ----------------------------------------------------- */
#define MAX_HOOKS_PER_PRIO 4

static vkey_hook_fn hook_tables[VKEY_HOOK_PRIO_MAX][MAX_HOOKS_PER_PRIO];
static int hook_counts[VKEY_HOOK_PRIO_MAX];

int
vkey_register_hook(VKeyHookPriority prio, vkey_hook_fn fn)
{
    int i;
    if (prio < 0 || prio >= VKEY_HOOK_PRIO_MAX || !fn)
        return -1;

    assert(hook_counts[prio] < MAX_HOOKS_PER_PRIO);
    if (hook_counts[prio] >= MAX_HOOKS_PER_PRIO)
        return -1;

    for (i = 0; i < hook_counts[prio]; i++) {
        if (hook_tables[prio][i] == fn)
            return 0;
    }

    hook_tables[prio][hook_counts[prio]++] = fn;
    return 0;
}

int
vkey_unregister_hook(vkey_hook_fn fn)
{
    int p, i, j;
    if (!fn) return -1;
    for (p = 0; p < VKEY_HOOK_PRIO_MAX; p++) {
        for (i = 0; i < hook_counts[p]; i++) {
            if (hook_tables[p][i] == fn) {
                for (j = i; j < hook_counts[p] - 1; j++) {
                    hook_tables[p][j] = hook_tables[p][j + 1];
                }
                hook_counts[p]--;
                return 0;
            }
        }
    }
    return -1;
}

int
vkey_dispatch_hooks(int ch)
{
    int p, i;
    for (p = 0; p < VKEY_HOOK_PRIO_MAX; p++) {
        for (i = 0; i < hook_counts[p]; i++) {
            int ret = hook_tables[p][i](ch);
            if (ret == KEY_INCOMPLETE)
                return KEY_INCOMPLETE;
            ch = ret;
        }
    }
    return ch;
}

static void GCC_UNUSED
draw_80x24() {
    int ox, oy, y, x;
    getyx(&oy, &ox);
    for (y = 0; y < 24; y++) {
        move(y, 0); prints("%d", (y + 1) % 10);
        move(y, 79); prints("%d", (y + 1) % 10);
    }
    for (int i = 0; i < 2; i++) {
        move (i * 23, 0);
        for (x = 0; x < 80; x++) {
            prints("%d", (x+1) % 10);
        }
    }
    move(oy, ox);
}

static int
system_key_hook(int ch)
{
    if (currutmp && ch != KEY_INCOMPLETE) {
        static time4_t lastact;
        syncnow();
        /* 3 秒內超過兩 byte 才算 active, anti-antiidle.
         * 不過方向鍵等組合鍵不止 1 byte */
        if (time4_diff(now, lastact) < 3)
            currutmp->lastact = now;
        lastact = now;
    }

    switch (ch)
    {
    case Ctrl('L'):
#ifdef CTRL_L_FOR_80x24
        draw_80x24();
#endif
        redrawwin();
        refresh();
        return KEY_INCOMPLETE;

    case Ctrl('Q'):
        if (IS_DEBUG) {
            char usage[STRLEN];
            get_memusage(sizeof(usage), usage);
            vmsg(usage);
            return KEY_INCOMPLETE;
        }
        return ch;

    case KEY_MOUSE_RELEASE:
        return KEY_INCOMPLETE;
    }
    return ch;
}

static void
system_init_hooks(void)
{
    vkey_register_hook(VKEY_HOOK_PRIO_SYSTEM, system_key_hook);
}

const vtkbd_mouse_t *
vkey_get_mouse(void)
{
    VKEY_CTX *ctx = vkey_get_context();
    return ctx ? vtkbd_get_mouse(&ctx->vtkbd) : NULL;
}

int
vkey_get_mouse_pos(int *x, int *y)
{
    VKEY_CTX *ctx = vkey_get_context();
    if (!ctx)
        return 0;
    if (x) *x = ctx->vtkbd.mouse.x;
    if (y) *y = ctx->vtkbd.mouse.y;
    return (ctx->vtkbd.mouse.x >= 0 && ctx->vtkbd.mouse.y >= 0);
}

int
vkey_attach(int fd)
{
    VKEY_CTX *ctx = vkey_get_context();
    if (!ctx)
        return 0;
    int r = ctx->attached_fd;
    ctx->attached_fd = fd;
    return r;
}

int
vkey_detach(void)
{
    return vkey_attach(0);
}

int
vkey_decode(VBUF *inbuf, int raw_ch)
{
    VKEY_CTX *ctx = vkey_get_context();
    if (!ctx)
        return raw_ch;

    int ch = vtkbd_process(raw_ch, &ctx->vtkbd);
    switch (ch) {
    case KEY_INCOMPLETE:
        return KEY_INCOMPLETE;

    case KEY_ESC:
        KEY_ESC_arg = ctx->vtkbd.esc_arg;
        break;

    case KEY_CR:
        if (inbuf && vbuf_peek(inbuf) == KEY_LF)
            vbuf_pop(inbuf);
        break;

    case KEY_LF:
        return KEY_INCOMPLETE;
    }

    return vkey_dispatch_hooks(ch);
}

/* ----------------------------------------------------- */
/* vbuf filter pipeline                                  */
/* ----------------------------------------------------- */

static ssize_t
dbcs_filter_process(void *buf, ssize_t len)
{
    if (!ISDBCSAWARE())
        return len;
    return vtkbd_ignore_dbcs_evil_repeats(buf, len);
}

/* tty_read
 * read from tty, abort if socket closed.
 * return: >0 = length, <=0 means read more, abort/eof is automatically processed.
 */
static ssize_t
tty_read(void *buf, ssize_t max)
{
    ssize_t l = read(0, buf, max);

    if(l == 0 || (l < 0 && !(errno == EINTR || errno == EAGAIN)))
	abort_bbs(0);

    return l;
}

ssize_t vbuf_from_tty(VBUF *v)
{
    // To prevent unnecessary vbuf copies, we will use a flat buffer and only
    // send it to a vbuf at the pipe sink.
    unsigned char buf[IBUFSIZE];
    ssize_t len = MIN(sizeof(buf), vbuf_space(v));

    static const vbuf_filter_fn tty_filters[] = {
        tty_read,
#ifdef DBG_OUTRPT
        debug_print_input_buffer,
#endif
        telnet_filter_process,
        dbcs_filter_process,
    };

    return vbuf_pipeline(
            v, buf, len, tty_filters, ARRAY_SIZE(tty_filters), convert_read);
}

/* ----------------------------------------------------- */
/* Input Output System                                   */
/* ----------------------------------------------------- */
int
init_io() {
    vbuf_new(pvout, OBUFSIZE);
#ifndef USE_NIOS
    vbuf_new(pvin, IBUFSIZE);
#endif
    vkey_init();
    system_init_hooks();
    pager_init_hooks();
    talk_init_hooks();
    return 0;
}

#ifndef USE_NIOS

/* ----------------------------------------------------- */
/* input routines                                        */
/* ----------------------------------------------------- */

// traditional implementation

static VKEY_CTX vkctx = {
    .peek_ch = KEY_INCOMPLETE,
};

VKEY_CTX *
vkey_get_context(void)
{
    return &vkctx;
}

static int
num_in_buf(void)
{
    return vbuf_size(pvin);
}

static void
drop_input(void)
{
    vbuf_clear(pvin);
}

/* returns:
 * >0 if read something
 * =0 if nothing read
 * <0 if need to read again
 */
static ssize_t
read_vin() {
    return vbuf_from_tty(pvin);
}

/*
 * dogetch() is not reentrant-safe. SIGUSR[12] might happen at any time, and
 * dogetch() might be called again, and then input buffer state may be
 * inconsistent. We try to not segfault here...
 */

static int
dogetch(void)
{
    ssize_t         len;

    while (vbuf_is_empty(pvin)) {
	refresh();

	if (vkctx.attached_fd) {

	    fd_set          readfds;

	    FD_ZERO(&readfds);
	    FD_SET(0, &readfds);
	    FD_SET(vkctx.attached_fd, &readfds);

	    /* jochang: modify first argument of select from FD_SETSIZE */
	    /* since we are only waiting input from fd 0 and attached_fd(>0) */

	    STATINC(STAT_SYSSELECT);
	    while ((len = select(vkctx.attached_fd + 1, &readfds, NULL, NULL, NULL)) < 0) {
		if (errno != EINTR)
		    abort_bbs(0);
		/* raise(SIGHUP); */
	    }

	    if (len == 0){
		syncnow();
		return I_TIMEOUT;
	    }

	    if (vkctx.attached_fd && FD_ISSET(vkctx.attached_fd, &readfds)){
		syncnow();
		return I_OTHERDATA;
	    }
	}


	STATINC(STAT_SYSREADSOCKET);

	do {
            len = read_vin();
            // warning: len is 1/0/-1 now, not real length.
	} while (len <= 0);
    }

    // see vtkbd.c for CR/LF Rules
    assert(!vbuf_is_empty(pvin));
    {
        unsigned char c = vbuf_pop(pvin);
	// CR LF are treated as one.
	if (c == KEY_CR)
	{
	    // peak next character. (peek return EOF for empty)
            if (vbuf_peek(pvin) == KEY_LF)
                vbuf_pop(pvin);
	    return KEY_ENTER;
	}
	else if (c == KEY_LF)
	{
	    return KEY_UNKNOWN;
	}

	return c;
    }
}

static int
igetch(void)
{
    while (1)
    {
	int ch = dogetch();
	if (ch == I_TIMEOUT || ch == I_OTHERDATA)
	    return ch;

	ch = vkey_decode(pvin, ch);
	if (ch == KEY_INCOMPLETE)
	    continue;

	return ch;
    }
}

/*
 * wait user input anything for f seconds.
 * if f == 0, return immediately
 * if f < 0,  wait forever.
 * Return 1 if anything available.
 */
static int
wait_input(float f, int bIgnoreBuf)
{
    int sel = 0;
    fd_set readfds;
    struct timeval tv, *ptv = &tv;

    if(!bIgnoreBuf && num_in_buf() > 0)
	return 1;

    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    if (vkctx.attached_fd) FD_SET(vkctx.attached_fd, &readfds);

    // adjust time
    if(f > 0)
    {
	tv.tv_sec = (long) f;
	tv.tv_usec = (f - (long)f) * 1000000L;
    }
    else if (f == 0)
    {
	tv.tv_sec  = 0;
	tv.tv_usec = 0;
    }
    else if (f < 0)
    {
	ptv = NULL;
    }

#ifdef STATINC
    STATINC(STAT_SYSSELECT);
#endif

    do {
	assert(vkctx.attached_fd >= 0);	// if == 0, use only fd=0 => count sill u_newfd+1.
	sel = select(vkctx.attached_fd+1, &readfds, NULL, NULL, ptv);

    } while (sel < 0 && errno == EINTR);
    /* EINTR, interrupted. I don't care! */

    // XXX should we abort? (from dogetch)
    if (sel < 0 && errno != EINTR)
    {
	abort_bbs(0);
	/* raise(SIGHUP); */
    }

    // syncnow();

    if(sel == 0)
	return 0;

    return 1;
}

/* nios vkey system emulation */

inline int
vkey_is_ready(void)
{
    return num_in_buf() > 0;
}

inline int
vkey_is_typeahead()
{
    return num_in_buf() > 0;
}

inline int
vkey_is_full(void)
{
    return vbuf_is_full(pvin);
}

inline void
vkey_purge(void)
{
    int max_try = 64;
    drop_input();

    STATINC(STAT_SYSREADSOCKET);
    while (wait_input(0.01, 1) && max_try-- > 0) {
        read_vin();
        drop_input();
    }
}

void
vkey_init() {
    memset(&vkctx, 0, sizeof(vkctx));
    vkctx.peek_ch = KEY_INCOMPLETE;
}



inline int
vkey(void)
{
    return igetch();
}

inline int
vkey_poll(int ms)
{
    if (ms) refresh();
    // XXX handle I_OTHERDATA?
    return wait_input(ms / (double)MILLISECONDS, 0);
}

int
vkey_prefetch(int timeout) {
    if (wait_input(timeout / (double)MILLISECONDS, 1) && !vbuf_is_full(pvin))
        read_vin();
    return num_in_buf() > 0;
}

int
vkey_is_prefetched(char c) {
    // only ^x keys are safe to be detected.
    // other keys may fall into escape sequence.
    assert (c == EOF || (c > 0 && c < ' '));

    if (c == EOF)
	return 0;

    return vbuf_strchr(pvin, c) >= 0 ? 1 : 0;
}

#endif

/* vim:sw=4
 */
