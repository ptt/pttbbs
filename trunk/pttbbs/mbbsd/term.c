/* $Id: term.c,v 1.4 2002/07/05 17:10:28 in2 Exp $ */
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

static void 
term_resize(int sig)
{
    struct winsize  newsize;
    screenline_t   *new_picture;

    signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */
    ioctl(0, TIOCGWINSZ, &newsize);
    if (newsize.ws_row > t_lines) {
	new_picture = (screenline_t *) calloc(newsize.ws_row,
					      sizeof(screenline_t));
	if (new_picture == NULL) {
	    syslog(LOG_ERR, "calloc(): %m");
	    return;
	}
	free(big_picture);
	big_picture = new_picture;
    }
    t_lines = newsize.ws_row;
    b_lines = t_lines - 1;
    p_lines = t_lines - 4;

    signal(SIGWINCH, term_resize);
}

int 
term_init()
{
    signal(SIGWINCH, term_resize);
    return YEA;
}

char            term_buf[32];

void 
do_move(int destcol, int destline)
{
    char            buf[16], *p;

    sprintf(buf, "\33[%d;%dH", destline + 1, destcol + 1);
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

    sprintf(buf, "\33[%d;%dr", top + 1, bottom + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void 
scroll_forward()
{
    ochar('\33');
    ochar('D');
}
