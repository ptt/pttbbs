/* $Id$ */
#include "bbs.h"

/*
 * "pmore" is "piaip's more", NOT "PTT's more"!!!
 *
 * piaip's new implementation of pager(more) with mmap,
 * designed for unlimilited length(lines).
 *
 * Author: Hung-Te Lin (piaip), June 2005.
 * <piaip@csie.ntu.edu.tw>
 *
 * MAJOR IMPROVEMENTS:
 *  - Clean source code, and more readble to mortal
 *  - Correct navigation
 *  - Excellent search ability (for correctness and user behavior)
 *  - Less memory consumption (mmap is not considered)
 *  - Better support for large terminals
 *  - Unlimited file length and line numbers
 *
 * TODO:
 *  - Speed up with supporting Scroll [done]
 *  - Support PTT_PRINTS [done]
 *  - Wrap long lines or left-right wide navigation
 *  - Big5 truncation
 * 
 * WONTDO:
 *  - The message seperator line is different from old more.
 *    I decided to abandon the old style (which is buggy).
 *    > old   style: increase one line to show seperator
 *    > pmore style: use blank line for seperator.
 *
 * HINTS:
 *  - Remember mmap pointers are NOT null terminated strings.
 *    You have to use strn* APIs and make sure not exceeding mmap buffer.
 *    DO NOT USE strcmp, strstr, strchr, ...
 *  - Scroll handling is painful. If you displayed anything on screen,
 *    remember to MFDISP_DIRTY();
 *  - To be portable between most BBS systems, pmore is designed to
 *    workaround most BBS bugs inside itself.
 */

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>

// Platform Related. NoSync is faster but if we don't have it...
#ifdef MAP_NOSYNC
#define MF_MMAP_OPTION (MAP_NOSYNC)
#else
#define MF_MMAP_OPTION (MAP_SHARED)
#endif

// --------------------------------------------------------------- <FEATURES>
#define PMORE_USE_PTT_PRINTS
#define PMORE_USE_SCROLL
#define PMORE_PRELOAD_SIZE (512*1024L) // on busy system set smaller or undef
// -------------------------------------------------------------- </FEATURES>

#define DEBUG
int debug = 0;

// --------------------------------------------- <Defines and constants>

// --------------------------- <Display>
// Escapes. I don't like \033 everywhere.
#define ESC_NUM (0x1b)
#define ESC_STR "\x1b"
#define ESC_CHR '\x1b'

// Common ANSI commands.
#define ANSI_RESET  (ESC_STR "[m")
#define ANSI_COLOR(x) ESC_STR "[" #x "m"

// Poor BBS terminal system Workarounds
// - Most BBS implements clrtoeol() as fake command
//   and usually messed up when output ANSI quoted string.
// - A workaround is suggested by kcwu:
//   https://opensvn.csie.org/traccgi/pttbbs/trac.cgi/changeset/519
#define FORCE_CLRTOEOL() outs(ESC_STR "[K");
// --------------------------- </Display>

// --------------------------- <Main Navigation>
typedef struct
{
    unsigned char 
	rawmode,	// show file as-is.
	*start, *end,	// file buffer
	*disps, *dispe,	// disply start/end
	*maxdisps;	// a very special pointer, 
    			//   consider as "disps of last page"
    off_t len; 		// file total length
    long  lineno,	// lineno of disps
	  oldlineno,	// last drawn lineno, < 0 means full update
	  maxlinenoS;	// lineno of maxdisps, "S"! 
    			// NOT REAL MAX LINENO NOR FILELENGTH!!!
} MmappedFile;

MmappedFile mf = { 
    0, 0, 0, 0, 0, 0, 
    0, 0, -1L, -1L 
};	// current file

/* mf_* navigation commands and return value meanings */
enum {
    MFNAV_OK,		// navigation ok
    MFNAV_EXCEED,	// request exceeds buffer
} MF_NAV_COMMANDS;

