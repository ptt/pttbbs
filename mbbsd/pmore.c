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
 *  - Less memory consumption (mmap is not considered anyway)
 *  - Better support for large terminals
 *  - Unlimited file length and line numbers
 *
 * TODO:
 *  - Speed up with supporting Scroll [done]
 *  - Support PTT_PRINTS [done]
 *  - Wrap long lines [done]
 *  - DBCS friendly wrap [done, but the original line will still be messed]
 *  - left-right wide navigation
 *  - BBSMovie Support
 * 
 * WONTDO:
 *  - The message seperator line is different from old more.
 *    I decided to abandon the old style (which is buggy).
 *    > old   style: increase one line to show seperator
 *    > pmore style: use blank line for seperator.
 *  - However I've changed my mind. Now this can be simulated
 *    with wrapping. So it's in preference now.
 *    Make it default if you REALLY eager for this.
 *    HOWEVER IT MAY BE SLOW BECAUSE OPTIMIZED SCROLL IS DISABLED
 *    IN WRAPPING MODE.
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
#define PMORE_USE_PTT_PRINTS		// PTT or special printing
#define PMORE_USE_OPT_SCROLL		// optimized scroll
#define PMORE_USE_DBCS_WRAP		// safef wrap for DBCS.
#define PMORE_PRELOAD_SIZE (128*1024L)	// on busy system set smaller or undef

#define PMORE_TRADITIONAL_PROMPTEND	// when prompt=NA, show only page 1
#define PMORE_TRADITIONAL_FULLCOL	// to work with traditional pictures
// -------------------------------------------------------------- </FEATURES>

//#define DEBUG
int debug = 0;

// --------------------------------------------- <Defines and constants>

// --------------------------- <Display>
// Escapes. I don't like \033 everywhere.
#define ESC_NUM (0x1b)
#define ESC_STR "\x1b"
#define ESC_CHR '\x1b'

// Common ANSI commands.
#define ANSI_RESET  ESC_STR "[m"
#define ANSI_COLOR(x) ESC_STR "[" #x "m"
#define STR_ANSICODE    "[0123456789;,"

// Poor BBS terminal system Workarounds
// - Most BBS implements clrtoeol() as fake command
//   and usually messed up when output ANSI quoted string.
// - A workaround is suggested by kcwu:
//   https://opensvn.csie.org/traccgi/pttbbs/trac.cgi/changeset/519
#define FORCE_CLRTOEOL() outs(ESC_STR "[K")
// --------------------------- </Display>

// --------------------------- <Main Navigation>
typedef struct
{
    unsigned char 
	*start, *end,	// file buffer
	*disps, *dispe,	// disply start/end
	*maxdisps;	// a very special pointer, 
    			//   consider as "disps of last page"
    off_t len; 		// file total length
    long  lineno,	// lineno of disps
	  oldlineno,	// last drawn lineno, < 0 means full update
	  		//
	  wraplines,	// wrapped lines in last display
	  dispedlines,	// how many different lines displayed
	  		//  usually dispedlines = PAGE-wraplines,
			//  but if last line is incomplete(wrapped),
			//  dispedlines = PAGE-wraplines + 1
	  lastpagelines,// lines of last page to show
	                //  this indicates how many lines can
			//  maxdisps(maxlinenoS) display.
	  maxlinenoS;	// lineno of maxdisps, "S"! 
			//  What does the magic "S" mean?
			//  Just trying to notify you that it's 
    			//  NOT REAL MAX LINENO NOR FILELENGTH!!!
			//  You may consider "S" of "Start" (disps).
} MmappedFile;

MmappedFile mf = { 
    0, 0, 0, 0, 0, 0L,
    0, 0, 0, -1L, -1L, -1L 
};	// current file

/* mf_* navigation commands return value meanings */
enum {
    MFNAV_OK,		// navigation ok
    MFNAV_EXCEED,	// request exceeds buffer
} MF_NAV_COMMANDS;

/* Navigation units (dynamic, so not in enum const) */
#define MFNAV_PAGE  (t_lines-2)	// when navigation, how many lines in a page to move

/* Display system */
enum {
    /* newline method (because of poor BBS implementation) */
    MFDISP_NEWLINE_CLEAR = 0, // \n and cleartoeol
    MFDISP_NEWLINE_SKIP,
    MFDISP_NEWLINE_MOVE,  // use move to simulate newline.

    MFDISP_WRAP_TRUNCATE = 0,
    MFDISP_WRAP_WRAP,

