/* $Id$ */

/*
 * pmore: piaip's more, a new replacement for traditional pager
 *
 * piaip's new implementation of pager(more) with mmap,
 * designed for unlimilited length(lines).
 *
 * "pmore" is "piaip's more", NOT "PTT's more"!!!
 * pmore is designed for general maple-family BBS systems, not 
 * specific to any branch.
 *
 * Author: Hung-Te Lin (piaip), June 2005.
 *
 * Copyright(c) 2005-2008 Hung-Te Lin <piaip@csie.ntu.edu.tw>
 * All Rights Reserved.
 * You are free to use, modify, redistribute this program in any
 * non-commercial usage (including network service).
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * MAJOR IMPROVEMENTS:
 *  - Clean source code, and more readable for mortal
 *  - Correct navigation
 *  - Excellent search ability (for correctness and user behavior)
 *  - Less memory consumption (mmap is not considered anyway)
 *  - Better support for large terminals
 *  - Unlimited file length and line numbers
 *
 * TODO AND DONE:
 *  - [2005, Initial Release]
 *  - Optimized speed up with Scroll supporting [done]
 *  - Wrap long lines [done]
 *  - Left-right wide navigation [done]
 *  - DBCS friendly wrap [done]
 *  - Reenrtance for main procedure [done]
 *  - Support PTT_PRINTS [done]
 *  - ASCII Art movie support [done]
 *  - ASCII Art movie navigation keys [pending]
 *  - A new optimized terminal base system (piterm) [dropped]
 *  - 
 *  - [2007, Interactive Movie Enhancement]
 *  - New Invisible Frame Header Code [done]
 *  - Playback Control (pause, stop, skip) [done]
 *  - Interactive Movie (Hyper-text) [done]
 *  - Preference System (like board-conf) [done]
 *  - Traditional Movie Compatible Mode [done]
 *  - movie: Optimization on relative frame numbers [done]
 *  - movie: Optimization on named frames (by hash)
 *  -
 *  - Support Anti-anti-idle (ex, PCMan sends up-down)
 *  - Better help system [pending]
 *  - Virtual Contatenate [pending]
 *  - Drop ANSI between DBCS words if outputing UTF8 [drop, done by term]
 */

// --------------------------------------------------------------- <FEATURES>
/* These are default values.
 * You may override them in your bbs.h or config.h etc etc.
 */
#define PMORE_PRELOAD_SIZE (64*1024L)   // on busy system set smaller or undef

#define PMORE_USE_PTT_PRINTS            // support PTT or special printing
#define PMORE_USE_OPT_SCROLL            // optimized scroll
#define PMORE_USE_DBCS_WRAP             // safe wrap for DBCS.
#define PMORE_USE_ASCII_MOVIE           // support ascii movie
//#define PMORE_RESTRICT_ANSI_MOVEMENT  // user cannot use ANSI escapes to move
#define PMORE_ACCURATE_WRAPEND          // try more harder to find file end in wrap mode
#define PMORE_TRADITIONAL_PROMPTEND     // when prompt=NA, show only page 1
#define PMORE_TRADITIONAL_FULLCOL       // to work with traditional ascii arts
#define PMORE_LOG_SYSOP_EDIT            // log whenever sysop uses E
#define PMORE_OVERRIDE_TIME             // override time format if possible

// if you are working with a terminal without ANSI control,
// you are using a poor term (define PMORE_USING_POOR_TERM).
#ifndef USE_PFTERM                      // pfterm is a good terminal system.
#define PMORE_USING_POOR_TERM
#define PMORE_WORKAROUND_CLRTOEOL       // try to work with poor terminal sys
#endif // USE_PFTERM
// -------------------------------------------------------------- </FEATURES>

// ----------------------------------------------------------- <LOCALIZATION>
// Messages for localization are listed here.
#define PMORE_MSG_PREF_TITLE \
    " pmore 2007 設定選項 "
#define PMORE_MSG_PREF_TITLE_QRAW \
    " pmore 2007 快速設定選項 - 色彩(ANSI碼)顯示模式 "
#define PMORE_MSG_WARN_FAKEUSERINFO \
    " ▲此頁內容會依閱\讀者不同,原文未必有您的資料 "
#define PMORE_MSG_WARN_MOVECMD \
    " ▲此頁內容含移位碼,可能會顯示偽造的系統訊息 "
#define PMORE_MSG_SEARCH_KEYWORD \
    "[搜尋]關鍵字:"

#define PMORE_MSG_MOVIE_DETECTED \
    " ★ 這份文件是可播放的文字動畫，要開始播放嗎？ [Y/n]"
#define PMORE_MSG_MOVIE_PLAYOLD_GETTIME \
    "這可能是傳統動畫檔, 若要直接播放請輸入速度(秒): "
#define PMORE_MSG_MOVIE_PLAYOLD_AS24L \
    "傳統動畫是以 24 行為單位設計的, 要模擬 24 行嗎? (否則會用現在的行數)[Yn] "
#define PMORE_MSG_MOVIE_PAUSE \
    " >>> 暫停播放動畫，請按任意鍵繼續或 q 中斷。 <<<"
#define PMORE_MSG_MOVIE_PLAYING \
    " >>> 動畫播放中... 可按 q, Ctrl-C 或其它任意鍵停止";
#define PMORE_MSG_MOVIE_INTERACTION_PLAYING \
    " >>> 互動式動畫播放中... 可按 q 或 Ctrl-C 停止";
#define PMORE_MSG_MOVIE_INTERACTION_STOPPED \
    "已強制中斷互動式系統"

// ----------------------------------------------------------- <LOCALIZATION>

#include "bbs.h"

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <string.h>

// Platform Related. NoSync is faster but if we don't have it...
// Experimental: POPULATE should work faster?
#ifdef MAP_NOSYNC
#define MF_MMAP_OPTION (MAP_NOSYNC|MAP_SHARED)
#elif defined(MAP_POPULATE)
#define MF_MMAP_OPTION (MAP_POPULATE|MAP_SHARED)
#else
#define MF_MMAP_OPTION (MAP_SHARED)
#endif

/* Developer's Guide
 * 
 * OVERVIEW
 *  - pmore is designed as a line-oriented pager. After you load (mf_attach)
 *    a file, you can move current display window by lines (mf_forward and
 *    mf_backward) and then display a page(mf_display).
 *    And please remember to delete allocated resources (mf_detach)
 *    when you exit.
 *  - Functions are designed to work with global variables.
 *    However you can overcome re-entrance problem by backuping up variables
 *    or replace all "." to "->" with little modification and add pointer as
 *    argument passed to each function. 
 *    (This is really tested and it works, however then using global variables
 *    is considered to be faster and easier to maintain, at lease shorter in
 *    time to key-in and for filelength).
 *  - Basically this file should only export one function, "pmore".
 *    Using any other functions here may be dangerous because they are not
 *    coded for external reentrance rightnow.
 *  - mf_* are operation functions to work with file buffer.
 *    Usually these function assumes "mf" can be accessed.
 *  - pmore_* are utility functions
 *
 * DETAIL
 *  - The most tricky part of pmore is the design of "maxdisps" and "maxlinenoS".
 *    What do they mean? "The pointer and its line number of last page".
 *    - Because pmore is designed to work with very large files, it's costly to
 *      calculate the total line numbers (and not necessary).  But if we don't
 *      know about how many lines left can we display then when navigating by
 *      pages may result in a page with single line conent (if you set display
 *      starting pointer to the real last line).
 *    - To overcome this issue, maxdisps is introduced. It tries to go backward
 *      one page from end of file (this operation is lighter than visiting 
 *      entire file content for line number calculation). Then we can set this
 *      as boundary of forward navigation.
 *    - maxlinenoS is the line number of maxdisps. It's NOT the real number of
 *      total line in current file (You have to add the last page). That's why
 *      it has a strange name of trailing "S", to hint you that it's not 
 *      "maxlineno" which is easily considered as "max(total) line number".
 *
 * HINTS:
 *  - Remember mmap pointers are NOT null terminated strings.
 *    You have to use strn* APIs and make sure not exceeding mmap buffer.
 *    DO NOT USE strcmp, strstr, strchr, ...
 *  - Scroll handling is painful. If you displayed anything on screen,
 *    remember to MFDISP_DIRTY();
 *  - To be portable between most BBS systems, pmore is designed to
 *    workaround most BBS bugs inside itself.
 *  - Basically pmore considered the 'outc' output system as unlimited buffer.
 *    However on most BBS implementation, outc used a buffer with ANSILINELEN
 *    in length. And for some branches they even used unsigned byte for index.
 *    So if user complained about output truncated or blanked, increase buffer.
 */

#ifdef DEBUG
int debug = 0;
# define MFPROTO
#else
# define MFPROTO inline static
#endif

/* DBCS users tend to write unsigned char. let's make compiler happy */
#define ustrlen(x)      strlen((char*)x)
#define ustrchr(x,y)    (unsigned char*)strchr((char*)x, y)
#define ustrrchr(x,y)   (unsigned char*)strrchr((char*)x, y)


// --------------------------------------------- <Defines and constants>

// --------------------------- <Display>

/* ANSI COMMAND SYSTEM */
/* On some systems with pmore style ANSI system applied,
 * we don't have to define these again.
 */
#ifndef PMORE_STYLE_ANSI
#define PMORE_STYLE_ANSI

// Escapes. I don't like \033 everywhere.
#define ESC_NUM (0x1b)
#define ESC_STR "\x1b"
#define ESC_CHR '\x1b'

// Common ANSI commands.
#define ANSI_RESET      ESC_STR "[m"
#define ANSI_COLOR(x)   ESC_STR "[" #x "m"
#define ANSI_CLRTOEND   ESC_STR "[K"
#define ANSI_MOVETO(y,x) ESC_STR "[" #y ";" #x "H"

#define ANSI_IN_ESCAPE(x) (((x) >= '0' && (x) <= '9') || \
        (x) == ';' || (x) == ',' || (x) == '[')

#endif /* PMORE_STYLE_ANSI */

#define ANSI_IN_MOVECMD(x) (strchr("ABCDfjHJRu", x) != NULL)
#define PMORE_DBCS_LEADING(c) (c >= 0x80)

// Poor BBS terminal system Workarounds
// - Most BBS implements clrtoeol() as fake command
//   and usually messed up when output ANSI quoted string.
// - A workaround is suggested by kcwu:
//   https://opensvn.csie.org/traccgi/pttbbs/trac.cgi/changeset/519
#define FORCE_CLRTOEOL() outs(ANSI_CLRTOEND)

/* Again, if you have a BBS system which optimized out* without recognizing
 * ANSI escapes, scrolling with ANSI text may result in melformed text (because
 * ANSI escapes were "optimized" ). So here we provide a method to overcome
 * with this situation. However your should increase your I/O buffer to prevent
 * flickers.
 */
MFPROTO void 
pmore_clrtoeol(int y, int x)
{
#ifdef PMORE_WORKAROUND_CLRTOEOL
    int i; 
    move(y, x); 
    for (i = x; i < t_columns; i++) 
        outc(' '); 
    clrtoeol();
    move(y, x); // this is required, due to outc().
#else
    move(y, x);
    clrtoeol();
#endif
}

// --------------------------- </Display>

// --------------------------- <Main Navigation>
typedef struct
{
    unsigned char 
        *start, *end,   // file buffer
        *disps, *dispe, // displayed content start/end
        *maxdisps;      // a very special pointer, 
                        //   consider as "disps of last page"
    off_t len;          // file total length
    long  lineno,       // lineno of disps
          oldlineno,    // last drawn lineno, < 0 means full update
          xpos,         // starting x position
                        //
          wraplines,    // wrapped lines in last display
          trunclines,   // truncated lines in last display
          dispedlines,  // how many different lines displayed
                        //  usually dispedlines = PAGE-wraplines,
                        //  but if last line is incomplete(wrapped),
                        //  dispedlines = PAGE-wraplines + 1
          lastpagelines,// lines of last page to show
                        //  this indicates how many lines can
                        //  maxdisps(maxlinenoS) display.
          maxlinenoS;   // lineno of maxdisps, "S"! 
                        //  What does the magic "S" mean?
                        //  Just trying to notify you that it's 
                        //  NOT REAL MAX LINENO NOR FILELENGTH!!!
                        //  You may consider "S" of "Start" (disps).
} MmappedFile;