// Navigation units (dynamic, so not in enum const)
#define MFNAV_PAGE  (t_lines-2)	// when navigation, how many lines in a page to move
#define MFDISP_PAGE (t_lines-1) // for display, the real number of lines to be shown.
#define MFDISP_DIRTY() { mf.oldlineno = -1; }
// --------------------------- </Main Navigation>

// --------------------------- <Aux. Structures>
/* pretty format header */
typedef struct
{
    int lines;	// header lines
    int authorlen;
    int boardlen;
} ArticleHeader;

ArticleHeader ah;

/* search records */
typedef struct
{
    int  len;
    int (*cmpfunc) (const char *, const char *, size_t);
    char search_str[81];	// maybe we can change to dynamic allocation
} SearchRecord;

SearchRecord sr = { 0, strncmp, "" };

enum {
    MFSEARCH_FORWARD,
    MFSEARCH_BACKWARD,
} MFSEARCH_DIRECTION;

// Reset structures
#define RESETMF() { memset(&mf, 0, sizeof(mf)); \
    mf.maxlinenoS = mf.oldlineno = -1; }
#define RESETAH() { memset(&ah, 0, sizeof(ah)); \
    ah.lines = ah.authorlen= ah.boardlen = -1; }
// --------------------------- </Aux. Structures>

// --------------------------------------------- </Defines and constants>


int mf_backward(int);	// used by mf_attach
void mf_sync_lineno();	// used by mf_attach

/* 
 * mmap basic operations 
 */
int mf_attach(unsigned char *fn)
{
    struct stat st;
    int fd = open(fn, O_RDONLY, 0600);

    if(fd < 0)
	return 0;

    if (fstat(fd, &st) || ((mf.len = st.st_size) <= 0) || S_ISDIR(st.st_mode))
    {
	mf.len = 0;
	close(fd);
	return 0;
    }

    /*
    mf.len = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    */

    mf.start = mmap(NULL, mf.len, PROT_READ, 
	    MF_MMAP_OPTION, fd, 0);
    close(fd);

    if(mf.start == MAP_FAILED)
    {
	RESETMF();
	return 0;
    }

    mf.end = mf.start + mf.len;
    mf.disps = mf.dispe = mf.start;
    mf.lineno = 0;

    // build maxdisps
    mf.disps = mf.end - 1;
    mf_backward(MFNAV_PAGE);
    mf.maxdisps = mf.disps;

#ifdef PMORE_PRELOAD_SIZE
    if(mf.len <= PMORE_PRELOAD_SIZE)
	mf_sync_lineno(); // maxlinenoS will be automatically updated
    else
#endif
	mf.maxlinenoS = -1;

    mf.disps = mf.dispe = mf.start;
    mf.lineno = 0;
    return  1;
}

void mf_detach()
{
    if(mf.start) {
	munmap(mf.start, mf.len);
	RESETMF();
    }
}

/*
 * lineno calculation, and moving
 */
void mf_sync_lineno()
{
    unsigned char *p;

    if(mf.disps == mf.maxdisps && mf.maxlinenoS >= 0)
    {
	mf.lineno = mf.maxlinenoS;
    } else {
	mf.lineno = 0;
	for (p = mf.start; p < mf.disps; p++)
	    if(*p == '\n')
		mf.lineno ++;

	if(mf.disps == mf.maxdisps && mf.maxlinenoS < 0)
	    mf.maxlinenoS = mf.lineno;
    }
}

