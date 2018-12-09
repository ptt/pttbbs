#include "vtkbd.h"
#include "bbs.h"

/*
 * vtuikit.c (was: visio.c)
 * piaip's new implementation of virtual terminal user interface toolkits
 *
 * This is not the original visio.c from Maple3.
 * We just borrowed its file name and few API names/prototypes
 * then re-implemented everything from scratch :)
 *
 * We will try to keep the API behavior similiar (to help porting)
 * but won't stick to it.
 * Maybe at the end only 'vmsg' and 'vmsgf' will still be compatible....
 *
 * m3 visio = (ptt) vtuikit+vtkbd+screen/term.
 *
 * Author: Hung-Te Lin (piaip), April 2008.
 *
 * Copyright (c) 2008-2009 Hung-Te Lin <piaip@csie.ntu.edu.tw>
 * Distributed under BSD license (GPL compatible).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * To add API here, please...
 * (1) name the API in prefix of 'v'.
 * (2) use only screen.c APIs.
 * (3) take care of wide screen and DBCS.
 * (4) utilize the colos in vtuikit.h, and name asa VCLR_* (vtuikit color)
 */

// ---- DEFINITION ---------------------------------------------------
#define MAX_COL		(t_columns-1)
#define SAFE_MAX_COL	(MAX_COL-1)
#define VBUFLEN		(ANSILINELEN)

// this is a special strlen to speed up processing.
// warning: x MUST be #define x "msg".
// otherwise you need to use real strlen.
#define MACROSTRLEN(x) (sizeof(x)-1)

#ifdef DEBUG
#define inline
#endif

// ---- UTILITIES ----------------------------------------------------
inline void
outnc(int n, unsigned char c)
{
    while (n-- > 0)
	outc(c);
}

inline void
nblank(int n)
{
    outnc(n, ' ');
}

static inline void
fillns(int n, const char *s)
{
    while (n > 0 && *s)
	outc(*s++), n--;
    if (n > 0)
	outnc(n, ' ');
}

static inline void
fillns_ansi(int n, const char *s)
{
    int d = strat_ansi(n, s);
    if (d < 0) {
	outs(s); nblank(-d);
    } else {
	outns(s, d);
    }
}

// ---- VREF API --------------------------------------------------

/**
 * vscr_save(): �Ǧ^�ثe�e�����ƥ�����C
 */
VREFSCR
vscr_save(void)
{
    // TODO optimize memory allocation someday.
    screen_backup_t *o = (screen_backup_t*)malloc(sizeof(screen_backup_t));
    assert(o);
    scr_dump(o);
    return o;
}

/**
 * vscr_restore(obj): �ϥΨçR���e�����ƥ�����C
 */
void
vscr_restore(VREFSCR obj)
{
    screen_backup_t *o = (screen_backup_t*)obj;
    if (o)
    {
	scr_restore(o);
	memset(o, 0, sizeof(screen_backup_t));
	free(o);
    }
}

/**
 * vcur_save(): �Ǧ^�ثe��Ъ��ƥ�����C
 */
VREFCUR
vcur_save(void)
{
    // XXX ���i�� new object �F�A pointer ���j
    int y, x;
    VREFCUR v;
    getyx(&y, &x);
    v = ((unsigned short)y << 16) | (unsigned short)x;
    return v;
}

/**
 * vcur_restore(obj): �ϥΨçR����Ъ��ƥ�����C
 */
void
vcur_restore(VREFCUR o)
{
    int y, x;
    y = (unsigned int)(o);
    x = (unsigned short)(y & 0xFFFF);
    y = (unsigned short)(y >> 16);
    move(y, x);
}

// ---- LOW LEVEL API -----------------------------------------------

/**
 * prints(fmt, ...): �ϥ� outs/outc ��X�î榡�Ʀr��C
 */
void
prints(const char *fmt,...)
{
    va_list args;
    char buff[VBUFLEN];

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    outs(buff);
}

/**
 * mvprints(int y, int x, fmt, ...): �ϥ� mvouts ��X�î榡�Ʀr��C
 */
void
mvprints(int y, int x, const char *fmt, ...)
{
    va_list args;
    char buff[VBUFLEN];

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    mvouts(y, x, buff);
}

/**
 * mvouts(y, x, str): = mvaddstr
 */
void
mvouts(int y, int x, const char *str)
{
    move(y, x);
    clrtoeol();
    SOLVE_ANSI_CACHE();
    outs(str);
}

void
outs_vbuf(VBUF *v)
{
    int c;
    while (EOF != (c = vbuf_pop(v)))
	outc(c);
}

void
outns_vbuf(VBUF *v, int n)
{
    int c;
    while (EOF != (c = vbuf_pop(v)) && n-- > 0)
	outc(c);
}

/**
 * vfill(n, flags, s): �L�X�ö� n �Ӧr�����Ŷ�
 *
 * @param n	space to occupy
 * @param flags	VFILL_* parameters
 * @param s	string to display
 */
