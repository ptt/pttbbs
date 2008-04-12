/* $Id$ */
#include "bbs.h"

/*
 * visio.c
 * High-level virtual screen input output control
 *
 * This is not the original visio.c from maple3.
 * m3 visio = (ptt) visio+screen.
 * This visio contains only high level UI element/widgets.
 * In fact the only APIs from m3 are vmsg/vget...
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

// ---- HIGH LEVEL API -----------------------------------------------

/**
 * 在最底部印出 caption msg 的形式
 * msg 中若有 () 則會變色為， \t 後的文字會靠右。
 * 最後面會自動留一個空白 (以避免自動偵測中文字的問題)。
 */
void 
vfooter(const char *caption, const char *msg)
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
	    outs(VCLR_FOOTER_QUOTE);
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