    MFDISP_SEP_NONE = 0x00,
    MFDISP_SEP_LINE = 0x01,
    MFDISP_SEP_WRAP = 0x02,
    MFDISP_SEP_OLD  = MFDISP_SEP_LINE | MFDISP_SEP_WRAP,

} MF_DISP_CONST;

#define MFDISP_PAGE (t_lines-1) // the real number of lines to be shown.
#define MFDISP_DIRTY() { mf.oldlineno = -1; }

/* Indicators */
#define MFDISP_TRUNC_INDICATOR	ANSI_COLOR(0;1;37) ">" ANSI_RESET
#define MFDISP_WRAP_INDICATOR	ANSI_COLOR(0;1;37) "\\" ANSI_RESET
// --------------------------- </Main Navigation>

// --------------------------- <Aux. Structures>
/* browsing preference */
typedef struct
{
    /* mode flags */
    unsigned short int
	wrapmode,	// wrap?
	seperator,	// seperator style
    	indicator,	// show wrap indicators

	oldwrapmode,	// traditional wrap
        oldstatusbar,	// traditional statusbar
	rawmode;	// show file as-is.
} MF_BrowsingPrefrence;

MF_BrowsingPrefrence bpref =
{ MFDISP_WRAP_WRAP, MFDISP_SEP_OLD, 1, 
    0, 0, 0, };

/* pretty format header */
#define FH_HEADERS    (4)  // how many headers do we know?
#define FH_HEADER_LEN (4)  // strlen of each heads
static const char *_fh_disp_heads[FH_HEADERS] = 
    {"作者", "標題", "時間", "轉信"};

typedef struct
{
    int lines;	// header lines
    unsigned char *headers[FH_HEADERS];
    unsigned char *floats[2];	// right floating, name and val
} MF_PrettyFormattedHeader;

MF_PrettyFormattedHeader fh = { 0, {0,0,0,0}, {0, 0}};

/* search records */
typedef struct
{
    int  len;
    int (*cmpfunc) (const char *, const char *, size_t);
    char search_str[81];	// maybe we can change to dynamic allocation
} MF_SearchRecord;

MF_SearchRecord sr = { 0, strncmp, "" };

enum {
    MFSEARCH_FORWARD,
    MFSEARCH_BACKWARD,
} MFSEARCH_DIRECTION;

// Reset structures
#define RESETMF() { memset(&mf, 0, sizeof(mf)); \
    mf.lastpagelines = mf.maxlinenoS = mf.oldlineno = -1; }
#define RESETFH() { memset(&fh, 0, sizeof(fh)); \
    fh.lines = -1; }

// --------------------------- </Aux. Structures>

// --------------------------------------------- </Defines and constants>

// used by mf_attach
void mf_parseHeaders();
void mf_freeHeaders();
void mf_determinemaxdisps(int, int);

/* 
 * mmap basic operations 
 */
int 
mf_attach(unsigned char *fn)
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

    mf_determinemaxdisps(MFNAV_PAGE, 0);

    mf.disps = mf.dispe = mf.start;
    mf.lineno = 0;

    /* reset and parse article header */
    mf_parseHeaders();

    /* a workaround for wrapped seperators */
    if(mf.maxlinenoS > 0 &&
	    fh.lines >= mf.maxlinenoS &&
	    bpref.seperator & MFDISP_SEP_WRAP)
    {
	mf_determinemaxdisps(+1, 1);
    }

    return  1;
}

void 
mf_detach()
{
    if(mf.start) {
	munmap(mf.start, mf.len);
	RESETMF();

    }
    mf_freeHeaders();
}

/*
 * lineno calculation, and moving
 */
void 
mf_sync_lineno()
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

int mf_backward(int); // used by mf_buildmaxdisps
int mf_forward(int); // used by mf_buildmaxdisps

