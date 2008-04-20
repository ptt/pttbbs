/* $Id$ */
#include "bbs.h"

/*
 * visio.c
 * piaip's new implementation of visio
 * (visio: virtual screen input output, the name from Maple3)
 *
 * This is not the original visio.c from Maple3.
 * We just borrowed its file name and few API names/prototypes
 * then re-implemented everything from scratch :)
 *
 * We will try to keep the API behavior similiar (to help porting)
 * but won't stick to it. 
 * Maybe at the end only 'vmsg' and 'vmsgf' will still be compatible....
 *
 * m3 visio = (ptt) visio+screen/term.
 *
 * Author: Hung-Te Lin (piaip), April 2008.
 *
 * Copyright (c) 2008 Hung-Te Lin <piaip@csie.ntu.edu.tw>
 * All rights reserved.
 * 
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
 * (4) utilize the colos in visio.h, and name asa VCLR_* (visio color)
 */

// ---- DEFINITION ---------------------------------------------------
#define MAX_COL		(t_columns-1)
#define SAFE_MAX_COL	(MAX_COL-1)
#define VBUFLEN		(ANSILINELEN)

// this is a special strlen to speed up processing.
// warning: x MUST be #define x "msg".
// otherwise you need to use real strlen.
#define MACROSTRLEN(x) (sizeof(x)-1)

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
 * vscr_save(): 傳回目前畫面的備份物件。
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
 * vscr_restore(obj): 使用並刪除畫面的備份物件。
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
 * vcur_save(): 傳回目前游標的備份物件。
 */
VREFCUR
vcur_save(void)
{
    // XXX 偷懶不 new object 了， pointer 夠大
    int y, x;
    VREFCUR v;
    getyx(&y, &x);
    v = ((unsigned short)y << 16) | (unsigned short)x;
    return v;
}

/**
 * vcur_restore(obj): 使用並刪除游標的備份物件。
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
 * prints(fmt, ...): 使用 outs/outc 輸出並格式化字串。
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

/**
 * vfill(n, flags, s): 印出並填滿 n 個字元的空間
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
	    int l = has_ansi ? strlen_noansi(s) : strlen(s);

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
 * vfill(n, flags, fmt, ...): 使用 vfill 輸出並格式化字串。
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
 * vpad(n, pattern): 填滿 n 個字元 (使用的格式為 pattern)
 *
 * @param n 要填滿的字元數 (無法填滿時會使用空白填補)
 * @param pattern 填充用的字串
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
 * vgety(): 取得目前所在位置的行數
 *
 * 考慮到 ANSI 系統，getyx() 較為少用且危險。
 * vgety() 安全而明確。
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
 * vshowmsg(s): 在底部印出指定訊息或單純的暫停訊息
 *
 * @param s 指定訊息。 NULL: 任意鍵繼續。 s: 若有 \t 則後面字串靠右 (若無則顯示任意鍵)
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
 * vans(s): 在底部印出訊息與小輸入欄，並傳回使用者的輸入(轉為小寫)。
 *
 * @param s 指定訊息，見 vshowmsg
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
 * vansf(s, ...): 在底部印出訊息與小輸入欄，並傳回使用者的輸入(轉為小寫)。
 *
 * @param s 指定訊息，見 vshowmsg
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
 * vmsg(s): 在底部印出指定訊息或單純的暫停訊息，並傳回使用者的按鍵。
 *
 * @param s 指定訊息，見 vshowmsg
 */
int
vmsg(const char *msg)
{
    int i = 0;

    vshowmsg(msg);

    // wait for key
    do {
	i = igetch();
    } while( i == 0 );

    // clear message bar
    move(b_lines, 0);
    clrtoeol();

    return i;
}


/**
 * vmsgf(s, ...): 格式化輸出暫停訊息並呼叫 vmsg)。
 *
 * @param s 格式化的訊息
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
 * vbarf(s, ...): 格式化輸出左右對齊的字串 (MAX_COL)
 *
 * @param s 格式化字串 (\t 後的內容會對齊右端)
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
 * vbarlr(l, r): 左右對齊畫滿螢幕 (MAX_COL)
 * 注意: 目前的實作自動認定游標已在行開頭。
 *
 * @param l 靠左對齊的字串 (可含 ANSI 碼)
 * @param r 靠右對齊的字串 (可含 ANSI 碼，後面不會補空白)
 */
