/* $Id$ */
#include "bbs.h"

//kcwu: 80x24 一般使用者名單 1.9k, 含 header 2.4k
// 一般文章推文頁約 2590 bytes
#define OBUFSIZE  3072
#define IBUFSIZE  128

/* realXbuf is Xbuf+3 because hz convert library requires buf[-2]. */
#define CVTGAP	  (3)

#ifdef DEBUG
#define register
// #define DBG_OUTRPT
#endif

static unsigned char real_outbuf[OBUFSIZE + CVTGAP*2] = "   ";
static unsigned char real_inbuf [IBUFSIZE + CVTGAP*2] = "   ";

// we've seen such pattern - make it accessible for movie mode.
#define CLIENT_ANTI_IDLE_STR   ESC_STR "OA" ESC_STR "OB"

// use defines instead - it is discovered that sometimes the input/output buffer was overflow,
// without knowing why.
// static unsigned char *outbuf = real_outbuf + 3, *inbuf = real_inbuf + 3;
#define inbuf  (real_inbuf +CVTGAP)
#define outbuf (real_outbuf+CVTGAP)

static int obufsize = 0;
static int ibufsize = 0;
static int icurrchar = 0;

#ifdef DBG_OUTRPT
// output counter
static unsigned long szTotalOutput = 0, szLastOutput = 0;
extern unsigned char fakeEscape;
unsigned char fakeEscape = 0;

static unsigned char fakeEscFilter(unsigned char c)
{
    if (!fakeEscape) return c;
    if (c == ESC_CHR) return '*';
    else if (c == '\n') return 'N';
    else if (c == '\r') return 'R';
    else if (c == '\b') return 'B';
    else if (c == '\t') return 'I';
    return c;
}
#endif // DBG_OUTRPT

/* ----------------------------------------------------- */
/* convert routines                                      */
/* ----------------------------------------------------- */
#ifdef CONVERT

inline static ssize_t input_wrapper(void *buf, ssize_t count) {
    /* input_wrapper is a special case.
     * because we may do nothing,
     * a if-branch is better than a function-pointer call.
     */
    if(input_type)
	return (*input_type)(buf, count);
    else
	return count;
}

inline static int read_wrapper(int fd, void *buf, size_t count) {
    return (*read_type)(fd, buf, count);
}

inline static int write_wrapper(int fd, void *buf, size_t count) {
    return (*write_type)(fd, buf, count);
}
#endif

/* ----------------------------------------------------- */
/* debug reporting                                       */
/* ----------------------------------------------------- */

#if defined(DEBUG) || defined(DBG_OUTRPT)
void
debug_print_input_buffer(char *s, size_t len)
{
    int y, x, i;
    if (!s || !len)
        return;

    getyx_ansi(&y, &x);
    move(b_lines, 0); clrtoeol();
    SOLVE_ANSI_CACHE();
    prints("Input Buffer (%d): [ ", (int)len);
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
    prints(" ] ");
    move_ansi(y, x);
}
#endif

/* ----------------------------------------------------- */
/* output routines                                       */
/* ----------------------------------------------------- */
void
oflush(void)
{
    if (obufsize) {
	STATINC(STAT_SYSWRITESOCKET);
#ifdef CONVERT
	write_wrapper(1, outbuf, obufsize);
#else
	write(1, outbuf, obufsize);
#endif
	obufsize = 0;
    }

#ifdef DBG_OUTRPT
    // if (0)
    {
	static char xbuf[128];
	sprintf(xbuf, ESC_STR "[s" ESC_STR "[H" " [%lu/%lu] " ESC_STR "[u",
		szLastOutput, szTotalOutput);
	write(1, xbuf, strlen(xbuf));
	szLastOutput = 0; 
    }
#endif // DBG_OUTRPT

    // XXX to flush, set TCP_NODELAY instead.
    // fsync does NOT work on network sockets.
    // fsync(1);
}

void
output(const char *s, int len)
{
#ifdef DBG_OUTRPT
    int i = 0;
    if (fakeEscape)
	for (i = 0; i < obufsize; i++)
	    outbuf[i] = fakeEscFilter(outbuf[i]);

    szTotalOutput += len; 
    szLastOutput  += len;
#endif // DBG_OUTRPT

    /* Invalid if len >= OBUFSIZE */
    assert(len<OBUFSIZE);

    if (obufsize + len > OBUFSIZE) {
	STATINC(STAT_SYSWRITESOCKET);
#ifdef CONVERT
	write_wrapper(1, outbuf, obufsize);
#else
	write(1, outbuf, obufsize);
#endif
	obufsize = 0;
    }
    memcpy(outbuf + obufsize, s, len);
    obufsize += len;
}