void
mf_determinemaxdisps(int backlines, int update_by_offset)
{
    unsigned char *pbak = mf.disps, *mbak = mf.maxdisps;
    long lbak = mf.lineno;

    if(update_by_offset)
    {
	if(backlines > 0)
	{
	    /* tricky way because usually 
	     * mf_forward checks maxdisps.
	     */
	    mf.disps = mf.maxdisps;
	    mf.maxdisps = mf.end-1;
	    mf_forward(backlines);
	    mf_backward(0);
	} else
	    mf_backward(backlines);
    } else {
	if( mf.lastpagelines >= 0 &&
		mf.lastpagelines <= backlines)
	    return;
	mf.lineno = backlines;
	mf.disps = mf.end - 1;
	backlines = mf_backward(backlines);
    }

    if(mf.disps != mbak)
    {
	mf.maxdisps = mf.disps;
	if(update_by_offset)
	    mf.lastpagelines -= backlines;
	else
	    mf.lastpagelines = backlines;

	mf.maxlinenoS = -1;
#ifdef PMORE_PRELOAD_SIZE
	if(mf.len <= PMORE_PRELOAD_SIZE)
	    mf_sync_lineno(); // maxlinenoS will be automatically updated
#endif
    }
    mf.disps = pbak;
    mf.lineno = lbak;
}

/*
 * mf_backwards is also used for maxno determination,
 * so we cannot change anything in mf except these:
 *   mf.disps
 *   mf.lineno
 */
int 
mf_backward(int lines)
{
    int flFirstLine = 1;
    int real_moved = 0;
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
	    mf.lineno--, lines--, real_moved++;
    }

    if(mf.disps == mf.start)
	mf.lineno = 0;
    else
	mf.disps ++;

    return real_moved;

    /*
    if(lines > 0)
	return MFNAV_OK;
    else
	return MFNAV_EXCEED;
	*/
}

int 
mf_forward(int lines)
{
    int real_moved = 0;

    while(mf.disps <= mf.maxdisps && lines > 0)
    {
	while (mf.disps <= mf.maxdisps && *mf.disps++ != '\n');

	if(mf.disps <= mf.maxdisps)
	    mf.lineno++, lines--, real_moved++;
    }

    if(mf.disps > mf.maxdisps)
	mf.disps = mf.maxdisps;

    /* please make sure you have lineno synced. */
    if(mf.disps == mf.maxdisps && mf.maxlinenoS < 0)
	mf.maxlinenoS = mf.lineno;

    return real_moved;
    /*
    if(lines > 0)
	return MFNAV_OK;
    else
	return MFNAV_EXCEED;
	*/
}

int 
mf_goTop()
{
    mf.disps = mf.start;
    mf.lineno = 0;
    return MFNAV_OK;
}

int 
mf_goBottom()
{
    mf.disps = mf.maxdisps;
    mf_sync_lineno();

    return MFNAV_OK;
}

int 
mf_goto(int lineno)
{
    mf.disps = mf.start;
    mf.lineno = 0;
    return mf_forward(lineno);
}

int 
mf_viewedNone()
{
    return (mf.disps <= mf.start);
}

int 
mf_viewedAll()
{
    return (mf.dispe >= mf.end);
}
/*
 * search!
 */
int 
mf_search(int direction)
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

/* String Processing
 *
 * maybe you already have your string processors (or not).
 * whether yes or no, here we provides some.
 */
void 
pmore_str_strip_ansi(unsigned char *p)	// warning: p is NULL terminated
{
    unsigned char *pb = p;
    while (*p != 0)
    {
	if (*p == ESC_CHR)
	{
	    // ansi code sequence, ignore them.
	    pb = p++;
	    while (*p && strchr(STR_ANSICODE, *p++));
	    memmove(pb, p, strlen(p)+1);
	    p = pb;
	}
	else if (*p < ' ')
	{
	    // control codes, ignore them.
	    memmove(p, p+1, strlen(p+1)+1);
	}
	else
	    p++;
    }
}

/* this chomp is a little different: 
 * it kills starting and trailing spaces.
 */
void 
pmore_str_chomp(unsigned char *p)
{
    unsigned char *pb = p + strlen(p)-1;

    while (pb >= p)
	if(isascii(*pb) && isspace(*pb))
	    *pb-- = 0;
	else
	    break;
    pb = p;
    while (*pb && isascii(*pb) && isspace(*pb))
	pb++;

    if(pb != p)
	memmove(p, pb, strlen(pb)+1);
}

#define PMORE_DBCS_LEADING(c) (c >= 0x80)
#if 0
int 
pmore_str_safe_big5len(unsigned char *p)
{
    return 0;
}
#endif

/*
 * Format Related
 */

void 
mf_freeHeaders()
{
    if(fh.lines > 0)
    {
	int i;

	for (i = 0; i < FH_HEADERS; i++)
	    if(fh.headers[i])
		free(fh.headers[i]);
	for (i = 0; i < sizeof(fh.floats) / sizeof(unsigned char*); i++)
	    free(fh.floats[i]);
	RESETFH();
    }
}

