/* $Id$ */
#include "bbs.h"

int             tgetent(const char *bp, char *name);
char           *tgetstr(const char *id, char **area);
int             tgetflag(const char *id);
int             tgetnum(const char *id);
int             tputs(const char *str, int affcnt, int (*putc) (int));
char           *tparm(const char *str,...);
char           *tgoto(const char *cap, int col, int row);

static struct termios tty_state, tty_new;

/* ----------------------------------------------------- */
/* basic tty control                                     */
/* ----------------------------------------------------- */
void
init_tty()
{
    if (tcgetattr(1, &tty_state) < 0) {
	syslog(LOG_ERR, "tcgetattr(): %m");
	return;
    }
    memcpy(&tty_new, &tty_state, sizeof(tty_new));
    tty_new.c_lflag &= ~(ICANON | ECHO | ISIG);
    /*
     * tty_new.c_cc[VTIME] = 0; tty_new.c_cc[VMIN] = 1;
     */
    tcsetattr(1, TCSANOW, &tty_new);
    system("stty raw -echo");
}

/* ----------------------------------------------------- */
/* init tty control code                                 */
/* ----------------------------------------------------- */


#define TERMCOMSIZE (40)

#if 0
static char    *outp;
static int     *outlp;

static int
outcf(int ch)
{
    if (*outlp < TERMCOMSIZE) {
	(*outlp)++;
	*outp++ = ch;
    }
    return 0;
}
#endif

static inline void
term_resize(int row, int col){
    screenline_t   *new_picture;

    /* make sure reasonable size */
    row = MAX(24, MIN(100, row));
    col = MAX(80, MIN(200, col));

    if (big_picture != NULL && row > t_lines) {
	new_picture = (screenline_t *) calloc(row, sizeof(screenline_t));
	if (new_picture == NULL) {
	    syslog(LOG_ERR, "calloc(): %m");
	    return;
	}
	memcpy(new_picture, big_picture, t_lines * sizeof(screenline_t));
	free(big_picture);
	big_picture = new_picture;
    }
    t_lines = row;
    t_columns = col;
    b_lines = t_lines - 1;
    p_lines = t_lines - 4;
}

static void
term_resize_catch(int sig)
{
    struct winsize  newsize;

    signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */
    ioctl(0, TIOCGWINSZ, &newsize);

    term_resize(newsize.ws_row, newsize.ws_col);

    signal(SIGWINCH, term_resize_catch);
}

#ifndef SKIP_TELNET_CONTROL_SIGNAL
void
telnet_parse_size(const unsigned char* msg){
    /* msg[0] == IAC, msg[1] == SB, msg[2] = TELOPT_NAWS */
    int i = 4, row, col;

    col = msg[3] << 8;
    if(msg[i] == 0xff)
	++i; /* avoid escaped 0xff */
    col |= msg[i++];

    row = msg[i++] << 8;
    if(msg[i] == 0xff)
	++i;
    row |= msg[i];
    term_resize(row, col);
}
#endif

int
term_init()
{
    signal(SIGWINCH, term_resize_catch);
    return YEA;
}

char            term_buf[32];

void
do_move(int destcol, int destline)
{
    char            buf[16], *p;

    snprintf(buf, sizeof(buf), "\33[%d;%dH", destline + 1, destcol + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void
save_cursor()
{
    ochar('\33');
    ochar('7');
}

void
restore_cursor()
{
    ochar('\33');
    ochar('8');
}

void
change_scroll_range(int top, int bottom)
{
    char            buf[16], *p;

    snprintf(buf, sizeof(buf), "\33[%d;%dr", top + 1, bottom + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void
scroll_forward()
{
    ochar('\33');
    ochar('D');
}