int mf_backward(int lines)
{
    int flFirstLine = 1;
    // first, because we have to trace back to line beginning,
    // add one line.
    lines ++;

    // now try to rollback for lines
    if(lines == 1)
    {
	/* special case! just rollback to start */
	while ( mf.disps > mf.start && 
		*(mf.disps-1) != '\n')
	    mf.disps --;
	mf.disps --;
	lines --;
    }
    else while(mf.disps > mf.start && lines > 0)
    {
	while (mf.disps > mf.start && *--mf.disps != '\n');
	if(flFirstLine)
	{
	    flFirstLine = 0; lines--;
	    continue;
	}

	if(mf.disps >= mf.start) 
	    mf.lineno--, lines--;
    }

    if(mf.disps == mf.start)
	mf.lineno = 0;
    else
	mf.disps ++;

    if(lines > 0)
	return MFNAV_OK;
    else
	return MFNAV_EXCEED;
}

int mf_forward(int lines)
{
    while(mf.disps <= mf.maxdisps && lines > 0)
    {
	while (mf.disps <= mf.maxdisps && *mf.disps++ != '\n');

	if(mf.disps <= mf.maxdisps)
	    mf.lineno++, lines--;
    }

    if(mf.disps > mf.maxdisps)
	mf.disps = mf.maxdisps;

    /* please make sure you have lineno synced. */
    if(mf.disps == mf.maxdisps && mf.maxlinenoS < 0)
	mf.maxlinenoS = mf.lineno;

    if(lines > 0)
	return MFNAV_OK;
    else
	return MFNAV_EXCEED;
}

int mf_goTop()
{
    mf.disps = mf.start;
    mf.lineno = 0;
    return MFNAV_OK;
}

int mf_goBottom()
{
    mf.disps = mf.maxdisps;
    /*
    mf.disps = mf.end-1;
    mf_backward(MFNAV_PAGE);
    */
    mf_sync_lineno();

    return MFNAV_OK;
}

int mf_goto(int lineno)
{
    mf.disps = mf.start;
    mf.lineno = 0;
    return mf_forward(lineno);
}

int mf_viewedNone()
{
    return (mf.disps <= mf.start);
}

int mf_viewedAll()
{
    return (mf.dispe >= mf.end);
}
/*
 * search!
 */
int mf_search(int direction)
{
    unsigned char *s = sr.search_str;
    int l = sr.len;
    int flFound = 0;

    if(!*s)
	return 0;

    if(direction ==  MFSEARCH_FORWARD) 
    {
	mf_forward(1);
	while(mf.disps < mf.end - l)
	{
	    if(sr.cmpfunc(mf.disps, s, l) == 0)
	    {
		flFound = 1;
		break;
	    } else
		mf.disps ++;
	}
	mf_backward(0);
	if(mf.disps > mf.maxdisps)
	    mf.disps = mf.maxdisps;
	mf_sync_lineno();
    } 
    else if(direction ==  MFSEARCH_BACKWARD) 
    {
	mf_backward(1);
	while (!flFound && mf.disps > mf.start)
	{
	    while(!flFound && mf.disps < mf.end-l && *mf.disps != '\n')
	    {
		if(sr.cmpfunc(mf.disps, s, l) == 0)
		{
		    flFound = 1;
		} else
		    mf.disps ++;
	    }
	    if(!flFound)
		mf_backward(1);
	}
	mf_backward(0);
	if(mf.disps < mf.start)
	    mf.disps = mf.start;
	mf_sync_lineno();
    }
    if(flFound)
	MFDISP_DIRTY();
    return flFound;
}

/*
 * Format Related
 */