void 
mf_parseHeaders()
{
    /* file format:
     * AUTHOR: author BOARD: blah <- headers[0], flaots[0], floats[1]
     * XXX: xxx			  <- headers[1]
     * XXX: xxx			  <- headers[n]
     * [blank, fill with seperator] <- lines
     *
     * #define STR_AUTHOR1     "作者:"
     * #define STR_AUTHOR2     "發信人:"
     */
    unsigned char *pmf = mf.start;
    int i = 0;

    RESETFH();

    if(mf.len < LEN_AUTHOR2)
	return;

    if (strncmp(mf.start, STR_AUTHOR1, LEN_AUTHOR1) == 0)
    {
	fh.lines = 3;	// local
    } 
    else if (strncmp(mf.start, STR_AUTHOR2, LEN_AUTHOR2) == 0)
    {
	fh.lines = 4;
    }
    else 
	return;

    for (i = 0; i < fh.lines; i++)
    {
	unsigned char *p = pmf, *pb = pmf;
	int l;

	/* first, go to line-end */
	while(pmf < mf.end && *pmf != '\n')
	    pmf++;
	if(pmf >= mf.end)
	    break;
	p = pmf;
	pmf ++;	// move to next line.

	// p is pointing at a new line. (\n)
	l = (int)(p - pb);
	p = (unsigned char*) malloc (l+1);
	fh.headers[i] = p;
	memcpy(p, pb, l);
	p[l] = 0;

	// now, postprocess p.
	pmore_str_strip_ansi(p);

	// strip to quotes[+1 space]
	if((pb = strchr(p, ':')) != NULL)
	{
	    if(*(pb+1) == ' ') pb++;
	    memmove(p, pb, strlen(pb)+1);
	}

	// kill staring and trailing spaces
	pmore_str_chomp(p);

	// special case, floats are in line[0].
	if(i == 0 && (pb = strrchr(p, ':')) != NULL && *(pb+1))
	{
	    unsigned char *np = strdup(pb+1);

	    fh.floats[1] = np;
	    pmore_str_chomp(np);
	    // remove quote and traverse back
	    *pb-- = 0;
	    while (pb > p && *pb != ',' && !(isascii(*pb) && isspace(*pb)))
		pb--;

	    if (pb > p) {
		fh.floats[0] = strdup(pb+1);
		pmore_str_chomp(fh.floats[0]);
		*pb = 0;
		pmore_str_chomp(fh.headers[0]);
	    } else {
		fh.floats[0] = strdup("");
	    }
	}
    }
}

/*
 * mf_disp utility macros
 */
inline static void
MFDISP_SKIPCURLINE()
{ 
    while (mf.dispe < mf.end && *mf.dispe != '\n')
	mf.dispe++;
}

inline static int
MFDISP_DBCS_HEADERWIDTH(int originalw)
{
    return originalw - (originalw %2);
//    return (originalw >> 1) << 1;
}

/*
 * display mf content from disps for MFDISP_PAGE
 */