MmappedFile mf = { 
    0, 0, 0, 0, 0, 0L,
    0, -1L, 0, 0, -1L, -1L, -1L,-1L
};      // current file

/* mf_* navigation commands return value meanings */
enum MF_NAV_COMMANDS {
    MFNAV_OK,           // navigation ok
    MFNAV_EXCEED,       // request exceeds buffer
};

/* Navigation units (dynamic, so not in enum const) */
#define MFNAV_PAGE  (t_lines-2) // when navigation, how many lines in a page to move

/* Display system */
enum MF_DISP_CONST {
    /* newline method (because of poor BBS implementation) */
    MFDISP_NEWLINE_CLEAR = 0, // \n and cleartoeol
    MFDISP_NEWLINE_SKIP,
    MFDISP_NEWLINE_MOVE,  // use move to simulate newline.

    MFDISP_OPT_CLEAR = 0,
    MFDISP_OPT_OPTIMIZED,
    MFDISP_OPT_FORCEDIRTY,

    // prefs
   
    MFDISP_WRAP_TRUNCATE = 0,
    MFDISP_WRAP_WRAP,
    MFDISP_WRAP_MODES,

    MFDISP_SEP_NONE = 0x00,
    MFDISP_SEP_LINE = 0x01,
    MFDISP_SEP_WRAP = 0x02,
    MFDISP_SEP_OLD  = MFDISP_SEP_LINE | MFDISP_SEP_WRAP,
    MFDISP_SEP_MODES= 0x04,

    MFDISP_RAW_NA   = 0x00,
    MFDISP_RAW_NOANSI,
    MFDISP_RAW_PLAIN,
    MFDISP_RAW_MODES,

};

#define MFDISP_PAGE (t_lines-1) // the real number of lines to be shown.
#define MFDISP_DIRTY() { mf.oldlineno = -1; }

/* Indicators */
#define MFDISP_TRUNC_INDICATOR  ANSI_COLOR(0;1;37) ">" ANSI_RESET
#define MFDISP_WRAP_INDICATOR   ANSI_COLOR(0;1;37) "\\" ANSI_RESET
#define MFDISP_WNAV_INDICATOR   ANSI_COLOR(0;1;37) "<" ANSI_RESET
// --------------------------- </Main Navigation>

// --------------------------- <Aux. Structures>
/* browsing preference */
typedef struct
{
    /* mode flags */
    unsigned char
        wrapmode,       // wrap?
        separator,      // separator style
        wrapindicator,  // show wrap indicators

        oldwrapmode,    // traditional wrap
        oldstatusbar,   // traditional statusbar
        rawmode;        // show file as-is.
} MF_BrowsingPreference;

MF_BrowsingPreference bpref =
{ MFDISP_WRAP_WRAP, MFDISP_SEP_OLD, 1, 
    0, 0, 0, };

/* pretty format header */
#define FH_HEADERS    (4)  // how many headers do we know?
#define FH_HEADER_LEN (4)  // strlen of each heads
#define FH_FLOATS     (2)  // right floating, name and val
static const char *_fh_disp_heads[FH_HEADERS] = 
    {"作者", "標題", "時間", "轉信"};

typedef struct
{
    int lines;  // header lines
    unsigned char *headers[FH_HEADERS];
    unsigned char *floats [FH_FLOATS];
} MF_PrettyFormattedHeader;

MF_PrettyFormattedHeader fh = { 0, {0,0,0,0}, {0, 0}};

/* search records */
typedef struct
{
    int  len;
    int (*cmpfunc) (const char *, const char *, size_t);
    unsigned char *search_str;  // maybe we can change to dynamic allocation
} MF_SearchRecord;

MF_SearchRecord sr = { 0, strncmp, NULL};

enum MFSEARCH_DIRECTION {
    MFSEARCH_FORWARD,
    MFSEARCH_BACKWARD,
};

// Reset structures
#define RESETMF() { memset(&mf, 0, sizeof(mf)); \
    mf.lastpagelines = mf.maxlinenoS = mf.oldlineno = -1; }
#define RESETFH() { memset(&fh, 0, sizeof(fh)); \
    fh.lines = -1; }

// Artwork
#define OPTATTR_NORMAL      ANSI_COLOR(0;34;47)
#define OPTATTR_NORMAL_KEY  ANSI_COLOR(0;31;47)
#define OPTATTR_SELECTED    ANSI_COLOR(0;1;37;46)
#define OPTATTR_SELECTED_KEY ANSI_COLOR(0;31;46)
#define OPTATTR_BAR         ANSI_COLOR(0;1;30;47)

// --------------------------- </Aux. Structures>

// --------------------------------------------- </Defines and constants>

// --------------------------------------------- <Optional Modules>
#ifdef PMORE_USE_ASCII_MOVIE
enum _MFDISP_MOVIE_MODES {
    MFDISP_MOVIE_UNKNOWN= 0,
    MFDISP_MOVIE_DETECTED,
    MFDISP_MOVIE_YES,
    MFDISP_MOVIE_NO,
    MFDISP_MOVIE_PLAYING,
    MFDISP_MOVIE_PLAYING_OLD,
};

typedef struct {
    struct timeval frameclk;
    struct timeval synctime;
    unsigned char *options,
                  *optkeys;
    unsigned char  mode,
                   compat24,
                   interactive,
                   pause;
} MF_Movie;

MF_Movie mfmovie;

#define STOP_MOVIE() {  \
    mfmovie.options = NULL; \
    mfmovie.pause    = 0; \
    if (mfmovie.mode == MFDISP_MOVIE_PLAYING) \
        mfmovie.mode =  MFDISP_MOVIE_YES; \
    if (mfmovie.mode == MFDISP_MOVIE_PLAYING_OLD) \
        mfmovie.mode =  MFDISP_MOVIE_NO; \
    mf_determinemaxdisps(MFNAV_PAGE, 0); \
    mf_forward(0); \
}

#define RESET_MOVIE() { \
    mfmovie.mode = MFDISP_MOVIE_UNKNOWN; \
    mfmovie.options = NULL; \
    mfmovie.optkeys = NULL; \
    mfmovie.compat24 = 1; \
    mfmovie.pause    = 0; \
    mfmovie.interactive     = 0; \
    mfmovie.synctime.tv_sec = mfmovie.synctime.tv_usec = 0; \
    mfmovie.frameclk.tv_sec = 1; mfmovie.frameclk.tv_usec = 0; \
}

#define MOVIE_IS_PLAYING() \
   ((mfmovie.mode == MFDISP_MOVIE_PLAYING) || \
    (mfmovie.mode == MFDISP_MOVIE_PLAYING_OLD))

unsigned char *
    mf_movieFrameHeader(unsigned char *p, unsigned char *end);

int pmore_wait_key(struct timeval *ptv, int dorefresh);
int mf_movieNextFrame();
int mf_movieSyncFrame();
int mf_moviePromptPlaying(int type);
int mf_movieMaskedInput(int c);

void mf_float2tv(float f, struct timeval *ptv);

#define MOVIE_MIN_FRAMECLK (0.1f)
#define MOVIE_MAX_FRAMECLK (3600.0f)
#define MOVIE_SECOND_U (1000000L)
#define MOVIE_ANTI_ANTI_IDLE

// some magic value that your igetch() will never return
#define MOVIE_KEY_ANY (0x4d464b41)  

#ifndef MOVIE_KEY_BS2
#define MOVIE_KEY_BS2 (0x7f)
#endif

#endif
// --------------------------------------------- </Optional Modules>

// used by mf_attach
void mf_parseHeaders();
void mf_freeHeaders();
void mf_determinemaxdisps(int, int);

/* 
 * mmap basic operations 
 */
int 
mf_attach(const char *fn)
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

    // BSD mmap advise. comment if your OS does not support this.
    madvise(mf.start, mf.len, MADV_SEQUENTIAL);

    mf.end = mf.start + mf.len;
    mf.disps = mf.dispe = mf.start;
    mf.lineno = 0;

    mf_determinemaxdisps(MFNAV_PAGE, 0);

    mf.disps = mf.dispe = mf.start;
    mf.lineno = 0;

    /* reset and parse article header */
    mf_parseHeaders();

    /* a workaround for wrapped separators */
    if(mf.maxlinenoS > 0 &&
            fh.lines >= mf.maxlinenoS &&
            bpref.separator & MFDISP_SEP_WRAP)
    {
        mf_determinemaxdisps(+1, 1);
    }

    return  1;
}

