/* $Id: record.c,v 1.7 2002/07/05 17:10:28 in2 Exp $ */
#include "bbs.h"

#undef  HAVE_MMAP
#define BUFSIZE 512

static void 
PttLock(int fd, int size, int mode)
{
    static struct flock lock_it;
    int             ret;

    lock_it.l_whence = SEEK_CUR;/* from current point */
    lock_it.l_start = 0;	/* -"- */
    lock_it.l_len = size;	/* length of data */
    lock_it.l_type = mode;	/* set exclusive/write lock */
    lock_it.l_pid = 0;		/* pid not actually interesting */
    while ((ret = fcntl(fd, F_SETLKW, &lock_it)) < 0 && errno == EINTR);
}

#define safewrite       write

int 
get_num_records(char *fpath, int size)
{
    struct stat     st;
    if (stat(fpath, &st) == -1)
	return 0;
    return st.st_size / size;
}

int 
get_sum_records(char *fpath, int size)
{
    struct stat     st;
    long            ans = 0;
    FILE           *fp;
    fileheader_t    fhdr;
    char            buf[200], *p;

    if (!(fp = fopen(fpath, "r")))
	return -1;

    strcpy(buf, fpath);
    p = strrchr(buf, '/') + 1;

    while (fread(&fhdr, size, 1, fp) == 1) {
	strcpy(p, fhdr.filename);
	if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1)
	    ans += st.st_size;
    }
    fclose(fp);
    return ans / 1024;
}

int 
get_record(char *fpath, void *rptr, int size, int id)
{
    int             fd = -1;

    if (id < 1 || (fd = open(fpath, O_RDONLY, 0)) != -1) {
	if (lseek(fd, (off_t) (size * (id - 1)), SEEK_SET) != -1) {
	    if (read(fd, rptr, size) == size) {
		close(fd);
		return 0;
	    }
	}
	close(fd);
    }
    return -1;
}