void 
mf_disp()
{
    int lines = 0, col = 0, currline = 0, wrapping = 0;
    int startline = 0, endline = MFDISP_PAGE-1;

    /* why t_columns-1 here?
     * because BBS systems usually have a poor terminal system
     * and many stupid clients behave differently.
     * So we try to avoid using the last column, leave it for
     * BBS to place '\n' and CLRTOEOL.
     */
    const int headerw = MFDISP_DBCS_HEADERWIDTH(t_columns-1);
    const int dispw = headerw - (t_columns - headerw < 2);
    const int maxcol = dispw - 1;

    if(mf.wraplines)
	MFDISP_DIRTY();	// we can't scroll with wrapped lines.
    mf.wraplines = 0;
    mf.dispedlines = 0;

#ifdef PMORE_USE_OPT_SCROLL
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
	int newline = MFDISP_NEWLINE_CLEAR;

#ifdef PMORE_USE_DBCS_WRAP
	unsigned char *dbcs_incomplete = NULL;
#endif

	currline = mf.lineno + lines;
	col = 0;

	if(!wrapping && mf.dispe < mf.end)
	    mf.dispedlines++;
	
	/* Is currentline visible? */
	if (lines < startline || lines > endline)
	{
	    while(mf.dispe < mf.end && *mf.dispe != '\n')
		mf.dispe++;
	    newline = MFDISP_NEWLINE_SKIP;
	}
	/* Now, consider what kind of line
	 * (header, seperator, or normal text)
	 * is current line.
	 */
	else if (!bpref.rawmode && currline == fh.lines)
	{
	    /* case 1, header seperator line */
	    if (bpref.seperator & MFDISP_SEP_LINE)
	    {
		outs(ANSI_COLOR(36));
		for(col = 0; col < headerw; col+=2)
		{
		    // prints("%02d", col);
		    outs("─");
		}
		outs(ANSI_RESET);
	    }

	    /* Traditional 'more' adds seperator as a newline.
	     * This is buggy, however we can support this
	     * by using wrapping features.
	     * Anyway I(piaip) don't like this. And using wrap
	     * leads to slow display (we cannt speed it up with
	     * optimized scrolling.
	     */
	    if(bpref.seperator & MFDISP_SEP_WRAP) 
	    {
		/* we have to do all wrapping stuff
		 * in normal text section.
		 * make sure this is updated.
		 */
		wrapping = 1;
		mf.wraplines ++;
		endline = MFDISP_PAGE-1;
		if(mf.dispe > mf.start && 
			mf.dispe < mf.end &&
			*mf.dispe == '\n')
		    mf.dispe --;
	    }
	    else
		MFDISP_SKIPCURLINE();
	} 
	else if (!bpref.rawmode && currline < fh.lines)
	{
	    /* case 2, we're printing headers */
	    const char *val = fh.headers[currline];
	    const char *name = _fh_disp_heads[currline];
	    int w = headerw - FH_HEADER_LEN - 3;

	    outs(ANSI_COLOR(47;34) " ");
	    outs(name);
	    outs(" " ANSI_COLOR(44;37) " "); 

	    /* right floating stuff? */
	    if (currline == 0 && fh.floats[0])
	    {
		w -= strlen(fh.floats[0]) + strlen(fh.floats[1]) + 4;
	    }

	    prints("%-*.*s", w, w, 
		    (val ? val : ""));

	    if (currline == 0 && fh.floats[0])
	    {
		outs(ANSI_COLOR(47;34) " ");
		outs(fh.floats[0]);
		outs(" " ANSI_COLOR(44;37) " "); 
		outs(fh.floats[1]);
		outs(" ");
	    }

	    outs(ANSI_RESET);
	    MFDISP_SKIPCURLINE();
	} 
	else if(mf.dispe < mf.end)
	{
	    /* case 3, normal text */
	    long dist = mf.end - mf.dispe;
	    long flResetColor = 0;
	    int  srlen = -1;
	    int breaknow = 0;

	    // first check quote
	    if(!bpref.rawmode)
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

	    while(!breaknow && mf.dispe < mf.end && *mf.dispe != '\n')
	    {
		if(inAnsi)
		{
		    if (!strchr(STR_ANSICODE, *mf.dispe))
			inAnsi = 0;
		    // if(col <= maxcol)
			outc(*mf.dispe);
		} else {
		    if(*mf.dispe == ESC_CHR)
		    {
			inAnsi = 1;
			/* we can't output now because maybe
			 * ptt_prints wants to do something.
			 */
		    }
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

			if(bpref.rawmode)
			    buf[0] = '*';
			else
			    Ptt_prints(buf, NO_RELOAD); // result in buf
			i = strlen(buf);

			if (col + i > maxcol)
			    i = maxcol - col;
			if(i > 0)
			{
			    buf[i] = 0;
			    col += i;
			    outs(buf);
			}
			inAnsi = 0;
		    } else
#endif
		    if(inAnsi)
		    {
			// outc(*mf.dispe);
			outc(ESC_CHR);
		    } else {
			int canOutput = 0;
			/* if col > maxcol,
			 * because we have the space for
			 * "indicators" (one byte),
			 * so we can tolerate one more byte.
			 */
			if(col <= maxcol)	// normal case
			    canOutput = 1;
			else if (bpref.oldwrapmode && // oldwrapmode
			    col < t_columns)
			{
			    canOutput = 1;
			    newline = MFDISP_NEWLINE_MOVE;
			} else {
			    int off = 0;
			    int inAnsi = 0;
			    unsigned char *p = mf.dispe +1;
			    // put more efforts to determine
			    // if we can use indicator space
			    // determine real offset between \n
			    while (p < mf.end && *p != '\n')
			    {
				if(inAnsi)
				{
				    if(!strchr(STR_ANSICODE, *p))
					inAnsi = 0;
				} else {
				    if(*p == ESC_CHR)
					inAnsi = 1;
				    else
					off ++;
				}
				p++;
			    }
			    if (col + off <= (maxcol+1))
				canOutput = 1;	// indicator space
#ifdef PMORE_TRADITIONAL_FULLCOL
			    else if (col + off < t_columns)
			    {
				canOutput = 1;
				newline = MFDISP_NEWLINE_MOVE;
			    }
#endif
			}

			if(canOutput)
			{
			    /* the real place to output text
			     */
			    unsigned char c = *mf.dispe;
#ifdef PMORE_USE_DBCS_WRAP
			    if (dbcs_incomplete)
				dbcs_incomplete = NULL;
			    else if(PMORE_DBCS_LEADING(c)) 
				dbcs_incomplete = mf.dispe;
#endif
			    outc(c);
			    col++;

			    if (srlen == 0)
				outs(ANSI_RESET);
			    if(srlen >= 0)
				srlen --;
			}
			else switch (bpref.wrapmode)
			{
			    case MFDISP_WRAP_WRAP:
				breaknow = 1;
				wrapping = 1;
				mf.wraplines ++;
#ifdef PMORE_USE_DBCS_WRAP
				if(dbcs_incomplete)
				{
				    /*
				    if(col < t_columns)
					outc(*mf.dispe), col++;
					*/
				    mf.dispe = dbcs_incomplete;
				    dbcs_incomplete = NULL;
				}
#endif
				break;
			    case MFDISP_WRAP_TRUNCATE:
				breaknow = 1;
				MFDISP_SKIPCURLINE();
				wrapping = 0;
				break;
			}
		    }
		}
		if(!breaknow)
		    mf.dispe ++;
	    }
	    if(flResetColor)
		outs(ANSI_RESET);

	    /* "wrapping" should be only in normal text section.
	     * We don't support wrap within scrolling,
	     * so if we have to wrap, invalidate all lines.
	     */
	    if(breaknow)
	    {
		if(wrapping)
		    endline = MFDISP_PAGE-1;

		if(!bpref.oldwrapmode && bpref.indicator)
		{
		    if(wrapping)
			outs(MFDISP_WRAP_INDICATOR);
		    else
			outs(MFDISP_TRUNC_INDICATOR);
		} else {
		    outs(ANSI_RESET);
		}
	    }
	    else
		wrapping = 0;
	}

	if(mf.dispe < mf.end && *mf.dispe == '\n') 
	    mf.dispe ++;
	// else, we're in wrap mode.

	switch(newline)
	{
	    case MFDISP_NEWLINE_SKIP:
		break;
	    case MFDISP_NEWLINE_CLEAR:
		FORCE_CLRTOEOL();
		outc('\n');
		break;
	    case MFDISP_NEWLINE_MOVE:
		move(lines+1, 0);
		break;
	}
	lines ++;
    }
    /*
     * we've displayed the file.
     * but if we got wrapped lines in last page,
     * mf.maxdisps may be required to be larger.
     */
    if(mf.disps == mf.maxdisps && mf.dispe < mf.end)
    {
	/*
	 * never mind if that's caused by seperator
	 * however this code is rarely used now.
	 * only if no preload file.
	 */
	if (bpref.seperator & MFDISP_SEP_WRAP &&
	    mf.wraplines == 1 &&
	    mf.lineno < fh.lines)
	{
	    /*
	     * o.k, now we know maxline should be one line forward.
	     */
	    mf_determinemaxdisps(+1, 1);
	} else 
	{
	    mf_determinemaxdisps(+mf.wraplines, 1);
	}
    }
    mf.oldlineno = mf.lineno;
}

