/* $Id$ */
#include "bbs.h"

// XXX why linux use smaller buffer?
#if defined(linux)
#define OBUFSIZE  2048
#define IBUFSIZE  128
#else
#define OBUFSIZE  4096
#define IBUFSIZE  256
#endif

static char     outbuf[OBUFSIZE], inbuf[IBUFSIZE];
static int      obufsize = 0, ibufsize = 0;
static int      icurrchar = 0;

/* ----------------------------------------------------- */
/* output routines                                       */
/* ----------------------------------------------------- */

void
oflush()
{
    if (obufsize) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
}

void
init_buf()
{

    memset(inbuf, 0, IBUFSIZE);
}
void
output(char *s, int len)
{
    /* Invalid if len >= OBUFSIZE */

    assert(len<OBUFSIZE);
    if (obufsize + len > OBUFSIZE) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
    memcpy(outbuf + obufsize, s, len);
    obufsize += len;
}

int
ochar(int c)
{
    if (obufsize > OBUFSIZE - 1) {
	write(1, outbuf, obufsize);
	obufsize = 0;
    }
    outbuf[obufsize++] = c;
    return 0;
}

/* ----------------------------------------------------- */
/* input routines                                        */
/* ----------------------------------------------------- */

static int      i_newfd = 0;
static struct timeval i_to, *i_top = NULL;
static int      (*flushf) () = NULL;

void
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

int
num_in_buf()
{
    return icurrchar - ibufsize;
}

/*
 * dogetch() is not reentrant-safe. SIGUSR[12] might happen at any time, and
 * dogetch() might be called again, and then ibufsize/icurrchar/inbuf might
 * be inconsistent. We try to not segfault here...
 */

static int
dogetch()
{
    int             len;
    static time_t   lastact;
    if (ibufsize <= icurrchar) {

	if (flushf)
	    (*flushf) ();

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

	    while ((len = select(i_newfd + 1, &readfds, NULL, NULL, i_top ? &timeout : NULL)) < 0) {
		if (errno != EINTR)
		    abort_bbs(0);
		/* raise(SIGHUP); */
	    }

	    if (len == 0)
		return I_TIMEOUT;

	    if (i_newfd && FD_ISSET(i_newfd, &readfds))
		return I_OTHERDATA;
	}
#ifdef SKIP_TELNET_CONTROL_SIGNAL
	do{
#endif
	    while ((len = read(0, inbuf, IBUFSIZE)) <= 0) {
		if (len == 0 || errno != EINTR)
		    abort_bbs(0);
		/* raise(SIGHUP); */
	    }
#ifdef SKIP_TELNET_CONTROL_SIGNAL
	} while( inbuf[0] == -1 );
#endif
	ibufsize = len;
	icurrchar = 0;
    }
    if (currutmp) {
#ifdef OUTTA_TIMER
	now = SHM->GV2.e.now;
#else
	now = time(0);
#endif
	if (now - lastact < 3)
	    currutmp->lastact = now;
	lastact = now;
    }
    return inbuf[icurrchar++];
}