void mf_parseHeader()
{
    /* format:
     * AUTHOR: author BOARD: blah
     * XXX: xxx
     * XXX: xxx
     * [blank, fill with seperator]
     *
     * #define STR_AUTHOR1     "作者:"
     * #define STR_AUTHOR2     "發信人:"
     * #define STR_POST1       "看板:"
     * #define STR_POST2       "站內:"
     */
    RESETAH();
    if(mf.len > LEN_AUTHOR2)
    {
	if (strncmp(mf.start, STR_AUTHOR1, LEN_AUTHOR1) == 0)
	{
	    ah.lines = 3;	// local
	    ah.authorlen = LEN_AUTHOR1;
	} 
	else if (strncmp(mf.start, STR_AUTHOR2, LEN_AUTHOR2) == 0)
	{
	    ah.lines = 4;
	    ah.authorlen = LEN_AUTHOR2;
	}
	/* traverse for author length */
	{
	    unsigned char *p = mf.start;
	    unsigned char *pb = p;

	    /* first, go to line-end */
	    while(p < mf.end && *p != '\n')
		p++;
	    pb = p;
	    /* next, rollback for ':' */
	    while(p > mf.start && *p != ':')
		p--;
	    if(p > mf.start && *p == ':')
	    {
		ah.boardlen = pb - p;
		while (p > mf.start && *p != ' ')
		    p--;
		if( *p == ' ')
		{
		    ah.authorlen = p - mf.start - ah.authorlen;
		} else
		    ah.boardlen = -1, ah.authorlen = -1;
	    } else
		ah.authorlen = -1;
	}
    }
}

/*
 * display mf content from disps for MFDISP_PAGE
 */
#define STR_ANSICODE    "[0123456789;,"
#define DISP_HEADS_LEN (4)	// strlen of each heads
static const char *disp_heads[] = {"作者", "標題", "時間", "轉信"};