int
ochar(int c)
{

#ifdef DBG_OUTRPT
    c = fakeEscFilter(c);
    szTotalOutput ++; 
    szLastOutput ++;
#endif // DBG_OUTRPT

    if (obufsize > OBUFSIZE - 1) {
	STATINC(STAT_SYSWRITESOCKET);
	/* suppose one byte data doesn't need to be converted. */
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
    outbuf[obufsize++] = c;
    return 0;
}

/* ----------------------------------------------------- */
/* pager processor                                       */
/* ----------------------------------------------------- */

static int i_newfd;
int 
process_pager_keys(int ch)
{
    static int water_which_flag;
    assert(currutmp);
    switch (ch) 
    {
	case Ctrl('U') :
	    if ( currutmp->mode == EDITING ||
		 currutmp->mode == LUSERS  || 
		!currutmp->mode) {
		return ch;
	    } else {
		screen_backup_t old_screen;
		int		oldroll = roll;
		int             my_newfd;

		scr_dump(&old_screen);
		my_newfd = i_newfd;
		i_newfd = 0;

		t_users();

		i_newfd = my_newfd;
		roll = oldroll;
		scr_restore(&old_screen);
	    }
	    return KEY_INCOMPLETE;

	    // TODO 醜死了的 code ，等好心人 refine
	case Ctrl('R'):

	    if (PAGER_UI_IS(PAGER_UI_OFO))
	    {
		int my_newfd;
		screen_backup_t old_screen;

		if (!currutmp->msgs[0].pid ||
		    wmofo != NOTREPLYING)
		    break;

		scr_dump(&old_screen);

		my_newfd = i_newfd;
		i_newfd = 0;
		my_write2();
		scr_restore(&old_screen);
		i_newfd = my_newfd;
		return KEY_INCOMPLETE;

	    }

	    // non-UFO
	    check_water_init();

	    if (watermode > 0) 
	    {
		watermode = (watermode + water_which->count)
		    % water_which->count + 1;
		t_display_new();
		return KEY_INCOMPLETE;
	    } 
	    else if (watermode == 0 &&
		    currutmp->mode == 0 &&
		    (currutmp->chatid[0] == 2 || currutmp->chatid[0] == 3) &&
		    water_which->count != 0) 
	    {
		/* 第二次按 Ctrl-R */
		watermode = 1;
		t_display_new();
		return KEY_INCOMPLETE;
	    } 
	    else if (watermode == -1 && 
		    currutmp->msgs[0].pid) 
	    {
		/* 第一次按 Ctrl-R (必須先被丟過水球) */
		screen_backup_t old_screen;
		int             my_newfd;
		scr_dump(&old_screen);

		/* 如果正在talk的話先不處理對方送過來的封包 (不去select) */
		my_newfd = i_newfd;
		i_newfd = 0;
		show_call_in(0, 0);
		watermode = 0;
#ifndef PLAY_ANGEL
		my_write(currutmp->msgs[0].pid, "水球丟過去： ",
			currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
#else
		switch (currutmp->msgs[0].msgmode) {
		    case MSGMODE_TALK:
		    case MSGMODE_WRITE:
			my_write(currutmp->msgs[0].pid, "水球丟過去： ",
				 currutmp->msgs[0].userid, WATERBALL_GENERAL, NULL);
			break;
		    case MSGMODE_FROMANGEL:
			my_write(currutmp->msgs[0].pid, "再問祂一次： ",
				 currutmp->msgs[0].userid, WATERBALL_ANGEL, NULL);
			break;
		    case MSGMODE_TOANGEL:
			my_write(currutmp->msgs[0].pid, "回答小主人： ",
				 currutmp->msgs[0].userid, WATERBALL_ANSWER, NULL);
			break;
		}
#endif
		i_newfd = my_newfd;

		/* 還原螢幕 */
		scr_restore(&old_screen);
		return KEY_INCOMPLETE;
	    }
	    break;

	case KEY_TAB:
	    if (watermode <= 0 || 
		(!PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
		break;

	    check_water_init();
	    watermode = (watermode + water_which->count)
		% water_which->count + 1;
	    t_display_new();
	    return KEY_INCOMPLETE;

	case Ctrl('T'):
	    if (watermode <= 0 ||
		!(PAGER_UI_IS(PAGER_UI_ORIG) || PAGER_UI_IS(PAGER_UI_NEW)))
		   break;

	    check_water_init();
	    if (watermode > 1)
		watermode--;
	    else
		watermode = water_which->count;
	    t_display_new();
	    return KEY_INCOMPLETE;

	case Ctrl('F'):
	    if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
		break;

	    check_water_init();
	    if (water_which_flag == (int)water_usies)
		water_which_flag = 0;
	    else
		water_which_flag =
		    (water_which_flag + 1) % (int)(water_usies + 1);
	    if (water_which_flag == 0)
		water_which = &water[0];
	    else
		water_which = swater[water_which_flag - 1];
	    watermode = 1;
	    t_display_new();
	    return KEY_INCOMPLETE;

	case Ctrl('G'):
	    if (watermode <= 0 || !PAGER_UI_IS(PAGER_UI_NEW))
		break;

	    check_water_init();
	    water_which_flag = (water_which_flag + water_usies) % (water_usies + 1);
	    if (water_which_flag == 0)
		water_which = &water[0];
	    else
		water_which = swater[water_which_flag - 1];

	    watermode = 1;
	    t_display_new();
	    return KEY_INCOMPLETE;
    }
    return ch;
}

/* ----------------------------------------------------- */
/* input routines                                        */
/* ----------------------------------------------------- */

static int    i_newfd = 0;
static struct timeval i_to, *i_top = NULL;

inline void
add_io(int fd, int timeout)
{
    i_newfd = fd;
    if (timeout) {
	i_to.tv_sec = timeout;
	i_to.tv_usec = 16384;	/* Ptt: 改成16384 避免不按時for loop吃cpu
				 * time 16384 約每秒64次 */
	i_top = &i_to;
    } else
	i_top = NULL;
}

inline int
num_in_buf(void)
{
    if (ibufsize <= icurrchar)
	return 0;
    return ibufsize - icurrchar;
}

inline int
vkey_is_full(void)
{
    return ibufsize >= IBUFSIZE;
}

inline static ssize_t 
wrapped_tty_read(unsigned char *buf, size_t max)
{
    /* tty_read will handle abort_bbs.
     * len <= 0: read more */
    ssize_t len = tty_read(buf, max);
    if (len <= 0)
	return len;

    // apply additional converts
#ifdef DBCSAWARE
    if (ISDBCSAWARE() && HasUserFlag(UF_DBCS_DROP_REPEAT))
	len = vtkbd_ignore_dbcs_evil_repeats(buf, len);
#endif
#ifdef CONVERT
    len = input_wrapper(inbuf, len);
#endif
#ifdef DBG_OUTRPT

#if 1
    if (len > 0)
	debug_print_input_buffer((char*)inbuf, len);
#else
    {
	static char xbuf[128];
	sprintf(xbuf, ESC_STR "[s" ESC_STR "[2;1H [%ld] " 
		ESC_STR "[u", len);
	write(1, xbuf, strlen(xbuf));
    }
#endif

#endif // DBG_OUTRPT
    return len;
}

/*
 * dogetch() is not reentrant-safe. SIGUSR[12] might happen at any time, and
 * dogetch() might be called again, and then ibufsize/icurrchar/inbuf might
 * be inconsistent. We try to not segfault here...
 */

static int
dogetch(void)
{
    ssize_t         len;
    static time4_t  lastact;
    if (ibufsize <= icurrchar) {

	refresh();

	if (i_newfd) {

	    struct timeval  timeout;
	    fd_set          readfds;

	    if (i_top)
		timeout = *i_top;	/* copy it because select() might
					 * change it */

	    FD_ZERO(&readfds);
	    FD_SET(0, &readfds);
	    FD_SET(i_newfd, &readfds);

	    /* jochang: modify first argument of select from FD_SETSIZE */
	    /* since we are only waiting input from fd 0 and i_newfd(>0) */

	    STATINC(STAT_SYSSELECT);
	    while ((len = select(i_newfd + 1, &readfds, NULL, NULL,
			    i_top ? &timeout : NULL)) < 0) {
		if (errno != EINTR)
		    abort_bbs(0);
		/* raise(SIGHUP); */
	    }

	    if (len == 0){
		syncnow();
		return I_TIMEOUT;
	    }

	    if (i_newfd && FD_ISSET(i_newfd, &readfds)){
		syncnow();
		return I_OTHERDATA;
	    }
	}

#ifdef NOKILLWATERBALL
	if( currutmp && currutmp->msgcount && !reentrant_write_request )
	    write_request(1);
#endif

	STATINC(STAT_SYSREADSOCKET);

	do {
	    len = wrapped_tty_read(inbuf, IBUFSIZE);
	} while (len <= 0);

	ibufsize = len;
	icurrchar = 0;
    }

    if (currutmp) {
	syncnow();
	/* 3 秒內超過兩 byte 才算 active, anti-antiidle.
	 * 不過方向鍵等組合鍵不止 1 byte */
	if (now - lastact < 3)
	    currutmp->lastact = now;
	lastact = now;
    }

    // see vtkbd.c for CR/LF Rules
    {
	unsigned char c = (unsigned char) inbuf[icurrchar++];

	// CR LF are treated as one.
	if (c == KEY_CR)
	{
	    // peak next character.
	    if (icurrchar < ibufsize && inbuf[icurrchar] == KEY_LF)
		icurrchar ++;
	    return KEY_ENTER;
	} 
	else if (c == KEY_LF)
	{
	    return KEY_UNKNOWN;
	}

	return c;
    }
}

// virtual terminal keyboard context
static VtkbdCtx vtkbd_ctx;

inline int
vkey(void)
{
    register int ch;

    while (1) 
    {
	ch = dogetch();

	// convert virtual terminal keys
	ch = vtkbd_process(ch, &vtkbd_ctx);
	switch(ch)
	{
	    case KEY_INCOMPLETE:
		// XXX what if endless?
		continue;

	    case KEY_ESC:
		KEY_ESC_arg = vtkbd_ctx.esc_arg;
		return ch;

	    case KEY_UNKNOWN:
		return ch;

	    // common global hot keys...
	    case Ctrl('L'):
		redrawwin();
		refresh();
		continue;
#ifdef DEBUG
	    case Ctrl('Q'):
		{
		    struct rusage ru;
		    getrusage(RUSAGE_SELF, &ru);
		    vmsgf("sbrk: %d KB, idrss: %d KB, isrss: %d KB",
			    ((int)sbrk(0) - 0x8048000) / 1024,
			    (int)ru.ru_idrss, (int)ru.ru_isrss);
		}
		continue;
#endif
	}

	// complex pager hot keys
	if (currutmp)
	{
	    ch = process_pager_keys(ch);
	    if (ch == KEY_INCOMPLETE)
		continue;
	}

	return ch;
    }
    // should not reach here. just to make compiler happy.
    return ch;
}

/*
 * wait user input anything for f seconds.
 * if f < 0, then wait forever.
 * Return 1 if anything available.
 */
inline int 
wait_input(float f, int bIgnoreBuf)
{
    int sel = 0;
    fd_set readfds;
    struct timeval tv, *ptv = &tv;

    if(!bIgnoreBuf && num_in_buf() > 0)
	return 1;

    FD_ZERO(&readfds);
    FD_SET(0, &readfds);

    if(f > 0)
    {
	tv.tv_sec = (long) f;
	tv.tv_usec = (f - (long)f) * 1000000L;
    } else
	ptv = NULL;

#ifdef STATINC
    STATINC(STAT_SYSSELECT);
#endif

    do {
	if(!bIgnoreBuf && num_in_buf() > 0)
	    return 1;
	sel = select(1, &readfds, NULL, NULL, ptv);
    } while (sel < 0 && errno == EINTR);
    /* EINTR, interrupted. I don't care! */

    if(sel == 0)
	return 0;

    return 1;
}

inline void 
vkey_purge(void)
{
    icurrchar = ibufsize = 0;
}

int
vkey_detach(void)
{
    int r = i_newfd;
    add_io(0, 0);
    return r;
}

int
vkey_attach(int fd)
{
    int r = i_newfd;
    add_io(fd, 0);
    return r;
}

/*
 * wait user input for f seconds.
 * return 1 if control key c is available.
 */
inline int 
peek_input(float f, int c)
{
    int i = 0;
    assert (c > 0 && c < ' '); // only ^x keys are safe to be detected.
    // other keys may fall into escape sequence.

    if (wait_input(f, 1) && (IBUFSIZE > ibufsize))
    {
	int len = wrapped_tty_read(inbuf + ibufsize, IBUFSIZE - ibufsize);
	if (len > 0)
	    ibufsize += len;
    }
    for (i = icurrchar; i < ibufsize; i++)
    {
	if (inbuf[i] == c)
	    return 1;
    }
    return 0;
}

/* vim:sw=4
 */
