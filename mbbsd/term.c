/* $Id$ */
#include "bbs.h"

/* ----------------------------------------------------- */
/* basic tty control                                     */
/* ----------------------------------------------------- */
void
init_tty(void)
{
    struct termios tty_state, tty_new;

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

static void
sig_term_resize(int sig)
{
    struct winsize  newsize;
    Signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */
    ioctl(0, TIOCGWINSZ, &newsize);
    term_resize(newsize.ws_col, newsize.ws_row);
}

void term_resize(int w, int h)
{
    Signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */

    /* make sure reasonable size */
    h = MAX(24, MIN(100, h));
    w = MAX(80, MIN(200, w));

    // invoke terminal system resize
    resizeterm(h, w);

    t_lines = h;
    t_columns = w;
    scr_lns = t_lines;	/* XXX: scr_lns 跟 t_lines 有什麼不同, 為何分成兩個 */
    b_lines = t_lines - 1;
    p_lines = t_lines - 4;

    Signal(SIGWINCH, sig_term_resize);
}

int
term_init(void)
{
    Signal(SIGWINCH, sig_term_resize);
    return YEA;
}

void
do_move(int destcol, int destline)
{
    char            buf[16], *p;

    snprintf(buf, sizeof(buf), ANSI_MOVETO(%d,%d), destline + 1, destcol + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void
save_cursor(void)
{
    ochar(ESC_CHR);
    ochar('7');
}

void
restore_cursor(void)
{
    ochar(ESC_CHR);
    ochar('8');
}

void
change_scroll_range(int top, int bottom)
{
    char            buf[16], *p;

    snprintf(buf, sizeof(buf), ESC_STR "[%d;%dr", top + 1, bottom + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void
scroll_forward(void)
{
    ochar(ESC_CHR);
    ochar('D');
}