void mf_disp()
{
    int lines = 0, col = 0, currline = 0;
    int startline = 0, endline = MFDISP_PAGE-1;
    // int x_min = 0;
    int x_max = t_columns - 1;
    int header_w = (t_columns - 1 - (t_columns-1)%2);
    /*
     * it seems like that BBS scroll has some bug
     * if we scroll a fulfilled buffer, so leave one space.
     */

#ifdef PMORE_USE_SCROLL
    /* process scrolling */
    if (mf.oldlineno >= 0 && mf.oldlineno != mf.lineno)
    {
	int scrll = mf.lineno - mf.oldlineno, i;
	int reverse = (scrll > 0 ? 0 : 1);

	if(reverse) 
	    scrll = -scrll;
	else
	{
	    /* because bottom status line is also scrolled,
	     * we have to erase it here.
	     */
	    move(b_lines, 0);
	    clrtoeol();
	}

	if(scrll > MFDISP_PAGE)
	    scrll = MFDISP_PAGE;

	i = scrll;
	while(i-- > 0)
	    if (reverse)
		rscroll();	// v
	    else
		scroll();	// ^

	if(reverse)
	{
	    startline = 0;	// v
	    endline = scrll-1;
	    // clear the line which will be scrolled
	    // to bottom (status line position).
	    move(b_lines, 0);
	    clrtoeol();
	}
	else
	{
	    startline = MFDISP_PAGE - scrll; // ^
	    endline   = MFDISP_PAGE - 1;
	}
	move(startline, 0);
	// return;	// uncomment if you want to observe scrolling
    }
    else
#endif
	clear(), move(0, 0);

    mf.dispe = mf.disps;
    while (lines < MFDISP_PAGE) 
    {
	int inAnsi = 0;
	int skip = 0;

	currline = mf.lineno + lines;
	col = 0;
	
	/* Is currentline visible? */
	if (lines < startline || lines > endline)
	{
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	    skip = 1;	/* prevent printing trailing '\n' */
	}
	/* Now, consider what kind of line
	 * (header, seperator, or normal text)
	 * is current line.
	 */
	else if (!mf.rawmode && currline == ah.lines)
	{
	    /* case 1, header seperator line */
	    outs(ANSI_COLOR(36));
	    for(col = 0; col < header_w; col+=2)
	    {
		// prints("%02d", col);
		outs("─");
	    }
	    outs(ANSI_RESET);
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	    col = x_max-1;
	} 
	else if (!mf.rawmode && currline < ah.lines)
	{
	    /* case 2, we're printing headers */
	    int w = t_columns, i_author = 0;
	    int flDrawBoard = 0, flDrawAuthor = 0;
	    const char *ph = disp_heads[currline];

	    if (currline == 0 && ah.boardlen > 0)
		flDrawAuthor = 1;
draw_header:
	    if(flDrawAuthor)
		w = header_w - ah.boardlen - 6;
	    else
		w = header_w;

	    outs(ANSI_COLOR(47;34) " "); col++;

	    /* special case for STR_AUTHOR2 */
	    if(!flDrawBoard)
	    {
		outs(ph);
		col += DISP_HEADS_LEN;	// strlen(disp_heads[currline])
	    } else {
		/* display as-is */
		while (*mf.dispe != ':' && *mf.dispe != '\n')
		if(col++ <= header_w)
		    outc(*mf.dispe++);
	    }

	    while (*mf.dispe != ':' && *mf.dispe != '\n')
		mf.dispe ++;

	    if(*mf.dispe == ':') {
		outs(" " ANSI_COLOR(44;37)); 
		col++;
		mf.dispe ++;
	    }

	    while (col < w) {	// -2 to match seperator
		int flCanDraw = (*mf.dispe != '\n');

		if(flDrawAuthor)
		    flCanDraw = i_author < ah.authorlen;
		if(flCanDraw)
		{
		    /* strip ansi in headers */
		    unsigned char c = *mf.dispe++;
		    if(inAnsi)
		    {
			if (!strchr(STR_ANSICODE, c))
			    inAnsi = 0;
		    } else {
			if(c == ESC_CHR)
			    inAnsi = 1;
			else
			    outc(c), col++, i_author++;
		    }
		} else {
		    outc(' '), col++;
		}
	    }

	    if (flDrawAuthor)
	    {
		flDrawBoard = 1;
		flDrawAuthor = 0;
		while(*mf.dispe == ' ')
		    mf.dispe ++;
		goto draw_header;
	    }

	    outs(ANSI_RESET);
	    // skip to end of line
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	    col = x_max-1;
	} 
	else if(mf.dispe < mf.end)
	{
	    /* case 3, normal text */
	    long dist = mf.end - mf.dispe;
	    long flResetColor = 0;
	    int  srlen = -1;

	    // first check quote
	    if(!mf.rawmode)
	    {
		if(dist > 1 && 
			(*mf.dispe == ':' || *mf.dispe == '>') && 
			*(mf.dispe+1) == ' ')
		{
		    outs(ANSI_COLOR(36));
		    flResetColor = 1;
		} else if (dist > 2 && 
			(!strncmp(mf.dispe, "※", 2) || 
			 !strncmp(mf.dispe, "==>", 3)))
		{
		    outs(ANSI_COLOR(32));
		    flResetColor = 1;
		}
	    }

	    while(mf.dispe < mf.end && *mf.dispe != '\n')
	    {
		if(inAnsi)
		{
		    if (!strchr(STR_ANSICODE, *mf.dispe))
			inAnsi = 0;
		    if(col <= x_max)
			outc(*mf.dispe);
		} else {
		    if(*mf.dispe == ESC_CHR)
			inAnsi = 1;
		    else if(srlen < 0 && sr.search_str[0] && // support search
			    //tolower(sr.search_str[0]) == tolower(*mf.dispe) &&
			    mf.end - mf.dispe > sr.len &&
			    sr.cmpfunc(mf.dispe, sr.search_str, sr.len) == 0)
		    {
			    outs(ANSI_COLOR(7)); 
			    srlen = sr.len-1;
			    flResetColor = 1;
		    }

#ifdef PMORE_USE_PTT_PRINTS
		    /* special case to resolve dirty Ptt_Prints */
		    if(inAnsi && 
			    mf.end - mf.dispe > 2 &&
			    *(mf.dispe+1) == '*')
		    {
			int i;
			char buf[64];	// make sure ptt_prints will not exceed

			memset(buf, 0, sizeof(buf));
			strncpy(buf, mf.dispe, 3);  // ^[[*s
			mf.dispe += 2;

			Ptt_prints(buf, NO_RELOAD); // result in buf
			i = strlen(buf);

			if (col + i > x_max)
			    i = x_max - col;
			if(i > 0)
			{
			    buf[i] = 0;
			    col += i;
			    outs(buf);
			}
			inAnsi = 0;
		    } else
#endif
		    {
			if(col <= x_max)
			    outc(*mf.dispe);
			if(!inAnsi)
			{
			    col++;
			    if (srlen == 0)
				outs(ANSI_RESET);
			    if(srlen >= 0)
				srlen --;
			}
		    }
		}
		mf.dispe ++;
	    }
	    if(flResetColor)
		outs(ANSI_RESET);
	}

	if(mf.dispe < mf.end) 
	    mf.dispe ++;

	if(!skip)
	{
	    if(col < x_max)	/* can we do so? */
	    {
		FORCE_CLRTOEOL();
		outc('\n');
	    }
	    else
		// outc('>'),
		move(lines+1, 0);
	}
	lines ++;
    }
    mf.oldlineno = mf.lineno;
}