void
vfill(int n, int flags, const char *s)
{
    // warning: flag determination must take care of default values.
    char has_ansi = ((flags & VFILL_HAS_ANSI) || (*s == ESC_CHR));
    char has_border = !(flags & VFILL_NO_BORDER);

    if (n < 1)
	return;

    // quick return
    if (!*s)
    {
	nblank(n);
	return;
    }

    // calculate border size (always draw because n > 0)
    if (has_border)
	n--;

    if (n > 0)
    {
	if (flags & VFILL_RIGHT_ALIGN)
	{
	    // right-align
	    int l = has_ansi ? strlen_noansi(s) : (int)strlen(s);

	    if (l >= n) // '=' prevents blanks
		l = n;
	    else {
		nblank(n - l);
		n = l;
	    }
	    // leave the task to left-align
	}

	// left-align
	if (has_ansi)
	    fillns_ansi(n, s);
	else
	    fillns(n, s);
    }

    // print border if required
    if (has_border)
	outc(' ');

    // close fill.
    if (has_ansi)
	outs(ANSI_RESET);
}

/**
 * vfill(n, flags, fmt, ...): �ϥ� vfill ��X�î榡�Ʀr��C
 *
 * @param n	space to occupy
 * @param flags	VFILL_* parameters
 * @param fmt	string to display
 */
void
vfillf(int n, int flags, const char *s, ...)
{
    va_list args;
    char buff[VBUFLEN];

    va_start(args, s);
    vsnprintf(buff, sizeof(buff), s, args);
    va_end(args);

    vfill(n, flags, s);
}

/**
 * vpad(n, pattern): �� n �Ӧr�� (�ϥΪ��榡�� pattern)
 *
 * @param n �n�񺡪��r���� (�L�k�񺡮ɷ|�ϥΪťն��)
 * @param pattern ��R�Ϊ��r��
 */
inline void
vpad(int n, const char *pattern)
{
    int len = strlen(pattern);
    // assert(len > 0);

    while (n >= len)
    {
	outs(pattern);
	n -= len;
    }
    if (n) nblank(n);
}

/**
 * vgety(): ���o�ثe�Ҧb��m�����
 *
 * �Ҽ{�� ANSI �t�ΡAgetyx() �����֥ΥB�M�I�C
 * vgety() �w���ө��T�C
 */
inline int
vgety(void)
{
    int y, x;
    getyx(&y, &x);
    return y;
}

// ---- HIGH LEVEL API -----------------------------------------------

/**
 * vshowmsg(s): �b�����L�X���w�T���γ�ª��Ȱ��T��
 *
 * @param s ���w�T���C NULL: ���N���~��C s: �Y�� \t �h�᭱�r��a�k (�Y�L�h��ܥ��N��)
 */
void
vshowmsg(const char *msg)
{
    int w = SAFE_MAX_COL;
    move(b_lines, 0); clrtoeol();

    if (!msg)
    {
	// print default message in middle
	outs(VCLR_PAUSE_PAD);
	outc(' '); // initial one space

	// VMSG_PAUSE MUST BE A #define STRING.
	w -= MACROSTRLEN(VMSG_PAUSE); // strlen_noansi(VMSG_PAUSE);
	w--; // initial space
	vpad(w/2, VMSG_PAUSE_PAD);
	outs(VCLR_PAUSE);
	outs(VMSG_PAUSE);
	outs(VCLR_PAUSE_PAD);
	vpad(w - w/2, VMSG_PAUSE_PAD);
    } else {
	// print in left, with floating (if \t exists)
	char *pfloat = strchr(msg, '\t');
	int  szfloat = 0;
	int  nmsg = 0;

	// print prefix
	w -= MACROSTRLEN(VMSG_MSG_PREFIX); // strlen_noansi(VMSG_MSG_PREFIX);
	outs(VCLR_MSG);
	outs(VMSG_MSG_PREFIX);

	// if have float, calculate float size
	if (pfloat) {
	    nmsg = pfloat - msg;
	    w -= strlen_noansi(msg) -1;	    // -1 for \t
	    pfloat ++; // skip \t
	    szfloat = strlen_noansi(pfloat);
	} else {
	    pfloat = VMSG_MSG_FLOAT;
	    szfloat = MACROSTRLEN(VMSG_MSG_FLOAT); // strlen_noansi()
	    w -= strlen_noansi(msg) + szfloat;
	}

	// calculate if we can display float
	if (w < 0)
	{
	    w += szfloat;
	    szfloat = 0;
	}

	// print msg body
	if (nmsg)
	    outns(msg, nmsg);
	else
	    outs(msg);

	// print padding for floats
	if (w > 0)
	    nblank(w);

	// able to print float?
	if (szfloat)
	{
	    outs(VCLR_MSG_FLOAT);
	    outs(pfloat);
	}
    }

    // safe blank
    outs(" " ANSI_RESET);
}

/**
 * vans(s): �b�����L�X�T���P�p��J��A�öǦ^�ϥΪ̪���J(�ର�p�g)�C
 *
 * @param s ���w�T���A�� vshowmsg
 */
int
vans(const char *msg)
{
    char buf[3];

    move(b_lines, 0);
    clrtoeol(); SOLVE_ANSI_CACHE();
    outs(msg);
    vgets(buf, sizeof(buf), VGET_LOWERCASE);
    return (unsigned char)buf[0];
}

/**
 * vansf(s, ...): �b�����L�X�T���P�p��J��A�öǦ^�ϥΪ̪���J(�ର�p�g)�C
 *
 * @param s ���w�T���A�� vshowmsg
 */