/* --------------------- MAIN PROCEDURE ------------------------- */

static const char    * const pmore_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(k/↑) (j/↓/Enter)   上捲/下捲一行",
    "(^B)(PgUp)(BackSpace) 上捲一頁",
    "(^F)(PgDn)(Space)(→) 下捲一頁",
    "(0/g/Home) ($/G/End)  檔案開頭/結尾",
    "(;/:)                 跳至某行/某頁",
    "數字鍵 1-9            跳至輸入的行號",
    "\01其他功\能鍵",
    "(/" ANSI_COLOR(1;30) "/" ANSI_RESET 
       "s)                 搜尋字串",
    "(n/N)                 重複正/反向搜尋",
    "(Ctrl-T)              存入暫存檔",
    "(f/b)                 跳至下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "(t/[-/]+)             主題式閱\讀:循序/前/後篇",
//    "(\\/w/W)               切換顯示原始內容/自動折行/折行符號", // this IS already aligned!
    "(\\)                   切換顯示原始內容", // this IS already aligned!
    "(w/W/l)               切換自動折行/顯示折行符號/分隔線顯示方式",
    "(o)                   傳統模式(狀態列與折行方式)",
    "(q)(←)               結束",
    "(h)(H)(?)             本說明畫面",
#ifdef DEBUG
    "(d)                   切換除錯(debug)模式",
