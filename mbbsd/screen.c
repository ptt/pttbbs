/* $Id$ */
#include "bbs.h"

#define o_clear()     output(clearbuf,clearbuflen)
#define o_cleol()     output(cleolbuf,cleolbuflen)
#define o_scrollrev() output(scrollrev,scrollrevlen)
#define o_standup()   output(strtstandout,strtstandoutlen)
#define o_standdown() output(endstandout,endstandoutlen)

static unsigned char cur_ln = 0, cur_col = 0;
static unsigned char docls;
static unsigned char standing = NA;
static int      scrollcnt, tc_col, tc_line;

#define MODIFIED (1)		/* if line has been modifed, screen output */
#define STANDOUT (2)		/* if this line has a standout region */


void
initscr(void)
{
    if (!big_picture) {
	big_picture = (screenline_t *) calloc(scr_lns, sizeof(screenline_t));
	docls = YEA;
    }
}

void
move(int y, int x)
{
    cur_col = x;
    cur_ln = y;
}

void
getyx(int *y, int *x)
{
    *y = cur_ln;
    *x = cur_col;
}

static inline
screenline_t* GetCurrentLine(){
    register int i = cur_ln + roll;
    if(i >= scr_lns)
	i -= scr_lns;
    return &big_picture[i];
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
redoscr(void)
{
    register screenline_t *bp;
    register int    i, j, len;

    o_clear();
    for (tc_col = tc_line = i = 0, j = roll; i < scr_lns; i++, j++) {
	if (j >= scr_lns)
	    j = 0;
	bp = &big_picture[j];
	if ((len = bp->len)) {
	    rel_move(tc_col, tc_line, 0, i);
	    if (bp->mode & STANDOUT) {
		standoutput((char *)bp->data, 0, len, bp->sso, bp->eso);
	    }
	    else
		output((char *)bp->data, len);
	    tc_col += len;
	    if (tc_col >= t_columns) {
		/* XXX Is this code right? */
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

void
redoln(void)
{
    screenline_t *slp = GetCurrentLine();
    int len, mode;

    len = slp->len;
    rel_move(tc_col, tc_line, 0, cur_ln);
    if (len)
    {    
	if ((mode = slp->mode) & STANDOUT)
	    standoutput((char*)slp->data, 0, len, slp->sso, slp->eso);
	else
	    output((char*)slp->data, len);

	slp->mode = mode & ~(MODIFIED);

	slp->oldlen = tc_col = len;
    }
    else
	clrtoeol();
    rel_move(tc_col, tc_line, cur_col, cur_ln);
    oflush();
}

void
refresh(void)
{
    /* TODO remove unnecessary refresh() call, to save CPU time */
    register screenline_t *bp = big_picture;
    register int    i, j, len;
    if (num_in_buf())
	return;

    if ((docls) || (abs(scrollcnt) >= (scr_lns - 3))) {
	redoscr();
	return;
    }
    if (scrollcnt < 0) {
	if (!scrollrevlen) {
	    redoscr();
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
    for (i = 0, j = roll; i < scr_lns; i++, j++) {
	if (j >= scr_lns)
	    j = 0;
	bp = &big_picture[j];
	len = bp->len;
	if (bp->mode & MODIFIED && bp->smod < len) {
	    bp->mode &= ~(MODIFIED);
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
		/* XXX Is this code right? */
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
	     * before we fix it, we must print \033[K everywhere */
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
    for(i=0; i<scr_lns; i++) {
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

/**
 * 從目前的行數(scr_ln) clear 到第 line 行
 */
void
clrtoline(int line)
{
    register screenline_t *slp;
    register int    i, j;

    for (i = cur_ln, j = i + roll; i < line; i++, j++) {
	if (j >= scr_lns)
	    j -= scr_lns;
	slp = &big_picture[j];
	slp->mode = slp->len = 0;
	if (slp->oldlen)
	    slp->oldlen = 255;
    }
}

/**
 * 從目前的行數(scr_ln) clear 到底
 */
inline void
clrtobot(void)
{
    clrtoline(scr_lns);
}

void
outc(unsigned char c)
{
    register screenline_t *slp = GetCurrentLine();
    register int    i;

    if (c == '\n' || c == '\r') {
	if (standing) {
	    slp->eso = MAX(slp->eso, cur_col);
	    standing = NA;
	}
	if ((i = cur_col - slp->len) > 0)
	    memset(&slp->data[slp->len], ' ', i + 1);
	slp->len = cur_col;
	cur_col = 0;
	if (cur_ln < scr_lns)
	    cur_ln++;
	return;
    }
    /*
     * else if(c != '\033' && !isprint2(c)) { c = '*'; //substitute a '*' for
     * non-printable }
     */
    if (cur_col >= slp->len) {
	for (i = slp->len; i < cur_col; i++)
	    slp->data[i] = ' ';
	slp->data[cur_col] = '\0';
	slp->len = cur_col + 1;
    }
    if (slp->data[cur_col] != c) {
	slp->data[cur_col] = c;
	if (!(slp->mode & MODIFIED))
	    slp->smod = slp->emod = cur_col;
	slp->mode |= MODIFIED;
	if (cur_col > slp->emod)
	    slp->emod = cur_col;
	if (cur_col < slp->smod)
	    slp->smod = cur_col;
    }
#if 1
    ++cur_col;
#else
    /* this comparison is always false (cur_col is unsigned char and scr_cols
     * is 511). */
    if (++cur_col >= scr_cols) {
	if (standing && (slp->mode & STANDOUT)) {
	    standing = 0;
	    slp->eso = MAX(slp->eso, cur_col);
	}
	cur_col = 0;
	if (cur_ln < scr_lns)
	    cur_ln++;
    }
#endif
}

/**
 * Just like outs, but print out '*' instead of 27(decimal) in the given string.
 *
 * FIXME column could not start from 0
 */
void
edit_outs(const char *text)
{
    register int    column = 0;
    register char   ch;
    while ((ch = *text++) && (++column < t_columns))
	outc(ch == 27 ? '*' : ch);
}

void
edit_outs_n(const char *text, int n)
{
    register int    column = 0;
    register char   ch;
    while ((ch = *text++) && n-- && (++column < t_columns))
	outc(ch == 27 ? '*' : ch);
}

void
outs(const char *str)
{
    while (*str) {
	outc(*str++);
    }
}

void
outs_n(const char *str, int n)
{
    while (*str && n--) {
	outc(*str++);
    }
}

/* Jaky */
void
out_lines(const char *str, int line)
{
    while (*str && line) {
	outc(*str);
	if (*str == '\n')
	    line--;
	str++;
    }
}

void
outmsg(const char *msg)
{
    move(b_lines, 0);
    clrtoeol();
    outs(msg);
}

void
prints(const char *fmt,...)
{
    va_list         args;
    char            buff[1024];

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);
    outs(buff);
}

void
mouts(int y, int x, const char *str)
{
    move(y, x);
    clrtoeol();
    outs(str);
}

void
scroll(void)
{
    scrollcnt++;
    if (++roll >= scr_lns)
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
    if (top < 0 || bottom >= scr_lns)
	return;

    for (i = top; i < bottom; i++)
	big_picture[i] = big_picture[i + 1];
    memset(big_picture + i, 0, sizeof(*big_picture));
    memset(big_picture[i].data, ' ', scr_cols);
    save_cursor();
    change_scroll_range(top, bottom);
    do_move(0, bottom);
    scroll_forward();
    change_scroll_range(0, scr_lns - 1);
    restore_cursor();
    refresh();
}

void
standout(void)
{
    if (!standing && strtstandoutlen) {
	register screenline_t *slp;

	slp = GetCurrentLine();
	standing = YEA;
	slp->sso = slp->eso = cur_col;
	slp->mode |= STANDOUT;
    }
}

void
standend(void)
{
    if (standing && strtstandoutlen) {
	register screenline_t *slp;

	slp = GetCurrentLine();
	standing = NA;
	slp->eso = MAX(slp->eso, cur_col);
    }
}

// XXX race: signal handler write to screen, data larger than allocated buf
void screen_backup(int len, const screenline_t *bp, void *buf)
{
    int i;
    size_t offset=0;
    for(i=0;i<len;i++) {
	memcpy((char*)buf+offset, &bp[i], ((char*)&bp[i].data-(char*)&bp[i]));
	offset+=((char*)&bp[i].data-(char*)&bp[i]);
	memcpy((char*)buf+offset, &bp[i].data, bp[i].len);
	offset+=bp[i].len;
    }
}

size_t screen_backupsize(int len, const screenline_t *bp)
{
    int i;
    size_t sum=0;
    for(i=0;i<len;i++)
	sum+=((char*)&bp[i].data-(char*)&bp[i]) + bp[i].len;
    return sum;
}

void screen_restore(int len, screenline_t *bp, const void *buf)
{
    int i;
    size_t offset=0;
    for(i=0;i<len;i++) {
	memcpy(&bp[i], (char*)buf+offset, ((char*)&bp[i].data-(char*)&bp[i]));
	offset+=((char*)&bp[i].data-(char*)&bp[i]);
	memcpy(&bp[i].data, (char*)buf+offset, bp[i].len);
	offset+=bp[i].len;
    }
}
