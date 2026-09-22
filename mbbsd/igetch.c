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
    vbuf_attach(pvin, vin_buf, sizeof(vin_buf));
    memset(&vkctx, 0, sizeof(vkctx));
    vkctx.peek_ch = KEY_INCOMPLETE;
}

/*
 * vkey(): receive next key.
 * Note: returns ASCII (0x00..0x7F), KEY_* special keys, and for non-ASCII
 * input either raw multibyte bytes (VKEY_IS_MB=1, default) or a decoded
 * wchar (VKEY_IS_MB=0: UCS/Unicode scalar for UTF-8, 16-bit word for Big5).
 */
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

/* vim:sw=4
 */
