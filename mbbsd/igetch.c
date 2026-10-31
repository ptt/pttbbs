#include "bbs.h"

#define IBUFSIZE  128

#ifdef DEBUG
#define inline
#endif

static char vin_buf[IBUFSIZE];
static VBUF vin = {
    .head     = vin_buf,
    .tail     = vin_buf,
    .capacity = sizeof(vin_buf) - 1,
    .buf      = vin_buf,
    .buf_end  = vin_buf + sizeof(vin_buf),
}, *pvin = &vin;

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

	{
	    int ev = vkey_take_event();
	    if (ev)
		return ev;
	}

	vkey_sync_ipc();
	if (ipc_has_pending()) {
	    if (vkey_process_ipc() > 0)
		refresh();
	    continue;   // re-check events; pending queue is now drained
	}

	/* Auto-reconnect is enabled only after login (ipc_init). */
	int want_reconnect = (vkctx.ipc_fd <= 0 && ipc_reconnect_enabled());
	if (want_reconnect) {
	    ipc_check_reconnect();
	    want_reconnect = (vkey_sync_ipc() <= 0);
	}

	fd_set          readfds;
	int             maxfd = 0;

	FD_ZERO(&readfds);
	FD_SET(0, &readfds);
	if (vkctx.attached_fd > 0) {
	    FD_SET(vkctx.attached_fd, &readfds);
	    if (vkctx.attached_fd > maxfd)
		maxfd = vkctx.attached_fd;
	}
	if (vkctx.ipc_fd > 0) {
	    FD_SET(vkctx.ipc_fd, &readfds);
	    if (vkctx.ipc_fd > maxfd)
		maxfd = vkctx.ipc_fd;
	}

	/* If disconnected from IPC service, set a 2-second timeout on select()
	 * so we wake up periodically during idle to retry reconnecting with backoff.
	 * If connected, timeout is NULL (wait forever on keyboard or IPC events). */
	struct timeval tv;
	struct timeval *ptv = NULL;
	if (want_reconnect) {
	    tv.tv_sec = 2;
	    tv.tv_usec = 0;
	    ptv = &tv;
	}

	STATINC(STAT_SYSSELECT);
	while ((len = select(maxfd + 1, &readfds, NULL, NULL, ptv)) < 0) {
	    if (errno != EINTR)
		abort_bbs(0);
	}

	if (len == 0) {
	    /* Timeout during idle: reconnect is retried at the top of the loop */
	    continue;
	}

	if (vkctx.ipc_fd > 0 && FD_ISSET(vkctx.ipc_fd, &readfds)) {
	    syncnow();
	    if (vkey_process_ipc() > 0)
		refresh();
	    continue;   // events are returned at the top of the loop
	}

	if (vkctx.attached_fd > 0 && FD_ISSET(vkctx.attached_fd, &readfds)) {
	    syncnow();
	    return I_OTHERDATA;
	}

	if (FD_ISSET(0, &readfds)) {
	    STATINC(STAT_SYSREADSOCKET);
	    do {
		len = read_vin();
	    } while (len <= 0);
	}
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
	if (ch == I_TIMEOUT || ch == I_OTHERDATA || ch == I_EV_PUBSUB)
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
    int maxfd = 0;
    vkey_sync_ipc();
    if (vkctx.attached_fd > 0) {
        FD_SET(vkctx.attached_fd, &readfds);
        if (vkctx.attached_fd > maxfd)
            maxfd = vkctx.attached_fd;
    }
    if (vkctx.ipc_fd > 0) {
        FD_SET(vkctx.ipc_fd, &readfds);
        if (vkctx.ipc_fd > maxfd)
            maxfd = vkctx.ipc_fd;
    }

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
	sel = select(maxfd + 1, &readfds, NULL, NULL, ptv);

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
    return (vkctx.peek_ch != KEY_INCOMPLETE) || (num_in_buf() > 0);
}

inline int
vkey_is_typeahead()
{
    return (vkctx.peek_ch != KEY_INCOMPLETE) || (num_in_buf() > 0);
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
    vkctx.peek_ch = KEY_INCOMPLETE;

    STATINC(STAT_SYSREADSOCKET);
    while (wait_input(0.01, 1) && max_try-- > 0) {
        read_vin();
        drop_input();
    }
}

void
vkey_init() {
    vbuf_attach(pvin, vin_buf, sizeof(vin_buf));
    memset(&vkctx, 0, sizeof(vkctx));
    vkctx.peek_ch = KEY_INCOMPLETE;
}

inline int
vk_byte(void)
{
    if (vkctx.peek_ch != KEY_INCOMPLETE) {
        int c = vkctx.peek_ch;
        vkctx.peek_ch = KEY_INCOMPLETE;
        return c;
    }
    return igetch();
}

void
vk_ungetc(int c)
{
    vkctx.peek_ch = c;
}

int
vkey_poll_ex(int ms, int events)
{
    if (vkctx.peek_ch != KEY_INCOMPLETE)
        return 1;
    if (vkctx.ev_pending & events)
        return 1;
    if (ms) refresh();
    // XXX handle I_OTHERDATA?
    // Note: IPC data also wakes this up; the following vkey_ex() processes
    // it and returns a wanted event, or keeps waiting for a key otherwise.
    return wait_input(ms / (double)MILLISECONDS, 0);
}

inline int
vkey_poll(int ms)
{
    return vkey_poll_ex(ms, 0);
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

/* vim:sw=4
 */