int 
get_records(char *fpath, void *rptr, int size, int id, int number)
{
    int             fd;

    if (id < 1 || (fd = open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    if (lseek(fd, (off_t) (size * (id - 1)), SEEK_SET) == -1) {
	close(fd);
	return 0;
    }
    if ((id = read(fd, rptr, size * number)) == -1) {
	close(fd);
	return -1;
    }
    close(fd);
    return id / size;
}

int 
substitute_record(char *fpath, void *rptr, int size, int id)
{
    int             fd;

    if (id < 1 || (fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1)
	return -1;

    lseek(fd, (off_t) (size * (id - 1)), SEEK_SET);
    PttLock(fd, size, F_WRLCK);
    safewrite(fd, rptr, size);
    PttLock(fd, size, F_UNLCK);
    close(fd);

    return 0;
}

/* rocker.011022: 避免lock檔開啟時不正常斷線,造成永久lock */
static int
force_open(char *fname)
{
    int             fd;
    time_t          expire;

    expire = now - 3600;	/* lock 存在超過一個小時就是有問題! */

    if (dasht(fname) < expire)
	return -1;
    unlink(fname);
    fd = open(fname, O_WRONLY | O_TRUNC, 0644);

    return fd;
}


#if !defined(_BBS_UTIL_C_)
/* new/old/lock file processing */
typedef struct nol_t {
    char            newfn[256];
    char            oldfn[256];
    char            lockfn[256];
}               nol_t;

static void 
nolfilename(nol_t * n, char *fpath)
{
    sprintf(n->newfn, "%s.new", fpath);
    sprintf(n->oldfn, "%s.old", fpath);
    sprintf(n->lockfn, "%s.lock", fpath);
}

int 
delete_record(char fpath[], int size, int id)
{
    nol_t           my;
    char            abuf[BUFSIZE];
    int             fdr, fdw, fd;
    int             count;

    nolfilename(&my, fpath);
    if ((fd = open(my.lockfn, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1)
	return -1;

    flock(fd, LOCK_EX);

    if ((fdr = open(fpath, O_RDONLY, 0)) == -1) {
	move(10, 10);
	outs("delete_record failed!!! (open)");
	pressanykey();
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    if (
	((fdw = open(my.newfn, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) &&
	((fdw = force_open(my.newfn)) == -1)) {
	flock(fd, LOCK_UN);
	close(fd);
	close(fdr);
	return -1;
    }
    count = 1;
    while (read(fdr, abuf, size) == size) {
	if (id != count++ && (safewrite(fdw, abuf, size) == -1)) {
	    unlink(my.newfn);
	    close(fdr);
	    close(fdw);
	    flock(fd, LOCK_UN);
	    close(fd);
	    return -1;
	}
    }
    close(fdr);
    close(fdw);
    if (Rename(fpath, my.oldfn) == -1 || Rename(my.newfn, fpath) == -1) {
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

#if 0
static char    *
title_body(char *title)
{
    if (!strncasecmp(title, str_reply, 3)) {
	title += 3;
	if (*title == ' ')
	    title++;
    }
    return title;
}
#endif

int 
delete_range(char *fpath, int id1, int id2)
{
    fileheader_t    fhdr;
    nol_t           my;
    char            fullpath[STRLEN], *t;
    int             fdr, fdw, fd;
    int             count;

    nolfilename(&my, fpath);

    if ((fd = open(my.lockfn, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1)
	return -1;

    flock(fd, LOCK_EX);

    if ((fdr = open(fpath, O_RDONLY, 0)) == -1) {
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    if (
	((fdw = open(my.newfn, O_WRONLY | O_CREAT | O_EXCL, 0644)) == -1) &&
	((fdw = force_open(my.newfn)) == -1)) {
	close(fdr);
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    count = 1;
    strcpy(fullpath, fpath);
    t = strrchr(fullpath, '/') + 1;

    while (read(fdr, &fhdr, sizeof(fileheader_t)) == sizeof(fileheader_t)) {
	strcpy(t, fhdr.filename);

	/* rocker.011018: add new tag delete */
	if (
	    (fhdr.filemode & FILE_MARKED) ||	/* 標記 */
	    (fhdr.filemode & FILE_DIGEST) ||	/* 文摘 */
	    (id1 && (count < id1 || count > id2)) ||	/* range */
	    (!id1 && Tagger(atoi(t + 2), count, TAG_NIN))) {	/* TagList */
	    if ((safewrite(fdw, &fhdr, sizeof(fileheader_t)) == -1)) {
		close(fdr);
		close(fdw);
		unlink(my.newfn);
		flock(fd, LOCK_UN);
		close(fd);
		return -1;
	    }
	} else {
	    //if (dashd(fullpath))
		unlink(fullpath);
	}
	++count;
    }
    close(fdr);
    close(fdw);
    if (Rename(fpath, my.oldfn) == -1 || Rename(my.newfn, fpath) == -1) {
	flock(fd, LOCK_UN);
	close(fd);
	return -1;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

int 
search_rec(char *dirname, int (*filecheck) ())
{
    fileheader_t    fhdr;
    FILE           *fp;
    int             ans = 0;

    if (!(fp = fopen(dirname, "r")))
	return 0;

    while (fread(&fhdr, sizeof(fhdr), 1, fp)) {
	ans++;
	if ((*filecheck) (&fhdr)) {
	    fclose(fp);
	    return ans;
	}
    }
    fclose(fp);
    return 0;
}

int             delete_files(char *dirname, int (*filecheck) (), int record){
    fileheader_t    fhdr;
    FILE           *fp, *fptmp;
    int             ans = 0;
    char            tmpfname[128];
    char            genbuf[256];
    char            deleted[256];
    fileheader_t    delfh;
    char            deletedDIR[] = "boards/d/deleted/.DIR";

    strcpy(deleted, "boards/d/deleted");

    if (!(fp = fopen(dirname, "r")))
	return ans;

    strcpy(tmpfname, dirname);
    strcat(tmpfname, "_tmp");

    if (!(fptmp = fopen(tmpfname, "w"))) {
	fclose(fp);
	return ans;
    }
    while (fread(&fhdr, sizeof(fhdr), 1, fp)) {
	if ((*filecheck) (&fhdr)) {
	    ans++;
	    setdirpath(genbuf, dirname, fhdr.filename);
	    if (record) {
		deleted[14] = '\0';
		stampfile(deleted, &delfh);
		strcpy(delfh.owner, fhdr.owner);
		strcpy(delfh.title, fhdr.title);
		Link(genbuf, deleted);
		append_record(deletedDIR, &delfh, sizeof(delfh));
	    }
	    unlink(genbuf);
	} else
	    fwrite(&fhdr, sizeof(fhdr), 1, fptmp);
    }

    fclose(fp);
    fclose(fptmp);
    unlink(dirname);
    Rename(tmpfname, dirname);

    return ans;
}

int 
delete_file(char *dirname, int size, int ent, int (*filecheck) ())
{
    char            abuf[BUFSIZE];
    int             fd;
    struct stat     st;
    long            numents;

    if (ent < 1 || (fd = open(dirname, O_RDWR)) == -1)
	return -1;
    flock(fd, LOCK_EX);
    fstat(fd, &st);
    numents = ((long)st.st_size) / size;
    if (((long)st.st_size) % size)
	fprintf(stderr, "align err\n");
    if (lseek(fd, (off_t) size * (ent - 1), SEEK_SET) != -1) {
	if (read(fd, abuf, size) == size) {
	    if ((*filecheck) (abuf)) {
		int             i;

		for (i = ent; i < numents; i++) {
		    if (lseek(fd, (off_t) ((i) * size), SEEK_SET) == -1 ||
			read(fd, abuf, size) != size ||
			lseek(fd, (off_t) (i - 1) * size, SEEK_SET) == -1)
			break;
		    if (safewrite(fd, abuf, size) != size)
			break;
		}
		ftruncate(fd, (off_t) size * (numents - 1));
		flock(fd, LOCK_UN);
		close(fd);
		return 0;
	    }
	}
    }
    lseek(fd, 0, SEEK_SET);
    ent = 1;
    while (read(fd, abuf, size) == size) {
	if ((*filecheck) (abuf)) {
	    int             i;

	    for (i = ent; i < numents; i++) {
		if (lseek(fd, (off_t) (i + 1) * size, SEEK_SET) == -1 ||
		    read(fd, abuf, size) != size ||
		    lseek(fd, (off_t) (i) * size, SEEK_SET) == -1 ||
		    safewrite(fd, abuf, size) != size)
		    break;
	    }
	    ftruncate(fd, (off_t) size * (numents - 1));
	    flock(fd, LOCK_UN);
	    close(fd);
	    return 0;
	}
	ent++;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return -1;
}

#endif				/* !defined(_BBS_UTIL_C_) */

int             apply_record(char *fpath, int (*fptr) (), int size){
    char            abuf[BUFSIZE];
    FILE           *fp;

    if (!(fp = fopen(fpath, "r")))
	return -1;

    while (fread(abuf, 1, size, fp) == size)
	if ((*fptr) (abuf) == QUIT) {
	    fclose(fp);
	    return QUIT;
	}
    fclose(fp);
    return 0;
}

/* mail / post 時，依據時間建立檔案，加上郵戳 */
int 
stampfile(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = now;
    struct tm      *ptime;
    int             fp = 0;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "M.%ld.A.%3.3X", ++dtime, rand() & 0xFFF);
	if (fp == -1 && errno != EEXIST)
	    return -1;
    } while ((fp = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644)) == -1);
    close(fp);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    return 0;
}

void 
stampdir(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = now;
    struct tm      *ptime;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "D%lX", ++dtime & 07777);
    } while (mkdir(fpath, 0755) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

void 
stamplink(char *fpath, fileheader_t * fh)
{
    register char  *ip = fpath;
    time_t          dtime = now;
    struct tm      *ptime;

    if (access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "S%lX", ++dtime);
    } while (symlink("temp", fpath) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

int 
do_append(char *fpath, fileheader_t * record, int size)
{
    int             fd;

    if ((fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1) {
	perror("open");
	return -1;
    }
    flock(fd, LOCK_EX);
    lseek(fd, 0, SEEK_END);

    safewrite(fd, record, size);

    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

int 
append_record(char *fpath, fileheader_t * record, int size)
{
#if !defined(_BBS_UTIL_C_)
    int             m, n;
    if (get_num_records(fpath, sizeof(fileheader_t)) <= MAX_KEEPMAIL * 2) {
	FILE           *fp;
	char            buf[512], address[200];

	for (n = strlen(fpath) - 1; fpath[n] != '/' && n > 0; n--);
	strncpy(buf, fpath, n + 1);
	buf[n + 1] = 0;
	for (m = strlen(buf) - 2; buf[m] != '/' && m > 0; m--);
	strcat(buf, ".forward");
	if ((fp = fopen(buf, "r"))) {
	    fscanf(fp, "%s", address);
	    fclose(fp);
	    if (buf[0] != 0 && buf[0] != ' ') {
		buf[n + 1] = 0;
		strcat(buf, record->filename);
		do_append(fpath, record, size);
#ifndef  USE_BSMTP
		bbs_sendmail(buf, record->title, address);
#else
		bsmtp(buf, record->title, address, 0);
#endif
		return 0;
	    }
	}
    }
#endif

    do_append(fpath, record, size);

    return 0;
}
