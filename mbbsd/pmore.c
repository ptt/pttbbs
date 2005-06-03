#include "bbs.h"

/*
 * piaip's new implementation of pager(more) with mmap,
 * designed for unlimilited length(lines).
 *
 * Author: Hung-Te Lin (piaip), 2005
 * <piaip@csie.ntu.edu.tw>
 */

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <string.h>

// Platform Related. NoSync is faster but if we don't have it...
#ifndef MAP_NOSYNC
#define MAP_NOSYNC MAP_SHARED
#endif

//#define DEBUG
#define SUPPORT_PTT_PRINTS

typedef struct
{
    unsigned char 
	*start , *end,	// file buffer
	*disps,	*dispe,	// disply start/end
	*maxdisps;	// a very special pointer, 
    			//   consider as "disps of last page"
    off_t len, 		// file total length
	  lineno,	// lineno of disps
	  oldlineno;	// last drawn lineno, < 1 means full update
} MmappedFile;

MmappedFile mf = { 0, 0, 0, 0, 0, 0, 0 };	// current file

/* mf_* navigation commands and return value meanings */
enum {
    MFNAV_OK,		// navigation ok
    MFNAV_EXCEED,	// request exceeds buffer
} MF_NAV_COMMANDS;

#define MFNAV_PAGE  (t_lines-2)	// when navigation, how many lines in a page to move
#define MFDISP_PAGE (t_lines-1) // for display, the real number of lines to be shown.
#define ANSI_ESC (0x1b)
#define RESETMF() { memset(&mf, 0, sizeof(mf)); mf.oldlineno = -1; }
#define RESETAH() { memset(&ah, 0, sizeof(ah)); }

/* search records */
typedef struct
{
    int  len;
    int (*cmpfunc) (const char *, const char *, size_t);
    char search_str[81];
} SearchRecord;

SearchRecord sr = { 0, strncmp, "" };

enum {
    MFSEARCH_FORWARD,
    MFSEARCH_BACKWARD,
} MFSEARCH_DIRECTION;

int mf_backward(int);	// used by mf_attach

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

//    mf.len = lseek(fd, 0L, SEEK_END);
//    lseek(fd, 0, SEEK_SET);

    mf.start = mmap(NULL, mf.len, PROT_READ, 
	    MAP_NOSYNC, fd, 0);
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
    mf.lineno = 0;
    for (p = mf.start; p < mf.disps; p++)
	if(*p == '\n')
	    mf.lineno ++;
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
    // lineno?
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
    return (mf.dispe >= mf.end-1);
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
    return flFound;
}

/*
 * Format Related
 */
typedef struct
{
    int lines;	// header lines
    int authorlen;
    int boardlen;
} ArticleHeader;

ArticleHeader ah;

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
    ah.lines 	= -1;
    ah.authorlen= -1;
    ah.boardlen = -1;
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

    /* process scrolling */
    if (mf.oldlineno >= 0 && mf.oldlineno != mf.lineno)
    {
	int scrll = mf.lineno - mf.oldlineno, i;
	int reverse = (scrll > 0 ? 0 : 1);

	if(reverse) 
	    scrll = -scrll;
	if(scrll > MFDISP_PAGE-1)
	    scrll = MFDISP_PAGE-1;

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
	}
	else
	{
	    startline = MFDISP_PAGE - 1 - scrll;
	    endline = MFDISP_PAGE - 1;
	}
	move(startline, 0);
    }
    else
	clear(), move(0, 0);

    mf.dispe = mf.disps;
    while (lines < MFDISP_PAGE) 
    {
	int inAnsi = 0;

	currline = mf.lineno + lines;
	col = 0;
	
	/* Is currentline visible? */
	if (lines < startline || lines > endline)
	{
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	    col = t_columns;	/* prevent printing trailing '\n' */
	}
	/* Now, consider what kind of line
	 * (header, seperator, or normal text)
	 * is current line.
	 */
	else if (currline == ah.lines)
	{
	    /* case 1, header seperator line */
	    outs("\033[36m");
	    for(col = 0; col < t_columns -2; col+=2)
	    {
		outs("─");
	    }
	    outs("\033[m");
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	} 
	else if (currline < ah.lines)
	{
	    /* case 2, we're printing headers */
	    int w = t_columns - 2, i_author = 0;
	    int flDrawBoard = 0, flDrawAuthor = 0;
	    const char *ph = disp_heads[currline];

	    if (currline == 0 && ah.boardlen > 0)
		flDrawAuthor = 1;
draw_header:
	    if(flDrawAuthor)
		w = t_columns - 2 - ah.boardlen - 6;
	    else
		w = t_columns - 2;

	    outs("\033[47;34m "); col++;

	    /* special case for STR_AUTHOR2 */
	    if(!flDrawBoard)
	    {
		outs(ph);
		col += DISP_HEADS_LEN;	// strlen(disp_heads[currline])
	    } else {
		/* display as-is */
		while (*mf.dispe != ':' && *mf.dispe != '\n')
		if(col++ < t_columns)
		    outc(*mf.dispe++);
	    }

	    while (*mf.dispe != ':' && *mf.dispe != '\n')
		mf.dispe ++;

	    if(*mf.dispe == ':') {
		outs(" \033[44;37m"); col++;
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
			if(c == ANSI_ESC)
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

	    outs("\033[m");
	    // skip to end of line
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	} 
	else if(mf.dispe < mf.end)
	{
	    /* case 3, normal text */
	    long dist = mf.end - mf.dispe;
	    long flResetColor = 0;
	    int  srlen = -1;

	    // first check quote
	    if(dist > 1 && 
		    (*mf.dispe == ':' || *mf.dispe == '>') && 
		    *(mf.dispe+1) == ' ')
	    {
		outs("\033[36m");
		flResetColor = 1;
	    } else if (dist > 2 && 
		    (!strncmp(mf.dispe, "※", 2) || 
		     !strncmp(mf.dispe, "==>", 3)))
	    {
		outs("\033[32m");
		flResetColor = 1;
	    }

	    while(mf.dispe < mf.end && *mf.dispe != '\n')
	    {
		if(inAnsi)
		{
		    if (!strchr(STR_ANSICODE, *mf.dispe))
			inAnsi = 0;
		    if(col < t_columns)
			outc(*mf.dispe);
		} else {
		    if(*mf.dispe == ANSI_ESC)
			inAnsi = 1;
		    else if(srlen < 0 && sr.search_str[0] && // support search
			    //tolower(sr.search_str[0]) == tolower(*mf.dispe) &&
			    mf.end - mf.dispe > sr.len &&
			    sr.cmpfunc(mf.dispe, sr.search_str, sr.len) == 0)
		    {
			    outs("\033[7m"); 
			    srlen = sr.len-1;
			    flResetColor = 1;
		    }

#ifdef SUPPORT_PTT_PRINTS
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

			if (col + i >= t_columns)
			    i = t_columns - col;
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
			if(col < t_columns)
			    outc(*mf.dispe);
			if(!inAnsi)
			{
			    col++;
			    if (srlen == 0)
				outs("\033[m");
			    if(srlen >= 0)
				srlen --;
			}
		    }
		}
		mf.dispe ++;
	    }
	    if(flResetColor)
		outs("\033[m");
	}

	if(mf.dispe < mf.end) 
	    mf.dispe ++;
	if(col < t_columns)
	    outc('\n');
	lines ++;
    }
    mf.oldlineno = mf.lineno;
}

