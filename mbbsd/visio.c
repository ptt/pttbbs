/* $Id$ */
#include "bbs.h"

/*
 * visio.c
 * High-Level virtual screen input output control
 *
 * Author: piaip, 2008
 *
 * This is not the original visio.c from maple3.
 * We just borrowed its file name and some API prototypes
 * then re-implemented everything :)
 * We will try to keep the API behavior similiar (to help porting)
 * but won't stick to it. 
 * Maybe at the end only 'vmsg' and 'vmsgf' can still be compatible....
 *
 * m3 visio = (ptt) visio+screen/term.
 *
 * This visio contains only high level UI element/widgets.
 *
 * To add API here, please...
 * (1) name the API in prefix of 'v'.
 * (2) use only screen.c APIs.
 * (3) take care of wide screen.
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

inline void
fillns(int n, const char *s)
{
    while (n > 0 && *s)
	outc(*s++), n--;
    if (n > 0)
	outnc(n, ' ');
}

inline void
fillns_ansi(int n, char *s)
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
    outs(str);
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

    // move(b_lines, 0); clrtoeol();
    vget(b_lines, 0, msg, buf, sizeof(buf), LCECHO);
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
	outs(caption); i+= strlen(caption);
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
 * vs_cols_layout(cols, ws, n): 依據 cols (大小寫 n) 的定義計算適合的行寬於 ws
 */

void 
vs_cols_layout(const VCOL *cols, VCOLW *ws, int n)
{
    int i, tw, d = 0;
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

    // try to iterate through all.
    while (tw < MAX_COL) {
	char run = 0;
	d++;
	for (i = 0; i < n; i++)
	{
	    // increase fields if still ok.
	    if (cols[i].maxw - cols[i].minw < d)
		continue;
	    ws[i] ++; 
	    if (++tw >= MAX_COL) break;
	    run ++;
	}
	// if no more fields...
	if (!run) break;
    }
}

/**
 * vs_cols_hdr: 依照已經算好的欄位輸出標題列
 */
void 
vs_cols_hdr    (const VCOL* cols, const VCOLW *ws, int n)
{
    int i;
    char *s;

    outs(ANSI_COLOR(5)); // TODO use theme color?
    for (i = 0; i < n; i++, cols++, ws++)
    {
	int w = *ws;

	s = cols->caption;
	if (!s) s = "";

	if (!cols->usewhole) 
	    w--;

	if (w > 0) {
	    switch(cols->align)
	    {
		case VCOL_ALIGN_LEFT:
		    fillns(w, s);
		    break;

		case VCOL_ALIGN_RIGHT:
		    {
			int l = strlen(s);

			if (l >= w) 
			    l = w;
			else
			    nblank(w - l);

			// simular to left align
			fillns(l, s);
		    }
		    break;

		default:
		    assert(0);
	    }
	}

	// only drop if w < 0 (no space for whole)
	if (!cols->usewhole && w >= 0) 
	    outc(' ');
    }
    outs(ANSI_RESET "\n");
}

/**
 * vs_cols: 依照已經算好的欄位大小進行輸出
 */
void
vs_cols(const VCOL *cols, const VCOLW *ws, int n, ...)
{
    int i = 0, w = 0;
    char *s = NULL;
    char ovattr = 0;

    va_list ap;
    va_start(ap, n);

    for (i = 0; i < n; i++, cols++, ws++)
    {
	s = va_arg(ap, char*);

	// quick check input.
	if (!s) 
	{
	    s = "";
	}
	else if (*s == ESC_CHR && !cols->has_ansi) // special rule to escape
	{
	    outs(s); i--; // one more parameter!
	    ovattr = 1;
	    continue;
	}

	if (cols->attr && !ovattr) 
	    outs(cols->attr);

	w = *ws;

	if (!cols->usewhole) 
	    w--;

	// render value if field has enough space.
	if (w > 0) {
	    switch (cols->align)
	    {
		case VCOL_ALIGN_LEFT:
		    if (cols->has_ansi)
			fillns_ansi(w, s);
		    else
			fillns(w, s);
		    break;

		case VCOL_ALIGN_RIGHT:
		    // complex...
		    {
			int l = 0;
			if (cols->has_ansi)
			    l = strlen_noansi(s);
			else
			    l = strlen(s);

			if (l >= w) 
			    l = w;
			else
			    nblank(w - l);

			// simular to left align
			if (cols->has_ansi)
			    fillns_ansi(l, s);
			else
			    fillns(l, s);
		    }
		    break;

		default:
		    assert(0);
		    break;
	    }
	}

	// only drop if w < 0 (no space for whole)
	if (!cols->usewhole && w >= 0) 
	    outc(' ');

	if (cols->attr || cols->has_ansi || ovattr)
	{
	    if (ovattr)
		ovattr = 0;
	    outs(ANSI_RESET);
	}
    }
    va_end(ap);

    // end line
    outs(ANSI_RESET "\n");
}