static int      water_which_flag = 0;
int
igetch()
{
    register int    ch;
    while ((ch = dogetch())) {
	switch (ch) {
#ifdef DEBUG
	case Ctrl('Q'):{
	    struct rusage ru;
	    getrusage(RUSAGE_SELF, &ru);
	    vmsg("sbrk: %d KB, idrss: %d KB, isrss: %d KB",
		 ((int)sbrk(0) - 0x8048000) / 1024,
		 (int)ru.ru_idrss, (int)ru.ru_isrss);
	}
	    continue;
#endif
	case Ctrl('L'):
	    redoscr();
	    continue;
	case Ctrl('U'):
	    if (currutmp != NULL && currutmp->mode != EDITING
		&& currutmp->mode != LUSERS && currutmp->mode) {

		screenline_t   *screen0 = calloc(t_lines, sizeof(screenline_t));
		int		oldroll = roll;
		int             y, x, my_newfd;

		getyx(&y, &x);
		memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
		my_newfd = i_newfd;
		i_newfd = 0;
		t_users();
		i_newfd = my_newfd;
		memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
		roll = oldroll;
		move(y, x);
		free(screen0);
		redoscr();
		continue;
	    } else
		return (ch);
	case KEY_TAB:
	    if (WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW))
		if (currutmp != NULL && watermode > 0) {
		    watermode = (watermode + water_which->count)
			% water_which->count + 1;
		    t_display_new();
		    continue;
		}
	    return ch;
	    break;

	case Ctrl('R'):
	    if (currutmp == NULL)
		return (ch);

	    if (currutmp->msgs[0].pid &&
		WATERMODE(WATER_OFO) && wmofo == -1) {
		int             y, x, my_newfd;
		screenline_t   *screen0 = calloc(t_lines, sizeof(screenline_t));
		memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));
		getyx(&y, &x);
		my_newfd = i_newfd;
		i_newfd = 0;
		my_write2();
		memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
		i_newfd = my_newfd;
		move(y, x);
		free(screen0);
		redoscr();
		continue;
	    } else if (!WATERMODE(WATER_OFO)) {
		if (watermode > 0) {
		    watermode = (watermode + water_which->count)
			% water_which->count + 1;
		    t_display_new();
		    continue;
		} else if (currutmp->mode == 0 &&
		    (currutmp->chatid[0] == 2 || currutmp->chatid[0] == 3) &&
			   water_which->count != 0 && watermode == 0) {
		    /* 第二次按 Ctrl-R */
		    watermode = 1;
		    t_display_new();
		    continue;
		} else if (watermode == -1 && currutmp->msgs[0].pid) {
		    /* 第一次按 Ctrl-R (必須先被丟過水球) */
		    screenline_t   *screen0;
		    int             y, x, my_newfd;
		    screen0 = calloc(t_lines, sizeof(screenline_t));
		    getyx(&y, &x);
		    memcpy(screen0, big_picture, t_lines * sizeof(screenline_t));

		    /* 如果正在talk的話先不處理對方送過來的封包 (不去select) */
		    my_newfd = i_newfd;
		    i_newfd = 0;
		    show_call_in(0, 0);
		    watermode = 0;
		    my_write(currutmp->msgs[0].pid, "水球丟過去 ： ",
			     currutmp->msgs[0].userid, 0, NULL);
		    i_newfd = my_newfd;

		    /* 還原螢幕 */
		    memcpy(big_picture, screen0, t_lines * sizeof(screenline_t));
		    move(y, x);
		    free(screen0);
		    redoscr();
		    continue;
		}
	    }
	    return ch;
	case '\n':		/* Ptt把 \n拿掉 */
	    continue;
	case Ctrl('T'):
	    if (WATERMODE(WATER_ORIG) || WATERMODE(WATER_NEW)) {
		if (watermode > 0) {
		    if (watermode > 1)
			watermode--;
		    else
			watermode = water_which->count;
		    t_display_new();
		    continue;
		}
	    }
	    return (ch);

	case Ctrl('F'):
	    if (WATERMODE(WATER_NEW)) {
		if (watermode > 0) {
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
		    continue;
		}
	    }
	    return ch;

	case Ctrl('G'):
	    if (WATERMODE(WATER_NEW)) {
		if (watermode > 0) {
		    water_which_flag = (water_which_flag + water_usies) % (water_usies + 1);
		    if (water_which_flag == 0)
			water_which = &water[0];
		    else
			water_which = swater[water_which_flag - 1];
		    watermode = 1;
		    t_display_new();
		    continue;
		}
	    }
	    return ch;

	default:
	    return ch;
	}
    }
    return 0;
}

