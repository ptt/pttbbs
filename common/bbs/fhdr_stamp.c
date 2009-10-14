/* $Id$ */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "cmsys.h"  // for time4_t
#include "cmbbs.h"

#if __GNUC__
#define GCC_WEAK __attribute__ ((weak))
#define GCC_INLINE __attribute__ ((always_inline))
#else
#define GCC_WEAK
#define GCC_INLINE
#endif

static inline int fhdr_stamp(char *fpath, fileheader_t *fh, int type) GCC_INLINE;
int stampfile(char *fpath, fileheader_t *fh) GCC_WEAK;
int stampfile_u(char *fpath, fileheader_t *fh) GCC_WEAK;
int stampdir(char *fpath, fileheader_t *fh) GCC_WEAK;
int stamplink(char *fpath, fileheader_t * fh) GCC_WEAK;

#define STAMP_FILE  0
#define STAMP_DIR   1
#define STAMP_LINK  2

/* mail / post 時，依據時間建立檔案或目錄，加上郵戳 */
/* @param[in,out] fpath input as dirname, output as filename */
static inline int
fhdr_stamp(char *fpath, fileheader_t *fh, int type)
{
    char       *ip = fpath;
    time4_t     dtime = time(0);
    struct tm   ptime;
    int         res = 0;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';

    switch (type) {
	case STAMP_FILE:
	    do {
		sprintf(ip, "M.%d.A.%3.3X", (int)(++dtime), (unsigned int)(random() & 0xFFF));
	    } while ((res = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644)) == -1 && errno == EEXIST);
	    break;
	case STAMP_DIR:
	    do {
		sprintf(ip, "D%X", (int)++dtime & 07777);
	    } while ((res = mkdir(fpath, 0755)) == -1 && errno == EEXIST);
	    break;
	case STAMP_LINK:
	    do {
		sprintf(ip, "S%X", (int)++dtime);
	    } while ((res = symlink("temp", fpath)) == -1 && errno == EEXIST);
	    break;
	default:
	    // unknown
	    return -1;
	    break;
    }

    if (res == -1)
	return -1;

    if (type == STAMP_FILE)
	close(res);

    strlcpy(fh->filename, ip, sizeof(fh->filename));
    localtime4_r(&dtime, &ptime);
    snprintf(fh->date, sizeof(fh->date), "%2d/%02d", ptime.tm_mon + 1, ptime.tm_mday);

    return 0;
}

int
stampfile(char *fpath, fileheader_t *fh)
{
    memset(fh, 0, sizeof(fileheader_t));
    return fhdr_stamp(fpath, fh, STAMP_FILE);
}

int
stampfile_u(char *fpath, fileheader_t *fh)
{
    // XXX do not reset fileheader (untouched)
    return fhdr_stamp(fpath, fh, STAMP_FILE);
}

int
stampdir(char *fpath, fileheader_t *fh)
{
    memset(fh, 0, sizeof(fileheader_t));
    return fhdr_stamp(fpath, fh, STAMP_DIR);
}

int
stamplink(char *fpath, fileheader_t * fh)
{
    memset(fh, 0, sizeof(fileheader_t));
    return fhdr_stamp(fpath, fh, STAMP_LINK);
}

