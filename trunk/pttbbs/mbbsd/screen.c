/* $Id: screen.c,v 1.4 2002/07/05 17:10:28 in2 Exp $ */
#include "bbs.h"

#ifdef SUPPORT_GB
static int      current_font_type = TYPE_BIG5;
static int      gbinited = 0;
#endif
#define SCR_WIDTH       80
#define o_clear()     output(clearbuf,clearbuflen)
#define o_cleol()     output(cleolbuf,cleolbuflen)
#define o_scrollrev() output(scrollrev,scrollrevlen)
#define o_standup()   output(strtstandout,strtstandoutlen)
#define o_standdown() output(endstandout,endstandoutlen)

static unsigned char cur_ln = 0, cur_col = 0;
static unsigned char docls, downfrom = 0;
static unsigned char standing = NA;
static char     roll = 0;
static int      scrollcnt, tc_col, tc_line;

#define MODIFIED (1)		/* if line has been modifed, screen output */
#define STANDOUT (2)		/* if this line has a standout region */

int             tputs(const char *str, int affcnt, int (*putc) (int));

void 
initscr()
{
    if (!big_picture) {
	scr_lns = t_lines;
	scr_cols = t_columns = ANSILINELEN;
	/* scr_cols = MIN(t_columns, ANSILINELEN); */
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
standoutput(char *buf, int ds, int de, int sso, int eso)
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
redoscr()
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
	    if (bp->mode & STANDOUT)
		standoutput((char *)bp->data, 0, len, bp->sso, bp->eso);
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

void 
refresh()
{
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
		if (automargins) {
		    tc_col -= t_columns;
		    if (++tc_line >= t_lines)
			tc_line = b_lines;
		} else
		    tc_col = t_columns - 1;
	    }
	}
	if (bp->oldlen > len) {
	    rel_move(tc_col, tc_line, len, i);
	    o_cleol();
	}
	bp->oldlen = len;

    }

    rel_move(tc_col, tc_line, cur_col, cur_ln);

    oflush();
}

void 
clear()
{
    register screenline_t *slp;

    register int    i;

    docls = YEA;
    cur_col = cur_ln = roll = downfrom = i = 0;
    do {
	slp = &big_picture[i];
	slp->mode = slp->len = slp->oldlen = 0;
    } while (++i < scr_lns);
}

void 
clrtoeol()
{
    register screenline_t *slp;
    register int    ln;

    standing = NA;
    if ((ln = cur_ln + roll) >= scr_lns)
	ln -= scr_lns;
    slp = &big_picture[ln];
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

void 
clrtobot()
{
    clrtoline(scr_lns);
}

void 
outch(unsigned char c)
{
    register screenline_t *slp;
    register int    i;

    if ((i = cur_ln + roll) >= scr_lns)
	i -= scr_lns;
    slp = &big_picture[i];

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
	if ((slp->mode & MODIFIED) != MODIFIED)
	    slp->smod = slp->emod = cur_col;
	slp->mode |= MODIFIED;
	if (cur_col > slp->emod)
	    slp->emod = cur_col;
	if (cur_col < slp->smod)
	    slp->smod = cur_col;
    }
    if (++cur_col >= scr_cols) {
	if (standing && (slp->mode & STANDOUT)) {
	    standing = 0;
	    slp->eso = MAX(slp->eso, cur_col);
	}
	cur_col = 0;
	if (cur_ln < scr_lns)
	    cur_ln++;
    }
}

static void 
parsecolor(char *buf)
{
    char           *val;
    char            data[24];

    data[0] = '\0';
    val = (char *)strtok(buf, ";");

    while (val) {
	if (atoi(val) < 30) {
	    if (data[0])
		strcat(data, ";");
	    strcat(data, val);
	}
	val = (char *)strtok(NULL, ";");
    }
    strcpy(buf, data);
}

#define NORMAL (00)
#define ESCAPE (01)
#define VTKEYS (02)