#endif
    "\01本系統使用 piaip 的新式瀏覽程式: pmore, piaip's more",
    NULL
};
/*
 * pmore utility macros
 */
inline static void
PMORE_UINAV_FORWARDPAGE()
{
    /* Usually, a forward is just mf_forward(MFNAV_PAGE);
     * but because of wrapped lines...
     * This function is used when user tries to nagivate
     * with page request.
     * If you want to a real page forward, don't use this.
     * That's why we have this special function.
     */
    int i = mf.dispedlines - 1;

    if(mf_viewedAll())
	return;

    if(i < 1)
	i = 1;
    mf_forward(i);
}

inline static void
PMORE_UINAV_FORWARLINE()
{
    if(mf_viewedAll())
	return;
    mf_forward(1);
}

/*
 * piaip's more, a replacement for old more
 */
int 
pmore(char *fpath, int promptend)
{
    int  flExit = 0, retval = 0;
    int  ch = 0;

    STATINC(STAT_MORE);
    if(!mf_attach(fpath))
	return -1;

    clear();
    while(!flExit)
    {
	mf_disp();

#ifdef	PMORE_TRADITIONAL_PROMPTEND
	if(promptend == NA) // && mf_viewedAll())
	    break;
#else
	if(promptend == NA && mf_viewedAll())
	    break;
#endif
	move(b_lines, 0);
	// clrtoeol(); // this shall be done in mf_disp to speed up.

	/* PRINT BOTTOM STATUS BAR */
#ifdef DEBUG
	if(debug)
	{
	    /* in debug mode don't print ANSI codes
	     * because themselves are buggy.
	     */
	    prints("L#%ld(w%ld,lp%ld) pmt=%d Dsp:%08X/%08X/%08X, "
		    "F:%08X/%08X(%d) tScr(%dx%d)",
		    mf.lineno, mf.wraplines, mf.lastpagelines,
		    promptend,
		    (unsigned int)mf.disps, 
		    (unsigned int)mf.maxdisps,
		    (unsigned int)mf.dispe,
		    (unsigned int)mf.start, (unsigned int)mf.end,
		    (int)mf.len,
		    t_columns,
		    t_lines
		  );
	}
	else
#endif
	{
	    char *printcolor;

	    char buf[256];	// orz
	    int prefixlen = 0;
	    int barlen = 0;
	    int postfix1len = 0, postfix2len = 0;

	    int progress  = 
		(int)((unsigned long)(mf.dispe-mf.start) * 100 / mf.len);
	    /*
	     * page determination is hard.
	     * should we use starting line, or finishing line?
	     */
	    int nowpage = 
		(int)((mf.lineno + mf.dispedlines/2) / MFNAV_PAGE)+1;
	    int allpages = -1; /* unknown yet */
	    if (mf.maxlinenoS >= 0)
	    {
		allpages = 
		(int)((mf.maxlinenoS + mf.lastpagelines -
		       ((bpref.seperator & MFDISP_SEP_WRAP) &&
			(fh.lines >= 0) ? 0:1)) / MFNAV_PAGE)+1;
		if (mf.lineno >= mf.maxlinenoS || nowpage > allpages)
		    nowpage = allpages;
		/*
		    nowpage = 
			(int)((mf.lineno + mf.dispedlines-2) / MFNAV_PAGE)+1 ;
			*/
	    }
	    /* why -2 and -1?
	     * because we want to determine by nav_page,
	     * and mf.dispedlines is based on disp_page (nav_page+1)
	     * mf.lastpagelines is based on nav_page
	     */

	    if(mf_viewedAll())
		printcolor = ANSI_COLOR(37;44);
	    else if (mf_viewedNone())
		printcolor = ANSI_COLOR(33;45);
	    else
		printcolor = ANSI_COLOR(34;46);

	    outs(ANSI_RESET);
	    outs(printcolor);

	    if(bpref.oldstatusbar)
	    {

		prints("  瀏覽 P.%d(%d%%)  %s  %-30.30s%s",
			nowpage,
			progress,
			ANSI_COLOR(31;47), 
			"(h)" 
			ANSI_COLOR(30) "求助  "
			ANSI_COLOR(31) "→↓[PgUp][",
			"PgDn][Home][End]"
			ANSI_COLOR(30) "游標移動  "
			ANSI_COLOR(31) "←[q]"
			ANSI_COLOR(30) "結束   ");

	    } else {

		if(allpages >= 0)
		    sprintf(buf,
			    "  瀏覽 第 %1d/%1d 頁 ",
			    nowpage,
			    allpages
			   );
		else
		    sprintf(buf,
			    "  瀏覽 第 %1d 頁 ",
			    nowpage
			   );
		outs(buf); prefixlen += strlen(buf);

		outs(ANSI_COLOR(1;30;47));

		sprintf(buf,
			" 閱\讀進度%3d%%, 目前顯示: 第 %02d~%02d 行",
			progress,
			(int)(mf.lineno + 1),
			(int)(mf.lineno + mf.dispedlines)
		       );

		outs(buf); prefixlen += strlen(buf);

		postfix1len = 12;	// check msg below
		postfix2len = 10;

		if (prefixlen + postfix1len + postfix2len + 1 > t_columns)
		{
		    postfix1len = 0;
		    if (prefixlen + postfix1len + postfix2len + 1 > t_columns)
			postfix2len = 0;
		}
		barlen = t_columns - 1 - postfix1len - postfix2len - prefixlen;

		while(barlen-- > 0)
		    outc(' ');

		if(postfix1len > 0)
		    outs(
			    ANSI_COLOR(0;31;47) "(h)" 
			    ANSI_COLOR(30) "按鍵說明 "
			);
		if(postfix2len > 0)
		    outs(
			    ANSI_COLOR(0;31;47) "←[q]" 
			    ANSI_COLOR(30) "離開 "
			);
	    }
	    outs(ANSI_RESET);
	    FORCE_CLRTOEOL();
	}

	/* igetch() will do refresh(); */
	ch = igetch();
	switch (ch) {
	    /* ------------------ EXITING KEYS ------------------ */
	    case 'r': case 'R':
	    case 'Y': case 'y':
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
	    case 'F': case 'f':
		flExit = 1,	retval = READ_NEXT;
		break;
	    case 'B': case 'b':
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
	    case 'k': case 'K':
		mf_backward(1);
		break;
	    case 'j': case 'J':
		PMORE_UINAV_FORWARLINE();
		break;

	    case Ctrl('F'):
	    case KEY_PGDN:
		PMORE_UINAV_FORWARDPAGE();
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
		    PMORE_UINAV_FORWARLINE();
		break;

	    case ' ':
		if (mf_viewedAll())
		    flExit = 1, retval = READ_NEXT;
		else
		    PMORE_UINAV_FORWARDPAGE();
		break;
	    case KEY_RIGHT:
		if(mf_viewedAll())
		    promptend = 0, flExit = 1, retval = 0;
		else
		    PMORE_UINAV_FORWARDPAGE();
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
		    PMORE_UINAV_FORWARDPAGE();
		break;
	    /* ------------------ SEARCH  KEYS ------------------ */
	    case 's':
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

	    case 'h': case 'H':
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
	    case 'w':
		switch(bpref.wrapmode)
		{
		    case MFDISP_WRAP_WRAP:
			bpref.wrapmode = MFDISP_WRAP_TRUNCATE;
			break;
		    case MFDISP_WRAP_TRUNCATE:
			bpref.wrapmode = MFDISP_WRAP_WRAP;
			break;
		}
		MFDISP_DIRTY();
		break;
	    case 'W':
		bpref.indicator = !bpref.indicator;
		MFDISP_DIRTY();
		break;
	    case 'o':
		bpref.oldwrapmode  = !bpref.oldwrapmode;
		bpref.oldstatusbar = !bpref.oldstatusbar;
		MFDISP_DIRTY();
		break;
	    case 'l':
		switch(bpref.seperator)
		{
		    case MFDISP_SEP_OLD:
			bpref.seperator = MFDISP_SEP_LINE;
			break;
		    case MFDISP_SEP_LINE:
			bpref.seperator = 0;
			break;
		    default:
			bpref.seperator = MFDISP_SEP_OLD;
			break;
		}
		MFDISP_DIRTY();
		break;
	    case '\\':
		bpref.rawmode = !bpref.rawmode;
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