/* --------------------- MAIN PROCEDURE ------------------------- */

static const char    * const pmore_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(j)(↑)               上捲一行",
    "(k)(↓)(Enter)        下捲一行",
    "(^B)(PgUp)(BackSpace) 上捲一頁",
    "(→)(PgDn)(Space)     下捲一頁",
    "(0)(g)(Home)          檔案開頭",
    "($)(G)(End)           檔案結尾",
    "(;/:)                 跳至某行/某頁",
    "數字鍵 1-9            跳至輸入的行號",
    "\01其他功\能鍵",
    "(/)                   搜尋字串",
    "(n/N)                 重複正/反向搜尋",
    "(Ctrl-T)              存到暫存檔",
    "(f/b)                 跳至下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "(t/[-/]+)             主題式閱\讀:循序/前/後篇",
    "(\\)                   切換顯示原始內容",	// this IS already aligned!
    "(q)(←)               結束",
    "(h)(H)(?)             本說明畫面",
#ifdef DEBUG
    "(d)                   切換除錯(debug)模式",
#endif
    "\01本系統使用 piaip 的新式瀏覽程式",
    NULL
};

/*
 * piaip's more, a replacement for old more
 */
int pmore(char *fpath, int promptend)
{
    int  flExit = 0, retval = 0;
    int  ch = 0;

    STATINC(STAT_MORE);
    if(!mf_attach(fpath))
	return -1;

    /* reset and parse article header */
    mf_parseHeader();

    clear();
    while(!flExit)
    {
	mf_disp();

	if(promptend == NA && mf_viewedAll())
	    break;

	move(b_lines, 0);
	// clrtoeol(); // this shall be done in mf_disp to speed up.

	/* PRINT BOTTOM STATUS BAR */
	if(debug)
	prints("L#%d pmt=%d Dsp:%08X/%08X/%08X, F:%08X/%08X(%d) tScr(%dx%d)",
		(int)mf.lineno, 
		promptend,
		(unsigned int)mf.disps, 
		(unsigned int)mf.maxdisps,
		(unsigned int)mf.dispe,
		(unsigned int)mf.start, (unsigned int)mf.end,
		(int)mf.len,
		t_columns,
		t_lines
		);
	else
	{
	    char *printcolor;
	    char buf[256];	// orz
	    int prefixlen = 0;
	    int barlen = 0;
	    int postfixlen = 0;

	    if(mf_viewedAll())
		printcolor = ANSI_COLOR(37;44);
	    else if (mf_viewedNone())
		printcolor = ANSI_COLOR(34;46);
	    else
		printcolor = ANSI_COLOR(33;45);

	    outs(ANSI_RESET);
	    outs(printcolor);
	    if(mf.maxlinenoS >= 0)
		sprintf(buf,
			"  瀏覽 第 %1d/%1d 頁 ",
			(int)(mf.lineno / MFNAV_PAGE)+1,
			(int)(mf.maxlinenoS / MFNAV_PAGE)+1
		       );
	    else
		sprintf(buf,
			"  瀏覽 第 %1d 頁 ",
			(int)(mf.lineno / MFNAV_PAGE)+1
		       );
	    outs(buf); prefixlen += strlen(buf);

	    outs(ANSI_COLOR(1;30;47));

	    sprintf(buf,
		    " 閱\讀進度%3d%%, 目前顯示: 第 %02d~%02d 行 ",
		    (int)((unsigned long)(mf.dispe-mf.start) * 100 / mf.len),
		    (int)(mf.lineno + 1),
		    (int)(mf.lineno + MFDISP_PAGE)
		   );

	    outs(buf); prefixlen += strlen(buf);

#define TRAILINGMSGLEN (23)  // trailing msg columns
	    postfixlen = TRAILINGMSGLEN;

	    if (prefixlen + postfixlen + 1 > t_columns)
		postfixlen = 0;
	    barlen = t_columns - 1 - postfixlen - prefixlen;

	    while(barlen-- > 0)
		outc(' ');

	    if(postfixlen > 0)	/* enough buffer */
		outs(
			ANSI_COLOR(0;31;47) "(h)" 
			ANSI_COLOR(30) "按鍵說明 "
			ANSI_COLOR(31) "←[q]" 
			ANSI_COLOR(30) "離開  "
		    );
	    outs(ANSI_RESET);
	    FORCE_CLRTOEOL();
	}

	/* igetch() will do refresh(); */
	ch = igetch();
	switch (ch) {
	    /* ------------------ EXITING KEYS ------------------ */
	    case 'r':
	    case 'R':
	    case 'Y':
	    case 'y':
		flExit = 1,	retval = 999;
		break;
	    case 'X':
		flExit = 1,	retval = 998;
		break;
	    case 'A':
		flExit = 1,	retval = AUTHOR_PREV;
		break;
	    case 'a':
		flExit = 1,	retval = AUTHOR_NEXT;
		break;
	    case 'F':
	    case 'f':
		flExit = 1,	retval = READ_NEXT;
		break;
	    case 'B':
	    case 'b':
		flExit = 1,	retval = READ_PREV;
		break;
	    case KEY_LEFT:
	    case 'q':
		flExit = 1,	retval = FULLUPDATE;
		break;

		/* from Kaede, thread reading */
	    case ']':
	    case '+':
		flExit = 1,	retval = RELATE_NEXT;
		break;
	    case '[':
	    case '-':
		flExit = 1,	retval = RELATE_PREV;
		break;
	    case '=':
		flExit = 1,	retval = RELATE_FIRST;
		break;
	    /* ------------------ NAVIGATION KEYS ------------------ */
	    /* Simple Navigation */
	    case 'j':
	    case 'J':
		mf_backward(1);
		break;
	    case 'k':
	    case 'K':
		mf_forward(1);
		break;

	    case Ctrl('F'):
	    case KEY_PGDN:
		mf_forward(MFNAV_PAGE);
		break;
	    case Ctrl('B'):
	    case KEY_PGUP:
		mf_backward(MFNAV_PAGE);
		break;

	    case '0':
	    case 'g':
	    case KEY_HOME:
		mf_goTop();
		break;
	    case '$':
	    case 'G':
	    case KEY_END:
		mf_goBottom();
		break;

	    /* Compound Navigation */
	    case '\r':
	    case '\n':
	    case KEY_DOWN:
		if (mf_viewedAll() ||
			(promptend == 2 && (ch == '\r' || ch == '\n')))
		    flExit = 1, retval = READ_NEXT;
		else
		    mf_forward(1);
		break;

	    case ' ':
		if (mf_viewedAll())
		    flExit = 1, retval = READ_NEXT;
		else
		    mf_forward(MFNAV_PAGE);
		break;
	    case KEY_RIGHT:
		if(mf_viewedAll())
		    promptend = 0, flExit = 1, retval = 0;
		else
		    mf_forward(MFNAV_PAGE);
		break;

	    case KEY_UP:
		if(mf_viewedNone())
		    flExit = 1, retval = READ_PREV;
		else
		    mf_backward(1);
		break;
	    case Ctrl('H'):
		if(mf_viewedNone())
		    flExit = 1, retval = READ_PREV;
		else
		    mf_backward(MFNAV_PAGE);
		break;

	    case 't':
		if (mf_viewedAll())
		    flExit = 1, retval = RELATE_NEXT;
		else
		    mf_forward(MFNAV_PAGE);
		break;
	    /* ------------------ SEARCH  KEYS ------------------ */
	    case '/':
		{
		    char ans[4] = "n";

		    sr.search_str[0] = 0;
		    getdata_buf(b_lines - 1, 0, "[搜尋]關鍵字:", sr.search_str,
			    40, DOECHO);
		    if (sr.search_str[0]) {
			if (getdata(b_lines - 1, 0, "區分大小寫(Y/N/Q)? [N] ",
				    ans, sizeof(ans), LCECHO) && *ans == 'y')
			    sr.cmpfunc = strncmp;
			else
			    sr.cmpfunc = strncasecmp;
			if (*ans == 'q')
			    sr.search_str[0] = 0;
		    }
		    sr.len = strlen(sr.search_str);
		    mf_search(MFSEARCH_FORWARD);
		    MFDISP_DIRTY();
		}
		break;
	    case 'n':
		mf_search(MFSEARCH_FORWARD);
		break;
	    case 'N':
		mf_search(MFSEARCH_BACKWARD);
		break;
	    /* ------------------ SPECIAL KEYS ------------------ */
	    case '1': case '2': case '3': case '4': case '5':
	    case '6': case '7': case '8': case '9':
	    case ';': case ':':
		{
		    char buf[10] = "";
		    int  i = 0;
		    int  pageMode = (ch == ':');
		    if (ch >= '1' && ch <= '9')
			buf[0] = ch, buf[1] = 0;

		    getdata_buf(b_lines-1, 0, 
			    (pageMode ? "跳至此頁: " : "跳至此行: "),
			    buf, 7, DOECHO);
		    if(buf[0]) {
			i = atoi(buf);
			if(i-- > 0)
			    mf_goto(i * (pageMode ? MFNAV_PAGE : 1));
		    }
		    MFDISP_DIRTY();
		}
		break;

	    case Ctrl('T'):
		{
		    char buf[10];
		    getdata(b_lines - 1, 0, "把這篇文章收入到暫存檔？[y/N] ",
			    buf, 4, LCECHO);
		    if (buf[0] == 'y') {
			setuserfile(buf, ask_tmpbuf(b_lines - 1));
                        Copy(fpath, buf);
		    }
		    MFDISP_DIRTY();
		}
		break;

	    case 'h':
	    case 'H':
	    case '?':
		// help
		show_help(pmore_help);
		MFDISP_DIRTY();
		break;

	    case 'E':
		// admin edit any files other than ve help file
		if (HAS_PERM(PERM_SYSOP) && strcmp(fpath, "etc/ve.hlp")) {
		    mf_detach();
		    vedit(fpath, NA, NULL);
		    return 0;
		}
		break;
	    case '\\':
		mf.rawmode = !mf.rawmode;
		MFDISP_DIRTY();
		break;
#ifdef DEBUG
	    case 'd':
		debug = !debug;
		MFDISP_DIRTY();
		break;
#endif
	}
    }

    mf_detach();
    if (retval == 0 && promptend) {
	pressanykey();
	clear();
    } else
	outs(reset_color);

    return retval;
}

/* vim:sw=4
 */