void 
outc(unsigned char ch)
{
    if (showansi)
	outch(ch);
    else {
	static char     buf[24];
	static int      p = 0;
	static int      mode = NORMAL;
	int             i;

	switch (mode) {
	case NORMAL:
	    if (ch == '\033')
		mode = ESCAPE;
	    else
		outch(ch);
	    return;
	case ESCAPE:
	    if (ch == '[')
		mode = VTKEYS;
	    else {
		mode = NORMAL;
		outch('');
		outch(ch);
	    }
	    return;
	case VTKEYS:
	    if (ch == 'm') {
		buf[p++] = '\0';
		parsecolor(buf);
	    } else if ((p < 24) && (not_alpha(ch))) {
		buf[p++] = ch;
		return;
	    }
	    if (buf[0]) {
		outch('');
		outch('[');

		for (i = 0; (p = buf[i]); i++)
		    outch(p);
		outch(ch);
	    }
	    p = 0;
	    mode = NORMAL;
	}
    }
}

static void 
do_outs(char *str)
{
    while (*str) {
	outc(*str++);
    }
}
#ifdef SUPPORT_GB
static void 
gb_init()
{
    if (current_font_type == TYPE_GB) {
	hc_readtab(BBSHOME "/etc/hc.tab");
    }
    gbinited = 1;
}

static void 
gb_outs(char *str)
{
    do_outs(hc_convert_str(str, HC_BIGtoGB, HC_DO_SINGLE));
}
#endif
int 
edit_outs(char *text)
{
    register int    column = 0;
    register char   ch;
#ifdef SUPPORT_GB
    if (current_font_type == TYPE_GB)
	text = hc_convert_str(text, HC_BIGtoGB, HC_DO_SINGLE);
#endif
    while ((ch = *text++) && (++column < SCR_WIDTH))
	outch(ch == 27 ? '*' : ch);

    return 0;
}

void 
outs(char *str)
{
#ifdef SUPPORT_GB
    if (current_font_type == TYPE_BIG5)
#endif
	do_outs(str);
#ifdef SUPPORT_GB
    else {
	if (!gbinited)
	    gb_init();
	gb_outs(str);
    }
#endif
}


/* Jaky */
void 
Jaky_outs(char *str, int line)
{
#ifdef SUPPORT_GB
    if (current_font_type == TYPE_GB)
	str = hc_convert_str(str, HC_BIGtoGB, HC_DO_SINGLE);
#endif
    while (*str && line) {
	outc(*str);
	if (*str == '\n')
	    line--;
	str++;
    }
}

void 
outmsg(char *msg)
{
    move(b_lines, 0);
    clrtoeol();
#ifdef SUPPORT_GB
    if (current_font_type == TYPE_GB)
	msg = hc_convert_str(msg, HC_BIGtoGB, HC_DO_SINGLE);
#endif
    while (*msg)
	outc(*msg++);
}

void 
prints(char *fmt,...)
{
    va_list         args;
    char            buff[1024];

    va_start(args, fmt);
    vsprintf(buff, fmt, args);
    va_end(args);
    outs(buff);
}

void 
mprints(int y, int x, char *str)
{
    move(y, x);
    clrtoeol();
    prints(str);
}

void 
scroll()
{
    scrollcnt++;
    if (++roll >= scr_lns)
	roll = 0;
    move(b_lines, 0);
    clrtoeol();
}

void 
rscroll()
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
standout()
{
    if (!standing && strtstandoutlen) {
	register screenline_t *slp;

	slp = &big_picture[((cur_ln + roll) % scr_lns)];
	standing = YEA;
	slp->sso = slp->eso = cur_col;
	slp->mode |= STANDOUT;
    }
}

void 
standend()
{
    if (standing && strtstandoutlen) {
	register screenline_t *slp;

	slp = &big_picture[((cur_ln + roll) % scr_lns)];
	standing = NA;
	slp->eso = MAX(slp->eso, cur_col);
    }
}