/* --------------------- MAIN PROCEDURE ------------------------- */

static const char    * const pmore_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(↑)                  上捲一行",
    "(↓)(Enter)           下捲一行",
    "(^B)(PgUp)(BackSpace) 上捲一頁",
    "(→)(PgDn)(Space)     下捲一頁",
    "(0)(g)(Home)          檔案開頭",
    "($)(G) (End)          檔案結尾",
    "\01其他功\能鍵",
    "(/)                   搜尋字串",
    "(n/N)                 重複正/反向搜尋",
//    "(TAB)                 URL連結",
    "(Ctrl-T)              存到暫存檔",
    "(:/f/b)               跳至某頁/下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "([-/]+)               主題式閱\讀 上/下",
    "(t)                   主題式循序閱\讀",
    "(q)(←)               結束",
    "(h)(H)(?)             輔助說明畫面",
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
	clrtoeol();

#ifdef DEBUG
	prints("L#%d prmpt=%d Disp:%08X/%08X/%08X, File:%08X/%08X(%d)",
		(int)mf.lineno, 
		promptend,
		(unsigned int)mf.disps, 
		(unsigned int)mf.maxdisps,
		(unsigned int)mf.dispe,
		(unsigned int)mf.start, (unsigned int)mf.end,
		mf.len);
#else
	if(mf.len)
	prints("\033[m\033[%sm  瀏覽 P.%d(%d%%)  %s  %-30.30s%s",
		"44", //printcolor[(int)color],
		(int)(mf.lineno / MFNAV_PAGE)+1,
		(int)((unsigned long)(mf.dispe-mf.start) * 100 / mf.len),
		"\033[31;47m",
		"(h)\033[30m求助  \033[31m→↓[PgUp][",
		"PgDn][Home][End]\033[30m游標移動  \033[31m←[q]\033[30m結束   \033[m");
#endif

	ch = igetch();
	switch (ch) {
	    /* ------------------ EXITING KEYS ------------------ */
	    case 'r': // Ptt: put all reply/recommend function here
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
	    case 't':
		if (mf_viewedAll())
		    flExit = 1, retval = RELATE_NEXT;
		else
		    mf_forward(MFNAV_PAGE);
		break;
	    /* ------------------ NAVIGATION KEYS ------------------ */
	    /* Simple Navigation */
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
		}
		break;
	    case 'n':
		mf_search(MFSEARCH_FORWARD);
		break;
	    case 'N':
		mf_search(MFSEARCH_BACKWARD);
		break;
	    /* ------------------ SPECIAL KEYS ------------------ */
	    case ':':
		{
		    char buf[10];
		    int  i = 0;

		    getdata(t_lines, 0, "Goto Page: ", buf, 5, DOECHO);
		    sscanf(buf, "%d", &i);
		    if(--i < 0) i = 0;
		    mf_goto(i * MFNAV_PAGE);
		    break;
		}
		break;

	    case Ctrl('T'):
		{
		    char buf[10];
		    getdata(b_lines - 2, 0, "把這篇文章收入到暫存檔？[y/N] ",
			    buf, 4, LCECHO);
		    if (buf[0] == 'y') {
			setuserfile(buf, ask_tmpbuf(b_lines - 1));
                        Copy(fpath, buf);
		    }
		}
		break;

	    case 'h':
	    case 'H':
	    case '?':
		// help
		show_help(pmore_help);
		break;

	    case 'E':
		// admin edit any files other than ve help file
		if (HAS_PERM(PERM_SYSOP) && strcmp(fpath, "etc/ve.hlp")) {
		    mf_detach();
		    vedit(fpath, NA, NULL);
		    return 0;
		}
		break;
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
