/* $Id$ */
#include "bbs.h"

#define o_clear()     output(clearbuf,clearbuflen)
#define o_cleol()     output(cleolbuf,cleolbuflen)
#define o_scrollrev() output(scrollrev,scrollrevlen)
#define o_standup()   output(strtstandout,strtstandoutlen)
#define o_standdown() output(endstandout,endstandoutlen)

static unsigned short cur_ln = 0, cur_col = 0;
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
    assert(y>=0);
    assert(x>=0);
    cur_col = x;
    cur_ln = y;
}

void
move_ansi(int y, int x)
{
    // take ANSI length in consideration
    register screenline_t *slp;
    cur_ln = y;
    cur_col = x;

    if (y >= scr_lns || x < 1)
	return;

    slp = &big_picture[y];
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

static inline
screenline_t* GetCurrentLine(){
    register int i = cur_ln + roll;
    if(i >= scr_lns)
	i %= scr_lns;
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
    register int    i, j;
    int len;

    o_clear();
    for (tc_col = tc_line = i = 0, j = roll; i < scr_lns; i++, j++) {
	if (j >= scr_lns)
	    j = 0;
	bp = &big_picture[j];
	if ((len = bp->len)) {
	    rel_move(tc_col, tc_line, 0, i);

#ifdef DBCSAWARE
	    if (!(bp->mode & STANDOUT) &&
		    (cuser.uflag & DBCS_NOINTRESC) &&
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

// deprecated?
#if 0
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
#endif 

void
refresh(void)
{
    /* TODO remove unnecessary refresh() call, to save CPU time */
    register screenline_t *bp = big_picture;
    register int    i, j;
    int len;
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

	if (bp->mode & MODIFIED && bp->smod < len) 
	{
	    bp->mode &= ~(MODIFIED);

#ifdef DBCSAWARE
	    if (!(bp->mode & STANDOUT) &&
		(cuser.uflag & DBCS_NOINTRESC) &&
		DBCS_RemoveIntrEscape(bp->data, &len))
	    {
		// if anything changed, dirty whole line.
		bp->len = len;
		bp->smod = 0; bp->emod = len;
	    }
#endif // DBCSAWARE

#if 0
	    // disable now, bugs: 
	    // (1) pmore scrolling failed
	    // (2) input number (goto) in bbs list (search_num)
	    //
	    // more effort to determine ANSI smod
	    if (bp->smod > 0)
	    {
		int iesc;
		for (iesc = bp->smod-1; iesc >= 0; iesc--)
		{
		    if (bp->data[iesc] == ESC_CHR)
		    {
			bp->smod = iesc;
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
	    slp->oldlen = scr_cols;
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
     * else if(c != ESC_CHR && !isprint2(c)) { c = '*'; //substitute a '*' for
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
    if(cur_col < scr_cols)
	    cur_col++;
#else
    /* vvv commented by piaip: but SCR_COLS is 511 > unsigned char! */
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

// XXX left-right (for large term)
// TODO someday please add ANSI detection version
void 
outslr(const char *left, int leftlen, const char *right, int rightlen)
{
    if (left == NULL)
	left = "";
    if (right == NULL)
	right = "";
    if(*left && leftlen < 0)
	leftlen = strlen(left);
    if(*right && rightlen < 0)
	rightlen = strlen(right);
    // now calculate padding
    rightlen = t_columns - leftlen - rightlen;
    outs(left);

    // ignore right msg if we need to.
    if(rightlen >= 0)
    {
	while(--rightlen > 0)
	    outc(' ');
	outs(right);
    } else {
	rightlen = t_columns - leftlen;
	while(--rightlen > 0)
	    outc(' ');
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
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outs(msg);
}

void
outmsglr(const char *msg, int llen, const char *rmsg, int rlen)
{
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outslr(msg, llen, rmsg, rlen);
    outs(ANSI_RESET ANSI_CLRTOEND);
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

// level: 
// -1 - black (not implemented)
//  0 - dark text
//  1 - text
//  2 - no highlight (not implemented)
void
grayout_lines(int y, int end, int level)
{
    register screenline_t *slp = NULL;
    char buf[ANSILINELEN];
    int i = 0;

    if (y < 0) y = 0;
    if (end > b_lines) end = b_lines;

    // loop lines
    for (; y < end; y ++)
    {
	// modify by scroll
	i = y + roll;
	if (i < 0)
	    i += scr_lns;
	else if (i >= scr_lns)
	    i %= scr_lns;

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
	    case 0: // dark text
		// basically, in current system slp->data will
		// not exceed t_columns. buffer overflow is impossible.
		// but to make it more robust, let's quick check here.
		// of course, t_columns should always be far smaller.
		if (strlen((char*)slp->data) > t_columns)
		    slp->data[t_columns] = 0;
		strcpy((char*)slp->data, ANSI_COLOR(1;30;40));
		strcat((char*)slp->data, buf);
		strcat((char*)slp->data, ANSI_RESET ANSI_CLRTOEND);
		slp->len = strlen((char*)slp->data);
		break;

	    case 1: // Plain text
		memcpy(slp->data, buf, slp->len + 1);
		break;
	}
	slp->emod = slp->len -1;
    }
}

void grayout_line(int y, int level)
{
    return grayout_lines(y, y+1, level);
}


static size_t screen_backupsize(int len, const screenline_t *bp)
{
    int i;
    size_t sum = 0;
    for(i = 0; i < len; i++)
	sum += ((char*)&bp[i].data - (char*)&bp[i]) + bp[i].len;
    return sum;
}

void screen_backup(screen_backup_t *old)
{
    int i;
    size_t offset = 0;
    void *buf;
    screenline_t* bp = big_picture;

    buf = old->raw_memory = malloc(screen_backupsize(t_lines, big_picture));

    old->col = t_columns;
    old->row = t_lines;
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

void screen_restore(const screen_backup_t *old)
{
    int i;
    size_t offset=0;
    void *buf = old->raw_memory;
    screenline_t* bp = big_picture;
    const int len = MIN(old->row, t_lines);

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
    redoscr();
}

/* vim:sw=4
 */