void
vbarlr(const char *l, const char *r)
{
    // TODO strlen_noansi 跑兩次... 其實 l 可以邊 output 邊算。
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

// ---- THEMED FORMATTING OUTPUT -------------------------------------

/**
 * vs_header(title, mid, right): 清空螢幕並輸出完整標題 (MAX_COL)
 *
 * @param title: 靠左的主要標題，不會被切斷。
 * @param mid: 置中說明，可被切齊。
 * @param right: 靠右的說明，空間夠才會顯示。
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
 * vs_hdr(title): 清空螢幕並輸出簡易的標題
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
 * vs_footer(caption, msg): 在螢幕底部印出格式化的 caption msg (不可含 ANSI 碼)
 *
 * @param caption 左邊的分類字串
 * @param msg 訊息字串, \t 後文字靠右、最後面會自動留一個空白。
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
	    strlen(caption);
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
 * vs_cols_layout(cols, ws, n): 依據 cols (大小 n) 的定義計算適合的行寬於 ws
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
 * vs_cols: 依照已經算好的欄位大小進行輸出
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
_vgetcbhandler(VGET_FCALLBACK cbptr, int *pabort, 
	int c, char *buf, int *picurr, int *piend, int len, void *cbparam)
{
    if (!cbptr)
	return 0;

    switch(cbptr(c, buf, picurr, piend, len, cbparam))
    {
	case VGETCB_NONE:
	    return 0;

	case VGETCB_NEXT:
	    return 1;

	case VGETCB_END:
	    *pabort = 1;
	    return 1;

	case VGETCB_ABORT:
	    *pabort = 1;
	    *picurr = *piend = 0;
	    buf[0] = 0;
	    return 1;
    }
    assert(0); // shall never reach here
    return 0;
}

int 
vgetstring(char *_buf, int len, int flags, const char *defstr, const VGET_CALLBACKS *pcb, void *cbparam)
{
    // iend points to NUL address, and
    // icurr points to cursor.
    int line, col;
    int icurr = 0, iend = 0, abort = 0;
    int c;
    char ismsgline = 0;

    // always use internal buffer to prevent temporary input issue.
    char buf[STRLEN] = "";  // zero whole.

    // callback 
    VGET_CALLBACKS cb = {NULL};

    // it is wrong to design input with larger buffer
    // than STRLEN. Although we support large screen,
    // inputting huge line will just make troubles...
    if (len > STRLEN) len = STRLEN;
    assert(len <= sizeof(buf) && len >= 2);

    // memset(buf, 0, len);
    if (defstr && *defstr)
    {
	strlcpy(buf, defstr, len);
	strip_ansi(buf, buf, STRIP_ALL); // safer...
	icurr = iend = strlen(buf);
    }

    // setup callbacks
    if (pcb) 
    {
	if (pcb->peek)	cb.peek = pcb->peek;
	if (pcb->data)	cb.data = pcb->data;
	if (pcb->post)  cb.post = pcb->post;
    }

    getyx(&line, &col);	    // now (line,col) is the beginning of our new fields.

    // XXX be compatible with traditional...
    if (line == b_lines - msg_occupied)
	ismsgline = 1;

    if (ismsgline)
	msg_occupied ++;

    while (!abort)
    {
	if (!(flags & VGET_NOECHO))
	{
	    // print current buffer
	    move(line, col);
	    clrtoeol();
	    SOLVE_ANSI_CACHE();
	    if (!(flags & VGET_TRANSPARENT))
		outs(VCLR_INPUT_FIELD); // change color to prompt fields
	    vfill(len, 0, buf);
	    if (!(flags & VGET_TRANSPARENT))
		outs(ANSI_RESET);

	    // move to cursor position
	    move(line, col+icurr);
	} else {
	    // to simulate the "clrtoeol" behavior...
	    // XXX make this call only once? or not?
	    clrtoeol();
	}
	c = vkey();

	// callback 1: peek
	if (_vgetcbhandler(cb.peek, &abort,
		    c, buf, &icurr, &iend, len, cbparam))
	    continue;

	// standard key bindings
	switch(c) {
	    // history navigation
	    case KEY_DOWN: case Ctrl('N'):
		c = KEY_DOWN;
		// let UP do the magic.
	    case KEY_UP:   case Ctrl('P'):
		if ((flags & VGET_NOECHO) ||
		    (flags & VGET_DIGITS))
		{
		    bell(); 
		    continue;
		}

		// NOECHO is already checked...
		if (!InputHistoryExists(buf))
		    InputHistoryAdd(buf);

		if (c == KEY_DOWN)
		    InputHistoryNext(buf, len);
		else
		    InputHistoryPrev(buf, len);

		icurr = iend = strlen(buf);
		break;

	    // exiting keys
	    case '\n':	    case '\r':
		abort = 1;
		break;

	    case Ctrl('C'):
		icurr = iend = 0;
		buf[0] = 0;
		buf[1] = c;
		abort = 1;
		break;

	    // standard navigation
	    case KEY_HOME:  case Ctrl('A'):
		icurr = 0;
		break;

	    case KEY_END:   case Ctrl('E'):
		icurr = iend;
		break;

	    case KEY_LEFT:  case Ctrl('B'):
		if (icurr > 0)
		    icurr--;
		else
		    bell();
		if (icurr > 0 && CHKDBCSTRAIL(buf, icurr))
		    icurr--;
		break;

	    case KEY_RIGHT: case Ctrl('F'):
		if (icurr < iend)
		    icurr++;
		else
		    bell();
		if (icurr < iend && CHKDBCSTRAIL(buf, icurr))
		    icurr++;
		break;

	    // editing keys
	    case KEY_DEL:   case Ctrl('D'):
		if (icurr+1 < iend && CHKDBCSTRAIL(buf, icurr+1)) {
		    // kill next one character.
		    memmove(buf+icurr, buf+icurr+1, iend-icurr);
		    iend--;
		}
		if (icurr < iend) {
		    // kill next one character.
		    memmove(buf+icurr, buf+icurr+1, iend-icurr);
		    iend--;
		}
		break;

	    case Ctrl('H'): case KEY_BS2:
		if (icurr > 0) {
		    // kill previous one charracter.
		    memmove(buf+icurr-1, buf+icurr, iend-icurr+1);
		    icurr--; iend--;
		} else
		    bell();
		if (icurr > 0 && CHKDBCSTRAIL(buf, icurr)) {
		    // kill previous one charracter.
		    memmove(buf+icurr-1, buf+icurr, iend-icurr+1);
		    icurr--; iend--;
		}
		break;

	    case Ctrl('Y'):
		icurr = 0;
		// reuse Ctrl-K code
	    case Ctrl('K'):
		iend = icurr;
		buf[iend] = 0;
		break;

	    // defaults
	    default:

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

		// size check
		if(iend+1 >= len)
		{
		    bell(); continue;
		}

		// prevent incomplete DBCS
		if (c > 0x80 && num_in_buf() &&
		    len - iend < 3)	// we need 3 for DBCS+NUL.
		{
		    drop_input();
		    bell(); 
		    continue;
		}

		// callback 2: data
		if (_vgetcbhandler(cb.data, &abort,
			    c, buf, &icurr, &iend, len, cbparam))
		    continue;

		// size check again, due to data callback.
		if(iend+1 >= len)
		{
		    bell(); continue;
		}

		// add one character.
		memmove(buf+icurr+1, buf+icurr, iend-icurr+1);
		buf[icurr++] = c;
		iend++;

		// callback 3: post
		if (_vgetcbhandler(cb.post, &abort,
			    c, buf, &icurr, &iend, len, cbparam))
		    continue;

		break;
	}
    }

    assert(iend >= 0 && iend < len);
    buf[iend] = 0;

    // final filtering
    if (iend && (flags & VGET_LOWERCASE))
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

    return iend;
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