void 
mf_detach()
{
    mf_freeHeaders();
    if(mf.start) {
        munmap(mf.start, mf.len);
        RESETMF();
    }
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

MFPROTO int mf_backward(int); // used by mf_buildmaxdisps
MFPROTO int mf_forward(int); // used by mf_buildmaxdisps

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
MFPROTO int 
mf_backward(int lines)
{
    int real_moved = 0;

    /* backward n lines means to find n times of '\n'. */

    /* if we're already in a line break, add one mark. */
   if (mf.disps < mf.end && *mf.disps == '\n')
       lines++, real_moved --;

    while (1)
    {
        if (mf.disps < mf.start || *mf.disps == '\n')
        {
            real_moved ++;
            if(lines-- <= 0 || mf.disps < mf.start)
                break;
        }
        mf.disps --;
    }

    /* now disps points to previous 1 byte of new address */
    mf.disps ++;
    real_moved --;
    mf.lineno -= real_moved;

    return real_moved;
}

MFPROTO int 
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
    if(mf.disps == mf.start && mf.xpos > 0)
        mf.xpos = 0;
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

MFPROTO int 
mf_goto(int lineno)
{
    mf.disps = mf.start;
    mf.lineno = 0;
    return mf_forward(lineno);
}

MFPROTO int 
mf_viewedNone()
{
    return (mf.disps <= mf.start);
}

MFPROTO int 
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

    if(!s || !*s)
        return 0;

    if(direction ==  MFSEARCH_FORWARD) 
    {
        mf_forward(1);
        while(mf.disps < mf.end - l)
        {
            if(sr.cmpfunc((char*)mf.disps, (char*)s, l) == 0)
            {
                flFound = 1;
                break;
            } else {
                /* DBCS check here. */
                if(PMORE_DBCS_LEADING(*mf.disps++))
                        mf.disps++;
            }
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
                if(sr.cmpfunc((char*)mf.disps, (char*)s, l) == 0)
                {
                    flFound = 1;
                } else
                {
                    /* DBCS check here. */
                    if(PMORE_DBCS_LEADING(*mf.disps++))
                        mf.disps++;
                }
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

#define ISSPACE(x) (x <= ' ')

MFPROTO void 
pmore_str_strip_ansi(unsigned char *p)  // warning: p is NULL terminated
{
    unsigned char *pb = p;
    while (*p != 0)
    {
        if (*p == ESC_CHR)
        {
            // ansi code sequence, ignore them.
            pb = p++;
            while (ANSI_IN_ESCAPE(*p))
                p++;
            memmove(pb, p, ustrlen(p)+1);
            p = pb;
        }
        else if (*p < ' ' || *p == 0xff)
        {
            // control codes, ignore them.
            // what is 0xff? old BBS does not handle telnet protocol
            // so IACs were inserted.
            memmove(p, p+1, ustrlen(p+1)+1);
        }
        else
            p++;
    }
}

/* this chomp is a little different: 
 * it kills starting and trailing spaces.
 */
MFPROTO void 
pmore_str_chomp(unsigned char *p)
{
    unsigned char *pb = p + ustrlen(p)-1;

    while (pb >= p)
        if(ISSPACE(*pb))
            *pb-- = 0;
        else
            break;
    pb = p;
    while (*pb && ISSPACE(*pb))
        pb++;

    if(pb != p)
        memmove(p, pb, ustrlen(pb)+1);
}

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
            free(fh.headers[i]);
        for (i = 0; i < FH_FLOATS; i++)
            free(fh.floats[i]);
        RESETFH();
    }
}

void 
mf_parseHeaders()
{
    /* file format:
     * AUTHOR: author BOARD: blah <- headers[0], floats[0], floats[1]
     * XXX: xxx                   <- headers[1]
     * XXX: xxx                   <- headers[n]
     * [blank, fill with separator] <- lines
     *
     * #define STR_AUTHOR1     "作者:"
     * #define STR_AUTHOR2     "發信人:"
     */
    unsigned char *pmf = mf.start;
    int i = 0;

    RESETFH();

    if(mf.len < LEN_AUTHOR2)
        return;

    if (strncmp((char*)mf.start, STR_AUTHOR1, LEN_AUTHOR1) == 0)
    {
        fh.lines = 3;   // local
    } 
    else if (strncmp((char*)mf.start, STR_AUTHOR2, LEN_AUTHOR2) == 0)
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
        pmf ++; // move to next line.

        // p is pointing at a new line. (\n)
        l = (int)(p - pb);
#ifdef CRITICAL_MEMORY
        // kcwu: dirty hack, avoid 64byte slot. use 128byte slot instead.
        if (l<100) {
            p = (unsigned char*) malloc (100+1);
        } else {
            p = (unsigned char*) malloc (l+1);
        }
#else
        p = (unsigned char*) malloc (l+1);
#endif
        fh.headers[i] = p;
        memcpy(p, pb, l);
        p[l] = 0;

        // now, postprocess p.
        pmore_str_strip_ansi(p);

#ifdef PMORE_OVERRIDE_TIME
        // (deprecated: too many formats for newsgroup)
        // try to see if this is a valid time line
        // use strptime to convert
#endif // PMORE_OVERRIDE_TIME

        // strip to quotes[+1 space]
        if((pb = ustrchr((char*)p, ':')) != NULL)
        {
            if(*(pb+1) == ' ') pb++;
            memmove(p, pb, ustrlen(pb)+1);
        }

        // kill staring and trailing spaces
        pmore_str_chomp(p);

        // special case, floats are in line[0].
        if(i == 0 && (pb = ustrrchr(p, ':')) != NULL && *(pb+1))
        {
            unsigned char *np = (unsigned char*)strdup((char*)(pb+1));

            fh.floats[1] = np;
            pmore_str_chomp(np);
            // remove quote and traverse back
            *pb-- = 0;
            while (pb > p && *pb != ',' && !(ISSPACE(*pb)))
                pb--;

            if (pb > p) {
                fh.floats[0] = (unsigned char*)strdup((char*)(pb+1));
                pmore_str_chomp(fh.floats[0]);
                *pb = 0;
                pmore_str_chomp(fh.headers[0]);
            } else {
                fh.floats[0] = (unsigned char*)strdup("");
            }
        }
    }
}

/*
 * mf_display utility macros
 */
MFPROTO void
MFDISP_SKIPCURLINE()
{ 
    while (mf.dispe < mf.end && *mf.dispe != '\n')
        mf.dispe++;
}

MFPROTO int
MFDISP_PREDICT_LINEWIDTH(unsigned char *p)
{
    /* predict from p to line-end, without ANSI seq.
     */
    int off = 0;
    int inAnsi = 0;

    while (p < mf.end && *p != '\n')
    {
        if(inAnsi)
        {
            if(!ANSI_IN_ESCAPE(*p))
                inAnsi = 0;
        } else {
            if(*p == ESC_CHR)
                inAnsi = 1;
            else
                off ++;
        }
        p++;
    }
    return off;
}

MFPROTO int
MFDISP_DBCS_HEADERWIDTH(int originalw)
{
    return originalw - (originalw %2);
//    return (originalw >> 1) << 1;
}

#define MFDISP_FORCEUPDATE2TOP() { startline = 0; }
#define MFDISP_FORCEUPDATE2BOT() { endline   = MFDISP_PAGE - 1; }
#define MFDISP_FORCEDIRTY2BOT() \
    if(optimized == MFDISP_OPT_OPTIMIZED) { \
        optimized = MFDISP_OPT_FORCEDIRTY; \
        MFDISP_FORCEUPDATE2BOT(); \
    }

static char *override_msg = NULL;
static char *override_attr = NULL;

#define RESET_OVERRIDE_MSG() { override_attr = override_msg = NULL; }

/*
 * display mf content from disps for MFDISP_PAGE
 */

void 
mf_display()
{
    int lines = 0, col = 0, currline = 0, wrapping = 0;
    int startline, endline;
    int needMove2bot = 0;

    int optimized = MFDISP_OPT_CLEAR;

    /* why t_columns-1 here?
     * because BBS systems usually have a poor terminal system
     * and many stupid clients behave differently.
     * So we try to avoid using the last column, leave it for
     * BBS to place '\n' and CLRTOEOL.
     */
    const int headerw = MFDISP_DBCS_HEADERWIDTH(t_columns-1);
    const int dispw = headerw - (t_columns - headerw < 2);
    const int maxcol = dispw - 1;
    int newline_default = MFDISP_NEWLINE_CLEAR;

    if(mf.wraplines || mf.trunclines)
        MFDISP_DIRTY(); // we can't scroll with wrapped lines.

    mf.wraplines = 0;
    mf.trunclines = 0;
    mf.dispedlines = 0;

    MFDISP_FORCEUPDATE2TOP();
    MFDISP_FORCEUPDATE2BOT();

#ifdef PMORE_USE_OPT_SCROLL

#if defined(PMORE_USE_ASCII_MOVIE) && !defined(PMORE_USING_POOR_TERM)
    // For movies, maybe clear() is better.
    // Let's enable for good terminals (which does not need workarounds)
    if (MOVIE_IS_PLAYING())
    {
        clear(); move(0, 0);
    } else
#endif // PMORE_USE_ASCII_MOVIE && (!PMORE_USING_POOR_TERM)

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
            pmore_clrtoeol(b_lines, 0);
            // move(b_lines, 0);
            // clrtoeol();
        }

        if(scrll > MFDISP_PAGE)
            scrll = MFDISP_PAGE;

        i = scrll;

#if defined(USE_PFTERM)
        // In fact, pfterm will flash black screen when scrolling pages...
        // So it may work better if we refresh whole screen.
        if (i >= b_lines / 2)
        {
            clear(); move(0, 0);
            scrll = MFDISP_PAGE;
        } else
#endif // defined(USE_PFTERM)

        while(i-- > 0)
            if (reverse)
                rscroll();      // v
            else
                scroll();       // ^

        if(reverse)
        {
            endline = scrll-1;          // v
            // clear the line which will be scrolled
            // to bottom (status line position).
            pmore_clrtoeol(b_lines, 0);
            // move(b_lines, 0);
            // clrtoeol();
        }
        else
        {
            startline = MFDISP_PAGE - scrll; // ^
        }
        move(startline, 0);
        optimized = MFDISP_OPT_OPTIMIZED;
        // return;      // uncomment if you want to observe scrolling
    }
    else
