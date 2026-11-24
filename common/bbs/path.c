// #include "bbs.h"
#include "cmbbs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "common.h"
#include "var.h"


/* ----------------------------------------------------- */
/* set file path for boards/user home                    */
/* ----------------------------------------------------- */
static const char * const str_home_file = "home/%c/%s/%s";
static const char * const str_board_file = "boards/%c/%s/%s";
static const char * const str_board_n_file = "boards/%c/%s/%s.%d";
static const char * const str_man_file = "man/boards/%c/%s/%s";

static int is_validfilename(const char *fname) {
  return strstr(fname, "..") == NULL;
}

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
    snprintf(buf, PATHLEN, str_home_file, userid[0], userid, FN_DIR);
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
    assert(fname[0] && is_validfilename(fname));
    snprintf(buf, PATHLEN, str_home_file, userid[0], userid, fname);
}

void 
setuserhashedfile(char *buf GCC_UNUSED, const char *filename GCC_UNUSED)
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
    assert(buf != path);
    snprintf(buf, PATHLEN, "%s/%s", path, FN_DIR);
}

void
setbpath(char *buf, const char *boardname)
{
    //assert(boardname[0]);
    snprintf(buf, PATHLEN, "boards/%c/%s", boardname[0], boardname);
}

void
setafile(char *buf, const char *boardname, const char *fname)
{
    assert(fname[0] && is_validfilename(fname));
    snprintf(buf, PATHLEN, str_man_file, boardname[0], boardname, fname);
}

void
setbfile(char *buf, const char *boardname, const char *fname)
{
    //assert(boardname[0]);
    assert(fname[0] && is_validfilename(fname));
    snprintf(buf, PATHLEN, str_board_file, boardname[0], boardname, fname);
}

void
setbnfile(char *buf, const char *boardname, const char *fname, int n)
{
    //assert(boardname[0]);
    assert(fname[0] && is_validfilename(fname));
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
    assert(fname[0] && is_validfilename(fname));
    strlcpy(p + 1, fname, PATHLEN-(p+1-buf));
}

/*
 * Whether path is the directory of a board (boards/<c>/<name> under
 * BBSHOME, however it is written), for tools that take a directory: the
 * posts of a board change only through BoF. Copies the name of the board
 * into name (sz bytes). Returns 1 if it is one, 0 if not.
 */
int
bpath_to_brdname(const char *path, char *name, size_t sz)
{
    char real[PATH_MAX], base[PATH_MAX];
    const char *p;
    size_t n;

    if (!realpath(path, real) || !realpath(BBSHOME "/boards", base))
        return 0;
    n = strlen(base);
    if (strncmp(real, base, n) != 0 || real[n] != '/')
        return 0;
    p = real + n + 1;
    // <c>/<name>, where <c> is the first letter of the name (BoF checks
    // the name itself; not here, for the programs without string.o).
    if (!p[0] || p[1] != '/' || p[2] != p[0] || strchr(p + 2, '/'))
        return 0;
    if (strlcpy(name, p + 2, sz) >= sz)
        return 0;
    return 1;
}

/*
 * As bpath_to_brdname(), for a file (that may not exist yet), e.g. a .DIR:
 * whether it is in the directory of a board.
 */
int
bfile_to_brdname(const char *file, char *name, size_t sz)
{
    char dir[PATH_MAX], *p;

    if (strlcpy(dir, file, sizeof(dir)) >= sizeof(dir))
        return 0;
    if (!(p = strrchr(dir, '/')))
        return bpath_to_brdname(".", name, sz);
    if (p == dir)
        p[1] = 0;
    else
        *p = 0;
    return bpath_to_brdname(dir, name, sz);
}
