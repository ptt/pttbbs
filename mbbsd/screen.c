#include "bbs.h"

#if !defined(USE_PFTERM)

#define o_clear()     output(clearbuf,clearbuflen)
#define o_cleol()     output(cleolbuf,cleolbuflen)
#define o_scrollrev() output(scrollrev,scrollrevlen)
#define o_standup()   output(strtstandout,strtstandoutlen)
#define o_standdown() output(endstandout,endstandoutlen)

static unsigned short cur_ln = 0, cur_col = 0;
static unsigned char docls;
static unsigned char standing = NA;
static int      scrollcnt, tc_col, tc_line;
static unsigned char _typeahead = 1;

#define MODIFIED (1)		/* if line has been modifed, screen output */
#define STANDOUT (2)		/* if this line has a standout region */

static
screenline_t* GetLine(int i) {
    return &big_picture[(i + roll) % t_lines];
}

static
screenline_t* GetCurrentLine() {
    return GetLine(cur_ln);
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

void
do_move(int destcol, int destline)
{
    char            buf[16], *p;

    snprintf(buf, sizeof(buf), ANSI_MOVETO(%d,%d), destline + 1, destcol + 1);
    for (p = buf; *p; p++)
	ochar(*p);
}

void
do_save_cursor(void)
{
    ochar(ESC_CHR);
    ochar('7');
}

void
do_restore_cursor(void)
{
    ochar(ESC_CHR);
    ochar('8');
}


void
initscr(void)
{
    if (!big_picture) {
	big_picture = (screenline_t *) calloc(t_lines, sizeof(screenline_t));
	docls = YEA;
    }
}

int
resizeterm_within(int rows, int cols, int rows_full GCC_UNUSED, int cols_full GCC_UNUSED)
{
    // FIXME: rows_full instead of b_lines should be used for forward scrolling
    if (rows != t_lines || cols != t_columns) {
        return resizeterm(rows, cols);
    }
    return 0;
}

int
resizeterm(int w, int h)
{
    screenline_t   *new_picture;

    /* make sure reasonable size */
    h = MAX(24, MIN(100, h));
    w = MAX(80, MIN(200, w));

    if (h > t_lines && big_picture) {
	new_picture = (screenline_t *)
		calloc(h, sizeof(screenline_t));
	if (new_picture == NULL) {
	    syslog(LOG_ERR, "calloc(): %m");
	    return 0;
	}
	memcpy(new_picture, big_picture, t_lines * sizeof(screenline_t));
	free(big_picture);
	big_picture = new_picture;
	return 1;
    }
    return 0;
}

void
move(int y, int x)
{
    if (y < 0) y = 0;
    if (y >= t_lines) y = t_lines -1;
    if (x < 0) x = 0;
    if (x >= ANSILINELEN) x = ANSILINELEN -1;
    // assert(y>=0);
    // assert(x>=0);
    cur_col = x;
    cur_ln = y;
}

void
move_ansi(int y, int x)
{
    // take ANSI length in consideration
    register screenline_t *slp;
    if (y < 0) y = 0;
    if (y >= t_lines) y = t_lines -1;
    if (x < 0) x = 0;
    if (x >= ANSILINELEN) x = ANSILINELEN -1;

    cur_ln = y;
    cur_col = x;

    if (y >= t_lines || x < 1)
	return;

    slp = GetLine(y);
    if (slp->len < 1)
	return;

    slp->data[slp->len] = 0;
    x += (strlen((char*)slp->data) - strlen_noansi((char*)slp->data));
    cur_col = x;
}

void
getyx(int *y, int *x)
{
    *y = cur_ln;
    *x = cur_col;
}

void
getyx_ansi(int *py, int *px)
{
    // take ANSI length in consideration
    register screenline_t *slp;
    int y = cur_ln,  x = cur_col;
    char c = 0;

    if (y < 0) y = 0;
    if (y >= t_lines) y = t_lines -1;
    if (x < 0) x = 0;
    if (x >= ANSILINELEN) x = ANSILINELEN -1;

    *py = y; *px = x;

    if (y >= t_lines || x < 1)
	return;

    slp = GetLine(y);
    if (slp->len < 1)
	return;
    c = slp->data[x];
    slp->data[x] = 0;
    *px -= (strlen((char*)slp->data) - strlen_noansi((char*)slp->data));
    slp->data[x] = c;
}

static void
rel_move(int was_col, int was_ln, int new_col, int new_ln)
{
    if (new_ln >= t_lines || new_col >= t_columns)
	return;

    tc_col = new_col;
    tc_line = new_ln;
    if (new_col == 0) {
	if (new_ln == was_ln) {
	    if (was_col)
		ochar('\r');
	    return;
	} else if (new_ln == was_ln + 1) {
	    ochar('\n');
	    if (was_col)
		ochar('\r');
	    return;
	}
    }
    if (new_ln == was_ln) {
	if (was_col == new_col)
	    return;

	if (new_col == was_col - 1) {
	    ochar(Ctrl('H'));
	    return;
	}
    }
    do_move(new_col, new_ln);
}

static void
standoutput(const char *buf, int ds, int de, int sso, int eso)
{
    int             st_start, st_end;

    if (eso <= ds || sso >= de) {
	output(buf + ds, de - ds);
    } else {
	st_start = MAX(sso, ds);
	st_end = MIN(eso, de);
	if (sso > ds)
	    output(buf + ds, sso - ds);
	o_standup();
	output(buf + st_start, st_end - st_start);
	o_standdown();
	if (de > eso)
	    output(buf + eso, de - eso);
    }
}

void
redrawwin(void)
{
    register screenline_t *bp;
    register int    i, j;
    int len;

    o_clear();
    for (tc_col = tc_line = i = 0, j = roll; i < t_lines; i++, j++) {
	if (j >= t_lines)
	    j = 0;
	bp = &big_picture[j];
	if ((len = bp->len)) {
	    rel_move(tc_col, tc_line, 0, i);

#ifdef DBCSAWARE
	    if (!(bp->mode & STANDOUT) &&
		    (HasUserFlag(UF_DBCS_NOINTRESC)) &&
		    DBCS_RemoveIntrEscape(bp->data, &len))
	    {
		// if anything changed, dirty whole line.
		bp->len = len;
	    }
#endif // DBCSAWARE

	    if (bp->mode & STANDOUT) {
		standoutput((char *)bp->data, 0, len, bp->sso, bp->eso);
	    }
	    else
		output((char *)bp->data, len);
	    tc_col += len;
	    if (tc_col >= t_columns) {
		if (automargins)
		    tc_col = t_columns - 1;
		else {
		    tc_col -= t_columns;
		    tc_line++;
		    if (tc_line >= t_lines)
			tc_line = b_lines;
		}
	    }
	    bp->mode &= ~(MODIFIED);
	    bp->oldlen = len;
	}
    }
    rel_move(tc_col, tc_line, cur_col, cur_ln);
    docls = scrollcnt = 0;
    oflush();
}

int
typeahead(int fd)
{
    switch(fd)
    {
	case TYPEAHEAD_NONE:
	    _typeahead = 0;
	    break;
	case TYPEAHEAD_STDIN:
	    _typeahead = 1;
	    break;
	default: // shall never reach here
	    assert(NULL);
	    break;
    }
    return 0;
}

void
refresh(void)
{
    if (_typeahead && vkey_is_typeahead())
	return;
    doupdate();
}

void
doupdate(void)
{
    /* TODO remove unnecessary refresh() call, to save CPU time */
    register screenline_t *bp = big_picture;
    register int    i, j;
    int len;
    if ((docls) || (abs(scrollcnt) >= (t_lines - 3))) {
	redrawwin();
	return;
    }
    if (scrollcnt < 0) {
	if (!scrollrevlen) {
	    redrawwin();
	    return;
	}
	rel_move(tc_col, tc_line, 0, 0);
	do {
	    o_scrollrev();
	} while (++scrollcnt);
    } else if (scrollcnt > 0) {
	rel_move(tc_col, tc_line, 0, b_lines);
	do {
	    ochar('\n');
	} while (--scrollcnt);
    }
    for (i = 0, j = roll; i < t_lines; i++, j++) {
	if (j >= t_lines)
	    j = 0;
	bp = &big_picture[j];
	len = bp->len;

	if (bp->mode & MODIFIED && bp->smod < len)
	{
	    bp->mode &= ~(MODIFIED);

#ifdef DBCSAWARE
	    if (!(bp->mode & STANDOUT) &&
		(HasUserFlag(UF_DBCS_NOINTRESC)) &&
		DBCS_RemoveIntrEscape(bp->data, &len))
	    {
		// if anything changed, dirty whole line.
		bp->len = len;
		bp->smod = 0; bp->emod = len;
	    }
#endif // DBCSAWARE

// disable this if you encounter some bugs.
// bug history:
// (1) input number (goto) in bbs list (search_num) [solved: search_num merged to vget]
// (2) some empty lines becomes weird (eg, b_config) [not seen anymore?]
#if 1
	    if (bp->smod > 0)
	    {
		// more effort to determine ANSI smod
		int iesc;
		for (iesc = bp->smod-1; iesc >= 0; iesc--)
		{
		    if (bp->data[iesc] == ESC_CHR)
		    {
			bp->smod = 0;// iesc;
			bp->emod =len -1;
			break;
		    }
		}
	    }
#endif

	    if (bp->emod >= len)
		bp->emod = len - 1;
	    rel_move(tc_col, tc_line, bp->smod, i);

	    if (bp->mode & STANDOUT)
		standoutput((char *)bp->data, bp->smod, bp->emod + 1,
			    bp->sso, bp->eso);
	    else
		output((char *)&bp->data[bp->smod], bp->emod - bp->smod + 1);
	    tc_col = bp->emod + 1;
	    if (tc_col >= t_columns) {
		if (automargins)
		    tc_col = t_columns - 1;
		else {
		    tc_col -= t_columns;
		    tc_line++;
		    if (tc_line >= t_lines)
			tc_line = b_lines;
		}
	    }
	}
	if (bp->oldlen > len) {
	    /* XXX len/oldlen also count the length of escape sequence,
	     * before we fix it, we must print ANSI_CLRTOEND everywhere */
	    rel_move(tc_col, tc_line, len, i);
	    o_cleol();
	}
	bp->oldlen = len;
    }

    rel_move(tc_col, tc_line, cur_col, cur_ln);

    oflush();
}

void
clear(void)
{
    register screenline_t *slp;

    register int    i;

    docls = YEA;
    cur_col = cur_ln = roll = 0;
    for(i=0; i<t_lines; i++) {
	slp = &big_picture[i];
	slp->mode = slp->len = slp->oldlen = 0;
    }
}

void
clrtoeol(void)
{
    register screenline_t *slp = GetCurrentLine();
    register int    ln;

    standing = NA;

    if (cur_col <= slp->sso)
	slp->mode &= ~STANDOUT;

    /*
    if (cur_col == 0) // TODO and contains ANSI
    {
	// workaround poor ANSI issue
	size_t sz = (slp->len > slp->oldlen) ? slp->len : slp->oldlen;
	sz = (sz < ANSILINELEN) ? sz : ANSILINELEN;
	memset(slp->data, ' ', sz);
	slp->len = 0;
	return;
    }
    */

    if (cur_col > slp->oldlen) {
	for (ln = slp->len; ln <= cur_col; ln++)
	    slp->data[ln] = ' ';
    }
    if (cur_col < slp->oldlen) {
	for (ln = slp->len; ln >= cur_col; ln--)
	    slp->data[ln] = ' ';
    }
    slp->len = cur_col;
}


void newwin	(int nlines, int ncols, int y, int x)
{
    int i=0, oy, ox;
    getyx(&oy, &ox);

    while (nlines-- > 0)
    {
	move_ansi(y++, x);
	for (i = 0; i < ncols; i++)
	    outc(' ');
    }
    move(oy, ox);
}

/**
 * �q�ثe�����(scr_ln) clear ��� line ��
 */
void
clrtoln(int line)
{
    register screenline_t *slp;
    register int    i, j;

    for (i = cur_ln, j = i + roll; i < line; i++, j++) {
	if (j >= t_lines)
	    j -= t_lines;
	slp = &big_picture[j];
	slp->mode = slp->len = 0;
	if (slp->oldlen)
	    slp->oldlen = SCR_COLS;
    }
}

/**
 * �q�ثe�����(scr_ln) clear �쩳
 */
inline void
clrtobot(void)
{
    clrtoln(t_lines);
}

void
outc(unsigned char c)
{
    register screenline_t *slp = GetCurrentLine();
    register int    i;

    // 0xFF is invalid for most cases (even DBCS),
    if (c == 0xFF || c == 0x00)
	return;

    if (c == '\t') {
	int x, y;

	getyx_ansi(&y, &x);

	if (x % 8 == 0)
	    i = 8;
	else
	    i = 8 - (x % 8);

	for (;i > 0; i--)
	    outc(' ');

	return;
    }

    if (c == '\n' || c == '\r') {
	if (standing) {
	    slp->eso = MAX(slp->eso, cur_col);
	    standing = NA;
	}
	if ((i = cur_col - slp->len) > 0)
	    memset(&slp->data[slp->len], ' ', i + 1);
	slp->len = cur_col;
	cur_col = 0;
	if (cur_ln < t_lines)
	    cur_ln++;
	return;
    }

    if (cur_col >= slp->len) {
	for (i = slp->len; i < cur_col; i++)
	    slp->data[i] = ' ';
	slp->data[cur_col] = '\0';
	slp->len = cur_col + 1;
    }

    // Escape processing is a mess here.  If we don't always invalid ESC,
    // attributes will be not updated if some ouptut generates ESC on same
    // position (with extra '[m' on screen); if we do that, cross-line outputs
    // also corrupted, or more: #1FX6qLcO (PttCurrent).  This has been turned
    // on/off several times - and I still don't know how to make a perfect
    // workaround.  Well, we probably still need to call SOLVE_ANSI_CACHE
    // everywhere...
    if (slp->data[cur_col] != c) {
	slp->data[cur_col] = c;
	if (!(slp->mode & MODIFIED))
	    slp->smod = slp->emod = cur_col;
	slp->mode |= MODIFIED;
	if (cur_col > slp->emod)
	    slp->emod = cur_col;
	if (cur_col < slp->smod)
	    slp->smod = cur_col;
#ifdef USE_ESC_HACK
        // TODO If anything before cur_col has ESCAPE or if c is the second byte
        // of a DBCS, re-invalidate whole line.
        for (i = 0; i < cur_col; i++) {
            if (slp->data[i] != ESC_CHR)
                continue;
            slp->smod = 0;
        }
#endif
    }

    if(cur_col < SCR_COLS)
        cur_col++;
}

void
outs(const char *str)
{
    if (!str)
	return;
    while (*str) {
	outc(*str++);
    }
}

void
outns(const char *str, int n)
{
    if (!str)
	return;
    while (*str && n-- > 0) {
	outc(*str++);
    }
}

void
outstr(const char *str)
{
    // XXX TODO cannot prepare DBCS-ready environment?

    outs(str);
}

void
addch(unsigned char c)
{
    outc(c);
}

void
addstr(const char *s)
{
    outs(s);
}

void
addnstr(const char *s, int n)
{
    outns(s, n);
}

void
addstring(const char *s)
{
    outs(s);
}


void
scroll(void)
{
    scrollcnt++;
    if (++roll >= t_lines)
	roll = 0;
    move(b_lines, 0);
    clrtoeol();
}

void
rscroll(void)
{
    scrollcnt--;
    if (--roll < 0)
	roll = b_lines;
    move(0, 0);
    clrtoeol();
}

void
region_scroll_up(int top, int bottom)
{
    int             i;

    if (top > bottom) {
	i = top;
	top = bottom;
	bottom = i;
    }
    if (top < 0 || bottom >= t_lines)
	return;

    for (i = top; i < bottom; i++)
	big_picture[i] = big_picture[i + 1];
    memset(big_picture + i, 0, sizeof(*big_picture));
    memset(big_picture[i].data, ' ', SCR_COLS);
    do_save_cursor();
    change_scroll_range(top, bottom);
    do_move(0, bottom);
    scroll_forward();
    change_scroll_range(0, t_lines - 1);
    do_restore_cursor();
    refresh();
}

// readback
int
instr(char *str)
{
    register screenline_t *slp = GetCurrentLine();
    *str = 0;
    if (!slp)
	return 0;
    slp->data[slp->len] = 0;
    strip_ansi(str, (char*)slp->data, STRIP_ALL);
    return strlen(str);
}

int
innstr(char *str, int n)
{
    register screenline_t *slp = GetCurrentLine();
    char buf[ANSILINELEN];
    *str = 0;
    if (!slp)
	return 0;
    slp->data[slp->len] = 0;
    strip_ansi(buf, (char*)slp->data, STRIP_ALL);
    buf[ANSILINELEN-1] = 0;
    strlcpy(str, buf, n);
    return strlen(str);
}

int
inansistr(char *str, int n)
{
    register screenline_t *slp = GetCurrentLine();
    *str = 0;
    if (!slp)
	return 0;
    slp->data[slp->len] = 0;
    strlcpy(str, (char*)slp->data, n);
    return strlen(str);
}

// level:
// -1 - bold out
//  0 - dark text
//  1 - text
//  2 - no highlight (not implemented)
void
grayout(int y, int end, int level)
{
    register screenline_t *slp = NULL;
    char buf[ANSILINELEN];
    int i = 0;

    if (y < 0) y = 0;
    if (end > b_lines) end = b_lines;

    // TODO change to y <= end someday
    // loop lines
    for (; y <= end; y ++)
    {
	// modify by scroll
	i = y + roll;
	if (i < 0)
	    i += t_lines;
	else if (i >= t_lines)
	    i %= t_lines;

	slp = &big_picture[i];

	if (slp->len < 1)
	    continue;

	if (slp->len >= ANSILINELEN)
	    slp->len = ANSILINELEN -1;

	// tweak slp
	slp->data[slp->len] = 0;
	slp->mode &= ~STANDOUT;
	slp->mode |= MODIFIED;
	slp->oldlen = 0;
	slp->sso = slp->eso = 0;
	slp->smod = 0;

	// make slp->data a pure string
	for (i=0; i<slp->len; i++)
	{
	    if (!slp->data[i])
		slp->data[i] = ' ';
	}

	slp->len = strip_ansi(buf, (char*)slp->data, STRIP_ALL);
	buf[slp->len] = 0;

	switch(level)
	{
	    case GRAYOUT_DARK: // dark text
	    case GRAYOUT_BOLD:// bold text
		// basically, in current system slp->data will
		// not exceed t_columns. buffer overflow is impossible.
		// but to make it more robust, let's quick check here.
		// of course, t_columns should always be far smaller.
		if (strlen((char*)slp->data) > (size_t)t_columns)
		    slp->data[t_columns] = 0;
		strcpy((char*)slp->data,
			level < 0 ? ANSI_COLOR(1) : ANSI_COLOR(1;30;40));
		strcat((char*)slp->data, buf);
		strcat((char*)slp->data, ANSI_RESET ANSI_CLRTOEND);
		slp->len = strlen((char*)slp->data);
		break;

	    case GRAYOUT_NORM: // Plain text
		memcpy(slp->data, buf, slp->len + 1);
		break;
	}
	slp->emod = slp->len -1;
    }
}

static size_t screen_backupsize(int len, const screenline_t *bp)
{
    int i;
    size_t sum = 0;
    for(i = 0; i < len; i++)
	sum += ((char*)&bp[i].data - (char*)&bp[i]) + bp[i].len;
    return sum;
}

// XXX TODO FIXME
// scrolling is not well-supported ...

void scr_dump(screen_backup_t *old)
{
    int i;
    size_t offset = 0;
    void *buf;
    screenline_t* bp = big_picture;

    buf = old->raw_memory = malloc(screen_backupsize(t_lines, big_picture));

    old->col = t_columns;
    old->row = t_lines;
    old->roll = roll;
    getyx(&old->y, &old->x);

    for(i = 0; i < t_lines; i++) {
	/* backup header */
	memcpy((char*)buf + offset, &bp[i], ((char*)&bp[i].data - (char*)&bp[i]));
	offset += ((char*)&bp[i].data - (char*)&bp[i]);

	/* backup body */
	memcpy((char*)buf + offset, &bp[i].data, bp[i].len);
	offset += bp[i].len;
    }
}

void scr_restore(const screen_backup_t *old)
{
    int i;
    size_t offset=0;
    void *buf = old->raw_memory;
    screenline_t* bp = big_picture;
    const int len = MIN(old->row, t_lines);

    // XXX simple step to solve scrolling issue...
    roll = old->roll;

    for(i = 0; i < len; i++) {
	/* restore header */
	memcpy(&bp[i], (char*)buf + offset, ((char*)&bp[i].data - (char*)&bp[i]));
	offset += ((char*)&bp[i].data - (char*)&bp[i]);

	/* restore body */
	memcpy(&bp[i].data, (char*)buf + offset, bp[i].len);
	offset += bp[i].len;
    }

    free(old->raw_memory);
    move(old->y, old->x);
    redrawwin();
}

#endif //  !defined(USE_PFTERM)

/* vim:sw=4
 */