#endif
        clear(), move(0, 0);

    mf.dispe = mf.disps;
    while (lines < MFDISP_PAGE) 
    {
        int inAnsi = 0;
        int newline = newline_default;
        int predicted_linewidth = -1;
        int xprefix = mf.xpos;

#ifdef PMORE_USE_DBCS_WRAP
        unsigned char *dbcs_incomplete = NULL;
#endif

        currline = mf.lineno + lines;
        col = 0;

        if(!wrapping && mf.dispe < mf.end)
            mf.dispedlines++;

        if(optimized == MFDISP_OPT_FORCEDIRTY)
        {
            /* btw, apparently this line should be visible. 
             * if not, maybe something wrong.
             */
            pmore_clrtoeol(lines, 0);
        }

#ifdef PMORE_USE_ASCII_MOVIE
        if(mfmovie.mode == MFDISP_MOVIE_PLAYING_OLD &&
                mfmovie.compat24)
        {
            if(mf.dispedlines == 23)
                return;
        } 
        else if (mfmovie.mode == MFDISP_MOVIE_DETECTED)
        {
            // detected only applies for first page.
            // since this is not very often, let's prevent
            // showing control codes.
            if(mf_movieFrameHeader(mf.dispe, mf.end))
                MFDISP_SKIPCURLINE();
        }
        else if(mfmovie.mode == MFDISP_MOVIE_UNKNOWN || 
                mfmovie.mode == MFDISP_MOVIE_PLAYING)
        {
            if(mf_movieFrameHeader(mf.dispe, mf.end))
                switch(mfmovie.mode)
                {

                    case MFDISP_MOVIE_UNKNOWN:
                        mfmovie.mode = MFDISP_MOVIE_DETECTED;
                        /* let's remove the first control sequence. */
                        MFDISP_SKIPCURLINE();
                        break;

                    case MFDISP_MOVIE_PLAYING:
                        /*
                         * maybe we should do clrtobot() here,
                         * but it's even better if we do clear()
                         * all time. so we set dirty here for
                         * next frame, and please set dirty before
                         * playing.
                         */
                        MFDISP_DIRTY();
                        return;
                }
        } 
#endif

        /* Is currentline visible? */
        if (lines < startline || lines > endline)
        {
            MFDISP_SKIPCURLINE();
            newline = MFDISP_NEWLINE_SKIP;
        }
        /* Now, consider what kind of line
         * (header, separator, or normal text)
         * is current line.
         */
        else if (currline == fh.lines && bpref.rawmode == MFDISP_RAW_NA)
        {
            /* case 1, header separator line */
            if (bpref.separator & MFDISP_SEP_LINE)
            {
                outs(ANSI_COLOR(36));
                for(col = 0; col < headerw; col+=2)
                {
                    // prints("%02d", col);
                    outs("─");
                }
                outs(ANSI_RESET);
            }

            /* Traditional 'more' adds separator as a newline.
             * This is buggy, however we can support this
             * by using wrapping features.
             * Anyway I(piaip) don't like this. And using wrap
             * leads to slow display (we cannt speed it up with
             * optimized scrolling.
             */
            if(bpref.separator & MFDISP_SEP_WRAP) 
            {
                /* we have to do all wrapping stuff
                 * in normal text section.
                 * make sure this is updated.
                 */
                wrapping = 1;
                mf.wraplines ++;
                MFDISP_FORCEDIRTY2BOT();
                if(mf.dispe > mf.start && 
                        mf.dispe < mf.end &&
                        *mf.dispe == '\n')
                    mf.dispe --;
            }
            else
                MFDISP_SKIPCURLINE();
        } 
        else if (currline < fh.lines && bpref.rawmode == MFDISP_RAW_NA )
        {
            /* case 2, we're printing headers */
            const char *val = (const char*)fh.headers[currline];
            const char *name = _fh_disp_heads[currline];
            int w = headerw - FH_HEADER_LEN - 3;

            outs(ANSI_COLOR(47;34) " ");
            outs(name);
            outs(" " ANSI_COLOR(44;37) " "); 

            /* right floating stuff? */
            if (currline == 0 && fh.floats[0])
            {
                w -= ustrlen(fh.floats[0]) + ustrlen(fh.floats[1]) + 4;
            }

            prints("%-*.*s", w, w, 
                    (val ? val : ""));

            if (currline == 0 && fh.floats[0])
            {
                outs(ANSI_COLOR(47;34) " ");
                outs((const char*)fh.floats[0]);
                outs(" " ANSI_COLOR(44;37) " "); 
                outs((const char*)fh.floats[1]);
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

            unsigned char c;

            if(xprefix > 0 && !bpref.oldwrapmode && bpref.wrapindicator)
            {
                outs(MFDISP_WNAV_INDICATOR);
                col++;
            }

            // first check quote
            if(bpref.rawmode == MFDISP_RAW_NA)
            {
                if(dist > 1 && 
                        (*mf.dispe == ':' || *mf.dispe == '>') && 
                        *(mf.dispe+1) == ' ')
                {
                    outs(ANSI_COLOR(36));
                    flResetColor = 1;
                } else if (dist > 2 && 
                        (!strncmp((char*)mf.dispe, "※", 2) || 
                         !strncmp((char*)mf.dispe, "==>", 3)))
                {
                    outs(ANSI_COLOR(32));
                    flResetColor = 1;
                }
            }

            while(!breaknow && mf.dispe < mf.end && (c = *mf.dispe) != '\n')
            {
                if(inAnsi)
                {
                    if (!ANSI_IN_ESCAPE(c))
                        inAnsi = 0;
                    /* whatever this is, output! */
                    mf.dispe ++;
                    switch(bpref.rawmode)
                    {
                        case MFDISP_RAW_NOANSI:
                            /* TODO
                             * col++ here may be buggy. */
                            if(col < t_columns)
                            {
                                /* we tried our best to determine */
                                if(xprefix > 0)
                                    xprefix --;
                                else
                                {
                                    outc(c); 
                                    col++;
                                }
                            }
                            if(!inAnsi)
                                outs(ANSI_RESET);
                            break;
                        case MFDISP_RAW_PLAIN:
                            break;

                        default:
                            if(ANSI_IN_MOVECMD(c))
                            {
#ifdef PMORE_RESTRICT_ANSI_MOVEMENT
                                c = 's'; // "save cursor pos"
#else // PMORE_RESTRICT_ANSI_MOVEMENT
                                // some user cannot live without this.
                                // make them happy.
                                newline_default = newline = MFDISP_NEWLINE_MOVE;
#ifdef PMORE_USE_ASCII_MOVIE
                                // relax for movies
                                if (!MOVIE_IS_PLAYING())
#endif // PMORE_USE_ASCII_MOVIE
                                {
                                    override_attr = ANSI_COLOR(1;37;41);
                                    override_msg = PMORE_MSG_WARN_MOVECMD;
                                }
#endif // PMORE_RESTRICT_ANSI_MOVEMENT
                                needMove2bot = 1;
                            }
                            outc(c);
                            break;
                    }
                    continue;

                } else {

                    if(c == ESC_CHR)
                    {
                        inAnsi = 1;
                        /* we can't output now because maybe
                         * ptt_prints wants to do something.
                         */
                    }
                    else if(sr.search_str && srlen < 0 &&  // support search
#ifdef PMORE_USE_DBCS_WRAP
                            dbcs_incomplete == NULL &&
#endif
                            mf.end - mf.dispe > sr.len &&
                            sr.cmpfunc((char*)mf.dispe, (char*)sr.search_str, sr.len) == 0)
                    {
                            outs(ANSI_COLOR(7)); 
                            srlen = sr.len-1;
                            flResetColor = 1;
                    }

#ifdef PMORE_USE_PTT_PRINTS
                    /* special case to resolve dirty Ptt_prints */
                    if(inAnsi && 
                            mf.end - mf.dispe > 2 &&
                            *(mf.dispe+1) == '*')
                    {
                        int i;
                        char buf[64];   // make sure ptt_prints will not exceed

                        memset(buf, 0, sizeof(buf));
                        memcpy(buf, mf.dispe, 3);  // ^[*s
                        mf.dispe += 2;

                        if(bpref.rawmode)
                            buf[0] = '*';
                        else
                        {
#ifdef LOW_SECURITY
# define PTTPRINT_WARN_PATTERN "slpnbm"
#else
# define PTTPRINT_WARN_PATTERN "slpn" 
#endif // LOW_SECURITY
                            if(strchr(PTTPRINT_WARN_PATTERN, buf[2]) != NULL)
                            {
                                override_attr = ANSI_COLOR(1;37;41);
                                override_msg = PMORE_MSG_WARN_FAKEUSERINFO;
                            }
                            Ptt_prints(buf, sizeof(buf), NO_RELOAD); // result in buf
                        }
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
                        switch(bpref.rawmode)
                        {
                            case MFDISP_RAW_NOANSI:
                                /* TODO
                                 * col++ here may be buggy. */
                                if(col < t_columns)
                                {
                                    /* we tried our best to determine */
                                    if(xprefix > 0)
                                        xprefix --;
                                    else
                                    {
                                        outs(ANSI_COLOR(1) "*");
                                        col++;
                                    }
                                }
                                break;
                            case MFDISP_RAW_PLAIN:
                                break;
                            default:
                                outc(ESC_CHR);
                                break;
                        }
                    } else {
                        int canOutput = 0;
                        /* if col > maxcol,
                         * because we have the space for
                         * "indicators" (one byte),
                         * so we can tolerate one more byte.
                         */
                        if(col <= maxcol)       // normal case
                            canOutput = 1;
                        else if (bpref.oldwrapmode && // oldwrapmode
                            col < t_columns)
                        {
                            canOutput = 1;
                            newline = MFDISP_NEWLINE_MOVE;
                        } else {
                            int off = 0;
                            // put more efforts to determine
                            // if we can use indicator space
                            // determine real offset between \n
                            if(predicted_linewidth < 0)
                                predicted_linewidth = col + 1 +
                                    MFDISP_PREDICT_LINEWIDTH(mf.dispe+1);
                            off = predicted_linewidth - (col + 1);

                            if (col + off <= (maxcol+1))
                            {
                                canOutput = 1;  // indicator space
                            }
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
#ifdef PMORE_USE_DBCS_WRAP
                            if(mf.xpos > 0 && dbcs_incomplete && col < 2)
                            {
                                /* col = 0 or 1 only */
                                if(col == 0) /* no indicators */
                                    c = ' ';
                                else if(!bpref.oldwrapmode && bpref.wrapindicator)
                                    c = ' ';
                            }

                            if (dbcs_incomplete)
                                dbcs_incomplete = NULL;
                            else if(PMORE_DBCS_LEADING(c)) 
                                dbcs_incomplete = mf.dispe;
#endif
                            if(xprefix > 0)
                                xprefix --;
                            else
                            {
                                outc(c);
                                col++;
                            }

                            if (srlen == 0)
                                outs(ANSI_RESET);
                            if(srlen >= 0)
                                srlen --;
                        }
                        else
                        /* wrap modes */
                        if(mf.xpos > 0 || bpref.wrapmode == MFDISP_WRAP_TRUNCATE)
                        {
                            breaknow = 1;
                            mf.trunclines ++;
                            MFDISP_SKIPCURLINE();
                            wrapping = 0;
                        }
                        else if (bpref.wrapmode == MFDISP_WRAP_WRAP)
                        {
                            breaknow = 1;
                            wrapping = 1;
                            mf.wraplines ++;
#ifdef PMORE_USE_DBCS_WRAP
                            if(dbcs_incomplete)
                            {
                                mf.dispe = dbcs_incomplete;
                                dbcs_incomplete = NULL;
                                /* to be more dbcs safe,
                                 * use the followings to
                                 * erase printed character.
                                 */
                                if(col > 0) {
                                    /* TODO BUG BUGGY
                                     * using move is maybe actually non-sense
                                     * because BBS terminal system cannot
                                     * display this when ANSI escapes were used
                                     * in same line.  However, on most
                                     * situation this works.
                                     * So we used an alternative, forced ANSI 
                                     * move command.
                                     */
                                    // move(lines, col-1);
                                    char ansicmd[16];
                                    sprintf(ansicmd, ANSI_MOVETO(%d,%d),
                                            lines+1, col-1+1);
                                    /* to preven ANSI ESCAPE being tranlated as
                                     * DBCS trailing byte. */
                                    outc(' '); 
                                    /* move back one column */
                                    outs(ansicmd);
                                    /* erase it (previous leading byte)*/
                                    outc(' ');
                                    /* go to correct position */
                                    outs(ansicmd);
                                }
                            }
#endif
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
                    MFDISP_FORCEDIRTY2BOT();

                if(!bpref.oldwrapmode && bpref.wrapindicator && col < t_columns)
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
         * never mind if that's caused by separator
         * however this code is rarely used now.
         * only if no preload file.
         */
        if (bpref.separator & MFDISP_SEP_WRAP &&
            mf.wraplines == 1 &&
            mf.lineno < fh.lines)
        {
            /*
             * o.k, now we know maxline should be one line forward.
             */
            mf_determinemaxdisps(+1, 1);
        } else 
        {
            /* not caused by separator?
             * ok, then it's by wrapped lines.
             *
             * old flavor: go bottom: 
             *    mf_determinemaxdisps(0)
             * however we have "update" method now,
             * so we can achieve more user friendly behavior.
             */
            mf_determinemaxdisps(+mf.wraplines, 1);
        }
    }
    mf.oldlineno = mf.lineno;

    if (needMove2bot)
        move(b_lines, 0);
}

/* --------------------- MAIN PROCEDURE ------------------------- */
void pmore_Preference();
void pmore_QuickRawModePref();
void pmore_Help();

static const char    * const pmore_help[] = {
    "\0閱\讀文章功\能鍵使用說明",
    "\01游標移動功\能鍵",
    "(k/↑) (j/↓/Enter)   上捲/下捲一行",
    "(^B/PgUp/BackSpace)   上捲一頁",
    "(^F/PgDn/Space/→)    下捲一頁",
    "(,/</S-TAB)(./>/TAB)  左/右捲動",
    "(0/g/Home) ($/G/End)  檔案開頭/結尾",
    "數字鍵 1-9 (;/:)      跳至輸入的頁數或行數",
    "\01進階功\能鍵",
    "(/)  (s)              搜尋關鍵字/切換至其它看板",
    "(n/N)                 重複正/反向搜尋",
    "(f/b)                 跳至下/上篇",
    "(a/A)                 跳至同一作者下/上篇",
    "(t/[-/]+)             主題式閱\讀:循序/前/後篇",
    "\01其他功\能鍵",

    // the line below is already aligned, because of the backslash.
   "(o)/(\\)               選項設定/色彩顯示模式",

#if defined (PMORE_USE_ASCII_MOVIE) || defined(RET_DOCHESSREPLAY)
    "(p)/(z)               播放動畫/棋局打譜",
#endif // defined(PMORE_USE_ASCII_MOVIE) || defined(RET_DOCHESSREPLAY)
#ifdef RET_COPY2TMP
    "(Ctrl-T)              存入暫存檔",
#endif
    "(q/←) (h/H/?/F1)     結束/本說明畫面",
#ifdef DEBUG
    "(d)                   切換除錯(debug)模式",
#endif
    /* You don't have to delete this copyright line.
     * It will be located in bottom of screen and overrided by
     * status line. Only user with big terminals will see this :)
     */
    "\01本系統使用 piaip 的新式瀏覽程式: pmore 2007, piaip's more",
    NULL
};
/*
 * pmore utility macros
 */
MFPROTO void
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

MFPROTO void
PMORE_UINAV_FORWARDLINE()
{
    if(mf_viewedAll())
        return;
    mf_forward(1);
}

#define REENTRANT_RESTORE() { mf = bkmf; fh = bkfh; }

/*
 * piaip's more, a replacement for old more
 */
int 
pmore(char *fpath, int promptend)
{
    int flExit = 0, retval = 0;
    int ch = 0;
    int invalidate = 1;

    /* simple re-entrant hack
     * I don't want to write pointers everywhere,
     * and pmore should be simple enough (inside itself)
     * so we can do so.
     */

    MmappedFile bkmf;
    MF_PrettyFormattedHeader bkfh;

#ifdef PMORE_USE_ASCII_MOVIE
    RESET_MOVIE();
#endif

    bkmf = mf; /* simple re-entrant hack */
    bkfh = fh;
    RESETFH();
    RESETMF();
    
    override_msg = NULL; /* elimiate pending errors */

    STATINC(STAT_MORE);
    if(!mf_attach(fpath))
    {
        REENTRANT_RESTORE();
        return -1;
    }

    clear();
    while(!flExit)
    {
        if(invalidate)
        {
            mf_display(); 
            invalidate = 0;
        }

        /* in current implementation,
         * we want to invalidate for each keypress.
         */
        invalidate = 1;

#ifdef  PMORE_TRADITIONAL_PROMPTEND

        if(promptend == NA)
        {
#ifdef PMORE_USE_ASCII_MOVIE
            if(mfmovie.mode == MFDISP_MOVIE_DETECTED)
            {
                /* quick auto play */
                mfmovie.mode = MFDISP_MOVIE_YES;
                RESET_MOVIE();
                mfmovie.mode = MFDISP_MOVIE_PLAYING;
                mf_determinemaxdisps(0, 0); // display until last line
                mf_movieNextFrame();
                MFDISP_DIRTY();
                continue;
            } else if (mfmovie.mode != MFDISP_MOVIE_PLAYING)
#endif
            break;
        }
#else
        if(promptend == NA && mf_viewedAll())
            break;
#endif
        move(b_lines, 0);
        // clrtoeol(); // this shall be done in mf_display to speed up.
        
#ifdef USE_BBSLUA
        // TODO prompt BBS Lua status here.
#endif // USE_BBSLUA

#ifdef PMORE_USE_ASCII_MOVIE
        switch (mfmovie.mode)
        {
            case MFDISP_MOVIE_UNKNOWN:
                mfmovie.mode = MFDISP_MOVIE_NO;
                break;

            case MFDISP_MOVIE_DETECTED:
                mfmovie.mode = MFDISP_MOVIE_YES;
                {
                    // query if user wants to play movie.

                    int w = t_columns-1;
                    const char *s = PMORE_MSG_MOVIE_DETECTED;

                    outs(ANSI_RESET ANSI_COLOR(1;33;44));
                    w -= strlen(s); outs(s);

                    while(w-- > 0) outc(' '); outs(ANSI_RESET ANSI_CLRTOEND);
                    w = tolower(igetch());

                    if(     w != 'n' && 
                            w != KEY_UP && w != KEY_LEFT &&
                            w != 'q')
                    {
                        RESET_MOVIE();
                        mfmovie.mode = MFDISP_MOVIE_PLAYING;
                        mf_determinemaxdisps(0, 0); // display until last line
                        mf_movieNextFrame();
                        MFDISP_DIRTY();
                        // remove override messages
                        RESET_OVERRIDE_MSG();
                        continue;
                    }
                    /* else, we have to clean up. */
                    move(b_lines, 0);
                    clrtoeol();
                }
                break;

            case MFDISP_MOVIE_PLAYING_OLD:
            case MFDISP_MOVIE_PLAYING:

                mf_moviePromptPlaying(0);

                // doing refresh() here is better,
                // to prevent that we forgot to refresh
                // in SyncFrame.
                refresh();

                if(mf_movieSyncFrame())
                {
                    /* user did not hit anything.
                     * play next frame.
                     */
                    if(mfmovie.mode == MFDISP_MOVIE_PLAYING)
                    {
                        if(!mf_movieNextFrame())
                        {
                            STOP_MOVIE();

                            if(promptend == NA)
                            {
                                /* if we played to end,
                                 * no need to prevent pressanykey().
                                 */
                                flExit = 1, retval = 0;
                            }
                        }
                    }
                    else if(mfmovie.mode == MFDISP_MOVIE_PLAYING_OLD)
                    {
                        if(mf_viewedAll())
                        {
                            mfmovie.mode = MFDISP_MOVIE_NO;
                            mf_determinemaxdisps(MFNAV_PAGE, 0);
                            mf_forward(0);
                        }
                        else
                        {
                            if(!mfmovie.compat24)
                                PMORE_UINAV_FORWARDPAGE();
                            else
                                mf_forward(22);
                        }
                    }
                } else {
                    /* TODO simple navigation here? */

                    /* stop playing */
                    if(mfmovie.mode == MFDISP_MOVIE_PLAYING)
                    {
                        STOP_MOVIE();
                        if(promptend == NA)
                        {
                            flExit = 1, retval = READ_NEXT;
                        }
                    }
                    else if(mfmovie.mode == MFDISP_MOVIE_PLAYING_OLD)
                    {
                        mfmovie.mode = MFDISP_MOVIE_NO;
                        mf_determinemaxdisps(MFNAV_PAGE, 0);
                        mf_forward(0);
                    }
                }
                continue;
        }
#endif

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

            char buf[256];      // orz
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
                       ((bpref.separator & MFDISP_SEP_WRAP) &&
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
                            "  瀏覽 第 %1d/%1d 頁 (%3d%%) ",
                            nowpage,
                            allpages,
                            progress
                           );
                else
                    sprintf(buf,
                            "  瀏覽 第 %1d 頁 (%3d%%) ",
                            nowpage,
                            progress
                           );
                outs(buf); prefixlen += strlen(buf);

                outs(ANSI_COLOR(1;30;47));

                if(override_msg)
                {
                    buf[0] = 0;
                    if(override_attr) outs(override_attr);
                    snprintf(buf, sizeof(buf), override_msg);
                    RESET_OVERRIDE_MSG();
                }
                else
                if(mf.xpos > 0)
                {
                    snprintf(buf, sizeof(buf),
                            " 顯示範圍: %d~%d 欄位, %02d~%02d 行",
                            (int)mf.xpos+1, 
                            (int)(mf.xpos + t_columns-(mf.trunclines ? 2 : 1)),
                            (int)(mf.lineno + 1),
                            (int)(mf.lineno + mf.dispedlines)
                           );
                } else {
                    snprintf(buf, sizeof(buf),
                            " 目前顯示: 第 %02d~%02d 行",
                            (int)(mf.lineno + 1),
                            (int)(mf.lineno + mf.dispedlines)
                           );
                }

                outs(buf); prefixlen += strlen(buf);

                postfix1len = 12;       // check msg below
                postfix2len = 10;
                if(mf_viewedAll()) postfix1len = 17;

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
                        mf_viewedAll() ?
                            ANSI_COLOR(0;31;47)"(y)" ANSI_COLOR(30) "回信"
                            ANSI_COLOR(31) "(X/%)" ANSI_COLOR(30) "推文 "
                        :
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
            /* -------------- NEW EXITING KEYS ------------------ */
#ifdef RET_DOREPLY
            case 'r': case 'R':
                flExit = 1,     retval = RET_DOREPLY;
                break;
            case 'Y': case 'y':
                flExit = 1,     retval = RET_DOREPLYALL;
                break;
#endif
#ifdef RET_DORECOMMEND
                // recommend
            case '%':
            case 'X':
                flExit = 1,     retval = RET_DORECOMMEND;
                break;
#endif
#ifdef RET_DOQUERYINFO
            case 'Q': // info query interface
                flExit = 1,     retval = RET_DOQUERYINFO;
                break;
#endif
#ifdef RET_DOSYSOPEDIT
            case 'E':
                flExit = 1,     retval = RET_DOSYSOPEDIT;
                break;
#endif
#ifdef RET_DOCHESSREPLAY
            case 'z':
                flExit = 1,     retval = RET_DOCHESSREPLAY;
                break;
#endif 
#ifdef RET_COPY2TMP
            case Ctrl('T'):
                flExit = 1,     retval = RET_COPY2TMP;
                break;
#endif
#ifdef RET_SELECTBRD
            case 's':
                flExit = 1,     retval = RET_SELECTBRD;
                break;
#endif
            /* ------------------ EXITING KEYS ------------------ */
            case 'A':
                flExit = 1,     retval = AUTHOR_PREV;
                break;
            case 'a':
                flExit = 1,     retval = AUTHOR_NEXT;
                break;
            case 'F': case 'f':
                flExit = 1,     retval = READ_NEXT;
                break;
            case 'B': case 'b':
                flExit = 1,     retval = READ_PREV;
                break;
            case KEY_LEFT:
                flExit = 1,     retval = FULLUPDATE;
                break;
            case 'q':
                flExit = 1,     retval = FULLUPDATE;
                break;

                /* from Kaede, thread reading */
            case ']':
            case '+':
                flExit = 1,     retval = RELATE_NEXT;
                break;
            case '[':
            case '-':
                flExit = 1,     retval = RELATE_PREV;
                break;
            case '=':
                flExit = 1,     retval = RELATE_FIRST;
                break;
            /* ------------------ NAVIGATION KEYS ------------------ */
            /* Simple Navigation */
            case 'k': case 'K':
                mf_backward(1);
                break;
            case 'j': case 'J':
                PMORE_UINAV_FORWARDLINE();
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

#ifdef PMORE_ACCURATE_WRAPEND
                /* allright. in design of pmore,
                 * it's possible that when user navigates to file end,
                 * a wrapped line made nav not 100%.
                 */
                mf_display();
                invalidate = 0;

                if(!mf_viewedAll())
                {
                    /* one more try. */
                    mf_goBottom();
                    invalidate = 1;
                }
#endif
                break;

            /* Compound Navigation */
            case '.':
                if(mf.xpos == 0)
                    mf.xpos ++;
                mf.xpos ++;
                break;
            case ',':
                if(mf.xpos > 0)
                    mf.xpos --;
                break;
            case '\t':
            case '>':
                //if(mf.xpos == 0 || mf.trunclines)
                    mf.xpos = (mf.xpos/8+1)*8;
                break;
                /* acronym form shift-tab, ^[[Z */
                /* however some terminals does not send that. */
            case KEY_STAB:
            case '<':
                mf.xpos = (mf.xpos/8-1)*8;
                if(mf.xpos < 0) mf.xpos = 0;
                break;
            case '\r':
            case '\n':
            case KEY_DOWN:
                if (mf_viewedAll() ||
                        (promptend == 2 && (ch == '\r' || ch == '\n')))
                    flExit = 1, retval = READ_NEXT;
                else
                    PMORE_UINAV_FORWARDLINE();
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
                {
                    /* if mf.xpos > 0, widenav mode. */
                    /* because we have other keys to do so,
                     * disable it now.
                     */
                    /*
                    if(mf.trunclines > 0)
                    {
                        if(mf.xpos == 0)
                            mf.xpos++;
                        mf.xpos++;
                    }
                    else if (mf.xpos == 0)
                    */
                        PMORE_UINAV_FORWARDPAGE();
                }
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
            case '/':
                {
                    char sbuf[81] = "";
                    char ans[4] = "n";

                    if(sr.search_str) {
                        free(sr.search_str);
                        sr.search_str = NULL;
                    }

                    getdata(b_lines - 1, 0, PMORE_MSG_SEARCH_KEYWORD, sbuf,
                            40, DOECHO);

                    if (sbuf[0]) {
                        if (getdata(b_lines - 1, 0, "區分大小寫(Y/N/Q)? [N] ",
                                    ans, sizeof(ans), LCECHO) && *ans == 'y')
                            sr.cmpfunc = strncmp;
                        else if (*ans == 'q')
                            sbuf[0] = 0;
                        else
                            sr.cmpfunc = strncasecmp;
                    }
                    sr.len = strlen(sbuf);
                    if(sr.len) sr.search_str = (unsigned char*)strdup(sbuf);
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
                    char buf[16] = "";
                    int  i = 0;
                    int  pageMode = (ch != ':');
                    if (ch >= '1' && ch <= '9')
                        buf[0] = ch, buf[1] = 0;

                    pmore_clrtoeol(b_lines-1, 0);
                    getdata_buf(b_lines-1, 0, 
                            (pageMode ? 
                             "跳至此頁(若要改指定行數請在結尾加.): " : 
                             "跳至此行: "),
                            buf, 8, DOECHO);
                    if(buf[0]) {
                        i = atoi(buf);
                        if(buf[strlen(buf)-1] == '.')
                            pageMode = 0;
                        if(i-- > 0)
                            mf_goto(i * (pageMode ? MFNAV_PAGE : 1));
                    }
                    MFDISP_DIRTY();
                }
                break;

            case 'h': case 'H': case KEY_F1:
            case '?':
                // help
                show_help(pmore_help);
                MFDISP_DIRTY();
                break;

#ifdef  PMORE_NOTIFY_NEWPREF
                //let's be backward compatible!
            case 'l':
            case 'w':
            case 'W':
            case '|':
                vmsg("這個按鍵已整合進新的設定 (o) 了");
                break;

#endif // PMORE_NOTIFY_NEWPREF

            case '\\':  // everyone loves backslash, let's keep it.
                pmore_QuickRawModePref();
                MFDISP_DIRTY();
                break;

            case 'o':
                pmore_Preference();
                MFDISP_DIRTY();
                break;

#if defined(USE_BBSLUA) && defined(RET_DOBBSLUA)
            case 'P':
                vmsg("非常抱歉，BBS-Lua 的熱鍵已改為 L ，請改按 L");
                break;

            case 'L':
            case 'l':
                flExit = 1,     retval = RET_DOBBSLUA;
                break;
#endif

#ifdef PMORE_USE_ASCII_MOVIE
            case 'p':
                /* play ascii movie again
                 */
                if(mfmovie.mode == MFDISP_MOVIE_YES)
                {
                    RESET_MOVIE();
                    mfmovie.mode = MFDISP_MOVIE_PLAYING;
                    mf_determinemaxdisps(0, 0); // display until last line
                    /* it is said that it's better not to go top. */
                    // mf_goTop();
                    mf_movieNextFrame();
                    MFDISP_DIRTY();
                } 
                else if (mfmovie.mode == MFDISP_MOVIE_NO)
                {
                    static char buf[10]="1";
                    //move(b_lines-1, 0);
                    
                    /* 
                     * TODO scan current page to confirm if this is a new style movie
                     */
                    pmore_clrtoeol(b_lines-1, 0);
                    getdata_buf(b_lines - 1, 0, 
                            PMORE_MSG_MOVIE_PLAYOLD_GETTIME,
                            buf, 8, LCECHO);

                    if(buf[0])
                    {
                        float nf = 0;
                        nf = atof(buf); // sscanf(buf, "%f", &nf);
                        RESET_MOVIE();

                        mfmovie.mode = MFDISP_MOVIE_PLAYING_OLD;
                        mf_float2tv(nf, &mfmovie.frameclk);
                        mfmovie.compat24 = 0;
                        /* are we really going to start? check termsize! */
                        if (t_lines != 24)
                        {
                            char ans[4];
                            pmore_clrtoeol(b_lines-1, 0);
                            getdata(b_lines - 1, 0, 
                                    PMORE_MSG_MOVIE_PLAYOLD_AS24L,
                                    ans, 3, LCECHO);
                            if(ans[0] == 'n')
                                mfmovie.compat24 = 0;
                            else
                                mfmovie.compat24 = 1;
                        }
                        mf_determinemaxdisps(0, 0); // display until last line
                        MFDISP_DIRTY();
                    }
                }
                break;
#endif

#ifdef DEBUG
            case 'd':
                debug = !debug;
                MFDISP_DIRTY();
                break;
#endif

        }
        /* DO NOT DO ANYTHING HERE. NOT SAFE RIGHT NOW. */
    }

    mf_detach();
    if (retval == 0 && promptend) {
        pressanykey();
        clear();
    } else
        outs(reset_color);

    REENTRANT_RESTORE();
    return retval;
}

// ---------------------------------------------------- Preference and Help

static void
pmore_prefEntry(
        int isel,
        const char *key, int szKey,
        const char *text,int szText,
        const char* options)
{
    int i = 23;
    // print key/text
    outs(" " ANSI_COLOR(1;31)); //OPTATTR_NORMAL_KEY);
    if (szKey < 0)  szKey = strlen(key);
    if (szKey > 0)  outs_n(key, szKey);
    outs(ANSI_RESET " ");
    if (szText < 0) szText = strlen(text);
    if (szText > 0) outs_n(text, szText);

    i -= szKey + szText;
    if (i < 0) i+= 20; // one more chance
    while (i-- > 0) outc(' ');

    // print options
    i = 0;
    while (*options)
    {
        if (*options == '\t')
        {
            // blank option, skip it.
            i++, options++;
            continue;
        }

        if (i > 0)
            outs(ANSI_COLOR(1;30) " |" ANSI_RESET); //OPTATTR_BAR " | " ANSI_RESET); 

        // test if option has hotkey
        if (*options && *options != '\t' && 
                *(options+1) && *(options+1) == '.')
        {
            // found hotkey
            outs(ANSI_COLOR(1;31)); //OPTATTR_NORMAL_KEY);
            outc(*options);
            outs(ANSI_RESET);
            options +=2;
        }

        if (i == isel)
        {
            outs(ANSI_COLOR(1;36) "*");// OPTATTR_SELECTED);
        }
        else
            outc(' ');

        while (*options && *options != '\t')
            outc(*options++);

        outs(ANSI_RESET);

        if (*options)
            i++, options ++;
    }
    outc('\n');
}

void 
pmore_PromptBar(const char *caption, int shadow)
{
    int i = 0;

    if (shadow)
    {
        outs(ANSI_COLOR(0;1;30));
        for(i = 0; i+2 < t_columns ; i+=2)
            outs("▁");
        outs(ANSI_RESET "\n");
    }
    else
        i = t_columns -2;

    outs(ANSI_COLOR(7));
    outs(caption);
    for(i -= strlen(caption); i > 0; i--)
        outs(" ");
    outs(ANSI_RESET "\n");
}

void
pmore_QuickRawModePref()
{
    int ystart = b_lines -2;

#ifdef HAVE_GRAYOUT
    grayout(0, ystart-1, GRAYOUT_DARK);
#endif // HAVE_GRAYOUT

    while(1)
    {
        move(ystart, 0);
        clrtobot();
        pmore_PromptBar(PMORE_MSG_PREF_TITLE_QRAW, 0);

        // list options
        pmore_prefEntry(bpref.rawmode,
                "\\", 1, "色彩顯示方式:", -1,
                "1.預設格式化內容\t2.原始ANSI控制碼\t3.純文字");

        switch(vmsg("請調整設定 (1-3 可直接選定，\\可切換) 或其它任意鍵結束。"))
        {
            case '\\':
                bpref.rawmode = (bpref.rawmode+1) % MFDISP_RAW_MODES;
                break;
            case '1':
                bpref.rawmode = MFDISP_RAW_NA;
                return;
            case '2':
                bpref.rawmode = MFDISP_RAW_NOANSI;
                return;
            case '3':
                bpref.rawmode = MFDISP_RAW_PLAIN;
                return;
            case KEY_LEFT:
                if (bpref.rawmode > 0) bpref.rawmode --;
                break;
            case KEY_RIGHT:
                if (bpref.rawmode < MFDISP_RAW_MODES-1) bpref.rawmode ++;
                break;
            default:
                return;
        }
    }
}

void
pmore_Preference()
{
    int ystart = b_lines - 9;
    // TODO even better pref navigation, like arrow keys
    // static int lastkey = '\\'; // default key

#ifdef HAVE_GRAYOUT
    grayout(0, ystart-1, GRAYOUT_DARK);
#endif // HAVE_GRAYOUT

    while (1)
    {
        move(ystart, 0);
        clrtobot();
        pmore_PromptBar(PMORE_MSG_PREF_TITLE, 1);
        outs("\n");

        // list options
        pmore_prefEntry(bpref.rawmode,
                "\\", 1, "色彩顯示方式:", -1,
                "預設格式化內容\t原始ANSI控制碼\t純文字");

        pmore_prefEntry(bpref.wrapmode,
                "w", 1, "斷行方式:", -1,
                "直接截行\t自動斷行");

        pmore_prefEntry(bpref.wrapindicator,
                "m", 1, "斷行符號:", -1,
                "不顯示\t顯示");

        pmore_prefEntry(bpref.separator,
                "l", 1, "文章標頭分隔線:", -1,
                "無\t單行\t\t傳統分隔線加空行");

        pmore_prefEntry(bpref.oldstatusbar,
                "t", 1, "傳統狀態列與斷行方式: ", -1,
                "停用\t啟用");

        switch(vmsg("請調整設定或其它任意鍵結束。"))
        {
            case '\\':
            case '|':
                bpref.rawmode = (bpref.rawmode+1) % MFDISP_RAW_MODES;
                break;
            case 'w':
                bpref.wrapmode = (bpref.wrapmode+1) % MFDISP_WRAP_MODES;
                break;
            case 'm':
                bpref.wrapindicator = !bpref.wrapindicator;
                break;
            case 'l':
                // there's no MFDISP_SEP_WRAP only mode.
                if (++bpref.separator == MFDISP_SEP_WRAP)
                    bpref.separator ++;
                bpref.separator %= MFDISP_SEP_MODES;
                break;
            case 't':
                bpref.oldwrapmode  = !bpref.oldwrapmode;
                bpref.oldstatusbar = !bpref.oldstatusbar;
                break;

            default:
                // finished settings
                return;
        }
    }
}

void
pmore_Help()
{
    clear();
    stand_title("pmore 使用說明");
    vmsg("");
}

// ---------------------------------------------------- Extra modules

#ifdef PMORE_USE_ASCII_MOVIE
void 
mf_float2tv(float f, struct timeval *ptv)
{
    if(f < MOVIE_MIN_FRAMECLK)
        f = MOVIE_MIN_FRAMECLK;
    if (f > MOVIE_MAX_FRAMECLK)
        f = MOVIE_MAX_FRAMECLK;

    ptv->tv_sec = (long) f;
    ptv->tv_usec = (f - (long)f) * MOVIE_SECOND_U;
}

int 
mf_str2float(unsigned char *p, unsigned char *end, float *pf)
{
    char buf[16] = {0};
    int cbuf = 0;

    /* process time */
    while ( p < end && 
            cbuf < sizeof(buf)-1 &&
            (isdigit(*p) || *p == '.' || *p == '+' || *p == '-'))
        buf[cbuf++] = *p++;

    if (!cbuf)
        return 0;

    buf[cbuf] = 0;
    *pf = atof(buf);

    return 1;
}

/*
 * maybe you can use add_io or you have other APIs in
 * your I/O system, but we'll do it here.
 * override if you have better methods.
 */
int 
pmore_wait_key(struct timeval *ptv, int dorefresh)
{
    int sel = 0;
    fd_set readfds;
    int c = 0;

    if (dorefresh)
        refresh();

    do {
        // if already something in queue,
        // detemine if ok to break.
        while ( num_in_buf() > 0)
        {
            if (!mf_movieMaskedInput((c = igetch())))
                return c;
        }

        // wait for real user interaction
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);

#ifdef STATINC
        STATINC(STAT_SYSSELECT);
#endif
        sel = select(1, &readfds, NULL, NULL, ptv);

        // if select() stopped by other interrupt,
        // do it again.
        if (sel < 0 && errno == EINTR)
            continue;

        // if (sel > 0), try to read.
        // note: there may be more in queue.
        // will be processed at next loop.
        if (sel > 0 && !mf_movieMaskedInput((c = igetch())))
            return c;

    } while (sel > 0);

    // now, maybe something for read (sel > 0)
    // or time out (sel == 0)
    // or weird error (sel < 0)
    
    // sync clock(now) if timeout.
    if (sel == 0)
        syncnow();

    return (sel == 0) ? 0 : 1;
}

// type : 1 = option selection, 0 = normal
int
mf_moviePromptPlaying(int type)
{
    int w = t_columns - 1;
    // s may change to anykey...
    const char *s = PMORE_MSG_MOVIE_PLAYING;

    if (override_msg)
    {
        // we must warn user about something...
        move(type ? b_lines-2 : b_lines-1, 0); // clrtoeol?
        outs(ANSI_RESET);
        if (override_attr) outs(override_attr);
        w -= strlen(override_msg);
        outs(override_msg);
        while(w-- > 0) outc(' '); 

        outs(ANSI_RESET ANSI_CLRTOEND);
        RESET_OVERRIDE_MSG();
        w = t_columns -1;
    }

    move(type ? b_lines-1 : b_lines, 0); // clrtoeol?

    if (type) 
    {
        outs(ANSI_RESET ANSI_COLOR(1;34;47));
        s = " >> 請輸入選項:  (互動式動畫播放中，可按 q 或 Ctrl-C 中斷)";
    } 
    else if (mfmovie.interactive)
    {
        outs(ANSI_RESET ANSI_COLOR(1;34;47));
        s = PMORE_MSG_MOVIE_INTERACTION_PLAYING;
    } else {
        outs(ANSI_RESET ANSI_COLOR(1;30;47));
    }

    w -= strlen(s); outs(s); 

    while(w-- > 0) outc(' '); outs(ANSI_RESET ANSI_CLRTOEND);
    if (type)
    {
        move(b_lines, 0);
        clrtoeol();
    }

    return 1;
}

// return = printed characters
int
mf_moviePromptOptions(
        int isel, int maxsel,
        int key, 
        unsigned char *text, unsigned int szText)
{
    unsigned char *s = text;
    int printlen = 0;
    // determine if we need separator
    if (maxsel)
    {
        outs(OPTATTR_BAR "|" );
        printlen += 1;
    }

    // highlight if is selected
    if (isel == maxsel)
        outs(OPTATTR_SELECTED_KEY);
    else
        outs(OPTATTR_NORMAL_KEY);

    outc(' '); printlen ++;

    if (key > ' ' && key < 0x80) // isprint(key))
    {
        outc(key);
        printlen += 1;
    } else {
        // named keys
        printlen += 2;

        if (key == KEY_UP)          outs("↑");
        else if (key == KEY_LEFT)   outs("←");
        else if (key == KEY_DOWN)   outs("↓");
        else if (key == KEY_RIGHT)  outs("→");
        else if (key == KEY_PGUP)   { outs("PgUp"); printlen += 2; }
        else if (key == KEY_PGDN)   { outs("PgDn"); printlen += 2; }
        else if (key == KEY_HOME)   { outs("Home"); printlen += 2; }
        else if (key == KEY_END)    { outs("End");  printlen ++; }
        else if (key == KEY_INS)    { outs("Ins");  printlen ++; }
        else if (key == KEY_DEL)    { outs("Del");  printlen ++; }
        else if (key == '\b')       { outs("←BS"); printlen += 2; }
        // else if (key == MOVIE_KEY_ANY)  // same as default
        else printlen -= 2;
    }

    // check text: we don't allow special char.
    if (text && szText && text[0]) 
    {
        while (s < text + szText && *s > ' ')
        {
            s++;
        }
        szText = s - text;
    } 
    else
    { 
        // default option text
        text = (unsigned char*)"☆";
        szText = ustrlen(text);
    }

    if (szText)
    {
        if (isel == maxsel)
            outs(OPTATTR_SELECTED);
        else
            outs(OPTATTR_NORMAL);
        outs_n((char*)text, szText);
        printlen += szText;
    }

    outc(' '); printlen ++;

    // un-highlight
    if (isel == maxsel)
        outs(OPTATTR_NORMAL);

    return printlen;
}

int 
mf_movieNamedKey(int c)
{
    switch (c)
    {
        case 'u': return KEY_UP;
        case 'd': return KEY_DOWN;
        case 'l': return KEY_LEFT;
        case 'r': return KEY_RIGHT;

        case 'b': return '\b';

        case 'H': return KEY_HOME;
        case 'E': return KEY_END;
        case 'I': return KEY_INS;
        case 'D': return KEY_DEL;
        case 'P': return KEY_PGUP;
        case 'N': return KEY_PGDN;

        case 'a': return MOVIE_KEY_ANY;
        default:
            break;
    }
    return 0;
}

int mf_movieIsSystemBreak(int c)
{
    return (c == 'q' || c == 'Q' || c == Ctrl('C'))
        ? 1 : 0;
}

int 
mf_movieMaskedInput(int c)
{
    unsigned char *p = mfmovie.optkeys;

    if (!p)
        return 0;

    // some keys cannot be masked
    if (mf_movieIsSystemBreak(c))
            return 0;

    // treat BS and DEL as same one
    if (c == MOVIE_KEY_BS2)
        c = '\b';

    // general look up
    while (p < mf.end && *p && *p != '\n' && *p != '#')
    {
        if (*p == '@' && mf.end - p > 1 
                && isalnum(*(p+1))) // named key
        {
            p++;

            // special: 'a' masks all
            if (*p == 'a' || mf_movieNamedKey(*p) == c)
                return 1;
        } else {
            if ((int)*p == c)
                return 1;
        }
        p++;
    }
    return 0;
}

unsigned char * 
mf_movieFrameHeader(unsigned char *p, unsigned char *end)
{
    // ANSI has ESC_STR [8m as "Conceal" but
    // not widely supported, even PieTTY.
    // So let's go back to fixed format...
    static char *patHeader = "==" ESC_STR "[30;40m^L";
    static char *patHeader2= ESC_STR "[30;40m^L"; // patHeader + 2; // "=="
    // static char *patHeader3= ESC_STR "[m^L"; 
    static size_t szPatHeader   = 12; // strlen(patHeader);
    static size_t szPatHeader2  = 10; // strlen(patHeader2);
    // static size_t szPatHeader3  = 5;  // strlen(patHeader3);

    size_t sz = end - p;

    if (sz < 1) return NULL;
    if(*p == 12)        // ^L
        return p+1;

    if (sz < 2) return NULL;
    if( *p == '^' &&
            *(p+1) == 'L')
        return p+2;

    // Add more frame headers
    
    /* // *[m seems not so common, skip.
    if (sz < szPatHeader3) return NULL;
    if (memcmp(p, patHeader3, szPatHeader3) == 0)
        return p + szPatHeader3;
        */

    if (sz < szPatHeader2) return NULL;
    if (memcmp(p, patHeader2, szPatHeader2) == 0)
        return p + szPatHeader2;

    if (sz < szPatHeader) return NULL;
    if (memcmp(p, patHeader, szPatHeader) == 0)
        return p + szPatHeader;

    return NULL;
}

int 
mf_movieGotoNamedFrame(const unsigned char *name, const unsigned char *end)
{
    const unsigned char *p = name;
    size_t sz = 0;

    // resolve name first
    while (p < end && isalnum(*p))
        p++;
    sz = p - name;

    if (sz < 1) return 0;

    // now search entire file for frame

    mf_goTop();

    do
    {
        if ((p = mf_movieFrameHeader(mf.disps, mf.end)) == NULL ||
                *p != ':')
            continue; 

        // got some frame. let's check the name
        p++;
        if (mf.end - p < sz)
            continue;

        // check: target of p must end.
        if (mf.end -p > sz &&
                isalnum(*(p+sz)))
            continue;

        if (memcmp(p, name, sz) == 0)
            return 1;

    } while  (mf_forward(1) > 0);
    return 0;
}

int 
mf_movieGotoFrame(int fno, int relative)
{
    if (!relative)
        mf_goTop();
    else if (fno > 0)
        mf_forward(1);
    // for absolute, fno = 1..N

    if (fno > 0)
    {
        // move forward
        do {
            while (mf_movieFrameHeader(mf.disps, mf.end) == NULL)
            {
                if (mf_forward(1) < 1)
                    return 0;
            }
            // found frame.
            if (--fno > 0)
                mf_forward(1);
        } while (fno > 0);
    } else {
        // For backward, the first call moves to beginning of file.
        if (mf_backward(1) < 1)
            return 0;
        // backward
        // XXX check if we reached head?
        while (fno < 0)
        {
            do {
                if (mf_backward(1) < 1)
                    return 0;
            } while (mf_movieFrameHeader(mf.disps, mf.end) == NULL);
            fno ++;
        }
    }
    return 1;
}

int
mf_parseOffsetCmd(
        unsigned char *s, unsigned char *end,
        int base)
{
    // return is always > 0, or base.
    int v = 0;

    if (s >= end)
        return base;

    v = atoi((char*)s);

    if (*s == '+' || *s == '-')
    {
        // relative format
        v = base + v;
    } else if (isdigit(*s)) {
        // absolute format
    } else {
        // error format?
        v = 0;
    }

    if (v <= 0)
        v = base;
    return v;
}

int
mf_movieExecuteOffsetCmd(unsigned char *s, unsigned char *end)
{
    // syntax: type[+-]offset
    //
    // type:   l(line), f(frame), p(page).
    // +-:     if empty, absolute. if assigned, relative.
    // offset: is 1 .. N for all cases

    int curr = 0, newno = 0;
    
    switch(*s)
    {
        case 'p':       
            // by page
            curr = (mf.lineno / MFDISP_PAGE) + 1;
            newno = mf_parseOffsetCmd(s+1, end, curr);
#ifdef DEBUG
            vmsgf("page: %d -> %d\n", curr, newno);
#endif // DEBUG
            // prevent endless loop
            if (newno == curr)
                return 0;

            return mf_goto((newno -1) * MFDISP_PAGE);

        case 'l':
            // by lines
            curr = mf.lineno + 1;
            newno = mf_parseOffsetCmd(s+1, end, curr);
#ifdef DEBUG
            vmsgf("line: %d -> %d\n", curr, newno);
#endif // DEBUG
            // prevent endless loop
            if (newno == curr)
                return 0;

            return mf_goto(newno-1);

        case 'f':
            // by frame [optimized]
            if (++s >= end)
                return 0;

            curr = 0;
            newno = atoi((char*)s);
            if (*s == '+' || *s == '-') // relative
            {
                curr = 1;
                if (newno == 0)
                    return 0;
            } else {
                // newno starts from 1
                if (newno <= 0)
                    return 0;
            }
            return mf_movieGotoFrame(newno, curr);

        case ':':
            // by names
            return mf_movieGotoNamedFrame(s+1, end);

        default:
            // not supported yet
            break;
    }
    return 0;
}


int 
mf_movieOptionHandler(unsigned char *opt, unsigned char *end)
{
    // format: #time#key1,cmd,text1#key2,cmd,text2#
    // if key  is empty, use auto-increased key.
    // if cmd  is empty, invalid.
    // if text is empty, display key only or hide if time is assigned.
    
    int ient = 0;
    unsigned char *pkey = NULL, *cmd = NULL, *text = NULL;
    unsigned int szCmd = 0, szText = 0;
    unsigned char *p = opt;
    int key = 0;
    float optclk = -1.0f; // < 0 means infinite wait
    struct timeval tv;

    int isel = 0, c = 0, maxsel = 0, selected = 0;
    int newOpt = 1;
    int hideOpts = 0;
    int promptlen = 0;

    // TODO handle line length
    // TODO restrict option size
    
    // set up timer (opt points to optional time now)
    do {
        p = opt;
        while (  p < end &&
                (isdigit(*p) || *p == '+' || *p == '-' || *p == '.') )
            p++;

        // if no number, abort.
        if (p == opt || (p < end && *p != '#')) break;

        // p looks like valid timer now
        if (mf_str2float(opt, p, &optclk))
        {
            // conversion failed.
            if (optclk == 0)
                optclk = -1.0f;
        }

        // point opt to new var after #.
        opt = p + 1;
        break;

    } while (1);
    
    // UI Selection
    do {
        // do c test here because we need parser to help us
        // finding the selection
        if (c == '\r' || c == '\n' || c == ' ')
        {
            selected = 1;
        }

        newOpt = 1;
        promptlen = 0;

        // parse (key,frame,text)
        for (   p = opt, ient = 0, maxsel = 0,
                key = '0';      // default command
                p < end && *p != '\n'; p++)
        {
            if (newOpt)
            {
                // prepare for next loop
                pkey = p;
                cmd = text = NULL;
                szCmd = szText = 0;
                ient = 0;
                newOpt = 0;
            }

            // calculation of fields
            if (*p == ',' || *p == '#')
            {
                switch (++ient)
                {
                    // case 0 is already processed.
                    case 1:
                        cmd = p+1;
                        break;

                    case 2:
                        text = p+1;
                        szCmd = p - cmd;
                        break;

                    case 3:
                        szText = p - text;

                    default:
                        // unknown parameters
                        break;
                }
            } 

            // ready to parse one option
            if (*p == '#')
            {
                newOpt = 1;

                // first, fix pointers
                if (szCmd == 0 || *cmd == ',' || *cmd == '#')
                { cmd = NULL; szCmd = 0; }

                // quick abort if option is invalid.
                if (!cmd) 
                    continue;

                if (szText == 0 || *text == ',' || *text == '#')
                { text = NULL; szText = 0; }

                // assign key
                if (*pkey == ',' || *pkey == '#')
                    key++;
                else
                {
                    // named key?
                    int nk = 0;

                    // handle special case @a (all) here

                    if (*pkey == '@' && 
                            ++ pkey < end &&
                            (nk = mf_movieNamedKey(*pkey)))
                    {
                        key = nk;
                    } else {
                        key = *pkey;
                    }
                    // warning: pkey may be changed after this.
                }

                // calculation complete.
                
                // print option
                if (!hideOpts && maxsel == 0 && text == NULL)
                {
                    hideOpts = 1;
                    mf_moviePromptPlaying(0);
                    // prevent more hideOpt test
                }

                if (!hideOpts)
                {
                    // print header
                    if (maxsel == 0)
                    {
                        pmore_clrtoeol(b_lines-1, 0);
                        mf_moviePromptPlaying(1);
                    }

                    promptlen += mf_moviePromptOptions(
                            isel, maxsel, key,
                            text, szText);
                }

                // handle selection
                if (c == key || 
                        (key == MOVIE_KEY_ANY && c != 0))
                {
                    // hotkey pressed
                    selected = 1;
                    isel = maxsel;
                }

                maxsel ++;

                // parse complete.
                // test if this item is selected.
                if (selected && isel == maxsel - 1)
                    break;
            }
        }

        if (selected || maxsel == 0)
            break;

        // finish the selection bar
        if (!hideOpts && maxsel > 0)
        {
            int iw = 0;
            for (iw = 0; iw + promptlen < t_columns-1; iw++)
                outc(' ');
            outs(ANSI_RESET ANSI_CLRTOEND);
        }

        // wait for input
        if (optclk > 0)
        {
            // timed interaction

            // disable optkeys to allow masked input
            unsigned char *tmpopt = mfmovie.optkeys;
            mfmovie.optkeys = NULL;

            mf_float2tv(optclk, &tv);
            c = pmore_wait_key(&tv, 1);
            mfmovie.optkeys = tmpopt;

            // if timeout, drop.
            if (!c)
                return 0;
        } else {
            // infinite wait
            c = igetch();
        }

        // parse keyboard input
        if (mf_movieIsSystemBreak(c))
        {
            // cannot be masked,
            // also force stop of playback
            STOP_MOVIE();
            vmsg(PMORE_MSG_MOVIE_INTERACTION_STOPPED);
            return 0;
        }

        // treat BS and DEL as same one
        if (c == MOVIE_KEY_BS2)
            c = '\b';

        // standard navigation keys.
        if (mf_movieMaskedInput(c))
            continue;

        // these keys can be masked
        if (c == KEY_LEFT || c == KEY_UP)
        {
            if (isel > 0) isel --;
        } 
        else if (c == KEY_RIGHT || c == KEY_TAB || c == KEY_DOWN)
        {
            if (isel < maxsel-1) isel ++;
        } 
        else if (c == KEY_HOME)
        {
            isel = 0;
        } 
        else if (c == KEY_END)
        {
            isel = maxsel -1;
        } 

    } while ( !selected );

    // selection is made now.
    outs(ANSI_RESET); // required because options bar may be not closed
    pmore_clrtoeol(b_lines, 0);

#ifdef DEBUG
    prints("selection: %d\n", isel);
    igetch();
#endif

    // Execute Selection
    if (!cmd || !szCmd)
        return 0;

    return mf_movieExecuteOffsetCmd(cmd, cmd+szCmd);
}

/*
 * mf_movieSyncFrame: 
 *  wait until synchronization, and flush break key (if any).
 * return meaning:
 * I've got synchronized.
 * If no (user breaks), return 0
 */
int mf_movieSyncFrame()
{
    if (mfmovie.pause)
    {
        int c = 0;
        mfmovie.pause = 0;
        c = vmsg(PMORE_MSG_MOVIE_PAUSE);
        if (mf_movieIsSystemBreak(c))
            return 0;
        return 1;
    } 
    else if (mfmovie.options)
    {
        unsigned char *opt = mfmovie.options;
        mfmovie.options = NULL;
        mf_movieOptionHandler(opt, mf.end);
        return 1;
    }
    else if (mfmovie.synctime.tv_sec > 0)
    {
        /* synchronize world timeline model */
        struct timeval dv;
        gettimeofday(&dv, NULL);
        dv.tv_sec = mfmovie.synctime.tv_sec - dv.tv_sec;
        if(dv.tv_sec < 0)
            return 1;
        dv.tv_usec = mfmovie.synctime.tv_usec - dv.tv_usec;
        if(dv.tv_usec < 0) {
            dv.tv_sec --;
            dv.tv_usec += MOVIE_SECOND_U;
        }
        if(dv.tv_sec < 0)
            return 1;

        return !pmore_wait_key(&dv, 0);
    } else {
        /* synchronize each frame clock model */
        /* because Linux will change the timeval passed to select,
         * let's use a temp value here.
         */
        struct timeval dv = mfmovie.frameclk;
        return !pmore_wait_key(&dv, 0);
    }
}

#define MOVIECMD_SKIP_ALL(p,end) \
    while (p < end && *p && *p != '\n') \
    { p++; } \

unsigned char *
mf_movieProcessCommand(unsigned char *p, unsigned char *end)
{
    for (; p < end && *p != '\n'; p++)
    {
        if (*p == 'S') {
            // SYNCHRONIZATION
            gettimeofday(&mfmovie.synctime, NULL);
            // S can take other commands
        } 
        else if (*p == 'E') 
        {
            // END
            STOP_MOVIE();
            // MFDISP_SKIPCURLINE();
            MOVIECMD_SKIP_ALL(p,end);
            return p;
        }
        else if (*p == 'P') 
        {
            // PAUSE
            mfmovie.pause = 1;
            // MFDISP_SKIPCURLINE();
            MOVIECMD_SKIP_ALL(p,end);
            return p;
 
        }
        else if (*p == 'G') 
        {
            // GOTO
            // Gt+-n,t+-n,t+-n (random select one)
            // jump +-n of type(l,p,f)

            // random select, if multiple
            unsigned char *pe = p;
            unsigned int igs = 0;

            for (pe = p ; pe < end && *pe && 
                    *pe > ' ' && *pe < 0x80
                    ; pe ++)
                if (*pe == ',') igs++;

            if (igs)
            {
                // make random
                igs = random() % (igs+1);

                for (pe = p ; igs > 0 && pe < end && *pe && 
                        *pe > ' ' && *pe < 0x80
                        ; pe ++)
                    if (*pe == ',') igs--;

                if (pe != p)
                    p = pe-1;
            }

            mf_movieExecuteOffsetCmd(p+1, end); 
            MOVIECMD_SKIP_ALL(p,end);
            return p;
        } 
        else if (*p == ':') 
        {
            // NAMED
            // :name:
            // name allows alnum only
            p++;
            // TODO check isalnum p?
            
            // :name can accept trailing commands
            while (p < end && *p != '\n' && *p != ':')
                p++;

            if (*p == ':') p++;

            // continue will increase p
            p--;
            continue;
        }
        else if (*p == 'K') 
        {
            // Reserve Key for interactive usage.
            // Currently only K#...# format is supported.
            if (p+2 < end && *(p+1) == '#')
            {
                p += 2;
                mfmovie.optkeys = p;
                mfmovie.interactive = 1;

                // K#..# can accept trailing commands
                while (p < end && *p != '\n' && *p != '#')
                    p++;

                // if empty, set optkeys to NULL?
                if (mfmovie.optkeys == p)
                    mfmovie.optkeys = NULL;

                if (*p == '#') p++;

                // continue will increase p
                p--;
                continue;
            }
            MOVIECMD_SKIP_ALL(p,end);
            return p;
        }
        else if (*p == '#') 
        {
            // OPTIONS
            // #key1,frame1,text1#key2,frame2,text2#
            mfmovie.options = p+1;
            mfmovie.interactive = 1;
            // MFDISP_SKIPCURLINE();
            MOVIECMD_SKIP_ALL(p,end);
            return p;
        }
        else if (*p == 'O') 
        {
            // OLD compatible mode
            // =  -> compat24
            // -  -> ?
            // == -> ?
            p++;
            if (p >= end)
                return end;
            if (*p == '=')
            {
                mfmovie.mode = MFDISP_MOVIE_PLAYING_OLD;
                mfmovie.compat24 = 1;
                p++;
            }
            // MFDISP_SKIPCURLINE();
            return p+1;
        } 
        else if (*p == 'L') 
        {
            // LOOP
            // Lm,n   
            // m times to backward n
            break;
        } 
        else 
        {
            // end of known control codes
            break;
        }
    }
    return p;
}

int 
mf_movieNextFrame()
{
    while (1) 
    {
        unsigned char *p = mf_movieFrameHeader(mf.disps, mf.end);

        if(p) 
        {
            float nf = 0;
            unsigned char *odisps = mf.disps;
            
            /* process leading */
            p = mf_movieProcessCommand(p, mf.end);

            // disps may change after commands
            if (mf.disps != odisps)
            {
                // commands changing location must
                // support at least one frame pause
                // to allow user break
                struct timeval tv;
                mf_float2tv(MOVIE_MIN_FRAMECLK, &tv);

                if (pmore_wait_key(&tv, 0))
                {
                    STOP_MOVIE();
                    return 0;
                }
                continue;
            }

            /* process time */
            if (mf_str2float(p, mf.end, &nf))
            {
                mf_float2tv(nf, &mfmovie.frameclk);
            }

            if(mfmovie.synctime.tv_sec > 0)
            {
                mfmovie.synctime.tv_usec += mfmovie.frameclk.tv_usec;
                mfmovie.synctime.tv_sec  += mfmovie.frameclk.tv_sec;
                mfmovie.synctime.tv_sec  += mfmovie.synctime.tv_usec / MOVIE_SECOND_U;
                mfmovie.synctime.tv_usec %= MOVIE_SECOND_U;
            }

            if (mfmovie.mode != MFDISP_MOVIE_PLAYING_OLD)
                mf_forward(1);

            return 1;
        }

        if (mf_forward(1) <= 0)
            break;
    }

    return 0;
}
#endif

/* vim:sw=4:ts=8:expandtab:nofoldenable
 */