int
oldgetdata(int line, int col, char *prompt, char *buf, int len, int echo)
{
    register int    ch, i;
    int             clen;
    int             x = col, y = line;
#define MAXLASTCMD 12
    static char     lastcmd[MAXLASTCMD][80];

    strip_ansi(buf, buf, STRIP_ALL);

    if (prompt) {
	move(line, col);
	clrtoeol();
	outs(prompt);
	x += strip_ansi(NULL, prompt, 0);
    }

    if (!echo) {
	len--;
	clen = 0;
	while ((ch = igetch()) != '\r') {
	    if (ch == '\177' || ch == Ctrl('H')) {
		if (!clen) {
		    bell();
		    continue;
		}
		clen--;
		continue;
	    }
	    if (!isprint(ch)) {
		continue;
	    }
	    if (clen >= len) {
		continue;
	    }
	    buf[clen++] = ch;
	}
	buf[clen] = '\0';
	outc('\n');
	oflush();
    } else {
	int             cmdpos = 0;
	int             currchar = 0;

	standout();
	for(i=0; i<len; i++)
	    outc(' ');
	standend();
	len--;
	buf[len] = '\0';
	move(y, x);
	edit_outs(buf);
	clen = currchar = strlen(buf);

	while (move(y, x + currchar), (ch = igetkey()) != '\r') {
	    switch (ch) {
	    case KEY_DOWN: case Ctrl('N'):
	    case KEY_UP:   case Ctrl('P'):
		strlcpy(lastcmd[cmdpos], buf, sizeof(lastcmd[0]));
		if (ch == KEY_UP || ch == Ctrl('P'))
		    cmdpos++;
		else
		    cmdpos += MAXLASTCMD - 1;
		cmdpos %= MAXLASTCMD;
		strlcpy(buf, lastcmd[cmdpos], len+1);

		move(y, x);	/* clrtoeof */
		for (i = 0; i <= clen; i++)
		    outc(' ');
		move(y, x);
		edit_outs(buf);
		clen = currchar = strlen(buf);
		break;
	    case KEY_LEFT:
		if (currchar)
		    --currchar;
		break;
	    case KEY_RIGHT:
		if (buf[currchar])
		    ++currchar;
		break;
	    case '\177':
	    case Ctrl('H'):
		if (currchar) {
		    currchar--;
		    clen--;
		    for (i = currchar; i <= clen; i++)
			buf[i] = buf[i + 1];
		    move(y, x + clen);
		    outc(' ');
		    move(y, x);
		    edit_outs(buf);
		}
		break;
	    case Ctrl('Y'):
		currchar = 0;
	    case Ctrl('K'):
		buf[currchar] = '\0';
		move(y, x + currchar);
		for (i = currchar; i < clen; i++)
		    outc(' ');
		clen = currchar;
		break;
	    case Ctrl('D'):
	    case KEY_DEL:
		if (buf[currchar]) {
		    clen--;
		    for (i = currchar; i <= clen; i++)
			buf[i] = buf[i + 1];
		    move(y, x + clen);
		    outc(' ');
		    move(y, x);
		    edit_outs(buf);
		}
		break;
	    case Ctrl('A'):
	    case KEY_HOME:
		currchar = 0;
		break;
	    case Ctrl('E'):
	    case KEY_END:
		currchar = clen;
		break;
	    default:
		if (isprint2(ch) && clen < len && x + clen < scr_cols) {
		    for (i = clen + 1; i > currchar; i--)
			buf[i] = buf[i - 1];
		    buf[currchar] = ch;
		    move(y, x + currchar);
		    edit_outs(buf + currchar);
		    currchar++;
		    clen++;
		}
		break;
	    }			/* end case */
	}			/* end while */

	if (clen > 1) {
	    strlcpy(lastcmd[0], buf, sizeof(lastcmd[0]));
	    memmove(lastcmd+1, lastcmd, (MAXLASTCMD-1)*sizeof(lastcmd[0]));
	}
	outc('\n');
	refresh();
    }
    if ((echo == LCECHO) && isupper(buf[0]))
	buf[0] = tolower(buf[0]);
#ifdef SUPPORT_GB
    if (echo == DOECHO && current_font_type == TYPE_GB) {
	// FIXME check buffer length
	strcpy(buf, hc_convert_str(buf, HC_GBtoBIG, HC_DO_SINGLE));
    }
#endif
    return clen;
}

/* Ptt */
int
getdata_buf(int line, int col, char *prompt, char *buf, int len, int echo)
{
    return oldgetdata(line, col, prompt, buf, len, echo);
}

char
getans(char *prompt)
{
    char            ans[5];

    getdata(b_lines, 0, prompt, ans, sizeof(ans), LCECHO);
    return ans[0];
}

int
getdata_str(int line, int col, char *prompt, char *buf, int len, int echo, char *defaultstr)
{
    strlcpy(buf, defaultstr, len);

    return oldgetdata(line, col, prompt, buf, len, echo);
}

int
getdata(int line, int col, char *prompt, char *buf, int len, int echo)
{
    buf[0] = 0;
    return oldgetdata(line, col, prompt, buf, len, echo);
}

int
rget(int x, char *prompt)
{
    register int    ch;

    move(x, 0);
    clrtobot();
    outs(prompt);
    refresh();

    ch = igetch();
    if (ch >= 'A' && ch <= 'Z')
	ch = tolower(ch);

    return ch;
}


int
igetkey()
{
    int             mode;
    int             ch, last;

    mode = last = 0;
    while (1) {
	ch = igetch();
	if (mode == 0) {
	    if (ch == KEY_ESC)
		mode = 1;
	    else
		return ch;	/* Normal Key */
	} else if (mode == 1) {	/* Escape sequence */
	    if (ch == '[' || ch == 'O')
		mode = 2;
	    else if (ch == '1' || ch == '4')
		mode = 3;
	    else {
		KEY_ESC_arg = ch;
		return KEY_ESC;
	    }
	} else if (mode == 2) {	/* Cursor key */
	    if (ch >= 'A' && ch <= 'D')
		return KEY_UP + (ch - 'A');
	    else if (ch >= '1' && ch <= '6')
		mode = 3;
	    else
		return ch;
	} else if (mode == 3) {	/* Ins Del Home End PgUp PgDn */
	    if (ch == '~')
		return KEY_HOME + (last - '1');
	    else
		return ch;
	}
	last = ch;
    }
}
