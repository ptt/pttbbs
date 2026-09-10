#include "bbs.h"
#include <termios.h>

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
#if 1
    cfmakeraw(&tty_new);
    tty_new.c_cflag &= ~(CSIZE|PARENB);
    tty_new.c_cflag |= CS8;
    tcsetattr(1, TCSANOW, &tty_new);
#else
    tcsetattr(1, TCSANOW, &tty_new);
    system("stty raw -echo");
#endif
}

/* ----------------------------------------------------- */
/* init tty control code                                 */
/* ----------------------------------------------------- */


#define TERMCOMSIZE (40)

static void
sig_term_resize(int sig GCC_UNUSED)
{
    struct winsize  newsize;
    Signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */
    ioctl(0, TIOCGWINSZ, &newsize);
    term_resize(newsize.ws_col, newsize.ws_row);
}

void term_resize(int w, int h)
{
    int dorefresh = 0;
    Signal(SIGWINCH, SIG_IGN);	/* Don't bother me! */


    /* make sure reasonable size */
    int h_crop = MAX(24, MIN(100, h));
    int w_crop = MAX(80, MIN(200, w));

    // invoke terminal system resize
    resizeterm_within(h_crop, w_crop, h, w);
    if (w_crop != t_columns || h_crop != t_lines)
    {
	dorefresh = 1;
    }
    t_lines = h_crop;
    t_columns = w_crop;
    b_lines = t_lines - 1;
    p_lines = t_lines - 4;

    Signal(SIGWINCH, sig_term_resize);
    if (dorefresh)
    {
	redrawwin();
	refresh();
    }
}

static int current_mouse_mode = MOUSE_MODE_NONE;

#define ENABLE_MOUSE_CLICK      ESC_STR "[?1000h"
#define ENABLE_MOUSE_DRAG       ESC_STR "[?1002h"
#define ENABLE_MOUSE_MOTION     ESC_STR "[?1003h"
#define ENABLE_MOUSE_SGR        ESC_STR "[?1006h"
#define DISABLE_MOUSE_CLICK     ESC_STR "[?1000l"
#define DISABLE_MOUSE_DRAG      ESC_STR "[?1002l"
#define DISABLE_MOUSE_MOTION    ESC_STR "[?1003l"
#define DISABLE_MOUSE_SGR       ESC_STR "[?1006l"

void
term_enable_mouse(int mode)
{
    const char *seq = "";
    current_mouse_mode = mode;
    if (mode == MOUSE_MODE_NONE || !HasUserFlag(UF_MOUSE)) {
        // Disable all mouse reporting
        seq = DISABLE_MOUSE_CLICK
              DISABLE_MOUSE_DRAG
              DISABLE_MOUSE_MOTION
              DISABLE_MOUSE_SGR;
    } else if (mode == MOUSE_MODE_TRACK) {
        // MOUSE_MODE_TRACK (DECSET 1003 + 1006 SGR):
        // Any-event tracking (reports all hover motion, clicks, wheel)
        seq = ENABLE_MOUSE_MOTION
              ENABLE_MOUSE_SGR;
    } else if (mode == MOUSE_MODE_DRAG) {
        // MOUSE_MODE_DRAG (DECSET 1002 + 1006 SGR):
        // Button-event tracking (reports press, drag motion, release)
        seq = DISABLE_MOUSE_MOTION
              ENABLE_MOUSE_DRAG
              ENABLE_MOUSE_SGR;
    } else {
        // MOUSE_MODE_CLICK (DECSET 1000 + 1006 SGR):
        // Normal tracking (reports press, release, wheel; no hover motion)
        seq = DISABLE_MOUSE_MOTION
              ENABLE_MOUSE_CLICK
              ENABLE_MOUSE_SGR;
    }
    write(1, seq, strlen(seq));
}

int
term_get_mouse_mode(void)
{
    return current_mouse_mode;
}

void
term_uninit(void)
{
    term_enable_mouse(MOUSE_MODE_NONE);
}

int
term_init(void)
{
    Signal(SIGWINCH, sig_term_resize);
    term_enable_mouse(MOUSE_MODE_CLICK);
    return YEA;
}

void
bell(void)
{
    refresh();
    const char c = Ctrl('G');
    write(1, &c, 1);
}