int
vansf(const char *fmt, ...)
{
    char   msg[VBUFLEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    return vans(msg);
}

/**
 * vmsg(s): �b�����L�X���w�T���γ�ª��Ȱ��T���A�öǦ^�ϥΪ̪�����C
 *
 * @param s ���w�T���A�� vshowmsg
 */
int
vmsg(const char *msg)
{
    int i = 0;

    vshowmsg(msg);

    // wait for key
    do {
	i = vkey();
    } while( i == 0 );

    // clear message bar
    move(b_lines, 0);
    clrtoeol();

    return i;
}


/**
 * vmsgf(s, ...): �榡�ƿ�X�Ȱ��T���éI�s vmsg)�C
 *
 * @param s �榡�ƪ��T��
 */
int
vmsgf(const char *fmt,...)
{
    char   msg[VBUFLEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    return vmsg(msg);
}

/**
 * vbarf(s, ...): �榡�ƿ�X���k������r�� (MAX_COL)
 *
 * @param s �榡�Ʀr�� (\t �᪺���e�|����k��)
 */
void
vbarf(const char *s, ...)
{
    char msg[VBUFLEN], *s2;
    va_list ap;
    va_start(ap, s);
    vsnprintf(msg, sizeof(msg), s, ap);
    va_end(ap);

    s2 = strchr(msg, '\t');
    if (s2) *s2++ = 0;
    else s2 = "";

    return vbarlr(msg, s2);
}

/**
 * vbarlr(l, r): ���k����e���ù� (MAX_COL)
 * �`�N: �ثe����@�۰ʻ{�w��Фw�b��}�Y�C
 *
 * @param l �a��������r�� (�i�t ANSI �X)
 * @param r �a�k������r�� (�i�t ANSI �X�A�᭱���|�ɪť�)
 */
void
vbarlr(const char *l, const char *r)
{
    // TODO strlen_noansi �]�⦸... ��� l �i�H�� output ���C
    int szl = strlen_noansi(l),
	szr = strlen_noansi(r);

    // assume we are already in (y, 0)
    clrtoeol();
    outs(l);
    szl = MAX_COL - szl;

    if (szl > szr)
    {
	nblank(szl - szr);
	szl = szr;
    }

    if (szl == szr)
	outs(r);
    else if (szl > 0)
	nblank(szl);

    outs(ANSI_RESET);
}

void
vs_rectangle_simple(int l, int t, int r, int b)
{
    int ol = l;

    assert( l + 4 <= r &&
	    t + 2 <= b);

    // draw top line
    move(t++, ol); l = ol+2;
    outs("�z");
    while (l < r-2) { outs("�w"); l+= 2; }
    outs("�{");
    if (l+2 < r) outs(" ");

    while (t < b)
    {
	move(t++, ol); l = ol+2;
	outs("�x");
	// while (l < r-2) { outs("  "); l+= 2; }
	l += (r-l-1)/2*2;
	move_ansi(t-1, l);
	outs("�x");
	if (l+2 < r) outs(" ");
    }

    // draw bottom line
    move(t++, ol); l = ol+2;
    outs("�|");
    while (l < r-2) { outs("�w"); l+= 2; }
    outs("�}");
    if (l+2 < r) outs(" ");
}

// ---- THEMED FORMATTING OUTPUT -------------------------------------

/**
 * vs_header(title, mid, right): �M�ſù��ÿ�X������D (MAX_COL)
 *
 * @param title: �a�����D�n���D�A���|�Q���_�C
 * @param mid: �m�������A�i�Q�����C
 * @param right: �a�k�������A�Ŷ����~�|��ܡC
 */
void
vs_header(const char *title, const char *mid, const char *right)
{
    int w = MAX_COL;
    int szmid   = mid   ? strlen(mid) : 0;
    int szright = right ? strlen(right) : 0;

    clear();
    outs(VCLR_HEADER);

    if (title)
    {
	outs(VMSG_HDR_PREFIX);
	outs(title);
	outs(VMSG_HDR_POSTFIX);
	w -= MACROSTRLEN(VMSG_HDR_PREFIX) + MACROSTRLEN(VMSG_HDR_POSTFIX);
	w -= strlen(title);
    }

    // determine if we can display right message, and
    // if if we need to truncate mid.
    if (szmid + szright > w)
	szright = 0;

    if (szmid >= w)
	szmid = w;
    else {
	int l = (MAX_COL-szmid)/2;
	l -= (MAX_COL-w);
	if (l > 0)
	    nblank(l), w -= l;
    }

    if (szmid) {
	outs(VCLR_HEADER_MID);
	outns(mid, szmid);
	outs(VCLR_HEADER);
    }
    nblank(w - szmid);

    if (szright) {
	outs(VCLR_HEADER_RIGHT);
	outs(right);
    }
    outs(ANSI_RESET "\n");
}

/**
 * vs_hdr(title): �M�ſù��ÿ�X²�������D
 *
 * @param title
 */
void
vs_hdr(const char *title)
{
    clear();
    outs(VCLR_HDR VMSG_HDR_PREFIX);
    outs(title);
    outs(VMSG_HDR_POSTFIX ANSI_RESET "\n");
}

/**
 * vs_hdr2bar(left, right): (�b�歺)��X²�����k��q�������D
 *
 * @param left:  �a�����D�n���D�A���|�Q���_�C
 * @param right: ��r�a���A�Χ��ѤU���Ҧ��Ŷ�
 */
void
vs_hdr2bar(const char *left, const char *right)
{
    int w = MAX_COL;

    if (*left == ESC_CHR)
	w -= strlen_noansi(left);
    else
	w -= strlen(left);

    SOLVE_ANSI_CACHE();
    clrtoeol();
    outs(VCLR_HDR2_LEFT);
    outs(left);
    outs(VCLR_HDR2_RIGHT);

    if (w <= 0)
	return;

    if (*right == ESC_CHR)
	fillns_ansi(w, right);
    else
	fillns(w, right);

    outs(ANSI_RESET "\n");
}

/**
 * vs_hdr2barf(fmt, ...): (�b�歺)��X�榡�ƪ�²�����k��q�������D
 *
 * @param fmt: �� \t ���j���k���榡�r��
 */
void
vs_hdr2barf(const char *fmt, ...)
{
    va_list args;
    char buff[VBUFLEN];
    char *tab = NULL;

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    tab = strchr(buff, '\t');
    if (tab) *tab++ = 0;

    vs_hdr2bar(buff, tab ? tab : "");
}

/**
 * vs_hdr2(left, right): �M�ſù��ÿ�X²�����k��q�������D
 */
void
vs_hdr2(const char *left, const char *right)
{
    clear();
    vs_hdr2bar(left, right);
}

void
vs_hdr2f(const char *fmt, ...)
{
    va_list args;
    char buff[VBUFLEN];
    char *tab = NULL;

    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);

    tab = strchr(buff, '\t');
    if (tab) *tab++ = 0;

    vs_hdr2(buff, tab ? tab : "");
}

/**
 * vs_footer(caption, msg): �b�ù������L�X�榡�ƪ� caption msg (���i�t ANSI �X)
 *
 * @param caption ���䪺�����r��
 * @param msg �T���r��, \t ���r�a�k�B�̫᭱�|�۰ʯd�@�ӪťաC
 */
void
vs_footer(const char *caption, const char *msg)
{
    int i = 0;
    move(b_lines, 0); clrtoeol();

    if (caption)
    {
	outs(VCLR_FOOTER_CAPTION);
	outs(caption);
	i += (*caption == ESC_CHR) ?
	    strlen_noansi(caption) :
	    (int)strlen(caption);
    }

    if (!msg) msg = "";
    outs(VCLR_FOOTER);

    while (*msg && i < SAFE_MAX_COL)
    {
	if (*msg == '(')
	{
	    outs(VCLR_FOOTER_QUOTE);
	}
	else if (*msg == '\t')
	{
	    // if we don't have enough space, ignore whole.
	    int l = strlen(++msg);
	    if (i + l > SAFE_MAX_COL) break;
	    l = SAFE_MAX_COL - l - i;
	    nblank(l);
	    i += l;
	    continue;
	}
	outc(*msg); i++;
	if (*msg == ')')
	    outs(VCLR_FOOTER);
	msg ++;
    }
    nblank(SAFE_MAX_COL-i);
    outc(' ');
    outs(ANSI_RESET);
}

/**
 * vs_cols_layout(cols, ws, n): �̾� cols (�j�p n) ���w�q�p��A�X����e�� ws
 */

void
vs_cols_layout(const VCOL *cols, VCOLW *ws, int n)
{
    int i, tw;
    VCOLPRI pri1 = cols[0].pri;
    memset(ws, 0, sizeof(VCOLW) * n);

    // first run, calculate minimal size
    for (i = 0, tw = 0; i < n; i++)
    {
	// drop any trailing if required
	if (tw + cols[i].minw > MAX_COL)
	    break;
	ws[i] = cols[i].minw;
	tw += ws[i];
    }

    if (tw < MAX_COL) {
	// calculate highest priorities
	// (pri1 already set to col[0].pri)
	for (i = 1; i < n; i++)
	{
	    if (cols[i].pri > pri1)
		pri1 = cols[i].pri;
	}
    }

    // try to iterate through all.
    while (tw < MAX_COL) {
	char run = 0;

	// also update pri2 here for next run.
	VCOLPRI pri2 = cols[0].pri;

	for (i = 0; i < n; i++)
	{
	    // if reach max, skip.
	    if (ws[i] >= cols[i].maxw)
		continue;

	    // lower priority, update pri2 and skip.
	    if (cols[i].pri < pri1)
	    {
		if (cols[i].pri > pri2)
		    pri2 = cols[i].pri;
		continue;
	    }

	    // now increase fields
	    ws[i] ++;
	    if (++tw >= MAX_COL) break;
	    run ++;
	}

	// if no more fields...
	if (!run) {
	    if (pri1 <= pri2) // no more priorities
		break;
	    pri1 = pri2; // try lower priority.
	}
    }
}

/**
 * vs_cols: �̷Ӥw�g��n�����j�p�i���X
 */
void
vs_cols(const VCOL *cols, const VCOLW *ws, int n, ...)
{
    int i = 0, w = 0;
    char *s = NULL;

    va_list ap;
    va_start(ap, n);

    for (i = 0; i < n; i++, cols++, ws++)
    {
	int flags = 0;
	s = va_arg(ap, char*);

	// quick check input.
	if (!s)
	{
	    s = "";
	}
	w = *ws;

	if (cols->attr)
	    outs(cols->attr);

	// build vfill flag
	if (cols->flags.right_align)	flags |= VFILL_RIGHT_ALIGN;
	if (cols->flags.usewhole)	flags |= VFILL_NO_BORDER;

	vfill(w, flags, s);

	if (cols->attr)
	    outs(ANSI_RESET);
    }
    va_end(ap);

    // end line
    outs(ANSI_RESET "\n");
}

/*
 * vs_multi_T_table_simple: render multiple T-type tables
 *
 * @param t_tables  NOTE: the pointers inside will be changed.
 *
 * T table format:
 * const char *table[] = {
 *   "caption", NULL,
 *   "lvar",   "rvar",
 *   NULL
 * };
 */
void
vs_multi_T_table_simple(
	const char ***t_tables,   int  n_t_tables,
	const int  *col_widths,   const int  *l_widths,
	const char *attr_caption, const char *attr_l, const char *attr_r)
{
    int i;
    int incomplete;

    do
    {
	incomplete = n_t_tables;
	for (i = 0; i < n_t_tables; i++)
	{
	    const char *lvar = NULL, *rvar = "";

	    if (*t_tables[i])
	    {
		lvar = *t_tables[i]++;
		rvar = *t_tables[i]++;
	    }

	    if (!rvar) {
		// draw caption
		if (attr_caption) outs(attr_caption);
		vfill(col_widths[i], 0, lvar);
		continue;
	    }

	    if (!lvar) {
		// table is complete...
		incomplete --;
		lvar = "";
	    }

	    // draw table body
	    if(attr_l) outs(attr_l);
	    vfill(l_widths[i], 0, lvar);
	    if(attr_r) outs(attr_r);
	    vfill(col_widths[i] - l_widths[i], 0, rvar);
	}
	outc('\n');
    }
    while (incomplete);
}

// pmore_QuickRawModePref style pref setter, renders
// - title                             -
// - entry: options (separated by TAB) -
// - prompt                            -
int
vs_quick_pref(int default_value, const char *title, const char *entry,
              const char *options, const char *prompt) {
    int ystart = b_lines -2;
    int cOptions = 0;
    int index = default_value, k;
    const char *opt;

#ifdef HAVE_GRAYOUT
    grayout(0, ystart-1, GRAYOUT_DARK);
#endif // HAVE_GRAYOUT

    // adjust params
    if (!title)
        title = VMSG_QPREF_TITLE;
    if (!entry)
        entry = VMSG_QPREF_ENTRY;
    if (!prompt)
        prompt = VMSG_QPREF_PROMPT;
    assert(options && *options);

    while (1)
    {
        move(ystart, 0);
        clrtobot();
        outs(VCLR_QPREF_TITLE);
        vbarf("%s", title); // in case title has TAB for LR display.
        move(ystart + 1, 0);
        outs(VCLR_QPREF_PROMPT);
        outs(entry);
        cOptions = 0;

        // list options
        opt = options;
        while (*opt) {
            const char *ptab = strchr(opt, '\t');
            int slen = ptab ? (ptab - opt) : (int)strlen(opt);
            outs(VCLR_QPREF_ENTRY_KEY);
            prints("%d", cOptions + 1);
            if (cOptions == index) {
                outs(VCLR_QPREF_ENTRY_ACTIVE);
                outc('*');
            } else {
                outs(VCLR_QPREF_ENTRY_TEXT);
                outc(' ');
            }
            outns(opt, slen);
            outs(ANSI_RESET);
            outc(' ');
            opt += slen;
            cOptions++;
            if (*opt)
                opt++;
        }

        k = vmsg(prompt);
        if (isascii(k) && isdigit(k)) {
            k = k - '1';
            if (k >= 0 && k < cOptions) {
                index = k;
                break;
            }
        } else if (k == KEY_LEFT) {
            if (index > 0) {
                index--;
                continue;
            }
        } else if (k == KEY_RIGHT) {
            if (index + 1 < cOptions) {
                index++;
                continue;
            }
        } else {
            break;
        }
    }
    return index;
}

////////////////////////////////////////////////////////////////////////
// DBCS Aware Helpers
////////////////////////////////////////////////////////////////////////

#ifdef DBCSAWARE
#   define CHKDBCSTRAIL(_buf,_i) (ISDBCSAWARE() && DBCS_Status(_buf, _i) == DBCS_TRAILING)
#else  // !DBCSAWARE
#   define CHKDBCSTRAIL(buf,i) (0)
#endif // !DBCSAWARE

////////////////////////////////////////////////////////////////////////
// History Helpers
////////////////////////////////////////////////////////////////////////
//
#define IH_MAX_ENTRIES	(12)	    // buffer size = approx. 1k
#define IH_MIN_SIZE	(2)	    // only keep string >= 2 bytes

typedef struct {
    int icurr;	    // current retrival pointer
    int iappend;    // new location to append
    char buf[IH_MAX_ENTRIES][STRLEN];
} InputHistory;

static InputHistory ih; // everything intialized to zero.

int
InputHistoryExists(const char *s)
{
    int i = 0;

    for (i = 0; i < IH_MAX_ENTRIES; i++)
	if (strcmp(s, ih.buf[i]) == 0)
	    return i+1;

    return 0;
}

int
InputHistoryAdd(const char *s)
{
    int i = 0;
    int l = strlen(s);

    if (l < IH_MIN_SIZE)
	return 0;

    i = InputHistoryExists(s);
    if (i > 0) // found
    {
	i--; // i points to valid index
	assert(i < IH_MAX_ENTRIES);

	// change order: just delete it.
	ih.buf[i][0] = 0;
    }

    // now append s.
    strlcpy(ih.buf[ih.iappend], s, sizeof(ih.buf[ih.iappend]));
    ih.iappend ++;
    ih.iappend %= IH_MAX_ENTRIES;
    ih.icurr = ih.iappend;

    return 1;
}

static void
InputHistoryDelta(char *s, int sz, int d)
{
    int i, xcurr = 0;
    for (i = 1; i <= IH_MAX_ENTRIES; i++)
    {
	xcurr = (ih.icurr+ IH_MAX_ENTRIES + d*i)%IH_MAX_ENTRIES;
	if (ih.buf[xcurr][0])
	{
	    ih.icurr = xcurr;

	    // copy buffer
	    strlcpy(s, ih.buf[ih.icurr], sz);
            // TODO(piaip) serious check here, in case s contains invalid data.

	    // DBCS safe
	    i = strlen(s);
	    if (DBCS_Status(s, i) == DBCS_TRAILING)
		s[i-1] = 0;
	    break;
	}
    }
}

void
InputHistoryPrev(char *s, int sz)
{
    InputHistoryDelta(s, sz, -1);
}

void
InputHistoryNext(char *s, int sz)
{
    InputHistoryDelta(s, sz, +1);
}

////////////////////////////////////////////////////////////////////////
// vget*: mini editbox
////////////////////////////////////////////////////////////////////////

static inline int
_vgetcbhandler(VGET_FCALLBACK cbptr, int *pabort, int c, VGET_RUNTIME *prt, void *instance)
{
    if (!cbptr)
	return 0;

    switch(cbptr(c, prt, instance))
    {
	case VGETCB_NONE:
	    assert( prt->icurr >= 0 && prt->icurr <= prt->iend && prt->iend <= prt->len );
	    return 0;

	case VGETCB_NEXT:
	    assert( prt->icurr >= 0 && prt->icurr <= prt->iend && prt->iend <= prt->len );
	    return 1;

	case VGETCB_END:
	    assert( prt->icurr >= 0 && prt->icurr <= prt->iend && prt->iend <= prt->len );
	    *pabort = 1;
	    return 1;

	case VGETCB_ABORT:
	    assert( prt->icurr >= 0 && prt->icurr <= prt->iend && prt->iend <= prt->len );
	    *pabort = 1;
	    prt->icurr = 0;
	    prt->iend = 0;
	    prt->buf[0] = 0;
	    return 1;
    }
    assert(0); // shall never reach here
    return 0;
}

int
vgetstring(char *_buf, int len, int flags, const char *defstr, const VGET_CALLBACKS *pcbs, void *instance)
{
    // rt.iend points to NUL address, and
    // rt.icurr points to cursor.
    int line, col, line_ansi, col_ansi;
    int abort = 0, dirty = 0;
    int c = 0;
    char ismsgline = 0;

    // callback
    VGET_CALLBACKS cb = {NULL};

    // always use internal buffer to prevent temporary input issue.
    char buf[STRLEN] = "";  // zero whole.

    // runtime structure
    VGET_RUNTIME    rt = { buf, len > STRLEN ? STRLEN : len };

    // it is wrong to design input with larger buffer
    // than STRLEN. Although we support large screen,
    // inputting huge line will just make troubles...
    if (len > STRLEN) len = STRLEN;
    assert(len <= (int)sizeof(buf) && len >= 2);

    // adjust flags
    if (flags & (VGET_NOECHO | VGET_DIGITS))
	flags |= VGET_NO_NAV_HISTORY;

    // memset(buf, 0, len);
    if (defstr && *defstr)
    {
	strlcpy(buf, defstr, len);
	strip_ansi(buf, buf, STRIP_ALL); // safer...
	rt.icurr = rt.iend = strlen(buf);
    }

    // setup callbacks
    if (pcbs)
	cb = *pcbs;

    getyx(&line, &col);	    // now (line,col) is the beginning of our new fields.
    getyx_ansi(&line_ansi, &col_ansi);

    // XXX be compatible with traditional...
    if (line == b_lines - msg_occupied)
	ismsgline = 1;

    if (ismsgline)
	msg_occupied ++;

    // main loop
    while (!abort)
    {
	if (dirty)
	{
	    dirty = 0;
	    // callback: change
	    if (_vgetcbhandler(cb.change, &abort, c, &rt, instance))
		continue;
	}

	if (!(flags & VGET_NOECHO))
	{
	    // callback: redraw
	    if (_vgetcbhandler(cb.redraw, &abort, c, &rt, instance))
		continue;

	    // print current buffer
	    move(line, col);
	    SOLVE_ANSI_CACHE();
	    clrtoeol();
	    SOLVE_ANSI_CACHE();

	    if (!(flags & VGET_TRANSPARENT))
		outs(VCLR_INPUT_FIELD); // change color to prompt fields

            if (flags & VGET_PASSWORD) {
                int i;
                for (i = 0; i < rt.iend; i++)  {
                    outc('*');
                }
                for (; i < len; i++) {
                    outc(' ');
                }
            } else {
                vfill(len, 0, buf);
            }

	    if (!(flags & VGET_TRANSPARENT))
		outs(ANSI_RESET);

	    // move to cursor position
	    move(line_ansi, col_ansi+rt.icurr);
	} else {
	    // to simulate the "clrtoeol" behavior...
	    // XXX make this call only once? or not?
	    clrtoeol();
	}
	c = vkey();

	// callback: peek
	if (_vgetcbhandler(cb.peek, &abort, c, &rt, instance))
	    continue;

	// standard key bindings
	// note: if you processed anything, you must use 'continue' instead of 'break'.

	if (flags & VGET_NO_NAV_EDIT) {
	    // skip navigation editing keys
	} else switch (c) {

	    // standard navigation
	    case KEY_HOME:  case Ctrl('A'):
		rt.icurr = 0;
		continue;

	    case KEY_END:   case Ctrl('E'):
		rt.icurr = rt.iend;
		continue;

	    case KEY_LEFT:  case Ctrl('B'):
		if (rt.icurr > 0)
		rt.icurr--;
		else
		    bell();
		if (rt.icurr > 0 && CHKDBCSTRAIL(buf, rt.icurr))
		    rt.icurr--;
		continue;

	    case KEY_RIGHT: case Ctrl('F'):
		if (rt.icurr < rt.iend)
		rt.icurr++;
		else
		    bell();
		if (rt.icurr < rt.iend && CHKDBCSTRAIL(buf, rt.icurr))
		    rt.icurr++;
		continue;

	    // editing keys
	    case KEY_DEL:   case Ctrl('D'):
		if (rt.icurr+1 < rt.iend && CHKDBCSTRAIL(buf, rt.icurr+1)) {
		    // kill next one character.
		    memmove(buf+rt.icurr, buf+rt.icurr+1, rt.iend-rt.icurr);
		    rt.iend--;
		    dirty = 1;
		}
		if (rt.icurr < rt.iend) {
		    // kill next one character.
		    memmove(buf+rt.icurr, buf+rt.icurr+1, rt.iend-rt.icurr);
		    rt.iend--;
		    dirty = 1;
		}
		continue;

	    case Ctrl('Y'):
		rt.icurr = 0;
		// reuse Ctrl-K code
	    case Ctrl('K'):
		rt.iend = rt.icurr;
		if (!buf[rt.iend])
		    continue;
		buf[rt.iend] = 0;
		dirty = 1;
		continue;
	}

	if (flags & VGET_NO_NAV_HISTORY) {
	    // skip navigation history keys
	} else switch (c) {
	    // history navigation
	    case KEY_DOWN: case Ctrl('N'):
		c = KEY_DOWN;
		// let UP do the magic.
	    case KEY_UP:   case Ctrl('P'):
		// NOECHO is already checked...
		if (!InputHistoryExists(buf))
		    InputHistoryAdd(buf);

		if (c == KEY_DOWN)
		    InputHistoryNext(buf, len);
		else
		    InputHistoryPrev(buf, len);

		rt.icurr = rt.iend = strlen(buf);
		dirty = 1;
		continue;
	}

	// the basic keys
	switch(c)
	{
	    // exiting keys
	    case KEY_ENTER:
		abort = 1;
		continue;

	    case Ctrl('C'):
		rt.icurr = rt.iend = 0;
		buf[0] = 0;
		buf[1] = c; // XXX this is a dirty hack...
		abort = 1;
		continue;

	    // standard editing keys: backspace
	    case KEY_BS:
		if (rt.icurr > 0) {
		    // kill previous one charracter.
		    memmove(buf+rt.icurr-1, buf+rt.icurr, rt.iend-rt.icurr+1);
		    rt.icurr--; rt.iend--;
		    dirty = 1;
		} else
		    bell();
		if (rt.icurr > 0 && CHKDBCSTRAIL(buf, rt.icurr)) {
		    // kill previous one charracter.
		    memmove(buf+rt.icurr-1, buf+rt.icurr, rt.iend-rt.icurr+1);
		    rt.icurr--; rt.iend--;
		    dirty = 1;
		}
		continue;
	}

	// all special keys were processed, now treat as 'input data'.

	// content filter
	if (c < ' ' || c >= 0xFF)
	{
	    bell(); continue;
	}
	if ((flags & VGET_DIGITS) &&
		( !isascii(c) || !isdigit(c)))
	{
	    bell(); continue;
	}
	if (flags & VGET_LOWERCASE)
	{
	    if (!isascii(c))
	    {
		bell(); continue;
	    }
	    c = tolower(c);
	}
	if  (flags & VGET_ASCII_ONLY)
	{
	    if (!isascii(c) || !isprint(c))
	    {
		bell(); continue;
	    }
	}

	// size check
	if(rt.iend+1 >= len)
	{
	    bell(); continue;
	}

	// prevent incomplete DBCS
	if (c > 0x80 && vkey_is_ready() &&
		len - rt.iend < 3)	// we need 3 for DBCS+NUL.
	{
	    // XXX should we purge here, or wait the final DBCS_safe_trim?
	    vkey_purge();
	    bell(); continue;
	}

	// callback: data
	if (_vgetcbhandler(cb.data, &abort, c, &rt, instance))
	    continue;

	// size check again, due to data callback.
	if(rt.iend+1 >= len)
	{
	    bell(); continue;
	}

	// add one character.
	memmove(buf+rt.icurr+1, buf+rt.icurr, rt.iend-rt.icurr+1);
	buf[rt.icurr++] = c;
	rt.iend++;
	dirty = 1;
    }

    assert(rt.iend >= 0 && rt.iend < len);
    buf[rt.iend] = 0;

    DBCS_safe_trim(buf);

    // final filtering
    if (rt.iend && (flags & VGET_LOWERCASE))
	buf[0] = tolower(buf[0]);

    // save the history except password mode
    if (buf[0] && !(flags & VGET_NOECHO))
	InputHistoryAdd(buf);

    // copy buffer!
    memcpy(_buf, buf, len);

    // XXX update screen display
    if (ismsgline)
	msg_occupied --;

    /* because some code then outs so change new line.*/
    move(line+1, 0);

    return rt.iend;
}

int
vgets(char *buf, int len, int flags)
{
    return vgetstr(buf, len, flags, "");
}

int
vgetstr(char *buf, int len, int flags, const char *defstr)
{
    return vgetstring(buf, len, flags, defstr, NULL, NULL);
}

/////////////////////////////////////////////////////////////////

/**
 * �q�� y �C�}�l show �X filename �ɮפ����e lines ��C
 * mode �� output ���Ҧ��A�ѼƦP strip_ansi�C
 * @param filename: the file to show
 * @param y:	    starting line on screen
 * @param lines:    max lines to be displayed
 * @param mode:	    SHOWFILE_*, see modes.h
 * @return ���ѶǦ^ 0�A�_�h�� 1�C
 *         2 ��ܦ� PttPrints �X
 */
int
show_file(const char *filename, int y, int lines, int mode)
{
    FILE *fp;
    char buf[ANSILINELEN];
    int  ret = 1;
    int  strpmode = STRIP_ALL;

    if (mode & SHOWFILE_ALLOW_COLOR)
	strpmode = ONLY_COLOR;
    if (mode & SHOWFILE_ALLOW_MOVE)
	strpmode = NO_RELOAD;

    if (y >= 0)
	move(y, 0);
    clrtoln(lines + y);
    if ((fp = fopen(filename, "r"))) {
	while (fgets(buf, sizeof(buf), fp) && lines--)
	{
	    move(y++, 0);
	    if (mode == SHOWFILE_RAW)
	    {
		outs(buf);
	    }
	    else if ((mode & SHOWFILE_ALLOW_STAR) && (strstr(buf, ESC_STR "*") != NULL))
	    {
		// because Ptt_prints escapes are not so often,
		// let's try harder to detect it.
		outs(Ptt_prints(buf, sizeof(buf), strpmode));
		ret = 2;
	    } else {
		// ESC is very common...
		strip_ansi(buf, buf, strpmode);
		outs(buf);
	    }
	}
	fclose(fp);
	outs(ANSI_RESET); // prevent some broken Welcome file
    } else
	return 0;
    return ret;
}

int
show_80x24_screen(const char *filename)
{
    clear();
    // max 24 lines, holding one more line for pause/messages
    return show_file(filename, 0, 24, SHOWFILE_ALLOW_ALL);
}

int
search_num(int ch, int max)
{
    int  clen = 1, y = b_lines - msg_occupied;
    char genbuf[10];

    genbuf[0] = ch; genbuf[1] = 0;
    clen = getdata_buf(y, 0,
	    " ���ܲĴX��: ", genbuf, sizeof(genbuf)-1, NUMECHO);

    move(y, 0); clrtoeol();
    genbuf[clen] = '\0';
    if (genbuf[0] == '\0')
	return -1;
    clen = atoi(genbuf);
    if (clen == 0)
	return 0;
    if (clen > max)
	return max;
    return clen - 1;
}

void
cursor_show(int row, int column)
{
    if (HasUserFlag(UF_MENU_LIGHTBAR)) {
        grayout(row, row + 1, GRAYOUT_COLORBOLD);
    }
    move(row, column);
    if (HasUserFlag(UF_CURSOR_ASCII)) {
        outs(STR_CURSOR);
        move(row, column);
    } else {
        outs(STR_CURSOR2);
        move(row, column + 1);
    }
}

void
cursor_clear(int row, int column)
{
    move(row, column);
    if (HasUserFlag(UF_CURSOR_ASCII))
        outs(STR_UNCUR);
    else
        outs(STR_UNCUR2);

    if (HasUserFlag(UF_MENU_LIGHTBAR)) {
        grayout(row, row + 1, GRAYOUT_COLORNORM);
    }
}

int
cursor_key(int row, int column)
{
    int             ch;

    cursor_show(row, column);
    ch = vkey();
    cursor_clear(row, column);
    return ch;
}

void
printdash(const char *mesg, int msglen)
{
    int             head = 0, tail;

    if(msglen <= 0)
	msglen = strlen(mesg);

    if (mesg)
	head = (msglen + 1) >> 1;

    tail = head;

    while (head++ < t_columns/2-2)
	outc('-');

    if (tail) {
	outc(' ');
	if(mesg) outs(mesg);
	outc(' ');
    }
    while (tail++ < t_columns/2-2)
	outc('-');

    outc('\n');
}

void
show_help(const char * const helptext[])
{
    const char     *str;
    int             i;

    clear();
    for (i = 0; (str = helptext[i]); i++) {
	if (*str == '\0')
	    prints(ANSI_COLOR(1) "�i %s �j" ANSI_RESET "\n", str + 1);
	else if (*str == '\01')
	    prints("\n" ANSI_COLOR(36) "�i %s �j" ANSI_RESET "\n", str + 1);
	else
	    prints("        %s\n", str);
    }
    PRESSANYKEY();
}

void
show_helpfile(const char *helpfile)
{
    clear();
    show_file((char *)helpfile, 0, b_lines, SHOWFILE_ALLOW_ALL);
    PRESSANYKEY();
}

void
outmsg(const char *msg)
{
    move(b_lines - msg_occupied, 0);
    clrtoeol();
    outs(msg);
}

