/* $Id: util_record.c,v 1.2 2002/06/22 18:21:25 ptt Exp $ */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include "config.h"
#include "pttstruct.h"
#include "modes.h"
#include "proto.h"

#undef  HAVE_MMAP
#define BUFSIZE 512

extern char *str_reply;

static void PttLock(int fd, int size, int mode) {
    static struct flock lock_it;
    int ret;

    lock_it.l_whence = SEEK_CUR;        /* from current point */
    lock_it.l_start = 0;                /* -"- */
    lock_it.l_len = size;               /* length of data */
    lock_it.l_type = mode;              /* set exclusive/write lock */
    lock_it.l_pid = 0;                  /* pid not actually interesting */
    while((ret = fcntl(fd, F_SETLKW, &lock_it)) < 0 && errno == EINTR);
}

#define safewrite       write

int get_num_records(char *fpath, int size) {
    struct stat st;
    if(stat(fpath, &st) == -1)
	return 0;
    return st.st_size / size;
}

int get_sum_records(char* fpath, int size) {
    struct stat st;
    long ans = 0;
    FILE* fp;
    fileheader_t fhdr;
    char buf[200], *p;

    if(!(fp = fopen(fpath, "r")))
	return -1;
    
    strcpy(buf, fpath);
    p = strrchr(buf, '/') + 1;
    
    while(fread(&fhdr, size, 1, fp) == 1) {
	strcpy(p, fhdr.filename);
	if(stat(buf, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1)
	    ans += st.st_size;
    }
    fclose(fp);
    return ans / 1024;
}

int get_record(char *fpath, void *rptr, int size, int id) {
    int fd = -1;
    
    if(id < 1 || (fd = open(fpath, O_RDONLY, 0)) != -1) {
	if(lseek(fd, (off_t)(size * (id - 1)), SEEK_SET) != -1) {
	    if(read(fd, rptr, size) == size) {
		close(fd);
		return 0;
	    }
	}
	close(fd);
    }
    return -1;
}

int get_records(char *fpath, void *rptr, int size, int id, int number) {
    int fd;
    
    if(id < 1 || (fd = open(fpath, O_RDONLY, 0)) == -1)
	return -1;

    if(lseek(fd, (off_t)(size * (id - 1)), SEEK_SET) == -1) {
	close(fd);
	return 0;
    }
    if((id = read(fd, rptr, size * number)) == -1) {
	close(fd);
	return -1;
    }
    close(fd);
    return id / size;
}

int substitute_record(char *fpath, void *rptr, int size, int id) {
    int fd;

#ifdef POSTBUG
    if(size == sizeof(fileheader) && (id > 1) && ((id - 1) % 4 == 0))
	saverecords(fpath, size, id);
#endif
    
    if(id < 1 || (fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1)
	return -1;
    
#ifdef HAVE_REPORT
    if(lseek(fd, (off_t)(size * (id - 1)), SEEK_SET) == -1)
	report("substitute_record failed!!! (lseek)");
    PttLock(fd, size, F_WRLCK);
    if(safewrite(fd, rptr, size) != size)
	report("substitute_record failed!!! (safewrite)");
    PttLock(fd, size, F_UNLCK);
#else
    lseek(fd, (off_t) (size * (id - 1)), SEEK_SET);
    PttLock(fd, size, F_WRLCK);
    safewrite(fd, rptr, size);
    PttLock(fd, size, F_UNLCK);
#endif
    close(fd);
    
#ifdef POSTBUG
    if(size == sizeof(fileheader) && (id > 1) && ((id - 1) % 4 == 0))
	restorerecords(fpath, size, id);
#endif
    
    return 0;
}

int apply_record(char *fpath, int (*fptr)(), int size) {
    char abuf[BUFSIZE];
    FILE* fp;
    
    if(!(fp = fopen(fpath, "r")))
	return -1;
    
    while(fread(abuf, 1, size, fp) == size)
	if((*fptr) (abuf) == QUIT) {
	    fclose(fp);
	    return QUIT;
	}
    fclose(fp);
    return 0;
}

/* mail / post 時，依據時間建立檔案，加上郵戳 */
int stampfile(char *fpath, fileheader_t *fh) {
    register char *ip = fpath;
    time_t dtime;
    struct tm *ptime;
    int fp = 0;

    if(access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    time(&dtime);
    while (*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "M.%ld.A.%3.3X", ++dtime, rand()&0xfff );
	if(fp == -1 && errno != EEXIST)
	    return -1;
    } while((fp = open(fpath, O_CREAT | O_EXCL | O_WRONLY, 0644)) == -1);
    close(fp);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
    return 0;
}

void stampdir(char *fpath, fileheader_t *fh) {
    register char *ip = fpath;
    time_t dtime;
    struct tm *ptime;
    
    if(access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);
    
    time(&dtime);
    while(*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "D%lX", ++dtime & 07777);
    } while(mkdir(fpath, 0755) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

void stamplink(char *fpath, fileheader_t *fh) {
    register char *ip = fpath;
    time_t dtime;
    struct tm *ptime;

    if(access(fpath, X_OK | R_OK | W_OK))
	mkdir(fpath, 0755);

    time(&dtime);
    while(*(++ip));
    *ip++ = '/';
    do {
	sprintf(ip, "S%lX", ++dtime );
    } while(symlink("temp", fpath) == -1);
    memset(fh, 0, sizeof(fileheader_t));
    strcpy(fh->filename, ip);
    ptime = localtime(&dtime);
    sprintf(fh->date, "%2d/%02d", ptime->tm_mon + 1, ptime->tm_mday);
}

int do_append(char *fpath, fileheader_t *record, int size) {
    int fd;
    
    if((fd = open(fpath, O_WRONLY | O_CREAT, 0644)) == -1) {
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

int append_record(char *fpath, fileheader_t *record, int size) {
#ifdef POSTBUG
    int numrecs = (int)get_num_records(fpath, size);

    bug_possible = 1;
    if(size == sizeof(fileheader) && numrecs && (numrecs % 4 == 0))
	saverecords(fpath, size, numrecs + 1);
#endif
    do_append(fpath,record,size);
    
#ifdef POSTBUG
    if(size == sizeof(fileheader) && numrecs && (numrecs % 4 == 0))
	restorerecords(fpath, size, numrecs + 1);
    bug_possible = 0;
#endif
    return 0;
}
