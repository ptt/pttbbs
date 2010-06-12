/* $Id$ */
// #include "bbs.h"
#include "cmbbs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "var.h"


/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
static const char * const str_home_file = "home/%c/%s/%s";
static const char * const str_board_file = "boards/%c/%s/%s";
static const char * const str_board_n_file = "boards/%c/%s/%s.%d";
static const char * const str_dotdir = FN_DIR;

/* XXX set*() all assume buffer size = PATHLEN */
void
sethomepath(char *buf, const char *userid)
{
    assert(is_validuserid(userid));
    snprintf(buf, PATHLEN, "home/%c/%s", userid[0], userid);
}

void
sethomedir(char *buf, const char *userid)
{
    assert(is_validuserid(userid));
    snprintf(buf, PATHLEN, str_home_file, userid[0], userid, str_dotdir);
}

void
sethomeman(char *buf, const char *userid)
{
    assert(is_validuserid(userid));
    snprintf(buf, PATHLEN, str_home_file, userid[0], userid, "man");
}

void
sethomefile(char *buf, const char *userid, const char *fname)
{
    assert(is_validuserid(userid));
    assert(fname[0]);
    snprintf(buf, PATHLEN, str_home_file, userid[0], userid, fname);
}

void 
setuserhashedfile(char *buf, const char *filename)
{
#ifdef USERHASHSTORE_ROOTPATH
    // hash designed by kcwu
    unsigned hash = StringHash(cuser.userid) & 0xffff;
    assert(is_validuserid(cuser.userid));
    snprintf(buf, PATHLEN, 
            USERHASHSTORE_ROOTPATH "/%02x/%02x/%s.%s.%x",
            (hash >> 8) & 0xff, hash & 0xff, 
            filename, cuser.userid, cuser.firstlogin);
#else
    assert(!"you must define and initialize USERHASHSTORE_ROOTPATH");
#endif
}


void
setapath(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, "man/boards/%c/%s", boardname[0], boardname);
}

void
setadir(char *buf, const char *path)
{
    //assert(path[0]);
    snprintf(buf, PATHLEN, "%s/%s", path, str_dotdir);
}

void
setbpath(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, "boards/%c/%s", boardname[0], boardname);
}

#if 0
void
setbdir(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, str_board_file, boardname[0], boardname,
	    (currmode & MODE_DIGEST ? fn_mandex : str_dotdir));
}
#endif

void
setbfile(char *buf, const char *boardname, const char *fname)
{
    //assert(boardname[0]);
    assert(fname[0]);
    snprintf(buf, PATHLEN, str_board_file, boardname[0], boardname, fname);
}

void
setbnfile(char *buf, const char *boardname, const char *fname, int n)
{
    //assert(boardname[0]);
    assert(fname[0]);
    snprintf(buf, PATHLEN, str_board_n_file, boardname[0], boardname, fname, n);
}

/*
 * input	direct
 * output	buf: copy direct
 * 		fname: direct 的檔名部分
 */
void
setdirpath(char *buf, const char *direct, const char *fname)
{
    char *p;
    strcpy(buf, direct);
    p = strrchr(buf, '/');
    assert(p);
    strlcpy(p + 1, fname, PATHLEN-(p+1-buf));
}

